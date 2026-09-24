#include "dialogs/batch_export_dialog.hpp"
#include "export/export_queue.hpp"

#include <bl_core/project_data.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/timeline.hpp>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <fstream>
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
using bl::ui::BatchExportDialog;
using bl::ui::ExportJob;
using bl::ui::ExportJobStatus;
using bl::ui::loadExportQueue;
using bl::ui::saveExportQueue;

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

std::string pluginDir() { return std::string(BL_TEST_PLUGIN_DIR) + "/plugins"; }

bl::TimelineSnapshot makeSnapshot() {
    bl::Timeline timeline;
    timeline.sequence().settings.fps = kFps;
    timeline.sequence().settings.width = 320;
    timeline.sequence().settings.height = 240;
    timeline.sequence().addVideoTrack("V1");
    bl::TimelineSnapshot snapshot = timeline.snapshot();
    return snapshot;
}

std::vector<bl::MediaBinItem> makeMediaBin() {
    bl::MediaBinItem item;
    item.id = "1";
    item.path = std::string(BL_TEST_MEDIA_DIR) + "/test_video.mp4";
    item.name = "test_video.mp4";
    return {item};
}

ExportJob makeJob(const std::string& id, const std::string& out) {
    ExportJob job;
    job.id = id;
    job.settings.outputPath = out;
    job.settings.container = "mp4";
    job.settings.videoCodec = "h264";
    job.settings.includeAudio = false;
    return job;
}

struct BatchFixture {
    void setup() {
        // Rebuild the timeline with the clip so the snapshot carries video.
        timeline_.sequence().settings.fps = kFps;
        timeline_.sequence().settings.width = 320;
        timeline_.sequence().settings.height = 240;
        timeline_.sequence().addVideoTrack("V1");
        bl::Clip clip;
        clip.id = "c1";
        clip.source.mediaItemId = "1";
        clip.source.sourceIn = fr(0);
        clip.source.sourceOut = fr(24);
        clip.timelineStart = fr(0);
        clip.timelineDuration = durF(24);
        timeline_.sequence().videoTracks[0].addClip(clip);

        context_.snapshotOf = [this] { return timeline_.snapshot(); };
        context_.mediaBinOf = [] { return makeMediaBin(); };
        context_.pluginSpecOf = [] {
            bl::MediaDecodeSource::Spec spec;
            spec.pluginDirs.emplace_back(pluginDir(), bl::PluginOrigin::User);
            return spec;
        };
        context_.exportBusy = [] { return false; };
        context_.queuePath = QString();
    }

    bl::Timeline timeline_;
    BatchExportDialog::AppContext context_;
};

TEST(ExportQueue, PersistsAndRestoresJobs) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path =
        QDir(dir.path()).filePath("export_queue.json").toStdString();

    std::vector<ExportJob> jobs;
    jobs.push_back(makeJob("a", "/out/a.mp4"));
    jobs.back().status = ExportJobStatus::Done;
    jobs.back().settings.scale = bl::ui::ResolutionScale::Half;
    jobs.back().settings.videoCq = 21;
    jobs.push_back(makeJob("b", "/out/b.webm"));
    jobs.back().settings.container = "webm";
    jobs.back().settings.videoCodec = "vp9";
    jobs.back().status = ExportJobStatus::Failed;
    jobs.back().message = "boom";

    EXPECT_TRUE(saveExportQueue(jobs, path));

    const auto restored = loadExportQueue(path);
    ASSERT_EQ(restored.size(), 2u);
    EXPECT_EQ(restored[0].id, "a");
    EXPECT_EQ(restored[0].settings.scale, bl::ui::ResolutionScale::Half);
    EXPECT_EQ(restored[0].status, ExportJobStatus::Done);
    EXPECT_EQ(restored[1].settings.container, "webm");
    EXPECT_EQ(restored[1].settings.videoCodec, "vp9");
    EXPECT_EQ(restored[1].status, ExportJobStatus::Failed);
    EXPECT_EQ(restored[1].message, "boom");
    EXPECT_TRUE(restored[1].needsRender());
    EXPECT_FALSE(restored[0].needsRender());
}

TEST(ExportQueue, MissingOrCorruptFileLoadsEmpty) {
    EXPECT_TRUE(loadExportQueue("/nonexistent/queue.json").empty());

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path =
        QDir(dir.path()).filePath("corrupt.json").toStdString();
    {
        std::ofstream(path, std::ios::binary) << "{ not json !";
    }
    EXPECT_TRUE(loadExportQueue(path).empty());
}

TEST(BatchDialog, AddRemoveTracksJobs) {
    BatchFixture fx;
    fx.setup();
    BatchExportDialog dialog(fx.context_);
    const std::string out = QDir::temp()
                                .filePath(QStringLiteral("bl_batch_add.mp4"))
                                .toStdString();
    dialog.addJob(makeJob("j1", out));
    dialog.addJob(makeJob("j2", out + ".2"));
    EXPECT_EQ(dialog.jobCount(), 2);
    EXPECT_EQ(dialog.statusAt(0), ExportJobStatus::Pending);
    EXPECT_EQ(dialog.statusAt(1), ExportJobStatus::Pending);
    std::remove(out.c_str());
    std::remove((out + ".2").c_str());
}

TEST(BatchDialog, RendersQueuedJobsInOrder) {
    BatchFixture fx;
    fx.setup();
    BatchExportDialog dialog(fx.context_);

    const QString prefix = QDir::temp().filePath(
        QStringLiteral("bl_batch_run_%1")
            .arg(QDateTime::currentMSecsSinceEpoch()));
    const std::string outA = (prefix + "_a.mp4").toStdString();
    const std::string outB = (prefix + "_b.webm").toStdString();
    dialog.addJob(makeJob("j1", outA));
    ExportJob job2 = makeJob("j2", outB);
    job2.settings.container = "webm";
    job2.settings.videoCodec = "vp9";
    dialog.addJob(job2);

    QSignalSpy done(&dialog, &BatchExportDialog::queueFinished);
    dialog.runAll();
    QVERIFY2(done.wait(60000), "batch queue should finish");

    EXPECT_EQ(dialog.jobCount(), 2);
    EXPECT_EQ(dialog.statusAt(0), ExportJobStatus::Done);
    EXPECT_EQ(dialog.statusAt(1), ExportJobStatus::Done)
        << "job 2 message: " << dialog.jobs()[1].message;
    EXPECT_TRUE(QFileInfo(QString::fromStdString(outA)).exists());
    EXPECT_TRUE(QFileInfo(QString::fromStdString(outB)).exists());

    std::remove(outA.c_str());
    std::remove(outB.c_str());
}

TEST(BatchDialog, FailedJobStaysQueuedForRetry) {
    BatchFixture fx;
    fx.setup();
    fx.context_.snapshotOf = [] { return makeSnapshot(); }; // empty: no clips
    BatchExportDialog dialog(fx.context_);
    const std::string out = QDir::temp()
                                .filePath(QStringLiteral("bl_batch_fail.mp4"))
                                .toStdString();
    dialog.addJob(makeJob("j1", out));

    QSignalSpy done(&dialog, &BatchExportDialog::queueFinished);
    dialog.runAll();
    done.wait(60000);

    EXPECT_EQ(dialog.statusAt(0), ExportJobStatus::Failed)
        << "job with no rendered frames should fail but stay queued";
    std::remove(out.c_str());
}

} // namespace