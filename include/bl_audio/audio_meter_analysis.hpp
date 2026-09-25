#pragma once

#include <bl_core/result.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/sequence.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace bl {

class CodecRegistry;

// Peak/RMS envelopes for one media source's audio, reduced from a full decode
// into fixed-size sample windows so playback-time meter reads are a pure lookup
// (no audio output device involved).
struct AudioMeterEnvelope {
    uint32_t sampleRate{48000};
    uint32_t channels{0};
    double windowHz{60.0};

    // window index -> per-channel values. peak is the max |amplitude|, rms the
    // root-mean-square over the window's samples.
    std::vector<std::vector<float>> peak;
    std::vector<std::vector<float>> rms;

    size_t windowCount() const noexcept { return peak.size(); }
    bool empty() const noexcept { return peak.empty(); }
};

// Stereo meter levels for one audio track.
struct TrackMeterLevels {
    float left{0.0f};
    float right{0.0f};
};

struct MasterMeterLevels {
    float left{0.0f};
    float right{0.0f};
};

struct MeterLevels {
    std::vector<TrackMeterLevels> tracks;  // audio tracks in sequence order
    MasterMeterLevels master;
};

// Resolves the analyzed envelope for a media item; nullptr means "no audio or
// not analyzed yet", which the query reads as silence. Returning a shared_ptr
// keeps the envelope alive for the duration of the query even if a background
// analysis for the same item settles (replacing the cache entry) meanwhile.
using SourceMeterResolver =
    std::function<std::shared_ptr<const AudioMeterEnvelope>(
        const std::string& mediaItemId)>;

// Decodes the first audio stream of `path` through bl_core's Demuxer +
// DecoderBridge (the registry must already contain the codec plugins, e.g.
// builtins plus a plugin-directory scan) and reduces the f32 planar PCM to
// per-window envelopes. Fails with a helpful message for files without a
// decodable audio stream.
Result<AudioMeterEnvelope> analyzeAudio(const std::string& path,
                                        const CodecRegistry& registry);

// Computes mix-accurate L/R levels for every audio track and the master bus at
// `position`. Each contributing clip is mapped timeline->source (range/speed
// like trim and split), then shaped by clip and track gain/pan; track results
// honour mute/solo and sum into the master (x masterGain/masterPan). The
// stereo mapping mirrors bl_audio's TrackStrip: with two or more input channels
// the constant-power PanLaw scales channel 0 -> left / channel 1 -> right;
// mono input feeds the left channel only. Envelopes unavailable from `resolve`
// contribute silence.
MeterLevels meterLevelsAt(const Sequence& sequence, Time position,
                          const SourceMeterResolver& resolve);

} // namespace bl