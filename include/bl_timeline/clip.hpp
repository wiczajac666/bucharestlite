#pragma once

#include <bl_core/time.hpp>
#include <bl_timeline/keyframes.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bl {

using ClipId = std::string;

struct SourceRef {
    std::string mediaItemId;
    Time sourceIn{};
    Time sourceOut{};
};

inline bool operator==(const SourceRef& a, const SourceRef& b) {
    return a.mediaItemId == b.mediaItemId &&
           a.sourceIn == b.sourceIn && a.sourceOut == b.sourceOut;
}

struct SpeedRemap {
    int32_t rateNum{1};
    int32_t rateDen{1};
    bool reversed{false};

    bool isIdentity() const noexcept {
        return rateNum == 1 && rateDen == 1 && !reversed;
    }
};

inline bool operator==(const SpeedRemap& a, const SpeedRemap& b) {
    return a.rateNum == b.rateNum && a.rateDen == b.rateDen &&
           a.reversed == b.reversed;
}

struct EffectInstance {
    std::string effectId;
    bool enabled{true};
    nlohmann::json params = nlohmann::json::object();
};

inline bool operator==(const EffectInstance& a, const EffectInstance& b) {
    return a.effectId == b.effectId && a.enabled == b.enabled && a.params == b.params;
}

struct AudioClipProps {
    double gain{1.0};
    double pan{0.0};
};

inline bool operator==(const AudioClipProps& a, const AudioClipProps& b) {
    return a.gain == b.gain && a.pan == b.pan;
}

struct Clip {
    ClipId id;
    std::string name;
    uint32_t colorLabel{0};

    SourceRef source;
    Time timelineStart{};
    Duration timelineDuration{};

    SpeedRemap speed;
    std::vector<EffectInstance> effects;
    AudioClipProps audio;
    std::optional<KeyframeTrackSet> keyframes;

    Duration effectiveDuration() const noexcept;
};

inline bool operator==(const Clip& a, const Clip& b) {
    return a.id == b.id && a.name == b.name &&
           a.colorLabel == b.colorLabel && a.source == b.source &&
           a.timelineStart == b.timelineStart &&
           a.timelineDuration == b.timelineDuration &&
           a.speed == b.speed && a.effects == b.effects &&
           a.audio == b.audio && a.keyframes == b.keyframes;
}

void to_json(nlohmann::json& j, const Clip& c);
void from_json(const nlohmann::json& j, Clip& c);
void to_json(nlohmann::json& j, const SourceRef& s);
void from_json(const nlohmann::json& j, SourceRef& s);
void to_json(nlohmann::json& j, const SpeedRemap& s);
void from_json(const nlohmann::json& j, SpeedRemap& s);
void to_json(nlohmann::json& j, const EffectInstance& e);
void from_json(const nlohmann::json& j, EffectInstance& e);
void to_json(nlohmann::json& j, const AudioClipProps& a);
void from_json(const nlohmann::json& j, AudioClipProps& a);

Time advanceSourceTime(Time sourcePos, Duration timelineDelta,
                       SpeedRemap speed);

std::optional<Clip> makeThreePointClip(const SourceRef& source,
                                       SpeedRemap speed,
                                       std::string name = "");

Duration retimedTimelineDuration(Duration timelineDuration, SpeedRemap from,
                                 SpeedRemap to);

} // namespace bl
