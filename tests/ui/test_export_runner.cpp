#include <bl_core/project_data.hpp>
#include <bl_core/time.hpp>
#include <bl_export/export_types.h>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/timeline.hpp>

#include <export/export_plan.hpp>
#include <export/export_runner.hpp>
#include <export/export_settings.hpp>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

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
using bl::ui::ExportRange;
using bl::ui::ExportSettings;
using bl::ui::sequenceDuration;

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

Time fr(int64_t frame) { return Time::fromFrameAt(frame, kFps, kRate); }
Duration durF(int64_t frames) { return Duration::fromFrames(frames, kFps); }

std::string mediaDir() { return std::string(BL_TEST_MEDIA_DIR); }
std::string pluginDir() { return std::string(BL_TEST_PLUGIN_DIR) + "/plugins"; }

struct ExportFixture {
    bl::Timeline timeline;
    std::vector<bl::MediaBinItem> mediaBin;

    explicit ExportFixture() {
        ensureApp();
        timeline.sequence().settings.fps = kFps;
        timeline.sequence().settings.width = 320;
        timeline.sequence().settings.height = 240;
        timeline.sequence().addVideoTrack("V1");

        bl::MediaBinItem item;
        item.id = "1";
        item.path = mediaDir() + "/test_video.mp4";
        item.name = "test_video.mp4";
        mediaBin.push_back(item);

        bl::Clip clip;
        clip.id = "c1";
        clip.source.mediaItemId = "1";
        clip.source.sourceIn = fr(0);
        clip.source.sourceOut = fr(48);
        clip.timelineStart = fr(0);
        clip.timelineDuration = durF(48);
        timeline.sequence().videoTracks[0].addClip(clip);
    }

    bl::TimelineSnapshot snapshot() const {
        return timeline.snapshot();
    }

    bl::ui::ExportSettings settings(std::string outPath) const {
        bl::ui::ExportSettings s;
        s.range = bl::ui::ExportRange::EntireProject;
        s.outputPath = std::move(outPath);
        s.container = "mp4";
        s.videoCodec = "h264";
        s.includeAudio = false;
        s.videoCq = 23;
        return s;
    }
};

std::string tempOutputPath(const char* tag, const char* ext) {
    return QDir::temp()
        .filePath(QStringLiteral("bl_exporter_%1_%2%3")
                      .arg(tag)
                      .arg(QDateTime::currentMSecsSinceEpoch())
                      .arg(QString::fromUtf8(ext)))
        .toStdString();
}

TEST(ExportRunner, ExportsWebmFromTimeline) {
    ExportFixture fx;
    auto snapshot = fx.snapshot();

    const std::string out = tempOutputPath("webm", ".webm");
    bl::ui::ExportSettings settings = fx.settings(out);
    settings.container = "webm";
    settings.videoCodec = "vp9";
    bl::ui::ExportPlan plan;
    plan.settings = settings;
    auto build =
        plan.build(snapshot.sequence(), sequenceDuration(snapshot.sequence()));
    ASSERT_TRUE(build.ok()) << build.message();

    bl::MediaDecodeSource::Spec plugins{
        {std::make_pair(pluginDir(), bl::PluginOrigin::User)}};
    bl::ui::ExportRunner::Input input{plan, snapshot, fx.mediaBin, plugins,
                                      nullptr};

    auto result = bl::ui::ExportRunner::run(input, nullptr);
    ASSERT_TRUE(result.ok())
        << "webm export failed: " << result.message();

    QFileInfo fi(QString::fromStdString(out));
    EXPECT_TRUE(fi.exists()) << "output file missing: " << out;
    EXPECT_GT(fi.size(), 1024);
    std::remove(out.c_str());
}

TEST(ExportRunner, ExportsMp4FromTimeline) {
    ExportFixture fx;
    auto snapshot = fx.snapshot();

    const std::string out = tempOutputPath("ok", ".mp4");
    bl::ui::ExportPlan plan;
    plan.settings = fx.settings(out);
    auto build =
        plan.build(snapshot.sequence(), sequenceDuration(snapshot.sequence()));
    ASSERT_TRUE(build.ok()) << build.message();
    ASSERT_EQ(plan.range.frameCount, 48);

    bl::MediaDecodeSource::Spec plugins{
        {std::make_pair(pluginDir(), bl::PluginOrigin::User)}};
    bl::ui::ExportRunner::Input input{plan, snapshot, fx.mediaBin, plugins,
                                      nullptr};

    int lastPercent = -1;
    size_t lastFrame = 0;
    auto result = bl::ui::ExportRunner::run(
        input, [&](const bl::ui::ExportProgressInfo& info) {
            lastPercent = info.percent;
            lastFrame = info.frame;
        });

    ASSERT_TRUE(result.ok()) << "export failed: " << result.message();
    EXPECT_GE(lastPercent, 100);

    QFileInfo fi(QString::fromStdString(out));
    EXPECT_TRUE(fi.exists()) << "output file missing: " << out;
    EXPECT_GT(fi.size(), 1024) << "output too small to contain video data";

    if (QFileInfo::exists(QString::fromStdString(out + ".keep"))) {
        std::remove((out + ".keep").c_str());
    }
    if (qEnvironmentVariableIsSet("BL_KEEP_EXPORT")) {
        QFile::copy(QString::fromStdString(out),
                    QString::fromStdString(out + ".keep"));
    }
    std::remove(out.c_str());
}

TEST(ExportRunner, CancellationRemovesPartialFile) {
    ExportFixture fx;
    auto snapshot = fx.snapshot();

    const std::string out = tempOutputPath("cancel", ".mp4");
    bl::ui::ExportPlan plan;
    plan.settings = fx.settings(out);
    auto build =
        plan.build(snapshot.sequence(), sequenceDuration(snapshot.sequence()));
    ASSERT_TRUE(build.ok());
    ASSERT_TRUE(plan.range.frameCount > 1);

    std::atomic<bool> abort{true};
    bl::MediaDecodeSource::Spec plugins{
        {std::make_pair(pluginDir(), bl::PluginOrigin::User)}};
    bl::ui::ExportRunner::Input input{plan, snapshot, fx.mediaBin, plugins,
                                      &abort};

    auto result = bl::ui::ExportRunner::run(input, nullptr);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code(), bl::Err::Cancelled);
    EXPECT_TRUE(!std::ifstream(out).good())
        << "partial output not removed: " << out;
}

} // namespace