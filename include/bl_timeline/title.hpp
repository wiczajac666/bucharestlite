#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace bl {

// RGBA colour, 0-255 per channel. Serialised as a 4-element array.
struct TitleColor {
    uint8_t r{255};
    uint8_t g{255};
    uint8_t b{255};
    uint8_t a{255};
};

inline bool operator==(const TitleColor& x, const TitleColor& y) {
    return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
}
inline bool operator!=(const TitleColor& x, const TitleColor& y) {
    return !(x == y);
}

void to_json(nlohmann::json& j, const TitleColor& c);
void from_json(const nlohmann::json& j, TitleColor& c);

// Horizontal alignment of the text block relative to its anchor point.
enum class TitleAlign { Left, Center, Right };

// A text title: rendered to a transparent still (PNG) and placed on the
// timeline like any other clip. Deliberately Qt-free so it round-trips inside
// the project file and is unit-testable; the rasteriser lives in the UI layer
// (`bl::ui::renderTitleImage`).
//
// Geometry is resolution independent: `fontSizePx`/`paddingPx`/`outlinePx` are
// expressed against the canvas height, and the anchor is a fraction (0..1) of
// the canvas so the same spec renders correctly at any resolution.
struct TitleSpec {
    std::string text{"Title"};
    std::string fontFamily{"Sans"};
    double fontSizePx{96.0};
    bool bold{false};
    bool italic{false};

    TitleColor textColor{255, 255, 255, 255};
    TitleAlign align{TitleAlign::Center};

    // Anchor point of the text block as a fraction of the canvas. `align`
    // decides which point of the block the anchor refers to horizontally;
    // vertically the block's centre sits at `anchorY`.
    double anchorX{0.5};
    double anchorY{0.5};

    // Optional filled rectangle behind the text block.
    bool backgroundEnabled{false};
    TitleColor backgroundColor{0, 0, 0, 180};
    double paddingPx{16.0};

    // Optional text outline (drawn under the fill) for contrast over video.
    double outlinePx{0.0};
    TitleColor outlineColor{0, 0, 0, 255};

    // Multiple of the font's default line height.
    double lineSpacing{1.2};
};

// A sensible starting title: centred white text on a mid-grey canvas.
TitleSpec defaultTitleSpec();

// Clamps every numeric field into its valid range (font size, anchors, alpha,
// padding, outline, line spacing, colour channels) and trims an empty font
// family back to the default. Called by the serialiser and the UI so a
// hand-edited project cannot produce a degenerate render.
TitleSpec clampTitleSpec(TitleSpec spec);

void to_json(nlohmann::json& j, const TitleSpec& spec);
void from_json(const nlohmann::json& j, TitleSpec& spec);

} // namespace bl
