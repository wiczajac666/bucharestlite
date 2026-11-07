#include <bl_render/mask_raster.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/mask.hpp>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

namespace {

using bl::Clip;
using bl::Mask;
using bl::MaskPoint;

Mask rectangle(double x0, double y0, double x1, double y1) {
    Mask m;
    m.id = "m1";
    m.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    return m;
}

uint8_t coverageAt(const std::vector<uint8_t>& coverage, uint32_t width,
                   uint32_t x, uint32_t y) {
    return coverage[static_cast<size_t>(y) * width + x];
}

TEST(MaskModel, JsonRoundTripPreservesEveryField) {
    Mask mask;
    mask.id = "mask:7";
    mask.name = "Person";
    mask.points = {{0.1, 0.2}, {0.3, 0.4}, {0.5, 0.6}};
    mask.smooth = true;
    mask.feather = 0.075;
    mask.opacity = 0.8;
    mask.invert = true;
    mask.enabled = false;

    const nlohmann::json json = mask;
    const Mask restored = json.get<Mask>();
    EXPECT_EQ(restored, mask);
}

TEST(MaskModel, ClipRoundTripKeepsMasks) {
    Clip clip;
    clip.id = "c1";
    clip.name = "clip";
    clip.source.mediaItemId = "media";
    Mask mask = rectangle(0.0, 0.0, 0.5, 1.0);
    clip.masks = {mask};

    const nlohmann::json json = clip;
    const Clip restored = json.get<Clip>();
    ASSERT_EQ(restored.masks.size(), 1u);
    EXPECT_EQ(restored.masks.front(), mask);

    // Clips without masks keep the key out of the document.
    Clip plain = clip;
    plain.masks.clear();
    const nlohmann::json plainJson = plain;
    EXPECT_FALSE(plainJson.contains("masks"));
}

TEST(MaskRaster, LeftHalfPolygonCoversLeftPixelsOnly) {
    const Mask mask = rectangle(0.0, 0.0, 0.5, 1.0);
    const auto coverage = bl::rasteriseMask(mask, 40, 20);
    ASSERT_EQ(coverage.size(), 40u * 20u);

    EXPECT_EQ(coverageAt(coverage, 40, 5, 10), 255u);
    EXPECT_EQ(coverageAt(coverage, 40, 18, 10), 255u);
    EXPECT_EQ(coverageAt(coverage, 40, 25, 10), 0u);
    EXPECT_EQ(coverageAt(coverage, 40, 39, 0), 0u);
}

TEST(MaskRaster, FeatherSoftensTheEdge) {
    Mask mask = rectangle(0.0, 0.0, 0.5, 1.0);
    mask.feather = 0.1; // radius = 4 px at 40x40
    const auto coverage = bl::rasteriseMask(mask, 40, 40);

    EXPECT_EQ(coverageAt(coverage, 40, 2, 20), 255u);
    const uint8_t edge = coverageAt(coverage, 40, 20, 20);
    EXPECT_GT(edge, 90u);
    EXPECT_LT(edge, 165u);
    EXPECT_EQ(coverageAt(coverage, 40, 38, 20), 0u);
}

TEST(MaskRaster, InvertAndOpacityScaleCoverage) {
    Mask mask = rectangle(0.0, 0.0, 0.5, 1.0);
    mask.invert = true;
    auto coverage = bl::rasteriseMask(mask, 40, 20);
    EXPECT_EQ(coverageAt(coverage, 40, 25, 10), 255u);
    EXPECT_EQ(coverageAt(coverage, 40, 5, 10), 0u);

    mask.invert = false;
    mask.opacity = 0.5;
    coverage = bl::rasteriseMask(mask, 40, 20);
    const uint8_t inside = coverageAt(coverage, 40, 5, 10);
    EXPECT_GT(inside, 120u);
    EXPECT_LT(inside, 135u);
}

TEST(MaskRaster, SmoothCurveStillCoversItsControlCentre) {
    Mask mask = rectangle(0.2, 0.2, 0.8, 0.8);
    mask.smooth = true;
    const auto coverage = bl::rasteriseMask(mask, 40, 40);
    EXPECT_EQ(coverageAt(coverage, 40, 20, 20), 255u);
    EXPECT_EQ(coverageAt(coverage, 40, 1, 1), 0u);
}

TEST(MaskRaster, UnionOfMasksCoversBothAreas) {
    Mask left = rectangle(0.0, 0.0, 0.25, 1.0);
    left.id = "left";
    Mask right = rectangle(0.75, 0.0, 1.0, 1.0);
    right.id = "right";

    const auto coverage = bl::rasteriseMasks({left, right}, 40, 10);
    EXPECT_EQ(coverageAt(coverage, 40, 3, 5), 255u);
    EXPECT_EQ(coverageAt(coverage, 40, 20, 5), 0u);
    EXPECT_EQ(coverageAt(coverage, 40, 36, 5), 255u);
}

TEST(MaskRaster, DisabledAndDegenerateMasksDoNotMask) {
    Mask disabled = rectangle(0.0, 0.0, 0.5, 1.0);
    disabled.enabled = false;
    auto coverage = bl::rasteriseMasks({disabled}, 20, 10);
    EXPECT_TRUE(coverage.empty());

    Mask partial;
    partial.points = {{0.1, 0.1}, {0.2, 0.2}};
    coverage = bl::rasteriseMask(partial, 20, 10);
    for (uint8_t value : coverage) EXPECT_EQ(value, 255u);
}

TEST(MaskRaster, ApplyCoverageScalesAlphaOnly) {
    std::vector<uint8_t> frame = {10, 20, 30, 200, 40, 50, 60, 255};
    const std::vector<uint8_t> coverage = {128, 255};
    bl::applyMaskCoverage(frame, 2, 1, 8, coverage);

    EXPECT_EQ(frame[0], 10u);
    EXPECT_EQ(frame[1], 20u);
    EXPECT_EQ(frame[2], 30u);
    EXPECT_EQ(frame[3], 100u); // 200 * 128 / 255 rounded
    EXPECT_EQ(frame[7], 255u);

    // A mismatched buffer is ignored rather than corrupting the frame.
    bl::applyMaskCoverage(frame, 2, 1, 8, {255});
    EXPECT_EQ(frame[3], 100u);
}

} // namespace
