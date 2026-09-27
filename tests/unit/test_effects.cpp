#include <bl_render/effect.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using bl::EffectRegistry;
using bl::IEffect;

struct EffectsTest : ::testing::Test {
    void SetUp() override {
        EffectRegistry::instance().clear();
        bl::registerBuiltinEffects();
    }
};

TEST_F(EffectsTest, RegistryLookup) {
    auto& reg = EffectRegistry::instance();
    EXPECT_NE(reg.find("blur.box"), nullptr);
    EXPECT_NE(reg.find("brightness_contrast_gamma"), nullptr);
    EXPECT_NE(reg.find("greyscale"), nullptr);
    EXPECT_NE(reg.find("transform_2d"), nullptr);
    EXPECT_NE(reg.find("chroma_key"), nullptr);
    EXPECT_NE(reg.find("sharpen"), nullptr);
    EXPECT_NE(reg.find("hue_saturation"), nullptr);
    EXPECT_NE(reg.find("levels_curves"), nullptr);
    EXPECT_NE(reg.find("crop"), nullptr);
    EXPECT_EQ(reg.find("nonexistent"), nullptr);
    EXPECT_EQ(reg.count(), 9u);
}

TEST_F(EffectsTest, BoxBlurAveragesPixels) {
    std::vector<uint8_t> data = {
        100, 100, 100, 255,  // pixel 0: grey
        200, 200, 200, 255,  // pixel 1: light
    };
    uint32_t width = 2, height = 1, linesize = 8;

    nlohmann::json params;
    params["radius"] = 1;

    IEffect* blur = EffectRegistry::instance().find("blur.box");
    ASSERT_NE(blur, nullptr);
    blur->apply(data, width, height, linesize, params);

    // With radius=1 on a 2-pixel row, edge-clamped sliding window gives:
    // pixel 0: avg of [0,0,1] = (100+100+200)/3 = 133
    // pixel 1: avg of [0,1,1] = (100+200+200)/3 = 166
    EXPECT_NEAR(data[0], 133, 2);
    EXPECT_NEAR(data[4], 166, 2);
}

TEST_F(EffectsTest, BrightnessContrastDefaultIsIdentity) {
    std::vector<uint8_t> data = {128, 64, 32, 255};
    uint32_t width = 1, height = 1, linesize = 4;
    nlohmann::json params = {};

    IEffect* bcg = EffectRegistry::instance().find("brightness_contrast_gamma");
    ASSERT_NE(bcg, nullptr);
    bcg->apply(data, width, height, linesize, params);

    EXPECT_NEAR(data[0], 128, 1);
    EXPECT_NEAR(data[1], 64, 1);
    EXPECT_NEAR(data[2], 32, 1);
}

TEST_F(EffectsTest, GreyscaleProducesEqualChannels) {
    std::vector<uint8_t> data = {100, 150, 200, 255};
    uint32_t width = 1, height = 1, linesize = 4;
    nlohmann::json params;

    IEffect* grey = EffectRegistry::instance().find("greyscale");
    ASSERT_NE(grey, nullptr);
    grey->apply(data, width, height, linesize, params);

    // Luminance: 0.299*200 + 0.587*150 + 0.114*100 = 59.8 + 88.05 + 11.4 = 159.25
    EXPECT_NEAR(data[0], 159, 2);
    EXPECT_NEAR(data[1], 159, 2);
    EXPECT_NEAR(data[2], 159, 2);
    EXPECT_EQ(data[3], 255);  // alpha untouched
}

TEST_F(EffectsTest, Transform2DIdentityIsNoop) {
    std::vector<uint8_t> original = {10, 20, 30, 255, 40, 50, 60, 255,
                                     70, 80, 90, 255, 10, 20, 30, 255};
    std::vector<uint8_t> data = original;
    uint32_t width = 2, height = 2, linesize = 8;
    nlohmann::json params = {};

    IEffect* tf = EffectRegistry::instance().find("transform_2d");
    ASSERT_NE(tf, nullptr);
    tf->apply(data, width, height, linesize, params);

    EXPECT_EQ(data, original);
}

TEST_F(EffectsTest, CatalogListsRegisteredEffects) {
    auto& reg = EffectRegistry::instance();
    const auto names = reg.catalog();
    ASSERT_EQ(names.size(), 9u);
    EXPECT_EQ(names[0], "blur.box");
    EXPECT_EQ(names[1], "brightness_contrast_gamma");
    EXPECT_EQ(names[2], "chroma_key");
    EXPECT_EQ(names[3], "crop");
    EXPECT_EQ(names[4], "greyscale");
    EXPECT_EQ(names[5], "hue_saturation");
    EXPECT_EQ(names[6], "levels_curves");
    EXPECT_EQ(names[7], "sharpen");
    EXPECT_EQ(names[8], "transform_2d");
}

