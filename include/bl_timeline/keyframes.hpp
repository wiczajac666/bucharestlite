#pragma once

#include <bl_core/time.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace bl {

enum class Interpolation {
    Hold,
    Linear,
    Bezier,
};

struct Keyframe {
    Time t{};
    double value{0.0};
    Interpolation interpolation{Interpolation::Linear};
};

inline bool operator==(const Keyframe& a, const Keyframe& b) {
    return a.t == b.t && a.value == b.value &&
           a.interpolation == b.interpolation;
}

class KeyframeTrack {
public:
    const std::vector<Keyframe>& samples() const noexcept { return samples_; }
    size_t size() const noexcept { return samples_.size(); }
    bool empty() const noexcept { return samples_.empty(); }

    void set(Time t, double value, Interpolation interp) {
        for (size_t i = 0; i < samples_.size(); ++i) {
            if (samples_[i].t == t) {
                samples_[i].value = value;
                samples_[i].interpolation = interp;
                return;
            }
            if (samples_[i].t > t) {
                samples_.insert(
                    samples_.begin() + static_cast<ptrdiff_t>(i),
                    Keyframe{t, value, interp});
                return;
            }
        }
        samples_.push_back(Keyframe{t, value, interp});
    }

    bool remove(Time t) {
        for (size_t i = 0; i < samples_.size(); ++i) {
            if (samples_[i].t == t) {
                samples_.erase(samples_.begin() + static_cast<ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    double evaluate(Time t) const;

private:
    std::vector<Keyframe> samples_;
};

inline bool operator==(const KeyframeTrack& a, const KeyframeTrack& b) {
    return a.samples() == b.samples();
}

enum class KeyChannel {
    Scale,
    RotX,
    RotY,
    PosX,
    PosY,
    Opacity,
    Volume,
};

struct KeyframeTrackSet {
    std::optional<KeyframeTrack> scale;
    std::optional<KeyframeTrack> rotX;
    std::optional<KeyframeTrack> rotY;
    std::optional<KeyframeTrack> posX;
    std::optional<KeyframeTrack> posY;
    std::optional<KeyframeTrack> opacity;
    std::optional<KeyframeTrack> volume;

    const KeyframeTrack* track(KeyChannel channel) const;
    KeyframeTrack* track(KeyChannel channel);
    KeyframeTrack& ensure(KeyChannel channel);
    bool empty() const noexcept;
};

inline bool operator==(const KeyframeTrackSet& a, const KeyframeTrackSet& b) {
    return a.scale == b.scale && a.rotX == b.rotX && a.rotY == b.rotY &&
           a.posX == b.posX && a.posY == b.posY && a.opacity == b.opacity &&
           a.volume == b.volume;
}

std::optional<double> evaluateChannel(const KeyframeTrackSet& set,
                                      KeyChannel channel, Time t);

KeyframeTrackSet keyframeSetPrefix(const KeyframeTrackSet& set,
                                   Duration limit);
KeyframeTrackSet keyframeSetSuffixRebased(const KeyframeTrackSet& set,
                                          Duration offset);

void to_json(nlohmann::json& j, const Keyframe& k);
void from_json(const nlohmann::json& j, Keyframe& k);
void to_json(nlohmann::json& j, const KeyframeTrack& track);
void from_json(const nlohmann::json& j, KeyframeTrack& track);
void to_json(nlohmann::json& j, const KeyframeTrackSet& set);
void from_json(const nlohmann::json& j, KeyframeTrackSet& set);
void to_json(nlohmann::json& j, Interpolation interp);
void from_json(const nlohmann::json& j, Interpolation& interp);

} // namespace bl
