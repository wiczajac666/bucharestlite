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

} // namespace
