#pragma once

#include <bl_audio/types.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/keyframes.hpp>

#include <vector>

namespace bl {

class Mixer {
public:
    explicit Mixer(const AudioMixConfig& config);

    void mix(AudioSpan output,
             const std::vector<AudioSpan>& trackInputs,
             const std::vector<double>& trackGains,
             const std::vector<double>& trackPans,
             const std::vector<const KeyframeTrack*>& volumeTracks = {},
             Time position = {});

    const AudioMixConfig& config() const;

private:
    AudioMixConfig config_;
};

} // namespace bl
