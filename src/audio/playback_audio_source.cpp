#include <bl_audio/playback_audio_source.hpp>

#include <bl_core/codec_registry.hpp>
#include <bl_core/decoder_bridge.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/time.hpp>

#include <bl_plugins/codec_plugin.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bl {
namespace {

void* hostAlloc(size_t size, void*) { return std::malloc(size); }
void hostFree(void* ptr, void*) { std::free(ptr); }

constexpr int64_t kMicrosPerSecond = 1000000;

} // namespace

struct PlaybackAudioSource::Impl {
    explicit Impl(Demuxer input) : demuxer(std::move(input)) {}

    Demuxer demuxer;
    std::optional<DecoderBridge> decoder;
    int streamIndex{-1};

    uint32_t srcRate{48000};
    uint32_t srcChannels{2};
    uint32_t targetRate{48000};
    uint32_t outChannels{2};

    BlHostApi host{BL_PLUGIN_ABI_VERSION, hostAlloc, hostFree, nullptr};
    std::string codecName;
    std::vector<uint8_t> extradata;
    BlCodecConfig config{};

    // Decoded source frames, one plane per source channel.
    std::vector<std::vector<float>> pending;
    double readPos{0.0};        // source frames from the front of `pending`
    double sourcePosition{0.0}; // media seconds of the next output frame
    bool eof{false};

    bool decodeMore() {
        while (true) {
            auto packet = demuxer.nextPacket();
            if (!packet.ok()) {
                eof = true;
                return false;
            }
            if (!packet->has_value()) {
                eof = true;
                return false;
            }
            if ((*packet)->streamIndex != streamIndex) continue;

            auto decoded = decoder->decode(**packet);
            if (!decoded.ok()) continue;

            Frame& frame = decoded.value();
            if (frame.empty() || frame.type != Frame::Type::Audio ||
                frame.sampleCount == 0) {
                continue;
            }

            if (pending.empty()) pending.resize(srcChannels);
            const uint32_t frameChannels =
                frame.channels > 0 ? frame.channels : srcChannels;
            for (uint32_t c = 0; c < srcChannels; ++c) {
                const uint32_t sourceChannel = std::min(c, frameChannels - 1);
                const float* plane = reinterpret_cast<const float*>(
                    frame.data + static_cast<size_t>(sourceChannel) *
                                     frame.linesize * sizeof(float));
                pending[c].insert(pending[c].end(), plane,
                                  plane + frame.sampleCount);
            }
            return true;
        }
    }

    void compact() {
        const auto drop = static_cast<size_t>(readPos);
        if (drop < 8192) return;
        for (auto& plane : pending) {
            plane.erase(plane.begin(),
                        plane.begin() + static_cast<std::ptrdiff_t>(drop));
        }
        readPos -= static_cast<double>(drop);
    }

    float sampleAt(uint32_t channel, size_t frame) const {
        const uint32_t sourceChannel = std::min<uint32_t>(channel, srcChannels - 1);
        return pending[sourceChannel][frame];
    }
};

PlaybackAudioSource::PlaybackAudioSource(PlaybackAudioSource&& other) noexcept = default;
PlaybackAudioSource& PlaybackAudioSource::operator=(PlaybackAudioSource&& other) noexcept =
    default;
PlaybackAudioSource::~PlaybackAudioSource() = default;

