#include <bl_core/demuxer.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_core/project_data.hpp>
#include <bl_core/time.hpp>
#include <bl_export/audio_pcm_source.hpp>
#include <bl_export/export_types.h>
#include <bl_plugins/codec_plugin.h>
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
#include <cmath>
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

// Builds a timeline with a video and an audio clip both fed from test_av.mp4
// (320x240@24, 2s, with a 440 Hz sine track), so an include-audio export must
// mux a real, non-silent audio stream.
struct AudioExportFixture {
    bl::Timeline timeline;
    std::vector<bl::MediaBinItem> mediaBin;

    explicit AudioExportFixture() {
        ensureApp();
        timeline.sequence().settings.fps = kFps;
        timeline.sequence().settings.width = 320;
        timeline.sequence().settings.height = 240;
        timeline.sequence().addVideoTrack("V1");
        timeline.sequence().addAudioTrack("A1");

        bl::MediaBinItem video;
        video.id = "1";
        video.path = mediaDir() + "/test_av.mp4";
        video.name = "test_av.mp4";
        mediaBin.push_back(video);

        bl::MediaBinItem audio;
        audio.id = "2";
        audio.path = mediaDir() + "/test_av.mp4";
        audio.name = "test_av_audio.mp4";
        mediaBin.push_back(audio);

        bl::Clip vclip;
        vclip.id = "c1";
        vclip.source.mediaItemId = "1";
        vclip.source.sourceIn = fr(0);
        vclip.source.sourceOut = fr(48);
        vclip.timelineStart = fr(0);
        vclip.timelineDuration = durF(48);
        timeline.sequence().videoTracks[0].addClip(vclip);

        bl::Clip aclip;
        aclip.id = "c2";
        aclip.source.mediaItemId = "2";
        aclip.source.sourceIn = fr(0);
        aclip.source.sourceOut = fr(48);
        aclip.timelineStart = fr(0);
        aclip.timelineDuration = durF(48);
        timeline.sequence().audioTracks[0].addClip(aclip);
    }

    bl::TimelineSnapshot snapshot() const { return timeline.snapshot(); }
};

