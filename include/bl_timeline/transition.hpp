#pragma once

#include <bl_core/time.hpp>
#include <nlohmann/json.hpp>

namespace bl {

enum class TransitionKind : uint8_t {
    Crossfade,
    Wipe,
    Dissolve,
    Slide,
};

enum class TransitionAlignment : uint8_t {
    Center,
    Left,
    Right,
};

struct TransitionSpec {
    TransitionKind kind{TransitionKind::Crossfade};
    Duration duration{};
    TransitionAlignment alignment{TransitionAlignment::Center};
    nlohmann::json params = nlohmann::json::object();
};

inline bool operator==(const TransitionSpec& a, const TransitionSpec& b) {
    return a.kind == b.kind && a.duration == b.duration &&
           a.alignment == b.alignment && a.params == b.params;
}

void to_json(nlohmann::json& j, const TransitionSpec& s);
void from_json(const nlohmann::json& j, TransitionSpec& s);

} // namespace bl
