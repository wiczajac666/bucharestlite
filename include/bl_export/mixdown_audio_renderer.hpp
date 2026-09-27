#pragma once

#include <bl_core/codec_registry.hpp>
#include <bl_core/result.hpp>
#include <bl_timeline/sequence.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

struct BlHostApi;

namespace bl {
namespace export_ {

struct TimelineMixdownOptions {
    uint32_t sampleRate{48000};
    uint32_t channels{2};
    uint32_t blockSamples{4096};
    // Optional extraction window measured from the timeline start, in samples
    // of `sampleRate`. Zero maxSamples means "to the end of the timeline".
    uint64_t startSample{0};
    uint64_t maxSamples{0};
    const BlHostApi* host{nullptr};
    std::atomic<bool>* abort{nullptr};
};

// Qt-free offline mixdown renderer. Turns the audio portion of a timeline into
// fixed-size f32-planar blocks using the same channel/gain/pan laws as the
// metering path (clip gain & pan x track gain & pan, volume keyframes, mute and
// solo, then a master gain/pan stage), so exported audio matches what the
// meters preview. Deterministic and single-threaded; one PcmAudioSource is
// opened per clip that references media.
class TimelineMixdown {
public:
    // Resolves a timeline mediaItemId to a file path.
    using MediaResolver =
        std::function<Result<std::string>(const std::string& mediaItemId)>;
    // Called once per block; return false to abort the render.
    using BlockCallback = std::function<bool(const float* const* planes,
                                             uint32_t samples)>;

    static Result<void> render(const Sequence& sequence,
                               const TimelineMixdownOptions& opts,
                               const CodecRegistry& registry,
                               MediaResolver mediaResolver,
                               BlockCallback onBlock);

private:
    TimelineMixdown() = default;
};

// Total sample count the timeline occupies at the given rate (max of every
// video/audio clip end). Used to bound the export audio pass so it covers the
// whole project including video-only clips.
uint64_t timelineMixdownSampleCount(const Sequence& sequence,
                                    uint32_t sampleRate);

}  // namespace export_
}  // namespace bl