Result<PlaybackAudioSource> PlaybackAudioSource::open(const OpenRequest& request,
                                                      const CodecRegistry& registry) {
    if (request.path.empty()) {
        return Result<PlaybackAudioSource>::err(Err::InvalidArgument,
                                                "empty media path");
    }

    auto demuxed = Demuxer::open(request.path);
    if (!demuxed.ok()) {
        return Result<PlaybackAudioSource>::err(demuxed.code(), demuxed.message());
    }

    PlaybackAudioSource result;
    result.impl_ = std::make_unique<Impl>(std::move(demuxed.value()));
    Impl& impl = *result.impl_;

    const StreamInfo& info = impl.demuxer.info();
    if (info.audioStreams.empty()) {
        return Result<PlaybackAudioSource>::err(Err::InvalidArgument,
                                                "media has no audio stream");
    }

    const AudioStreamInfo& stream = info.audioStreams.front();
    impl.streamIndex = stream.index;
    impl.srcRate = stream.sampleRate > 0 ? stream.sampleRate : request.targetSampleRate;
    impl.srcChannels = stream.channels > 0 ? stream.channels : 2;
    impl.targetRate =
        request.targetSampleRate > 0 ? request.targetSampleRate : 48000;
    impl.outChannels = request.channels > 0 ? request.channels : 2;
    impl.codecName = stream.codecName;
    impl.extradata = stream.extradata;

    impl.config = BlCodecConfig{};
    impl.config.abi_version = BL_PLUGIN_ABI_VERSION;
    impl.config.codec_name = impl.codecName.c_str();
    impl.config.extradata =
        impl.extradata.empty() ? nullptr : impl.extradata.data();
    impl.config.extradata_size = impl.extradata.size();
    impl.config.audio.sample_rate = impl.srcRate;
    impl.config.audio.channels = impl.srcChannels;
    impl.config.host = &impl.host;

    auto bridge = DecoderBridge::create(registry, stream.codecName, impl.config);
    if (!bridge.ok()) {
        return Result<PlaybackAudioSource>::err(bridge.code(), bridge.message());
    }
    impl.decoder.emplace(std::move(bridge.value()));
    return result;
}

Result<void> PlaybackAudioSource::seekToSeconds(double seconds) {
    if (seconds < 0.0) seconds = 0.0;

    auto seek = impl_->demuxer.seek(
        Time::fromSeconds(seconds, Rational{kMicrosPerSecond, 1}));
    if (!seek.ok()) return seek;

    impl_->pending.clear();
    impl_->readPos = 0.0;
    impl_->sourcePosition = seconds;
    impl_->eof = false;

    // Do not call DecoderBridge::flush() here: it sends an EOF packet to the
    // decoder and leaves it in drain state, after which further decodes fail.
    // Audio decoders emit one packet per input packet, so there is normally
    // nothing stale buffered after a demuxer seek.
    return Result<void>{};
}

uint32_t PlaybackAudioSource::pull(AudioBuffer& output, uint32_t frames) {
    Impl& impl = *impl_;
    output.resize(frames, impl.outChannels);
    output.clear();

    if (frames == 0) return 0;

    const double step = static_cast<double>(impl.srcRate) /
                        static_cast<double>(impl.targetRate);
    uint32_t produced = 0;

    while (produced < frames) {
        const auto index = static_cast<size_t>(impl.readPos);

        // Need two neighbouring source frames for interpolation, or one at
        // end-of-stream where we hold the final sample.
        while (impl.pending.empty() ||
               index + 1 >= impl.pending.front().size()) {
            if (impl.eof) break;
            if (!impl.decodeMore()) break;
        }

        const bool enough = !impl.pending.empty() &&
                            impl.pending.front().size() > index;
        if (!enough) break;

        const size_t next =
            std::min(index + 1, impl.pending.front().size() - 1);
        const float frac = static_cast<float>(impl.readPos - index);

        for (uint32_t c = 0; c < impl.outChannels; ++c) {
            const float a = impl.sampleAt(c, index);
            const float b = impl.sampleAt(c, next);
            output.channelData(c)[produced] = a + (b - a) * frac;
        }

        ++produced;
        impl.readPos += step;
    }

    impl.sourcePosition +=
        static_cast<double>(produced) / static_cast<double>(impl.targetRate);
    impl.compact();
    return produced;
}

uint32_t PlaybackAudioSource::channels() const noexcept { return impl_->outChannels; }
uint32_t PlaybackAudioSource::sampleRate() const noexcept { return impl_->targetRate; }
double PlaybackAudioSource::positionSeconds() const noexcept {
    return impl_->sourcePosition;
}
bool PlaybackAudioSource::atEnd() const noexcept {
    return impl_->eof && (impl_->pending.empty() ||
                          static_cast<size_t>(impl_->readPos) >=
                              impl_->pending.front().size());
}

} // namespace bl
