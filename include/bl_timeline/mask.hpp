#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace bl {

// A point in a mask path, normalised to the frame ([0,1] on both axes). Keeping
// the coordinates resolution-independent means one mask survives proxy/preview
// and full-resolution export renders.
struct MaskPoint {
    double x{0.0};
    double y{0.0};
};

inline bool operator==(const MaskPoint& a, const MaskPoint& b) {
    return a.x == b.x && a.y == b.y;
}

// A rotoscoping mask: a closed polygon that limits a clip's alpha. When
// `smooth` is set the rasteriser replaces straight edges with a closed
// Catmull-Rom spline, giving bezier-like curves through every point.
struct Mask {
    std::string id;
    std::string name{"Mask"};
    std::vector<MaskPoint> points;
    bool smooth{false};
    // Feather width as a fraction of the shorter frame side (0 = hard edge).
    double feather{0.0};
    // Overall coverage multiplier (0 hides the clip inside the mask).
    double opacity{1.0};
    bool invert{false};
    bool enabled{true};
};

inline bool operator==(const Mask& a, const Mask& b) {
    return a.id == b.id && a.name == b.name && a.points == b.points &&
           a.smooth == b.smooth && a.feather == b.feather &&
           a.opacity == b.opacity && a.invert == b.invert &&
           a.enabled == b.enabled;
}

inline void to_json(nlohmann::json& j, const MaskPoint& p) {
    j = {{"x", p.x}, {"y", p.y}};
}

inline void from_json(const nlohmann::json& j, MaskPoint& p) {
    p.x = j.value("x", 0.0);
    p.y = j.value("y", 0.0);
}

inline void to_json(nlohmann::json& j, const Mask& m) {
    j = {{"id", m.id},         {"name", m.name},
         {"points", m.points}, {"smooth", m.smooth},
         {"feather", m.feather}, {"opacity", m.opacity},
         {"invert", m.invert}, {"enabled", m.enabled}};
}

inline void from_json(const nlohmann::json& j, Mask& m) {
    m.id = j.value("id", std::string());
    m.name = j.value("name", std::string("Mask"));
    m.points.clear();
    if (j.contains("points") && j.at("points").is_array()) {
        m.points = j.at("points").get<std::vector<MaskPoint>>();
    }
    m.smooth = j.value("smooth", false);
    m.feather = j.value("feather", 0.0);
    m.opacity = j.value("opacity", 1.0);
    m.invert = j.value("invert", false);
    m.enabled = j.value("enabled", true);
}

} // namespace bl
