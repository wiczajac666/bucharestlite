#include <bl_render/cube_lut.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace bl {
namespace {

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }

const char* kIdentity3D =
    "# comment\n"
    "TITLE \"Identity\"\n"
    "LUT_3D_SIZE 2\n"
    "DOMAIN_MIN 0.0 0.0 0.0\n"
    "DOMAIN_MAX 1.0 1.0 1.0\n"
    "0.0 0.0 0.0\n"  // (r0 g0 b0)
    "1.0 0.0 0.0\n"  // (r1 g0 b0)
    "0.0 1.0 0.0\n"  // (r0 g1 b0)
    "1.0 1.0 0.0\n"  // (r1 g1 b0)
    "0.0 0.0 1.0\n"  // (r0 g0 b1)
    "1.0 0.0 1.0\n"
    "0.0 1.0 1.0\n"
    "1.0 1.0 1.0\n";

TEST(CubeLut, ParsesIdentity3D) {
    const auto lut = parseCubeLut(kIdentity3D);
    ASSERT_TRUE(lut.ok()) << lut.message();
    EXPECT_EQ(lut->title, "Identity");
    EXPECT_EQ(lut->size, 2);
    EXPECT_TRUE(lut->is3d);
    EXPECT_TRUE(lut->valid());
    EXPECT_EQ(lut->entryCount(), 8u);
    EXPECT_EQ(lut->table.size(), 24u);
}

TEST(CubeLut, IdentityKeepsColorsUnchanged) {
    const auto lut = *parseCubeLut(kIdentity3D);
    const auto out = sampleCubeLut(lut, 0.25f, 0.5f, 0.75f);
    EXPECT_TRUE(near(out[0], 0.25f));
    EXPECT_TRUE(near(out[1], 0.5f));
    EXPECT_TRUE(near(out[2], 0.75f));
}

TEST(CubeLut, TrilinearInterpolatesBetweenCorners) {
    // LUT with only the (1,1,1) corner bright and red doubled at max.
    const std::string text =
        "LUT_3D_SIZE 2\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n2 2 2\n";
    const auto lut = *parseCubeLut(text);
    // Midpoint of the cube interpolates the eight corners.
    const auto out = sampleCubeLut(lut, 0.5f, 0.5f, 0.5f);
    // Average of r over the corners: (0+1+0+1+0+1+0+2)/8 = 0.625
    EXPECT_TRUE(near(out[0], 0.625f));
    // g: (0+0+1+1+0+0+1+2)/8 = 0.625
    EXPECT_TRUE(near(out[1], 0.625f));
    // b: (0+0+0+0+1+1+1+2)/8 = 0.625
    EXPECT_TRUE(near(out[2], 0.625f));
}

TEST(CubeLut, Parses1D) {
    const std::string text =
        "LUT_1D_SIZE 3\n"
        "0.0 0.0 0.0\n"
        "0.5 0.5 0.5\n"
        "1.0 1.0 1.0\n";
    const auto lut = parseCubeLut(text);
    ASSERT_TRUE(lut.ok());
    EXPECT_FALSE(lut->is3d);
    EXPECT_EQ(lut->entryCount(), 3u);
    // Linear interpolation between entries.
    const auto out = sampleCubeLut(*lut, 0.25f, 0.25f, 0.25f);
    EXPECT_TRUE(near(out[0], 0.25f));
}

TEST(CubeLut, DomainMinMaxRemapInput) {
    const std::string text =
        "LUT_3D_SIZE 2\n"
        "DOMAIN_MIN 0 0 0\n"
        "DOMAIN_MAX 2 2 2\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    const auto lut = *parseCubeLut(text);
    // Input 2.0 maps to the top of the domain (normalized 1.0).
    const auto out = sampleCubeLut(lut, 2.0f, 2.0f, 2.0f);
    EXPECT_TRUE(near(out[0], 1.0f));
    EXPECT_TRUE(near(out[1], 1.0f));
    EXPECT_TRUE(near(out[2], 1.0f));
}

TEST(CubeLut, RejectsMissingSize) {
    const auto lut = parseCubeLut("0 0 0\n1 1 1\n");
    EXPECT_FALSE(lut.ok());
    EXPECT_EQ(lut.code(), Err::InvalidArgument);
}

TEST(CubeLut, RejectsShortTable) {
    const auto lut = parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n");
    EXPECT_FALSE(lut.ok());
}

TEST(CubeLut, RejectsMalformedRow) {
    const auto lut = parseCubeLut("LUT_1D_SIZE 2\n0 0\n");
    EXPECT_FALSE(lut.ok());
}

TEST(CubeLut, InvalidLutSamplingReturnsInput) {
    CubeLut empty;  // size 0
    const auto out = sampleCubeLut(empty, 0.1f, 0.2f, 0.3f);
    EXPECT_TRUE(near(out[0], 0.1f));
    EXPECT_TRUE(near(out[1], 0.2f));
    EXPECT_TRUE(near(out[2], 0.3f));
}

TEST(CubeLut, LoadMissingFileFails) {
    const auto lut = loadCubeLutFile("/nonexistent/definitely-not-here.cube");
    EXPECT_FALSE(lut.ok());
    EXPECT_EQ(lut.code(), Err::FileNotFound);
}

} // namespace
} // namespace bl