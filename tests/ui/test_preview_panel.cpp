#include <bl_core/time.hpp>
#include <bl_timeline/clip.hpp>

#include <app/project_controller.hpp>
#include <panels/preview_panel.hpp>

#include <QApplication>
#include <QLabel>
#include <QString>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <string>
#include <utility>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

#ifndef BL_TEST_MEDIA_DIR
#define BL_TEST_MEDIA_DIR "."
#endif

namespace {

using bl::Duration;
using bl::Rational;
using bl::Time;

const bl::Rational kFps{24, 1};
const bl::Rational kRate{1'000'000, 1};

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

Time fr(int64_t frame) { return Time::fromFrameAt(frame, kFps, kRate); }
Duration durF(int64_t frames) { return Duration::fromFrames(frames, kFps); }

std::string mediaDir() { return std::string(BL_TEST_MEDIA_DIR); }

struct Fixture {
    bl::ui::ProjectController controller;
    bl::ui::PreviewPanel panel;

    Fixture()
        : panel(&controller, nullptr,
                {std::make_pair(std::string(BL_TEST_PLUGIN_DIR) + "/plugins",
                                bl::PluginOrigin::User)}) {
        ensureApp();
        controller.newProject(QStringLiteral("Preview"));
        controller.addToMediaBin(QString::fromStdString(mediaDir() + "/test_video_theora.ogv"));

        // One clip, 1 s @ 24 fps, referencing the media-bin entry (id "1").
        auto& seq = controller.timeline().sequence();
        seq.settings.fps = kFps;
        seq.settings.width = 160;
        seq.settings.height = 120;
        bl::Clip clip;
        clip.id = "c1";
        clip.source.mediaItemId = "1";
        clip.source.sourceIn = fr(0);
        clip.source.sourceOut = fr(24);
        clip.timelineStart = fr(0);
        clip.timelineDuration = durF(24);
        seq.videoTracks[0].addClip(clip);

        // In the standalone (MainWindow-free) fixture, mirror the wiring where
        // TimelinePanel::timelineChanged pulls a preview refresh after edits.
        panel.onTimelineChanged();

        panel.show();
        QApplication::processEvents();
    }
};

TEST(PreviewPanelTest, DefaultsAtPlayheadZeroWithFrame) {
    Fixture f;
    EXPECT_EQ(f.panel.transport().playhead(), Time::fromTicks(0, kRate));
    EXPECT_FALSE(f.panel.transport().playing());
    // Duration equals the 1 s clip.
    EXPECT_DOUBLE_EQ(f.panel.transport().duration().toSeconds(), 1.0);
    // The panel rendered a real frame during construction.
    EXPECT_FALSE(f.panel.surface()->lastFrame().isNull());
    EXPECT_EQ(f.panel.surface()->lastFrame().size().width(), 160);
}

TEST(PreviewPanelTest, StepForwardAdvancesTransportAndTimecode) {
    Fixture f;
    f.panel.transport().setFps({24, 1});
    f.panel.transport().stepForward();
    EXPECT_NEAR(f.panel.transport().playhead().toSeconds(), 1.0 / 24.0, 1e-6);

    auto* timecode = f.panel.findChild<QLabel*>(
        QStringLiteral("previewTimecode"));
    ASSERT_NE(timecode, nullptr);
    const QString text = timecode->text();
    EXPECT_NE(text, QString());
    EXPECT_TRUE(text.contains(QStringLiteral("00:00:00:01"))) << text.toStdString();
}

TEST(PreviewPanelTest, SetPlayheadFromTimelineRendersAndEmits) {
    Fixture f;
    f.panel.surface()->clearFrame();
    EXPECT_TRUE(f.panel.surface()->lastFrame().isNull());

    Time emitted;
    int notified = 0;
    QObject::connect(&f.panel, &bl::ui::PreviewPanel::playheadChanged,
                     [&](const bl::Time& t) {
                         emitted = t;
                         ++notified;
                     });

    f.panel.setPlayheadFromTimeline(fr(10));
    EXPECT_EQ(notified, 1);
    EXPECT_EQ(emitted, fr(10));
    EXPECT_EQ(f.panel.transport().playhead(), fr(10));
    EXPECT_FALSE(f.panel.surface()->lastFrame().isNull());
}

TEST(PreviewPanelTest, ScrubLoopIsFinitelyCoupled) {
    Fixture f;

    // Mirror the real MainWindow wiring: timeline scrub pulls the preview,
    // and preview playback pushes the timeline playhead back — this must
    // converge instantly (equality no-op), not recurse.
    int roundTrips = 0;
    QObject::connect(&f.panel, &bl::ui::PreviewPanel::playheadChanged,
                     [&](const bl::Time& t) {
                         ++roundTrips;
                         f.panel.setPlayheadFromTimeline(t);
                     });

    f.panel.transport().setPlayhead(fr(7));
    QApplication::processEvents();
    EXPECT_EQ(roundTrips, 1);

    f.panel.transport().setPlayhead(fr(9));
    QApplication::processEvents();
    EXPECT_EQ(roundTrips, 2);
    EXPECT_EQ(f.panel.transport().playhead(), fr(9));
}

TEST(PreviewPanelTest, PlayAdvancesUntilDurationThenStops) {
    Fixture f;
    f.panel.transport().setFps({24, 1});
    f.panel.transport().setDuration(Time::fromTicks(
        f.panel.transport().duration().ticks, kRate));

    f.panel.transport().play();
    EXPECT_TRUE(f.panel.transport().playing());

    // Let the 33 ms ticker run for ~1.5 ticks wall time.
    QTest::qWait(60);
    EXPECT_TRUE(f.panel.transport().playing());
    EXPECT_GT(f.panel.transport().playhead(), Time::fromTicks(0, kRate));

    // Pump ticks until the clip end is reached; transport auto-pauses.
    for (int i = 0; i < 200 && f.panel.transport().playing(); ++i) {
        QTest::qWait(10);
    }
    EXPECT_FALSE(f.panel.transport().playing());
    EXPECT_EQ(f.panel.transport().playhead(), f.panel.transport().duration());
}

} // namespace