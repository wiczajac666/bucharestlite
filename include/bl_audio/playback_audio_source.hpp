#pragma once

#include <bl_audio/types.hpp>
#include <bl_core/result.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace bl {

class CodecRegistry;

// Decodes one media file's first audio stream for preview playback.
//
// Unlike the export-side PcmAudioSource this reader is meant to be opened once
// per active clip and pulled continuously while the transport runs: seeking
// reuses the same demuxer/decoder instead of reopening the file. Output is
// always the requested rate and channel count (linear interpolation for rate
// conversion, first-two-channel selection for downmix, duplication for mono),
// so callers never have to match source formats.
class PlaybackAudioSource {
public:
    struct OpenRequest {
        std::string path;
        uint32_t targetSampleRate{48000};
        uint32_t channels{2};
    };

    static Result<PlaybackAudioSource> open(const OpenRequest& request,
                                            const CodecRegistry& registry);

    PlaybackAudioSource(PlaybackAudioSource&& other) noexcept;
    PlaybackAudioSource& operator=(PlaybackAudioSource&& other) noexcept;
    ~PlaybackAudioSource();

    PlaybackAudioSource(const PlaybackAudioSource&) = delete;
    PlaybackAudioSource& operator=(const PlaybackAudioSource&) = delete;

    // Position the reader at a media-relative time in seconds. The next pull
    // decodes from there; buffered samples are dropped.
    Result<void> seekToSeconds(double seconds);

    // Fill `output` with up to `frames` planar frames at the target rate.
    // Returns the number of frames produced; a short result means end of
    // stream (the remaining frames are left silent).
    uint32_t pull(AudioBuffer& output, uint32_t frames);

    uint32_t channels() const noexcept;
    uint32_t sampleRate() const noexcept;

    // Approximate media time of the next output frame.
    double positionSeconds() const noexcept;

    bool atEnd() const noexcept;

private:
    PlaybackAudioSource() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bl
