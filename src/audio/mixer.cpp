#include <bl_audio/mixer.hpp>
#include <bl_audio/track_strip.hpp>

#include <algorithm>

namespace bl {

Mixer::Mixer(const AudioMixConfig& config) : config_(config) {}

void Mixer::mix(AudioSpan output,
                const std::vector<AudioSpan>& trackInputs,
                const std::vector<double>& trackGains,
                const std::vector<double>& trackPans,
                const std::vector<const KeyframeTrack*>& volumeTracks,
                Time position) {
    std::fill(output.data, output.data + output.linesize * output.channels, 0.0f);

    const size_t trackCount = std::min({trackInputs.size(),
                                        trackGains.size(),
                                        trackPans.size()});

    TrackStrip strip(config_);

    for (size_t t = 0; t < trackCount; ++t) {
        const auto& volTrack = (t < volumeTracks.size()) ? volumeTracks[t] : nullptr;
        strip.process(output, trackInputs[t],
                      trackGains[t], trackPans[t],
                      volTrack, Time{}, position);
    }
}

const AudioMixConfig& Mixer::config() const { return config_; }

} // namespace bl
