#include <bl_render/compositor.hpp>

#include <panels/scopes_panel.hpp>

#include <QApplication>
#include <QComboBox>
#include <QImage>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using bl::CompositorResult;
using bl::ui::ScopesPanel;
using bl::ui::ScopeMode;

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

// A solid BGRA frame (r,g,b) with an opaque alpha.
CompositorResult solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    CompositorResult f;
    f.width = static_cast<uint32_t>(w);
    f.height = static_cast<uint32_t>(h);
    f.linesize = static_cast<uint32_t>(w * 4);
    f.data.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < f.data.size(); i += 4) {
        f.data[i + 0] = b;
        f.data[i + 1] = g;
        f.data[i + 2] = r;
        f.data[i + 3] = 255;
    }
    return f;
}

TEST(ScopesPanel, SampleStepScalesWithFrameSize) {
    EXPECT_EQ(ScopesPanel::sampleStep(320, 240), 1);
    EXPECT_EQ(ScopesPanel::sampleStep(1920, 1080), 3);
    EXPECT_EQ(ScopesPanel::sampleStep(3840, 2160), 6);
}

TEST(ScopesPanel, DefaultsToHistogramAndHostsAView) {
    ensureApp();
    ScopesPanel panel;
    EXPECT_EQ(panel.mode(), ScopeMode::Histogram);
    ASSERT_NE(panel.view(), nullptr);
    EXPECT_FALSE(panel.view()->hasData());
    // renderImage is never null, even before any frame arrives.
    EXPECT_FALSE(panel.view()->renderImage(120, 90).isNull());
}

TEST(ScopesPanel, SetFrameFillsTheView) {
    ensureApp();
    ScopesPanel panel;
    panel.setFrame(solid(32, 24, 255, 255, 255));
    ASSERT_TRUE(panel.view()->hasData());

    const QImage white = panel.view()->renderImage(160, 120);
    ASSERT_FALSE(white.isNull());
    EXPECT_EQ(white.size(), QSize(160, 120));

    // The drawn scope reflects the frame: a white frame places the histogram
    // mass at the top-right, a mid-gray frame puts it in the middle, so the
    // two images differ.
    panel.setFrame(solid(32, 24, 128, 128, 128));
    const QImage gray = panel.view()->renderImage(160, 120);
    EXPECT_NE(white, gray);
}

TEST(ScopesPanel, EmptyFrameClearsTheView) {
    ensureApp();
    ScopesPanel panel;
    panel.setFrame(solid(16, 16, 10, 20, 30));
    ASSERT_TRUE(panel.view()->hasData());

    panel.setFrame(CompositorResult{});
    EXPECT_FALSE(panel.view()->hasData());
}

TEST(ScopesPanel, ModeSwitchingChangesTheDrawing) {
    ensureApp();
    ScopesPanel panel;
    panel.setFrame(solid(48, 32, 180, 90, 40));

    panel.view()->setMode(ScopeMode::Histogram);
    const QImage histogram = panel.view()->renderImage(160, 120);
    panel.view()->setMode(ScopeMode::Waveform);
    const QImage waveform = panel.view()->renderImage(160, 120);
    panel.view()->setMode(ScopeMode::Vectorscope);
    const QImage vectorscope = panel.view()->renderImage(160, 120);

    ASSERT_FALSE(histogram.isNull());
    ASSERT_FALSE(waveform.isNull());
    ASSERT_FALSE(vectorscope.isNull());
    EXPECT_NE(histogram, waveform);
    EXPECT_NE(waveform, vectorscope);
    EXPECT_NE(histogram, vectorscope);
}

TEST(ScopesPanel, ComboSelectsTheMode) {
    ensureApp();
    ScopesPanel panel;
    auto* combo = panel.findChild<QComboBox*>(QStringLiteral("scopeMode"));
    ASSERT_NE(combo, nullptr);

    combo->setCurrentIndex(1); // Waveform
    EXPECT_EQ(panel.mode(), ScopeMode::Waveform);
    EXPECT_EQ(panel.view()->mode(), ScopeMode::Waveform);

    combo->setCurrentIndex(2); // Vectorscope
    EXPECT_EQ(panel.mode(), ScopeMode::Vectorscope);
    EXPECT_EQ(panel.view()->mode(), ScopeMode::Vectorscope);
}

} // namespace
