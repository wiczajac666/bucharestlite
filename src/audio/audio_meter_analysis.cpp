#include <bl_audio/audio_meter_analysis.hpp>

#include <bl_audio/types.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/decoder_bridge.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/logger.hpp>

#include <bl_plugins/codec_plugin.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bl {

namespace {

// Decoded frames are heap-allocated by the plugin host allocator and freed
// through the same pair, matching how every decode path owns plugin buffers.
const BlHostApi kAnalysisAllocHost = {
    /*host_abi_version*/ BL_PLUGIN_ABI_VERSION,
    /*alloc*/ [](size_t size, void*) { return std::malloc(size); },
    /*free*/ [](void* ptr, void*) { std::free(ptr); },
    /*userdata*/ nullptr,
};

// Envelope resolution matches the preview tick cadence (~16 ms), so meter
// reads under that and the playhead stay aligned.
constexpr double kEnvelopeWindowHz = 60.0;

} // namespace

Result<AudioMeterEnvelope> analyzeAudio(const std::string& path,
                                        const CodecRegistry& registry) {
    auto demuxed = Demuxer::open(path);
    if (!demuxed.ok()) {
        return Result<AudioMeterEnvelope>::err(demuxed.code(),
                                               demuxed.message());
    }
    Demuxer demuxer = std::move(demuxed.value());

    const StreamInfo& info = demuxer.info();
    if (info.audioStreams.empty()) {
        return Result<AudioMeterEnvelope>::err(
            Err::DecodeFailed, "media '" + path + "' has no audio stream");
    }
    const AudioStreamInfo& stream = info.audioStreams.front();

    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.codec_name = stream.codecName.c_str();
    cfg.extradata = stream.extradata.empty() ? nullptr : stream.extradata.data();
    cfg.extradata_size = stream.extradata.size();
    cfg.audio.sample_rate =
        stream.sampleRate > 0 ? stream.sampleRate : 48000;
    cfg.audio.channels = stream.channels > 0 ? stream.channels : 2;
    cfg.host = &kAnalysisAllocHost;

    auto bridgeResult = DecoderBridge::create(registry, stream.codecName, cfg);
    if (!bridgeResult.ok()) {
        return Result<AudioMeterEnvelope>::err(
            Err::DecodeFailed,
            "cannot analyze audio of '" + path + "' (" + stream.codecName +
                "): " + bridgeResult.message());
    }
    DecoderBridge decoder = std::move(bridgeResult.value());

    const uint32_t sampleRate =
        stream.sampleRate > 0 ? stream.sampleRate : 48000;
    const uint32_t channels = stream.channels > 0 ? stream.channels : 1;
    const size_t windowSamples =
        std::max<size_t>(1u, static_cast<size_t>(sampleRate /
                                                 kEnvelopeWindowHz));

    // Accumulators grow one row per touched window: peak per channel, sum of
    // squares per channel and the shared sample count.
    std::vector<std::vector<float>> peaks;
    std::vector<std::vector<double>> sumSq;
    std::vector<uint64_t> counts;
    uint64_t streamPos{0};

    auto ensureWindow = [&](size_t window) {
        if (counts.size() <= window) {
            peaks.resize(window + 1, std::vector<float>(channels, 0.0f));
            sumSq.resize(window + 1, std::vector<double>(channels, 0.0));
            counts.resize(window + 1, 0);
        }
    };

    auto consumeFrame = [&](const Frame& frame) {
        if (frame.type != Frame::Type::Audio || frame.empty()) return;
        const uint32_t frameChannels =
            frame.channels > 0 ? frame.channels : 1;
        const uint32_t frames = frame.sampleCount;
        if (frames == 0) return;
        const uint32_t chEff = std::min(frameChannels, channels);

        for (uint32_t i = 0; i < frames; ++i) {
            const size_t window = (streamPos + i) / windowSamples;
            ensureWindow(window);
            for (uint32_t c = 0; c < chEff; ++c) {
                const float* plane = reinterpret_cast<const float*>(
                    frame.data +
                    static_cast<size_t>(c) * frame.linesize * sizeof(float));
                const float sample = plane[i];
                float& pk = peaks[window][c];
                if (std::fabs(sample) > pk) {
                    pk = std::fabs(sample);
                }
                sumSq[window][c] += static_cast<double>(sample) * sample;
            }
            ++counts[window];
        }
        streamPos += frames;
    };

    for (;;) {
        auto packetResult = demuxer.nextPacket();
        if (!packetResult.ok()) {
            return Result<AudioMeterEnvelope>::err(packetResult.code(),
                                                   packetResult.message());
        }
        const std::optional<Packet> packet =
            std::move(packetResult.value());
        if (!packet.has_value()) break;
        if (packet->streamIndex != stream.index) continue;

        auto decoded = decoder.decode(*packet);
        if (!decoded.ok()) {
            if (decoded.message() == "need more input") {
                continue;
            }
            BL_LOG_WARN("meter", "decode error for '" + path +
                                     "': " + decoded.message());
            continue;
        }
        consumeFrame(*decoded);
    }

    // Drain the decoder's buffered tail so trailing silence is captured.
    for (;;) {
        auto flushed = decoder.flush();
        if (!flushed.ok() || !flushed.value().has_value()) break;
        const Frame& tail = **flushed;
        if (tail.empty() || tail.type != Frame::Type::Audio) continue;
        consumeFrame(tail);
    }

    AudioMeterEnvelope envelope;
    envelope.sampleRate = sampleRate;
    envelope.channels = channels;
    envelope.windowHz = kEnvelopeWindowHz;
    envelope.peak = std::move(peaks);
    envelope.rms.resize(envelope.peak.size());
    for (size_t w = 0; w < envelope.peak.size(); ++w) {
        envelope.rms[w].resize(channels, 0.0f);
        const uint64_t count = counts[w];
        if (count == 0) continue;
        for (uint32_t c = 0; c < channels; ++c) {
            envelope.rms[w][c] = static_cast<float>(
                std::sqrt(sumSq[w][c] / static_cast<double>(count)));
        }
    }

    return Result<AudioMeterEnvelope>::ok(std::move(envelope));
}

