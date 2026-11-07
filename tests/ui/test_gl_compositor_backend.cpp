#include <bl_render/compositor_backend.hpp>

#include <app/project_controller.hpp>
#include <panels/preview_panel.hpp>
#include <render/gl_compositor_backend.hpp>

#include <QApplication>
#include <QByteArray>
#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

QApplication* ensureApp() {
    static QApplication* app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char argv0[] = "bl_ui_tests";
        static char* argv[] = {argv0, nullptr};
        return new QApplication(argc, argv);
    }();
    return app;
}

struct AppBootstrapper {
    AppBootstrapper() { ensureApp(); }
};
const AppBootstrapper kAppBootstrapper{};

// Sets an environment variable for the duration of a test and restores the
// previous value afterwards.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        hadPrevious_ = qEnvironmentVariableIsSet(name);
        if (hadPrevious_) previous_ = qgetenv(name);
        if (value != nullptr) {
            qputenv(name, value);
        } else {
            qunsetenv(name);
        }
    }

    ~ScopedEnv() {
        if (hadPrevious_) {
            qputenv(name_.c_str(), previous_);
        } else {
            qunsetenv(name_.c_str());
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
    QByteArray previous_;
    bool hadPrevious_{false};
};

using bl::CompositeLayer;
using bl::ICompositorBackend;

CompositeLayer solidLayer(uint32_t width, uint32_t height, uint8_t b, uint8_t g,
                          uint8_t r, uint8_t a, double opacity = 1.0) {
    CompositeLayer layer;
    layer.width = width;
    layer.height = height;
    layer.linesize = width * 4;
    layer.opacity = opacity;
    layer.data.resize(static_cast<size_t>(layer.linesize) * height);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row =
            layer.data.data() + static_cast<size_t>(y) * layer.linesize;
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = a;
        }
    }
    return layer;
}

// A layer with a horizontal alpha ramp, to exercise per-pixel alpha (the case
// where a fixed-function blend would diverge from the CPU arithmetic).
CompositeLayer alphaRampLayer(uint32_t width, uint32_t height) {
    CompositeLayer layer;
    layer.width = width;
    layer.height = height;
    layer.linesize = width * 4;
    layer.data.resize(static_cast<size_t>(layer.linesize) * height);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row =
            layer.data.data() + static_cast<size_t>(y) * layer.linesize;
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 4 + 0] = static_cast<uint8_t>(60 + x * 3);
            row[x * 4 + 1] = static_cast<uint8_t>(200 - x * 2);
            row[x * 4 + 2] = static_cast<uint8_t>(20 + y * 4);
            row[x * 4 + 3] =
                static_cast<uint8_t>(std::min<uint32_t>(255, x * 8));
        }
    }
    return layer;
}

std::vector<CompositeLayer> sampleLayers(uint32_t width, uint32_t height) {
    std::vector<CompositeLayer> layers;
    layers.push_back(solidLayer(width, height, 0, 0, 255, 255, 0.75));
    layers.push_back(alphaRampLayer(width, height));
    // A smaller, inset layer exercises the "outside the layer" branch.
    layers.push_back(solidLayer(width / 2, height / 2, 255, 255, 0, 200, 0.6));
    return layers;
}

TEST(GlCompositorBackend, CreatesOrFallsBackCleanly) {
    auto backend = bl::ui::makeGlCompositorBackend();
    if (!backend) {
        GTEST_SKIP() << "OpenGL 3.3 offscreen context unavailable on this host";
    }
    EXPECT_EQ(backend->name(), "opengl");
    EXPECT_TRUE(backend->gpu());
}

TEST(GlCompositorBackend, MatchesCpuBlendWithinRounding) {
    auto gpu = bl::ui::makeGlCompositorBackend();
    if (!gpu) {
        GTEST_SKIP() << "OpenGL 3.3 offscreen context unavailable on this host";
    }
    auto cpu = bl::makeCpuCompositorBackend();

    constexpr uint32_t kWidth = 41;
    constexpr uint32_t kHeight = 17;
    const uint32_t stride = kWidth * 4;
    const auto layers = sampleLayers(kWidth, kHeight);

    std::vector<uint8_t> expected(static_cast<size_t>(stride) * kHeight, 0);
    std::vector<uint8_t> actual(static_cast<size_t>(stride) * kHeight, 0);
    cpu->composite(expected, kWidth, kHeight, stride, layers);
    gpu->composite(actual, kWidth, kHeight, stride, layers);

    size_t compared = 0;
    int maxDelta = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        const int delta = std::abs(static_cast<int>(actual[i]) -
                                   static_cast<int>(expected[i]));
        maxDelta = std::max(maxDelta, delta);
        ++compared;
    }
    EXPECT_EQ(compared, expected.size());
    EXPECT_LE(maxDelta, 2) << "GPU compositor drifted from the CPU reference";
}