// Open a previously exported container and probe it: returns true when it has
// at least one audio stream whose decoded samples contain audible content.
bool probeHasAudibleAudio(const std::string& path) {
    auto demuxed = bl::Demuxer::open(path);
    if (!demuxed.ok()) {
        return false;
    }
    const bl::StreamInfo& info = demuxed.value().info();
    if (info.audioStreams.empty()) {
        return false;
    }

    bl::CodecRegistry registry;
    bl::PluginLoader loader;
    auto report = loader.scanDirectory(pluginDir(), bl::PluginOrigin::User);
    if (report.ok()) {
        for (auto& handle : report.value().plugins) {
            const BlCodecPlugin* plugin = handle->plugin();
            if (plugin) {
                (void)registry.registerPlugin(
                    const_cast<BlCodecPlugin*>(plugin));
            }
        }
    }

    BlHostApi host{};
    host.host_abi_version = BL_PLUGIN_ABI_VERSION;
    host.alloc = [](size_t size, void*) { return std::malloc(size); };
    host.free = [](void* ptr, void*) { std::free(ptr); };

    bl::export_::PcmAudioSource::OpenRequest req;
    req.path = path;
    req.targetSampleRate = 48000;
    req.host = &host;
    auto src = bl::export_::PcmAudioSource::open(req, registry);
    if (!src.ok()) {
        return false;
    }
    float left[8192];
    float right[8192];
    double sum = 0.0;
    uint64_t count = 0;
    int guard = 0;
    for (;;) {
        uint32_t got = src.value().pull(left, right, 8192);
        if (got == 0) break;
        for (uint32_t i = 0; i < got; ++i) {
            sum += left[i] * left[i];
        }
        count += got;
        if (++guard > 200) break;
    }
    return count > 0 && std::sqrt(sum / static_cast<double>(count)) > 0.001;
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

TEST(ExportRunner, ExportsMp4WithAacAudio) {
    AudioExportFixture fx;
    auto snapshot = fx.snapshot();

    const std::string out = tempOutputPath("aac", ".mp4");
    bl::ui::ExportSettings settings;
    settings.range = bl::ui::ExportRange::EntireProject;
    settings.outputPath = out;
    settings.container = "mp4";
    settings.videoCodec = "h264";
    settings.includeAudio = true;
    settings.audioCodec = "aac";
    settings.videoCq = 23;
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
    ASSERT_TRUE(result.ok()) << "mp4+aac export failed: " << result.message();

    EXPECT_TRUE(probeHasAudibleAudio(out))
        << "mp4 output should contain a decodable, non-silent audio stream";
    std::remove(out.c_str());
}

bool probeHasSubtitleText(const std::string& path,
                          const std::string& expected) {
    auto demuxed = bl::Demuxer::open(path);
    if (!demuxed.ok()) {
        return false;
    }
    const bl::StreamInfo& info = demuxed.value().info();
    if (info.subtitleStreams.empty()) {
        return false;
    }
    for (;;) {
        auto packet = demuxed.value().nextPacket();
        if (!packet.ok() || !packet.value().has_value()) break;
        const bl::Packet& pkt = *packet.value();
        if (pkt.streamIndex != info.subtitleStreams.front().index) continue;
        const std::string payload(pkt.data.begin(), pkt.data.end());
        if (payload.find(expected) != std::string::npos) return true;
    }
    return false;
}

// Subtitle clips export as a soft MOV_TEXT stream: the sample bytes carry the
// authored text and the stream must exist even for video-only exports.
TEST(ExportRunner, ExportsSoftSubtitleStreamFromTimeline) {
    ExportFixture fx;
    ASSERT_TRUE(
        fx.timeline.sequence().videoTracks[0].setSubtitleText(
            "c1", "Bonjour neuf du sous-titre"));

    auto snapshot = fx.snapshot();

    const std::string out = tempOutputPath("sub", ".mp4");
    bl::ui::ExportPlan plan;
    plan.settings = fx.settings(out);
    auto build =
        plan.build(snapshot.sequence(), sequenceDuration(snapshot.sequence()));
    ASSERT_TRUE(build.ok()) << build.message();

    bl::MediaDecodeSource::Spec plugins{
        {std::make_pair(pluginDir(), bl::PluginOrigin::User)}};
    bl::ui::ExportRunner::Input input{plan, snapshot, fx.mediaBin, plugins,
                                      nullptr};
    auto result = bl::ui::ExportRunner::run(input, nullptr);
    ASSERT_TRUE(result.ok()) << "mp4 subtitle export failed: "
                             << result.message();

    auto demuxed = bl::Demuxer::open(out);
    ASSERT_TRUE(demuxed.ok());
    EXPECT_EQ(demuxed.value().info().subtitleStreams.size(), 1u)
        << "mp4 must carry one subtitle stream";
    EXPECT_TRUE(probeHasSubtitleText(out, "Bonjour"))
        << "subtitle samples must contain the authored text";
    std::remove(out.c_str());
}

TEST(ExportRunner, ExportsWebmWithOpusAudio) {
    AudioExportFixture fx;
    auto snapshot = fx.snapshot();

    const std::string out = tempOutputPath("opus", ".webm");
    bl::ui::ExportSettings settings;
    settings.range = bl::ui::ExportRange::EntireProject;
    settings.outputPath = out;
    settings.container = "webm";
    settings.videoCodec = "vp9";
    settings.includeAudio = true;
    settings.audioCodec = "opus";
    settings.videoCq = 23;
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
    ASSERT_TRUE(result.ok()) << "webm+opus export failed: " << result.message();

    EXPECT_TRUE(probeHasAudibleAudio(out))
        << "webm output should contain a decodable, non-silent audio stream";
    std::remove(out.c_str());
}

} // namespace