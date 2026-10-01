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

} // namespace
} // namespace bl::ui