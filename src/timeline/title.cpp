#include <bl_timeline/title.hpp>

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

const char* alignToString(TitleAlign align) {
    switch (align) {
        case TitleAlign::Left: return "left";
        case TitleAlign::Right: return "right";
        case TitleAlign::Center: break;
    }
    return "center";
}

TitleAlign alignFromString(const std::string& s) {
    if (s == "left") return TitleAlign::Left;
    if (s == "right") return TitleAlign::Right;
    return TitleAlign::Center;
}

double clampd(double v, double lo, double hi) {
    if (std::isnan(v)) return lo;
    return std::min(hi, std::max(lo, v));
}

uint8_t clampByte(int v) {
    return static_cast<uint8_t>(std::min(255, std::max(0, v)));
}

} // namespace

TitleSpec defaultTitleSpec() { return TitleSpec{}; }

TitleSpec clampTitleSpec(TitleSpec spec) {
    spec.fontSizePx = clampd(spec.fontSizePx, 4.0, 4000.0);
    spec.anchorX = clampd(spec.anchorX, 0.0, 1.0);
    spec.anchorY = clampd(spec.anchorY, 0.0, 1.0);
    spec.paddingPx = clampd(spec.paddingPx, 0.0, 1000.0);
    spec.outlinePx = clampd(spec.outlinePx, 0.0, 200.0);
    spec.lineSpacing = clampd(spec.lineSpacing, 0.5, 4.0);
    if (spec.fontFamily.empty()) spec.fontFamily = "Sans";
    return spec;
}

void to_json(nlohmann::json& j, const TitleColor& c) {
    j = nlohmann::json::array({c.r, c.g, c.b, c.a});
}

void from_json(const nlohmann::json& j, TitleColor& c) {
    if (!j.is_array() || j.size() != 4) return;
    auto at = [&j](size_t i, int def) {
        return j[i].is_number_integer() ? j[i].get<int>() : def;
    };
    c.r = clampByte(at(0, c.r));
    c.g = clampByte(at(1, c.g));
    c.b = clampByte(at(2, c.b));
    c.a = clampByte(at(3, c.a));
}

void to_json(nlohmann::json& j, const TitleSpec& spec) {
    j = nlohmann::json{
        {"text", spec.text},
        {"fontFamily", spec.fontFamily},
        {"fontSizePx", spec.fontSizePx},
        {"bold", spec.bold},
        {"italic", spec.italic},
        {"textColor", spec.textColor},
        {"align", alignToString(spec.align)},
        {"anchorX", spec.anchorX},
        {"anchorY", spec.anchorY},
        {"backgroundEnabled", spec.backgroundEnabled},
        {"backgroundColor", spec.backgroundColor},
        {"paddingPx", spec.paddingPx},
        {"outlinePx", spec.outlinePx},
        {"outlineColor", spec.outlineColor},
        {"lineSpacing", spec.lineSpacing},
    };
}

void from_json(const nlohmann::json& j, TitleSpec& spec) {
    spec = defaultTitleSpec();
    if (!j.is_object()) return;

    spec.text = j.value("text", spec.text);
    spec.fontFamily = j.value("fontFamily", spec.fontFamily);
    spec.fontSizePx = j.value("fontSizePx", spec.fontSizePx);
    spec.bold = j.value("bold", spec.bold);
    spec.italic = j.value("italic", spec.italic);
    if (j.contains("textColor")) j.at("textColor").get_to(spec.textColor);
    spec.align = alignFromString(j.value("align", std::string("center")));
    spec.anchorX = j.value("anchorX", spec.anchorX);
    spec.anchorY = j.value("anchorY", spec.anchorY);
    spec.backgroundEnabled = j.value("backgroundEnabled", spec.backgroundEnabled);
    if (j.contains("backgroundColor"))
        j.at("backgroundColor").get_to(spec.backgroundColor);
    spec.paddingPx = j.value("paddingPx", spec.paddingPx);
    spec.outlinePx = j.value("outlinePx", spec.outlinePx);
    if (j.contains("outlineColor"))
        j.at("outlineColor").get_to(spec.outlineColor);
    spec.lineSpacing = j.value("lineSpacing", spec.lineSpacing);

    spec = clampTitleSpec(std::move(spec));
}

} // namespace bl
