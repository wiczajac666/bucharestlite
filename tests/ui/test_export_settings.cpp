#include "export/export_settings.hpp"

#include <gtest/gtest.h>

namespace bl::ui {
namespace {

SequenceSettings makeSequence() {
    SequenceSettings s;
    s.width = 1920;
    s.height = 1080;
    s.fps = {24000, 1001};  // 23.976
    s.sampleRate = 48000;
    return s;
}

TEST(ExportSettings, ResolutionFullKeepBase) {
    ExportSettings s;
    s.scale = ResolutionScale::Full;
    EXPECT_EQ(outputWidth(s, makeSequence()), 1920u);
    EXPECT_EQ(outputHeight(s, makeSequence()), 1080u);
}

TEST(ExportSettings, ResolutionScales) {
    ExportSettings s;
    s.scale = ResolutionScale::Half;
    EXPECT_EQ(outputWidth(s, makeSequence()), 960u);
    EXPECT_EQ(outputHeight(s, makeSequence()), 540u);

    s.scale = ResolutionScale::Quarter;
    EXPECT_EQ(outputWidth(s, makeSequence()), 480u);
    EXPECT_EQ(outputHeight(s, makeSequence()), 270u);
}

TEST(ExportSettings, ResolutionCustom) {
    ExportSettings s;
    s.scale = ResolutionScale::Custom;
    s.customWidth = 1280;
    s.customHeight = 512;
    EXPECT_EQ(outputWidth(s, makeSequence()), 1280u);
    EXPECT_EQ(outputHeight(s, makeSequence()), 512u);
}

TEST(ExportSettings, FrameRangeEntireProject) {
    ExportSettings s;
    s.range = ExportRange::EntireProject;
    const auto range = frameRange(s, makeSequence(), Duration::fromSeconds(2.0, {9000, 1}));
    // 2s at 23.976fps ~ 48 frames; use the fps-aware conversion against outPoint
    EXPECT_EQ(range.firstFrame, 0);
    EXPECT_EQ(range.frameCount, 48);
}

TEST(ExportSettings, FrameRangeInOut) {
    ExportSettings s;
    s.range = ExportRange::InOut;
    s.inPoint = Time::fromFrame(0, {24000, 1001});
    s.outPoint = Time::fromFrame(100, {24000, 1001});
    const auto range = frameRange(s, makeSequence(), Duration::fromSeconds(10.0, {9000, 1}));
    EXPECT_EQ(range.firstFrame, 0);
    EXPECT_EQ(range.frameCount, 100);
}

TEST(ExportSettings, FrameRangeClipBehavesLikeInOut) {
    ExportSettings s;
    s.range = ExportRange::Clip;
    s.inPoint = Time::fromFrame(24, {24000, 1001});
    s.outPoint = Time::fromFrame(48, {24000, 1001});
    const auto range =
        frameRange(s, makeSequence(), Duration::fromSeconds(10.0, {9000, 1}));
    EXPECT_EQ(range.firstFrame, 24);
    EXPECT_EQ(range.frameCount, 24);
}

TEST(ExportSettings, FrameRangeInOutClampedToDuration) {
    ExportSettings s;
    s.range = ExportRange::InOut;
    s.inPoint = Time::fromFrame(0, {24000, 1001});
    s.outPoint = Time::fromFrame(500, {24000, 1001});
    // Timeline only 3s long ~ 72 frames.
    const auto range = frameRange(s, makeSequence(), Duration::fromSeconds(3.0, {9000, 1}));
    EXPECT_EQ(range.firstFrame, 0);
    EXPECT_EQ(range.frameCount, 72);
}

TEST(ExportSettings, FrameRangeInOutClampedToNonZeroStart) {
    ExportSettings s;
    s.range = ExportRange::InOut;
    s.inPoint = Time::fromFrame(150, {24000, 1001});
    s.outPoint = Time::fromFrame(500, {24000, 1001});
    const auto range = frameRange(s, makeSequence(), Duration::fromSeconds(3.0, {9000, 1}));
    EXPECT_EQ(range.firstFrame, 150);
    // 500-150 = 350 frames but only 72-150 < 0 remain; expect clamped to 0.
    EXPECT_EQ(range.frameCount, 0);
}

TEST(ExportSettings, FrameRangeInOutShiftsNegativeStart) {
    ExportSettings s;
    s.range = ExportRange::InOut;
    s.inPoint = Time::fromFrame(-10, {24000, 1001});
    s.outPoint = Time::fromFrame(90, {24000, 1001});
    const auto range = frameRange(s, makeSequence(), Duration::fromSeconds(10.0, {9000, 1}));
    EXPECT_EQ(range.firstFrame, 0);
    EXPECT_EQ(range.frameCount, 90);  // 10 dropped off the head, 90 remain
}

TEST(ExportSettings, DefaultContainerByCodec) {
    EXPECT_EQ(defaultContainerForCodec("h264"), "mp4");
    EXPECT_EQ(defaultContainerForCodec("vp9"), "webm");
    EXPECT_EQ(defaultContainerForCodec("av1"), "webm");
    EXPECT_EQ(defaultContainerForCodec("theora"), "webm");
    EXPECT_EQ(defaultContainerForCodec("mpeg4"), "mp4");
    EXPECT_EQ(defaultContainerForCodec("unknown"), "mp4");
}

TEST(ExportSettings, SupportedContainerAndCodec) {
    EXPECT_TRUE(isSupportedContainer("mp4"));
    EXPECT_TRUE(isSupportedContainer("webm"));
    EXPECT_TRUE(isSupportedContainer("mkv"));
    EXPECT_FALSE(isSupportedContainer("mov"));

    EXPECT_TRUE(isSupportedCodec("h264"));
    EXPECT_TRUE(isSupportedCodec("opus"));
    EXPECT_FALSE(isSupportedCodec("hevc"));
}

TEST(ExportSettings, StreamFormatForUrl) {
    EXPECT_EQ(streamFormatForUrl("rtmp://localhost:1935/live/key"), "flv");
    EXPECT_EQ(streamFormatForUrl("rtmps://ingest.example.com/app"), "flv");
    EXPECT_EQ(streamFormatForUrl("udp://127.0.0.1:5000"), "mpegts");
    // Scheme matching is case-insensitive.
    EXPECT_EQ(streamFormatForUrl("RTMP://HOST/app"), "flv");
    EXPECT_EQ(streamFormatForUrl("UDP://HOST:5000"), "mpegts");
    // Unsupported or malformed URLs.
    EXPECT_EQ(streamFormatForUrl("http://example.com/x.mp4"), "");
    EXPECT_EQ(streamFormatForUrl("https://example.com"), "");
    EXPECT_EQ(streamFormatForUrl("srt://host:9000"), "");
    EXPECT_EQ(streamFormatForUrl("no-scheme-here"), "");
    EXPECT_EQ(streamFormatForUrl("://host"), "");
    EXPECT_EQ(streamFormatForUrl(""), "");
}

TEST(ExportSettings, IsStreamUrl) {
    EXPECT_TRUE(isStreamUrl("rtmp://host/app"));
    EXPECT_TRUE(isStreamUrl("rtmps://host/app"));
    EXPECT_TRUE(isStreamUrl("udp://host:5000"));
    EXPECT_FALSE(isStreamUrl("http://host/x.mp4"));
    EXPECT_FALSE(isStreamUrl("srt://host:9000"));
    EXPECT_FALSE(isStreamUrl(""));
}

TEST(ExportSettings, PresetCatalogIsNonEmptyAndValid) {
    const auto& presets = exportPresets();
    ASSERT_FALSE(presets.empty());
    for (const ExportPreset& p : presets) {
        EXPECT_FALSE(p.id.empty());
        EXPECT_FALSE(p.name.empty());
        // Every preset must be encodable with the codecs/containers we ship.
        EXPECT_TRUE(isSupportedContainer(p.settings.container))
            << p.id << " container " << p.settings.container;
        EXPECT_TRUE(isSupportedCodec(p.settings.videoCodec))
            << p.id << " video codec " << p.settings.videoCodec;
        if (p.settings.includeAudio) {
            EXPECT_TRUE(isSupportedCodec(p.settings.audioCodec))
                << p.id << " audio codec " << p.settings.audioCodec;
        }
    }
}

TEST(ExportSettings, FindPresetById) {
    ASSERT_NE(findExportPreset("youtube-1080p"), nullptr);
    EXPECT_EQ(findExportPreset("youtube-1080p")->settings.videoCodec, "h264");
    EXPECT_EQ(findExportPreset("webm-vp9")->settings.container, "webm");
    EXPECT_EQ(findExportPreset("webm-vp9")->settings.audioCodec, "opus");
    EXPECT_EQ(findExportPreset("nope"), nullptr);
}

TEST(ExportSettings, Youtube4kPresetIsCustomUhd) {
    const ExportPreset* p = findExportPreset("youtube-4k");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->settings.scale, ResolutionScale::Custom);
    EXPECT_EQ(outputWidth(p->settings, makeSequence()), 3840u);
    EXPECT_EQ(outputHeight(p->settings, makeSequence()), 2160u);
}

} // namespace
} // namespace bl::ui