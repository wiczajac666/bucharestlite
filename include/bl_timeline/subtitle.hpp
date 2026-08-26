#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace bl {

struct SubtitleStyle {
    std::string font{"sans-serif"};
    int32_t fontSize{24};
    std::string color{"#FFFFFF"};

    enum class Position : uint8_t {
        Bottom,
        Top,
        Center,
    };

    Position position{Position::Bottom};
};

inline bool operator==(const SubtitleStyle& a, const SubtitleStyle& b) {
    return a.font == b.font && a.fontSize == b.fontSize &&
           a.color == b.color && a.position == b.position;
}

void to_json(nlohmann::json& j, const SubtitleStyle& s);
void from_json(const nlohmann::json& j, SubtitleStyle& s);

} // namespace bl