TEST_F(EffectsTest, DisplayNamesAreHumanReadable) {
    auto& reg = EffectRegistry::instance();
    EXPECT_EQ(reg.find("blur.box")->displayName(), "Box Blur");
    EXPECT_EQ(reg.find("brightness_contrast_gamma")->displayName(),
              "Color Correction");
    EXPECT_EQ(reg.find("greyscale")->displayName(), "Greyscale");
    EXPECT_EQ(reg.find("transform_2d")->displayName(), "Transform 2D");
    EXPECT_EQ(reg.find("chroma_key")->displayName(), "Chroma Key");
    EXPECT_EQ(reg.find("sharpen")->displayName(), "Sharpen");
    EXPECT_EQ(reg.find("hue_saturation")->displayName(),
              "Hue / Saturation");
    EXPECT_EQ(reg.find("levels_curves")->displayName(), "Levels / Curves");
    EXPECT_EQ(reg.find("crop")->displayName(), "Crop");
}

TEST_F(EffectsTest, ColorEffectExposesParamSpecs) {
    auto& reg = EffectRegistry::instance();
    IEffect* bcg = reg.find("brightness_contrast_gamma");
    ASSERT_NE(bcg, nullptr);

    const auto specs = bcg->paramSpecs();
    ASSERT_EQ(specs.size(), 3u);

    EXPECT_EQ(specs[0].key, "brightness");
    EXPECT_EQ(specs[0].min, -1.0);
    EXPECT_EQ(specs[0].max, 1.0);
    EXPECT_EQ(specs[0].def, 0.0);

    EXPECT_EQ(specs[1].key, "contrast");
    EXPECT_EQ(specs[1].def, 1.0);

    EXPECT_EQ(specs[2].key, "gamma");
    EXPECT_NEAR(specs[2].min, 0.1, 1e-9);
    EXPECT_NEAR(specs[2].def, 1.0, 1e-9);
}

TEST_F(EffectsTest, MakeDefaultParamsMatchesSpecs) {
    const std::vector<bl::ParamSpec> specs = {
        {"brightness", "Brightness", -1.0, 1.0, 0.0},
        {"contrast", "Contrast", 0.0, 3.0, 1.0},
        {"gamma", "Gamma", 0.1, 5.0, 1.0},
    };
    const nlohmann::json params = bl::makeDefaultParams(specs);
    EXPECT_EQ(params["brightness"], 0.0);
    EXPECT_EQ(params["contrast"], 1.0);
    EXPECT_EQ(params["gamma"], 1.0);
}

TEST_F(EffectsTest, MakeDefaultParamsEmptyForNoSpecs) {
    EXPECT_TRUE(bl::makeDefaultParams({}).empty());
}

TEST_F(EffectsTest, BrightnessLiftsPixelValues) {
    std::vector<uint8_t> data = {128, 100, 64, 255};
    uint32_t width = 1, height = 1, linesize = 4;
    nlohmann::json params = {{"brightness", 0.2}, {"contrast", 1.0},
                             {"gamma", 1.0}};

    IEffect* bcg = EffectRegistry::instance().find("brightness_contrast_gamma");
    ASSERT_NE(bcg, nullptr);
    bcg->apply(data, width, height, linesize, params);

    EXPECT_GT(data[2], 64); // red lifted from 64
}

TEST_F(EffectsTest, ChromaKeyKeysOutKeyColor) {
    std::vector<uint8_t> data = {
        0, 255, 0, 255,   // pixel 0: pure green (key)
        128, 128, 128, 255,  // pixel 1: unrelated grey
    };
    uint32_t width = 2, height = 1, linesize = 8;
    nlohmann::json params = {{"keyR", 0},
                             {"keyG", 255},
                             {"keyB", 0},
                             {"similarity", 0.15},
                             {"smoothness", 0.1}};

    IEffect* key = EffectRegistry::instance().find("chroma_key");
    ASSERT_NE(key, nullptr);
    key->apply(data, width, height, linesize, params);

    EXPECT_EQ(data[3], 0u);   // green keyed out
    EXPECT_EQ(data[7], 255u); // grey kept opaque
}

TEST_F(EffectsTest, ChromaKeyDefaultIsIdentityForOtherwiseOpaque) {
    std::vector<uint8_t> data = {140, 40, 10, 255};
    uint32_t width = 1, height = 1, linesize = 4;
    IEffect* key = EffectRegistry::instance().find("chroma_key");
    ASSERT_NE(key, nullptr);
    key->apply(data, width, height, linesize, nlohmann::json::object());
    EXPECT_EQ(data[3], 255u);
}

