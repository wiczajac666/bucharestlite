#include <bl_timeline/title.hpp>

#include <gtest/gtest.h>

#include <string>

namespace bl {
namespace {

TEST(TitleSpec, DefaultsAreSensible) {
    const TitleSpec spec = defaultTitleSpec();
    EXPECT_EQ(spec.text, "Title");
    EXPECT_EQ(spec.fontFamily, "Sans");
    EXPECT_GT(spec.fontSizePx, 0.0);
    EXPECT_EQ(spec.align, TitleAlign::Center);
    EXPECT_DOUBLE_EQ(spec.anchorX, 0.5);
    EXPECT_DOUBLE_EQ(spec.anchorY, 0.5);
    EXPECT_FALSE(spec.backgroundEnabled);
    EXPECT_EQ(spec.textColor, (TitleColor{255, 255, 255, 255}));
}

TEST(TitleSpec, JsonRoundTripsEveryField) {
    TitleSpec spec;
    spec.text = "Hello\nWorld";
    spec.fontFamily = "DejaVu Sans";
    spec.fontSizePx = 120.0;
    spec.bold = true;
    spec.italic = true;
    spec.textColor = TitleColor{10, 20, 30, 40};
    spec.align = TitleAlign::Right;
    spec.anchorX = 0.25;
    spec.anchorY = 0.75;
    spec.backgroundEnabled = true;
    spec.backgroundColor = TitleColor{1, 2, 3, 4};
    spec.paddingPx = 42.0;
    spec.outlinePx = 3.0;
    spec.outlineColor = TitleColor{5, 6, 7, 8};
    spec.lineSpacing = 1.5;

    const nlohmann::json j = spec;
    const TitleSpec back = j.get<TitleSpec>();

    EXPECT_EQ(back.text, spec.text);
    EXPECT_EQ(back.fontFamily, spec.fontFamily);
    EXPECT_DOUBLE_EQ(back.fontSizePx, spec.fontSizePx);
    EXPECT_EQ(back.bold, spec.bold);
    EXPECT_EQ(back.italic, spec.italic);
    EXPECT_EQ(back.textColor, spec.textColor);
    EXPECT_EQ(back.align, spec.align);
    EXPECT_DOUBLE_EQ(back.anchorX, spec.anchorX);
    EXPECT_DOUBLE_EQ(back.anchorY, spec.anchorY);
    EXPECT_EQ(back.backgroundEnabled, spec.backgroundEnabled);
    EXPECT_EQ(back.backgroundColor, spec.backgroundColor);
    EXPECT_DOUBLE_EQ(back.paddingPx, spec.paddingPx);
    EXPECT_DOUBLE_EQ(back.outlinePx, spec.outlinePx);
    EXPECT_EQ(back.outlineColor, spec.outlineColor);
    EXPECT_DOUBLE_EQ(back.lineSpacing, spec.lineSpacing);
}

TEST(TitleSpec, AlignSerialisesAsString) {
    TitleSpec spec;
    spec.align = TitleAlign::Left;
    EXPECT_EQ(nlohmann::json(spec)["align"], "left");
    spec.align = TitleAlign::Center;
    EXPECT_EQ(nlohmann::json(spec)["align"], "center");
    spec.align = TitleAlign::Right;
    EXPECT_EQ(nlohmann::json(spec)["align"], "right");
}

TEST(TitleSpec, FromJsonFillsDefaultsForMissingFields) {
    const nlohmann::json j = nlohmann::json::object();
    const TitleSpec spec = j.get<TitleSpec>();
    const TitleSpec def = defaultTitleSpec();
    EXPECT_EQ(spec.text, def.text);
    EXPECT_EQ(spec.fontFamily, def.fontFamily);
    EXPECT_EQ(spec.align, def.align);
}

TEST(TitleSpec, FromJsonClampsOutOfRangeValues) {
    nlohmann::json j;
    j["anchorX"] = 5.0;
    j["anchorY"] = -3.0;
    j["fontSizePx"] = 0.0;
    j["lineSpacing"] = 99.0;
    j["outlinePx"] = -1.0;
    j["align"] = "nonsense";
    j["textColor"] = nlohmann::json::array({999, -5, 128, 300});
    const TitleSpec spec = j.get<TitleSpec>();

    EXPECT_DOUBLE_EQ(spec.anchorX, 1.0);
    EXPECT_DOUBLE_EQ(spec.anchorY, 0.0);
    EXPECT_GE(spec.fontSizePx, 4.0);
    EXPECT_LE(spec.lineSpacing, 4.0);
    EXPECT_DOUBLE_EQ(spec.outlinePx, 0.0);
    EXPECT_EQ(spec.align, TitleAlign::Center);  // unknown -> center
    EXPECT_EQ(spec.textColor.r, 255);
    EXPECT_EQ(spec.textColor.g, 0);
    EXPECT_EQ(spec.textColor.b, 128);
    EXPECT_EQ(spec.textColor.a, 255);
}

TEST(TitleSpec, ClampReplacesEmptyFontFamily) {
    TitleSpec spec;
    spec.fontFamily.clear();
    EXPECT_EQ(clampTitleSpec(spec).fontFamily, "Sans");
}

TEST(TitleColor, SerialisesAsFourElementArray) {
    const nlohmann::json j = TitleColor{1, 2, 3, 4};
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 4u);
    EXPECT_EQ(j[0], 1);
    EXPECT_EQ(j[3], 4);
}

} // namespace
} // namespace bl
