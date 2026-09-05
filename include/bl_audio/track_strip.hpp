#pragma once

#include <bl_audio/types.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/keyframes.hpp>

namespace bl {

class TrackStrip {
public:
    explicit TrackStrip(const AudioMixConfig& config);

    void process(AudioSpan output, const AudioSpan& input,
                 double gainLinear, double pan,
                 const KeyframeTrack* volumeTrack = nullptr,
                 Time clipStart = {}, Time position = {});

    void setGain(double linear);
    void setPan(double pan);

    double gain() const;
    double pan() const;

private:
    AudioMixConfig config_;
    double gain_{1.0};
    double pan_{0.0};
};

} // namespace bl
