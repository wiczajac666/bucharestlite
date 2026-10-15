#include <bl_render/quality_metrics.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>

#include <bl_plugins/codec_plugin.h>

#include "test_media_utils.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

namespace {

using bl::CodecRegistry;
using bl::PluginHandle;
using bl::PluginLoader;
using bl::PluginOrigin;

// Passthrough builtins plus the staged codec plugins; kept alive for the
// process lifetime so the registry's raw plugin pointers stay valid.
const CodecRegistry& registry() {
    static CodecRegistry instance;
    static std::vector<std::unique_ptr<PluginHandle>> handles = [] {
        std::vector<std::unique_ptr<PluginHandle>> out;
        (void)bl::registerBuiltins(instance);
        PluginLoader loader;
        auto report = loader.scanDirectory(
            std::string(BL_TEST_PLUGIN_DIR) + "/plugins", PluginOrigin::User);
        if (report.ok()) {
            for (auto& handle : report.value().plugins) {
                auto loaded = instance.registerPlugin(
                    const_cast<BlCodecPlugin*>(handle->plugin()));
                if (loaded.ok()) {
                    out.push_back(std::move(handle));
                }
            }
        }
        return out;
    }();
    (void)handles;
    return instance;
}

// A solid BGRA image with the given B, G, R channel values.
std::vector<uint8_t> solidBgra(uint32_t width, uint32_t height, uint8_t b,
                               uint8_t g, uint8_t r) {
    std::vector<uint8_t> img(static_cast<size_t>(width) * height * 4u, 0xFFu);
    for (size_t i = 0; i < img.size(); i += 4) {
        img[i + 0] = b;
        img[i + 1] = g;
        img[i + 2] = r;
        img[i + 3] = 0xFFu;
    }
    return img;
}

// A horizontal luma ramp, so windows carry real variance.
std::vector<uint8_t> rampBgra(uint32_t width, uint32_t height) {
    std::vector<uint8_t> img(static_cast<size_t>(width) * height * 4u, 0xFFu);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t v = static_cast<uint8_t>((x * 255u) / (width - 1));
            uint8_t* p = img.data() + (static_cast<size_t>(y) * width + x) * 4u;
            p[0] = p[1] = p[2] = v;
        }
    }
    return img;
}

}  // namespace

TEST(QualityMetricsTest, MseIsZeroForIdenticalImages) {
    const auto a = solidBgra(16, 16, 10, 20, 30);
    EXPECT_DOUBLE_EQ(bl::bgraMse(a.data(), a.data(), 16, 16, 16 * 4u, 16 * 4u),
                     0.0);
}

TEST(QualityMetricsTest, MseOfConstantOffset) {
    const auto a = solidBgra(16, 16, 0, 0, 0);
    auto b = a;
    for (size_t i = 0; i < b.size(); i += 4) {
        b[i + 0] = 10;
        b[i + 1] = 10;
        b[i + 2] = 10;
    }
    // Every channel differs by 10, so MSE == 100.
    EXPECT_NEAR(bl::bgraMse(a.data(), b.data(), 16, 16, 16 * 4u, 16 * 4u), 100.0,
                1e-9);
}

TEST(QualityMetricsTest, PsnrFromMseMatchesKnownValue) {
    EXPECT_DOUBLE_EQ(bl::psnrFromMse(0.0), bl::kPsnrIdentical);
    // 10 * log10(255^2 / 100) == 10 * log10(650.25) == ~28.13 dB.
    EXPECT_NEAR(bl::psnrFromMse(100.0), 28.1313, 1e-3);
    // PSNR is capped, never infinite.
    EXPECT_DOUBLE_EQ(bl::psnrFromMse(1e-12), bl::kPsnrIdentical);
}

TEST(QualityMetricsTest, SsimIsOneForIdenticalImages) {
    const auto img = rampBgra(32, 32);
    EXPECT_NEAR(bl::bgraSsim(img.data(), img.data(), 32, 32, 32 * 4u, 32 * 4u),
                1.0, 1e-9);
}

TEST(QualityMetricsTest, SsimIsNearZeroForBlackAgainstWhite) {
    const auto black = solidBgra(32, 32, 0, 0, 0);
    const auto white = solidBgra(32, 32, 255, 255, 255);
    const double ssim =
        bl::bgraSsim(black.data(), white.data(), 32, 32, 32 * 4u, 32 * 4u);
    EXPECT_LT(ssim, 0.01);
    EXPECT_GT(ssim, -0.01);
}

TEST(QualityMetricsTest, SsimDropsWhenContentDiffers) {
    const auto a = rampBgra(32, 32);
    const auto b = solidBgra(32, 32, 128, 128, 128);
    const double ssim = bl::bgraSsim(a.data(), b.data(), 32, 32, 32 * 4u, 32 * 4u);
    EXPECT_LT(ssim, 1.0);
}

TEST(QualityMetricsTest, ComparesAFileAgainstItselfAsLossless) {
    const std::string path = bltest::mediaPath("test_video.mp4");
    auto report = bl::compareVideo(path, path, registry());
    ASSERT_TRUE(report.ok()) << report.message();
    const bl::QualityReport& r = report.value();

    EXPECT_GT(r.framesCompared, 0);
    EXPECT_FALSE(r.frameCountMismatch);
    EXPECT_GT(r.width, 0u);
    EXPECT_GT(r.height, 0u);
    EXPECT_DOUBLE_EQ(r.averagePsnr, bl::kPsnrIdentical);
    EXPECT_DOUBLE_EQ(r.minPsnr, bl::kPsnrIdentical);
    EXPECT_NEAR(r.averageSsim, 1.0, 1e-6);
    EXPECT_EQ(r.frames.size(), static_cast<size_t>(r.framesCompared));
}

TEST(QualityMetricsTest, TransmuxedMatroskaMatchesMp4) {
    // test_video.mkv is a stream-copy of test_video.mp4, so the decoded frames
    // are bit-identical even though the containers differ.
    auto report = bl::compareVideo(bltest::mediaPath("test_video.mp4"),
                                   bltest::mediaPath("test_video.mkv"),
                                   registry());
    ASSERT_TRUE(report.ok()) << report.message();
    EXPECT_GT(report.value().framesCompared, 0);
    EXPECT_DOUBLE_EQ(report.value().averagePsnr, bl::kPsnrIdentical);
    EXPECT_NEAR(report.value().averageSsim, 1.0, 1e-6);
}

TEST(QualityMetricsTest, MaxFramesBoundsTheComparison) {
    auto report = bl::compareVideo(bltest::mediaPath("test_video.mp4"),
                                   bltest::mediaPath("test_video.mp4"),
                                   registry(), /*maxFrames=*/2);
    ASSERT_TRUE(report.ok()) << report.message();
    EXPECT_EQ(report.value().framesCompared, 2);
    EXPECT_EQ(report.value().frames.size(), 2u);
}

TEST(QualityMetricsTest, RejectsEmptyPathsAndAudioOnlyInputs) {
    EXPECT_FALSE(bl::compareVideo("", bltest::mediaPath("test_video.mp4"),
                                  registry())
                     .ok());
    EXPECT_FALSE(bl::compareVideo(bltest::mediaPath("test_video.mp4"), "",
                                  registry())
                     .ok());
    // No video stream to compare.
    EXPECT_FALSE(bl::compareVideo(bltest::mediaPath("test_audio.wav"),
                                  bltest::mediaPath("test_audio.wav"), registry())
                     .ok());
}