TEST(GlCompositorBackend, EmptyStackProducesTransparentFrame) {
    auto gpu = bl::ui::makeGlCompositorBackend();
    if (!gpu) {
        GTEST_SKIP() << "OpenGL 3.3 offscreen context unavailable on this host";
    }
    constexpr uint32_t kWidth = 8;
    constexpr uint32_t kHeight = 4;
    std::vector<uint8_t> out(static_cast<size_t>(kWidth) * kHeight * 4, 0xFF);
    gpu->composite(out, kWidth, kHeight, kWidth * 4, {});
    for (uint8_t value : out) EXPECT_EQ(value, 0u);
}

TEST(GlCompositorBackend, ReusesOneBackendAcrossSizeChanges) {
    auto gpu = bl::ui::makeGlCompositorBackend();
    if (!gpu) {
        GTEST_SKIP() << "OpenGL 3.3 offscreen context unavailable on this host";
    }
    const std::vector<std::pair<uint32_t, uint32_t>> sizes = {
        {16, 9}, {32, 18}, {16, 9}};
    for (const auto& [width, height] : sizes) {
        std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4, 0);
        gpu->composite(out, width, height, width * 4,
                       {solidLayer(width, height, 10, 20, 30, 255)});
        EXPECT_EQ(out[0], 10u);
        EXPECT_EQ(out[1], 20u);
        EXPECT_EQ(out[2], 30u);
        EXPECT_EQ(out[3], 255u);
    }
}

TEST(GlCompositorBackend, ReportsThroughputVersusCpu) {
    auto gpu = bl::ui::makeGlCompositorBackend();
    if (!gpu) {
        GTEST_SKIP() << "OpenGL 3.3 offscreen context unavailable on this host";
    }
    auto cpu = bl::makeCpuCompositorBackend();

    constexpr uint32_t kWidth = 640;
    constexpr uint32_t kHeight = 360;
    constexpr int kFrames = 20;
    const uint32_t stride = kWidth * 4;
    const auto layers = sampleLayers(kWidth, kHeight);
    std::vector<uint8_t> out(static_cast<size_t>(stride) * kHeight, 0);

    const auto cpuStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i) {
        cpu->composite(out, kWidth, kHeight, stride, layers);
    }
    const auto cpuEnd = std::chrono::steady_clock::now();

    const auto gpuStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i) {
        gpu->composite(out, kWidth, kHeight, stride, layers);
    }
    const auto gpuEnd = std::chrono::steady_clock::now();

    const double cpuMs =
        std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count();
    const double gpuMs =
        std::chrono::duration<double, std::milli>(gpuEnd - gpuStart).count();
    std::cout << "[ GPU compositor ] " << kFrames << " frames at " << kWidth
              << "x" << kHeight << ": CPU " << cpuMs << " ms, GPU " << gpuMs
              << " ms" << std::endl;

    // Not a performance assertion (software rasterisers are common in CI),
    // but each backend must actually finish the work.
    EXPECT_GT(cpuMs, 0.0);
    EXPECT_GT(gpuMs, 0.0);
    EXPECT_LT(gpuMs, 30000.0);
}

TEST(GlCompositorBackend, PreviewPanelUsesCpuByDefault) {
    ensureApp();
    ScopedEnv mode("BL_COMPOSITOR", nullptr);

    bl::ui::ProjectController controller;
    controller.newProject(QStringLiteral("Compositor"));
    bl::ui::PreviewPanel panel(&controller);
    panel.resize(320, 240);
    panel.show();
    QApplication::processEvents();

    EXPECT_EQ(panel.compositorBackendName(), "cpu");
    EXPECT_FALSE(panel.usingGpu());
}

TEST(GlCompositorBackend, PreviewPanelOptsIntoGpuWhenAvailable) {
    ensureApp();
    ScopedEnv mode("BL_COMPOSITOR", "gpu");

    // Probe for the same context the panel will request, so the expectation
    // follows the host rather than assuming a GPU is present.
    const auto probe = bl::ui::makeGlCompositorBackend();

    bl::ui::ProjectController controller;
    controller.newProject(QStringLiteral("Compositor"));
    bl::ui::PreviewPanel panel(&controller);
    panel.resize(320, 240);
    panel.show();
    QApplication::processEvents();

    if (probe) {
        EXPECT_TRUE(panel.usingGpu());
        EXPECT_EQ(panel.compositorBackendName(), "opengl");
    } else {
        EXPECT_FALSE(panel.usingGpu());
        EXPECT_EQ(panel.compositorBackendName(), "cpu");
    }
}

} // namespace