MeterLevels meterLevelsAt(const Sequence& sequence, Time position,
                          const SourceMeterResolver& resolve) {
    MeterLevels out;
    out.tracks.resize(sequence.audioTracks.size());

    bool anySolo{false};
    for (const auto& track : sequence.audioTracks) {
        anySolo = anySolo || track.soloed();
    }

    for (size_t t = 0; t < sequence.audioTracks.size(); ++t) {
        const Track<Clip>& track = sequence.audioTracks[t];

        if (track.muted() || (anySolo && !track.soloed())) {
            continue;
        }

        for (const Clip& clip : track.clips()) {
            const Time clipEnd = clip.timelineStart + clip.timelineDuration;
            if (position < clip.timelineStart || position >= clipEnd) {
                continue;
            }

            const Time srcPos = clip.sourceTimeAt(position - clip.timelineStart);
            const Time srcLo = std::min(clip.source.sourceIn,
                                        clip.source.sourceOut);
            const Time srcHi = std::max(clip.source.sourceIn,
                                        clip.source.sourceOut);
            if (srcPos < srcLo || srcPos > srcHi) {
                continue;
            }

            const AudioMeterEnvelope* envelope = nullptr;
            if (auto resolved = resolve(clip.source.mediaItemId)) {
                envelope = resolved.get();
            }
            if (!envelope || envelope->empty()) {
                continue;
            }
            const size_t windowCount = envelope->windowCount();
            const double srcSeconds = srcPos.toSeconds();
            if (srcSeconds < 0.0) continue;
            size_t window =
                static_cast<size_t>(srcSeconds * envelope->windowHz);
            if (window >= windowCount) window = windowCount - 1;

            const double g = clip.audio.gain * track.gain();
            const uint32_t chEff =
                std::min<uint32_t>(envelope->channels, 2u);

            TrackMeterLevels& meter = out.tracks[t];
            if (chEff >= 2) {
                const auto [clipLeft, clipRight] =
                    PanLaw::compute(clip.audio.pan);
                const auto [trackLeft, trackRight] =
                    PanLaw::compute(track.pan());
                const double preLeft =
                    static_cast<double>(envelope->peak[window][0]) * g *
                    clipLeft;
                const double preRight =
                    static_cast<double>(envelope->peak[window][1]) * g *
                    clipRight;
                meter.left += static_cast<float>(preLeft * trackLeft);
                meter.right += static_cast<float>(preRight * trackRight);
            } else {
                // Mono input feeds the left bus only (mirrors TrackStrip).
                meter.left +=
                    static_cast<float>(static_cast<double>(
                        envelope->peak[window][0]) * g);
            }
        }
    }

    const auto [masterLeft, masterRight] =
        PanLaw::compute(sequence.settings.masterPan);
    const double masterGain = sequence.settings.masterGain;
    double leftTotal{0.0};
    double rightTotal{0.0};
    for (const TrackMeterLevels& meter : out.tracks) {
        leftTotal += meter.left;
        rightTotal += meter.right;
    }
    out.master.left = static_cast<float>(leftTotal * masterGain *
                                         masterLeft);
    out.master.right = static_cast<float>(rightTotal * masterGain *
                                          masterRight);

    return out;
}

} // namespace bl