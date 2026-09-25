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
    EXPECT_EQ(reg.find("nonexistent"), nullptr);
    EXPECT_EQ(reg.count(), 4u);
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
    ASSERT_EQ(names.size(), 4u);
    EXPECT_EQ(names[0], "blur.box");
    EXPECT_EQ(names[1], "brightness_contrast_gamma");
    EXPECT_EQ(names[2], "greyscale");
    EXPECT_EQ(names[3], "transform_2d");
}

TEST_F(EffectsTest, DisplayNamesAreHumanReadable) {
    auto& reg = EffectRegistry::instance();
    EXPECT_EQ(reg.find("blur.box")->displayName(), "Box Blur");
    EXPECT_EQ(reg.find("brightness_contrast_gamma")->displayName(),
              "Color Correction");
    EXPECT_EQ(reg.find("greyscale")->displayName(), "Greyscale");
    EXPECT_EQ(reg.find("transform_2d")->displayName(), "Transform 2D");
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

} // namespace