TEST_F(EffectsTest, SharpenPullsPixelsAwayFromNeighbors) {
    std::vector<uint8_t> data = {
        20, 20, 20, 255,   // dark
        220, 220, 220, 255,  // light
    };
    uint32_t width = 2, height = 1, linesize = 8;
    nlohmann::json params = {{"amount", 2.0}, {"radius", 1}};

    IEffect* sharpen = EffectRegistry::instance().find("sharpen");
    ASSERT_NE(sharpen, nullptr);
    sharpen->apply(data, width, height, linesize, params);

    // The dark pixel gets darker, the light pixel gets lighter.
    EXPECT_LT(data[2], 20);
    EXPECT_GT(data[6], 220);
}

TEST_F(EffectsTest, HueShiftTurnsRedGreen) {
    std::vector<uint8_t> data = {0, 0, 255, 255};  // red
    uint32_t width = 1, height = 1, linesize = 4;
    nlohmann::json params = {{"hueShift", 120.0}, {"saturation", 1.0},
                             {"lightness", 0.0}};

    IEffect* hue = EffectRegistry::instance().find("hue_saturation");
    ASSERT_NE(hue, nullptr);
    hue->apply(data, width, height, linesize, params);

    EXPECT_NEAR(data[1], 255, 3);  // G ≈ 255
    EXPECT_NEAR(data[2], 0, 3);    // R ≈ 0
}

TEST_F(EffectsTest, HueSaturationDefaultsAreIdentity) {
    std::vector<uint8_t> data = {70, 200, 160, 255};
    uint32_t width = 1, height = 1, linesize = 4;
    const auto original = data;
    IEffect* hue = EffectRegistry::instance().find("hue_saturation");
    ASSERT_NE(hue, nullptr);
    hue->apply(data, width, height, linesize, nlohmann::json::object());
    EXPECT_EQ(data, original);
}

TEST_F(EffectsTest, LevelsCurvesDefaultsAreIdentity) {
    std::vector<uint8_t> data = {123, 64, 200, 255};
    uint32_t width = 1, height = 1, linesize = 4;
    const auto original = data;
    IEffect* lc = EffectRegistry::instance().find("levels_curves");
    ASSERT_NE(lc, nullptr);
    lc->apply(data, width, height, linesize, nlohmann::json::object());
    EXPECT_EQ(data, original);
}

TEST_F(EffectsTest, LevelsCurvesBlackPointClipsLowValues) {
    std::vector<uint8_t> data = {64, 100, 120, 255};
    uint32_t width = 1, height = 1, linesize = 4;
    nlohmann::json params = {{"black", 0.5}, {"white", 1.0},
                             {"gamma", 1.0}, {"c0", 0.0}, {"c1", 0.25},
                             {"c2", 0.5}, {"c3", 0.75}, {"c4", 1.0}};

    IEffect* lc = EffectRegistry::instance().find("levels_curves");
    ASSERT_NE(lc, nullptr);
    lc->apply(data, width, height, linesize, params);

    EXPECT_EQ(data[0], 0u);
    EXPECT_EQ(data[1], 0u);
    EXPECT_EQ(data[2], 0u);
}

TEST_F(EffectsTest, CropStretchesCenterRegionToFullFrame) {
    // 4x1 frame: [black, black, white, white]. Crop left 25%/right 25% keeps
    // the middle half (pixels 1..2 = black+white boundaries) resampled across
    // the full frame.
    std::vector<uint8_t> data = {
        0, 0, 0, 255, 0, 0, 0, 255,
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    uint32_t width = 4, height = 1, linesize = 16;
    nlohmann::json params = {{"left", 0.25}, {"right", 0.25},
                             {"top", 0.0}, {"bottom", 0.0}};

    IEffect* crop = EffectRegistry::instance().find("crop");
    ASSERT_NE(crop, nullptr);
    crop->apply(data, width, height, linesize, params);

    // Left 25% of the output no longer samples the pure-black edge but the
    // region interior; x=0 → fu=1.0 (original black pixel 1), x=2 → ~1.67
    // (mix leaning white) ⇒ a gradient across the frame.
    EXPECT_EQ(data[2], 0u);         // R of pixel 0 (black interior)
    EXPECT_GT(data[10], 128);       // R of pixel 2 leans white
}

TEST_F(EffectsTest, CropDefaultsAreIdentity) {
    std::vector<uint8_t> data = {10, 20, 30, 255, 40, 50, 60, 255};
    uint32_t width = 2, height = 1, linesize = 8;
    const auto original = data;
    IEffect* crop = EffectRegistry::instance().find("crop");
    ASSERT_NE(crop, nullptr);
    crop->apply(data, width, height, linesize, nlohmann::json::object());
    EXPECT_EQ(data, original);
}

} // namespace
