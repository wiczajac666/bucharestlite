#include <bl_audio/track_strip.hpp>

#include <algorithm>
#include <cmath>

namespace bl {

TrackStrip::TrackStrip(const AudioMixConfig& config) : config_(config) {}

void TrackStrip::process(AudioSpan output, const AudioSpan& input,
                         double gainLinear, double pan,
                         const KeyframeTrack* volumeTrack,
                         Time /*clipStart*/, Time position) {
    const uint32_t frames = std::min(output.sampleCount, input.sampleCount);
    const uint32_t ch = std::min({output.channels, input.channels, config_.channels});

    auto [leftGain, rightGain] = PanLaw::compute(pan);

    for (uint32_t i = 0; i < frames; ++i) {
        double volMod = 1.0;
        if (volumeTrack) {
            Time t = position + Duration::fromFrames(static_cast<int64_t>(i),
                                                     Rational{1, static_cast<int64_t>(config_.sampleRate)});
            volMod = volumeTrack->evaluate(t);
        }

        double g = gainLinear * volMod;
        double gL = (ch >= 2) ? g * leftGain : g;
        double gR = (ch >= 2) ? g * rightGain : g;

        for (uint32_t c = 0; c < ch; ++c) {
            double gain = (c == 0) ? gL : gR;
            float sample = input.channelData(c)[i];
            output.channelData(c)[i] += static_cast<float>(sample * gain);
        }
    }
}

void TrackStrip::setGain(double linear) { gain_ = linear; }
void TrackStrip::setPan(double pan) { pan_ = pan; }
double TrackStrip::gain() const { return gain_; }
double TrackStrip::pan() const { return pan_; }

} // namespace bl
