#include <bl_export/audio_pcm_source.hpp>

#include <bl_core/decoder_bridge.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/logger.hpp>

#include <bl_plugins/codec_plugin.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace bl {
namespace export_ {

namespace {

constexpr int kSwrTileSamples = 4096;

}  // namespace

struct PcmAudioSource::Impl {
    std::optional<Demuxer> demuxer;
    std::optional<DecoderBridge> decoder;
    int streamIndex{-1};

    uint32_t sourceSampleRate{0};
    uint32_t targetSampleRate{0};
    uint32_t outChannels{0};

    SwrContext* swr{nullptr};
    int swrInRate{0};
    int swrInCh{0};

    bool packetsDone{false};
    bool eos{false};
    bool hardError{false};

    // Resampled planar PCM: outChannels planes, each up to batchCap samples.
    std::vector<float> batch;
    uint32_t batchCap{0};     // per-plane capacity in samples
    uint32_t batchCount{0};   // live samples per plane
    uint32_t batchStart{0};   // read offset into batch

    ~Impl() {
        if (swr) {
            swr_free(&swr);
        }
    }
};

PcmAudioSource::PcmAudioSource(PcmAudioSource&& other) noexcept = default;
PcmAudioSource& PcmAudioSource::operator=(PcmAudioSource&& other) noexcept =
    default;
PcmAudioSource::~PcmAudioSource() = default;

Result<PcmAudioSource> PcmAudioSource::open(const OpenRequest& request,
                                           const CodecRegistry& registry) {
    if (request.path.empty()) {
        return Result<PcmAudioSource>::err(Err::InvalidArgument,
                                           "empty media path");
    }
    if (request.targetSampleRate == 0 || request.host == nullptr) {
        return Result<PcmAudioSource>::err(
            Err::InvalidArgument,
            "audio source requires a non-zero target rate and a host");
    }

    auto demuxed = Demuxer::open(request.path);
    if (!demuxed.ok()) {
        return Result<PcmAudioSource>::err(demuxed.code(),
                                           demuxed.message());
    }
    PcmAudioSource source;
    source.impl_ = std::make_unique<Impl>();
    source.impl_->demuxer = std::move(demuxed.value());
    source.impl_->targetSampleRate = request.targetSampleRate;

    const StreamInfo& info = source.impl_->demuxer->info();
    if (info.audioStreams.empty()) {
        return Result<PcmAudioSource>::err(
            Err::DecodeFailed,
            "media '" + request.path + "' has no audio stream");
    }
    const AudioStreamInfo& stream = info.audioStreams.front();
    source.impl_->streamIndex = stream.index;
    source.impl_->sourceSampleRate =
        stream.sampleRate > 0 ? stream.sampleRate : 48000;
    source.impl_->outChannels =
        std::min<uint32_t>(stream.channels > 0 ? stream.channels : 1, 2);

    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.codec_name = stream.codecName.c_str();
    cfg.extradata = stream.extradata.empty() ? nullptr : stream.extradata.data();
    cfg.extradata_size = stream.extradata.size();
    cfg.audio.sample_rate = static_cast<uint32_t>(source.impl_->sourceSampleRate);
    cfg.audio.channels = source.impl_->outChannels;
    cfg.host = request.host;

    auto bridgeResult =
        DecoderBridge::create(registry, stream.codecName, cfg);
    if (!bridgeResult.ok()) {
        return Result<PcmAudioSource>::err(
            Err::DecodeFailed,
            "cannot decode audio of '" + request.path + "' (" +
                stream.codecName + "): " + bridgeResult.message());
    }
    source.impl_->decoder = std::move(bridgeResult.value());

    return Result<PcmAudioSource>::ok(std::move(source));
}

Result<void> PcmAudioSource::seekToSeconds(double seconds) {
    if (!impl_->demuxer) {
        return Result<void>::err(Err::InvalidArgument, "source not open");
    }
    if (seconds < 0.0) seconds = 0.0;
    Result<void> res = impl_->demuxer->seek(
        Time::fromSeconds(seconds, Rational{1'000'000, 1}));
    if (!res.ok()) {
        return res;
    }
    impl_->batchStart = 0;
    impl_->batchCount = 0;
    impl_->packetsDone = false;
    impl_->eos = false;
    impl_->hardError = false;
    return Result<void>();
}

uint32_t PcmAudioSource::channels() const noexcept {
    return impl_ ? impl_->outChannels : 0;
}

uint32_t PcmAudioSource::sampleRate() const noexcept {
    return impl_ ? impl_->sourceSampleRate : 0;
}

uint32_t PcmAudioSource::targetSampleRate() const noexcept {
    return impl_ ? impl_->targetSampleRate : 0;
}

uint32_t PcmAudioSource::pull(float* plane0, float* plane1, uint32_t capacity) {
    if (!impl_ || !plane0 || capacity == 0 || impl_->hardError) {
        return 0;
    }

    // Resample a decoded frame (f32 planar at the source rate) into the batch
    // buffer. A member lambda so it can reach the private Impl.
    const auto appendResampled = [impl = impl_.get()](const Frame& frame) {
        const uint32_t frameCh =
            std::min<uint32_t>(frame.channels > 0 ? frame.channels : 1, 2);
        const uint32_t rate = impl->sourceSampleRate;
        if (!impl->swr || impl->swrInRate != static_cast<int>(rate) ||
            impl->swrInCh != static_cast<int>(frameCh)) {
            impl->swrInRate = static_cast<int>(rate);
            impl->swrInCh = static_cast<int>(frameCh);
            swr_free(&impl->swr);
            AVChannelLayout inLayout;
            av_channel_layout_default(&inLayout, impl->swrInCh);
            AVChannelLayout outLayout;
            av_channel_layout_default(&outLayout, impl->outChannels);
            if (swr_alloc_set_opts2(&impl->swr, &outLayout, AV_SAMPLE_FMT_FLTP,
                                    static_cast<int>(impl->targetSampleRate),
                                    &inLayout, AV_SAMPLE_FMT_FLTP,
                                    impl->swrInRate, 0, nullptr) < 0 ||
                swr_init(impl->swr) < 0) {
                swr_free(&impl->swr);
                return;
            }
        }

        // Compact the batch so new samples land at the tail.
        if (impl->batchStart > 0) {
            for (uint32_t c = 0; c < impl->outChannels; ++c) {
                float* plane = impl->batch.data() + c * impl->batchCap;
                std::copy(plane + impl->batchStart,
                          plane + impl->batchStart + impl->batchCount, plane);
            }
            impl->batchStart = 0;
        }

        const int maxOut =
            swr_get_out_samples(impl->swr, frame.sampleCount) + 64;
        const uint32_t need = impl->batchCount +
                              static_cast<uint32_t>(std::max(maxOut, 0));
        if (need > impl->batchCap) {
            const uint32_t newCap =
                std::max(need, impl->batchCap + kSwrTileSamples);
            std::vector<float> grown;
            grown.resize(static_cast<size_t>(newCap) * impl->outChannels, 0.0f);
            for (uint32_t c = 0; c < impl->outChannels; ++c) {
                std::copy_n(impl->batch.data() + c * impl->batchCap,
                            impl->batchCount, grown.data() + c * newCap);
            }
            impl->batch = std::move(grown);
            impl->batchCap = newCap;
        }

        uint8_t* outPlanes[2] = {nullptr, nullptr};
        for (uint32_t c = 0; c < impl->outChannels; ++c) {
            outPlanes[c] = reinterpret_cast<uint8_t*>(
                impl->batch.data() + c * impl->batchCap + impl->batchCount);
        }
        const uint8_t* inPlanes[2] = {nullptr, nullptr};
        for (uint32_t c = 0; c < frameCh; ++c) {
            inPlanes[c] = frame.data + c * frame.linesize * sizeof(float);
        }

        const int conv = swr_convert(impl->swr, outPlanes, maxOut, inPlanes,
                                     frame.sampleCount);
        if (conv <= 0) {
            return;
        }
        impl->batchCount += static_cast<uint32_t>(conv);
    };

    uint32_t pulled = 0;
    while (pulled < capacity) {
        if (impl_->batchCount - impl_->batchStart == 0) {
            impl_->batchCount = 0;
            impl_->batchStart = 0;

            bool gotSamples = false;
            if (!impl_->packetsDone && impl_->demuxer && impl_->decoder) {
                for (;;) {
                    auto packetResult = impl_->demuxer->nextPacket();
                    if (!packetResult.ok()) {
                        impl_->hardError = true;
                        break;
                    }
                    const std::optional<Packet> packet =
                        std::move(packetResult.value());
                    if (!packet.has_value()) {
                        impl_->packetsDone = true;
                        break;
                    }
                    if (packet->streamIndex != impl_->streamIndex) continue;

                    auto decoded = impl_->decoder->decode(*packet);
                    if (!decoded.ok()) {
                        if (decoded.message() == "need more input") continue;
                        BL_LOG_WARN("audio", "decode error: " +
                                                 decoded.message());
                        continue;
                    }
                    const Frame& frame = *decoded;
                    if (frame.type != Frame::Type::Audio || frame.empty() ||
                        frame.sampleCount == 0) {
                        continue;
                    }
                    appendResampled(frame);
                    gotSamples = true;
                    break;
                }
            }

            if (impl_->packetsDone) {
                // Drain the decoder's buffered tail.
                if (impl_->decoder) {
                    for (;;) {
                        auto flushed = impl_->decoder->flush();
                        if (!flushed.ok() || !flushed.value().has_value()) {
                            break;
                        }
                        const Frame& tail = *flushed.value();
                        if (tail.type == Frame::Type::Audio &&
                            !tail.empty() && tail.sampleCount > 0) {
                            appendResampled(tail);
                            gotSamples = true;
                            break;
                        }
                    }
                }
                if (!gotSamples && impl_->batchCount - impl_->batchStart == 0) {
                    impl_->eos = true;
                }
            } else if (!gotSamples && impl_->hardError) {
                impl_->eos = true;
            }
        }

        if (impl_->batchCount - impl_->batchStart == 0) {
            if (impl_->eos || impl_->hardError) {
                break;
            }
            continue;
        }

        const uint32_t avail = impl_->batchCount - impl_->batchStart;
        const uint32_t take = std::min(avail, capacity - pulled);
        for (uint32_t c = 0; c < impl_->outChannels; ++c) {
            const float* plane =
                impl_->batch.data() + c * impl_->batchCap + impl_->batchStart;
            float* dst = (c == 0) ? plane0 : plane1;
            std::copy_n(plane, take, dst + pulled);
        }
        impl_->batchStart += take;
        pulled += take;
    }
    return pulled;
}

}  // namespace export_
}  // namespace bl