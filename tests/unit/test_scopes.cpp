#include <bl_render/scopes.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using bl::computeScopes;

// Builds a BGRA frame `w` x `h`, each pixel (r,g,b,255). `pad` extra bytes per
// row are filled with `padFill` so stride handling can be checked.
std::vector<uint8_t> frame(int w, int h, uint8_t r, uint8_t g, uint8_t b,
                           int pad = 0, uint8_t padFill = 0) {
    const int linesize = w * 4 + pad;
    std::vector<uint8_t> data(static_cast<size_t>(linesize) * h, padFill);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = data.data() + static_cast<size_t>(y) * linesize;
        for (int x = 0; x < w; ++x) {
            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = 255;
        }
    }
    return data;
}

constexpr int kCount = 4 * 3; // a 4x3 frame

TEST(VideoScopes, HistogramOfSolidWhite) {
    auto data = frame(4, 3, 255, 255, 255);
    const auto s = computeScopes(data.data(), 4, 3, 4 * 4, 256, 256, 1);

    EXPECT_EQ(s.sampleCount, static_cast<uint64_t>(kCount));
    EXPECT_EQ(s.luma[255], static_cast<uint32_t>(kCount));
    EXPECT_EQ(s.red[255], static_cast<uint32_t>(kCount));
    EXPECT_EQ(s.green[255], static_cast<uint32_t>(kCount));
    EXPECT_EQ(s.blue[255], static_cast<uint32_t>(kCount));
    EXPECT_EQ(s.histogramMax, static_cast<uint32_t>(kCount));
}

TEST(VideoScopes, HistogramOfBlackLandsInBinZero) {
    auto data = frame(4, 3, 0, 0, 0);
    const auto s = computeScopes(data.data(), 4, 3, 4 * 4, 256, 256, 1);

    EXPECT_EQ(s.luma[0], static_cast<uint32_t>(kCount));
    EXPECT_EQ(s.red[0], static_cast<uint32_t>(kCount));
    EXPECT_EQ(s.histogramMax, static_cast<uint32_t>(kCount));
}

TEST(VideoScopes, LumaUsesRec709Weights) {
    // Rec.709: Y = 0.2126R + 0.7152G + 0.0722B.
    const auto red = computeScopes(frame(2, 2, 255, 0, 0).data(), 2, 2, 8, 256,
                                   256, 1);
    EXPECT_EQ(red.luma[54], 4u);  // 0.2126*255 = 54.2

    const auto green = computeScopes(frame(2, 2, 0, 255, 0).data(), 2, 2, 8,
                                     256, 256, 1);
    EXPECT_EQ(green.luma[182], 4u);  // 0.7152*255 = 182.4

    const auto blue = computeScopes(frame(2, 2, 0, 0, 255).data(), 2, 2, 8, 256,
                                    256, 1);
    EXPECT_EQ(blue.luma[18], 4u);  // 0.0722*255 = 18.4
}

TEST(VideoScopes, WaveformSeparatesBlackAndWhiteColumns) {
    const int w = 8;
    const int h = 2;
    std::vector<uint8_t> data(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool white = x >= w / 2;
            uint8_t* p = data.data() + (static_cast<size_t>(y) * w + x) * 4;
            p[0] = p[1] = p[2] = white ? 255 : 0;
            p[3] = 255;
        }
    }

    const auto s = computeScopes(data.data(), w, h, w * 4, 2, 256, 1);
    ASSERT_EQ(s.waveformColumns, 2);
    // Left half (4 px * 2 rows) is luma 0 in column 0; right half is 255 in
    // column 1.
    EXPECT_EQ(s.waveform[0 * 256 + 0], 8u);
    EXPECT_EQ(s.waveform[1 * 256 + 255], 8u);
    EXPECT_EQ(s.waveform[0 * 256 + 255], 0u);
    EXPECT_EQ(s.waveform[1 * 256 + 0], 0u);
    EXPECT_EQ(s.waveformMax, 8u);
}

TEST(VideoScopes, VectorscopeCentresNeutralGray) {
    auto data = frame(4, 3, 128, 128, 128);
    const auto s = computeScopes(data.data(), 4, 3, 4 * 4, 256, 256, 1);

    // Neutral chroma (128) maps to the centre of the 256x256 grid.
    ASSERT_EQ(s.vectorscopeSize, 256);
    EXPECT_EQ(s.vectorscope[128 * 256 + 128], static_cast<uint32_t>(kCount));
    EXPECT_EQ(s.vectorscopeMax, static_cast<uint32_t>(kCount));
}

TEST(VideoScopes, StepSubsamplesPixels) {
    auto data = frame(4, 4, 100, 100, 100);
    const auto full = computeScopes(data.data(), 4, 4, 16, 256, 256, 1);
    EXPECT_EQ(full.sampleCount, 16u);

    const auto half = computeScopes(data.data(), 4, 4, 16, 256, 256, 2);
    EXPECT_EQ(half.sampleCount, 4u); // 2x2 sampled pixels
    EXPECT_EQ(half.luma[100], 4u);
}

TEST(VideoScopes, EmptyOrInvalidFrameIsZeroed) {
    const auto none = computeScopes(nullptr, 4, 4, 16);
    EXPECT_EQ(none.sampleCount, 0u);
    EXPECT_EQ(none.histogramMax, 0u);
    EXPECT_EQ(none.waveformColumns, 0);
    EXPECT_TRUE(none.waveform.empty());
    EXPECT_TRUE(none.vectorscope.empty());

    auto data = frame(4, 4, 10, 10, 10);
    const auto zeroWidth = computeScopes(data.data(), 0, 4, 16);
    EXPECT_EQ(zeroWidth.sampleCount, 0u);

    const auto badStride = computeScopes(data.data(), 4, 4, 0);
    EXPECT_EQ(badStride.sampleCount, 0u);

    const auto badStep = computeScopes(data.data(), 4, 4, 16, 256, 256, 0);
    EXPECT_EQ(badStep.sampleCount, 0u);
}

TEST(VideoScopes, RespectsRowStride) {
    auto tight = frame(4, 3, 200, 40, 60, 0);
    auto padded = frame(4, 3, 200, 40, 60, /*pad=*/12, /*padFill=*/255);

    const auto a = computeScopes(tight.data(), 4, 3, 4 * 4, 256, 256, 1);
    const auto b = computeScopes(padded.data(), 4, 3, 4 * 4 + 12, 256, 256, 1);

    EXPECT_EQ(a.sampleCount, b.sampleCount);
    EXPECT_EQ(a.luma, b.luma);
    EXPECT_EQ(a.red, b.red);
    EXPECT_EQ(a.green, b.green);
    EXPECT_EQ(a.blue, b.blue);
    EXPECT_EQ(a.waveform, b.waveform);
    EXPECT_EQ(a.vectorscope, b.vectorscope);
}

} // namespace
