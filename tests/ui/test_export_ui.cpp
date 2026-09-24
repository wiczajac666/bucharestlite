#include "dialogs/export_dialog.hpp"
#include "export/export_worker.hpp"

#include <bl_core/project_data.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/timeline.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QThread>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <string>
#include <vector>

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
using bl::ui::ExportDialog;
using bl::ui::ExportWorker;
using bl::ui::ResolutionScale;

const Rational kFps{24, 1};
const Rational kRate{1'000'000, 1};

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

std::string tempOutputPath() {
    return QDir::temp()
        .filePath(QStringLiteral("bl_export_ui_%1.mp4")
                      .arg(QDateTime::currentMSecsSinceEpoch()))
        .toStdString();
}

struct TimelineFixture {
    bl::Timeline timeline;
    std::vector<bl::MediaBinItem> mediaBin;

    TimelineFixture() {
        timeline.sequence().settings.fps = kFps;
        timeline.sequence().settings.width = 320;
        timeline.sequence().settings.height = 240;
        timeline.sequence().addVideoTrack("V1");
        bl::MediaBinItem item;
        item.id = "1";
        item.path = std::string(BL_TEST_MEDIA_DIR) + "/test_video.mp4";
        item.name = "test_video.mp4";
        mediaBin.push_back(item);
        bl::Clip clip;
        clip.id = "c1";
        clip.source.mediaItemId = "1";
        clip.source.sourceIn = fr(0);
        clip.source.sourceOut = fr(24);
        clip.timelineStart = fr(0);
        clip.timelineDuration = durF(24);
        timeline.sequence().videoTracks[0].addClip(clip);
    }
};

TEST(ExportDialog, CollectsSettingsAndValidates) {
    ExportDialog dialog;
    bl::SequenceSettings seq;
    seq.width = 1280;
    seq.height = 720;
    seq.fps = kFps;
    dialog.setSequenceDefaults(seq);

    auto* path = dialog.findChild<QLineEdit*>(QStringLiteral("exportOutputPath"));
    auto* exportButton =
        dialog.findChild<QPushButton*>(QStringLiteral("exportStart"));
    ASSERT_NE(path, nullptr);
    ASSERT_NE(exportButton, nullptr);

    // No output path yet -> Save disabled.
    EXPECT_FALSE(exportButton->isEnabled());

    const std::string out = tempOutputPath();
    path->setText(QString::fromStdString(out));
    EXPECT_TRUE(exportButton->isEnabled());

    // Custom resolution surfaces the spinners.
    auto* scale = dialog.findChild<QComboBox*>(QStringLiteral("exportScale"));
    scale->setCurrentIndex(scale->findData(
        static_cast<int>(ResolutionScale::Custom)));
    auto* w = dialog.findChild<QSpinBox*>(QStringLiteral("exportCustomWidth"));
    w->setValue(640);
    auto* h = dialog.findChild<QSpinBox*>(QStringLiteral("exportCustomHeight"));
    h->setValue(360);

    const auto s = dialog.settings();
    EXPECT_EQ(s.outputPath, out);
    EXPECT_EQ(s.container, "mp4");
    EXPECT_EQ(s.videoCodec, "h264");
    EXPECT_FALSE(s.includeAudio);
    EXPECT_GT(s.videoCq, 0);
    EXPECT_EQ(s.scale, ResolutionScale::Custom);
    EXPECT_EQ(s.customWidth, 640u);
    EXPECT_EQ(s.customHeight, 360u);

    std::remove(out.c_str());
}

TEST(ExportDialog, ContainerSyncsCodec) {
    ExportDialog dialog;
    auto* container =
        dialog.findChild<QComboBox*>(QStringLiteral("exportContainer"));
    auto* codec = dialog.findChild<QComboBox*>(QStringLiteral("exportVideoCodec"));
    ASSERT_NE(container, nullptr);
    ASSERT_NE(codec, nullptr);

    container->setCurrentIndex(
        container->findData(QStringLiteral("webm")));
    EXPECT_EQ(codec->currentData().toString().toStdString(), "vp9")
        << "webm container should default to vp9";

    container->setCurrentIndex(container->findData(QStringLiteral("mp4")));
    EXPECT_EQ(codec->currentData().toString().toStdString(), "h264")
        << "mp4 container should fall back to h264";
}

TEST(ExportWorker, RendersAndReportsProgress) {
    TimelineFixture fx;
    const std::string out = tempOutputPath();

    bl::ui::ExportSettings st;
    st.outputPath = out;
    st.container = "mp4";
    st.videoCodec = "h264";
    st.includeAudio = false;
    st.range = bl::ui::ExportRange::EntireProject;

    bl::MediaDecodeSource::Spec plugins;
    plugins.pluginDirs.emplace_back(std::string(BL_TEST_PLUGIN_DIR) +
                                        "/plugins",
                                    bl::PluginOrigin::User);
    ExportWorker::Request req{st, fx.timeline.snapshot(), fx.mediaBin,
                              plugins};

    QThread thread;
    auto* worker = new ExportWorker();
    worker->moveToThread(&thread);
    thread.start();

    QSignalSpy progress(worker, &ExportWorker::progress);
    QSignalSpy done(worker, &ExportWorker::finished);

    QMetaObject::invokeMethod(worker, "run", Qt::QueuedConnection,
                              Q_ARG(ExportWorker::Request, req));
    QVERIFY(done.wait(30000));

    thread.quit();
    thread.wait(5000);
    delete worker;

    ASSERT_EQ(done.count(), 1);
    EXPECT_TRUE(done.first().at(0).toBool()) << "export should succeed";
    EXPECT_TRUE(progress.count() > 0) << "progress should be reported";

    QFileInfo fi(QString::fromStdString(out));
    EXPECT_TRUE(fi.exists()) << "output missing: " << out;
    if (fi.exists()) {
        std::remove(out.c_str());
    }
}

} // namespace