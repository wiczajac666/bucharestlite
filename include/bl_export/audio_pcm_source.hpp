#pragma once

#include <bl_core/codec_registry.hpp>
#include <bl_core/result.hpp>

#include <cstdint>
#include <memory>
#include <string>

struct BlHostApi;

namespace bl {
namespace export_ {

// Sequential f32-planar PCM reader for a media file's first audio stream.
//
// Decodes through the Demuxer + DecoderBridge used everywhere else and
// resamples to a fixed target rate with libswresample. Roadmap-style pulls:
// each call returns up to `capacity` frames, and the reader advances forward.
// Caller-initiated seeks are supported so arbitrary clip source windows can be
// pulled back to back. Qt-free and deterministic.
class PcmAudioSource {
public:
    struct OpenRequest {
        std::string path;
        uint32_t targetSampleRate{48000};
        const BlHostApi* host{nullptr};
    };

    static Result<PcmAudioSource> open(const OpenRequest& request,
                                       const CodecRegistry& registry);

    PcmAudioSource(PcmAudioSource&& other) noexcept;
    PcmAudioSource& operator=(PcmAudioSource&& other) noexcept;
    ~PcmAudioSource();

    PcmAudioSource(const PcmAudioSource&) = delete;
    PcmAudioSource& operator=(const PcmAudioSource&) = delete;

    // Position the reader at a media-relative time (in seconds measured from
    // the start of the file). Buffered samples are dropped; the next pull
    // decodes from the new position.
    Result<void> seekToSeconds(double seconds);

    // Pull up to `capacity` frames into contiguous planar per-channel
    // buffers. `plane1` may be null for mono output. Returns the number of
    // frames pulled; 0 marks end-of-stream. Samples past the decoded source
    // are never synthesized (the caller fills those with silence).
    uint32_t pull(float* plane0, float* plane1, uint32_t capacity);

    // Output channel count the reader emits (1 or 2, decoded channels capped).
    uint32_t channels() const noexcept;
    uint32_t sampleRate() const noexcept;
    uint32_t targetSampleRate() const noexcept;

private:
    PcmAudioSource() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace export_
}  // namespace bl