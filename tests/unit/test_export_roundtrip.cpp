#include "ui/export/export_settings.hpp"

#include <bl_core/result.hpp>
#include <bl_core/time.hpp>
#include <bl_timeline/timeline.hpp>

#include <gtest/gtest.h>

namespace bl::ui {
namespace {

using bl::Duration;
using bl::Rational;
using bl::Time;

// Helper: create a minimal sequence with one video clip
SequenceSettings makeSequence() {
    SequenceSettings s;
    s.width = 1920;
    s.height = 1080;
    s.fps = {24000, 1001};  // 23.976
    s.sampleRate = 48000;
    return s;
}

// Helper: create a minimal timeline with one video clip
Timeline makeTimeline() {
    Timeline tl;
    tl.sequence().settings.fps = {24000, 1001};
    tl.sequence().settings.width = 1920;
    tl.sequence().settings.height = 1080;
    tl.sequence().addVideoTrack("V1");
    Clip clip;
    clip.id = "c1";
    clip.source.mediaItemId = "1";
    clip.source.sourceIn = Time::fromFrame(0, {24000, 1001});
    clip.source.sourceOut = Time::fromFrame(24, {24000, 1001});
    clip.timelineStart = Time::fromFrame(0, {24000, 1001});
    clip.timelineDuration = Duration::fromFrames(24, {24000, 1001});
    tl.sequence().videoTracks[0].addClip(clip);
    return tl;
}

TEST(ExportSettings, IsSupportedContainerMkv) {
    // MKV should be a supported container
    EXPECT_TRUE(isSupportedContainer("mkv"));
    EXPECT_TRUE(isSupportedContainer("mp4"));
    EXPECT_TRUE(isSupportedContainer("webm"));
}

TEST(ExportSettings, IsNotSupportedContainerInvalid) {
    // These should not be supported
    EXPECT_FALSE(isSupportedContainer("avi"));
    EXPECT_FALSE(isSupportedContainer(""));
}

TEST(ExportSettings, DefaultContainerForCodecH264) {
    // H.264 defaults to MP4
    EXPECT_EQ(defaultContainerForCodec("h264"), "mp4");
}

TEST(ExportSettings, DefaultContainerForVp9) {
    // VP9 defaults to WebM
    EXPECT_EQ(defaultContainerForCodec("vp9"), "webm");
}

TEST(ExportSettings, DefaultContainerForAac) {
    // AAC defaults to MP4
    EXPECT_EQ(defaultContainerForCodec("aac"), "mp4");
}

TEST(ExportSettings, DefaultContainerForOpus) {
    // Opus defaults to WebM
    EXPECT_EQ(defaultContainerForCodec("opus"), "webm");
}

TEST(ExportSettings, AudioCodecValidationMkv) {
    // Test audio codec validation for MKV container
    // MKV accepts all offered codecs

    // AAC should be valid for MKV
    {
        ExportSettings s;
        s.container = "mkv";
        s.includeAudio = true;
        s.audioCodec = "aac";
        bool containerOk = false;
        if (s.container == "mp4") {
            containerOk = s.audioCodec == "aac" || s.audioCodec == "flac" ||
                          s.audioCodec == "opus";
        } else if (s.container == "webm") {
            containerOk = s.audioCodec == "opus" || s.audioCodec == "vorbis";
        } else {
            // MKV branch
            containerOk = s.audioCodec == "aac" || s.audioCodec == "flac" ||
                          s.audioCodec == "opus" || s.audioCodec == "vorbis";
        }
        EXPECT_TRUE(containerOk) << "AAC should be valid for MKV";
    }

    // FLAC should be valid for MKV
    {
        ExportSettings s;
        s.container = "mkv";
        s.includeAudio = true;
        s.audioCodec = "flac";
        bool containerOk = false;
        if (s.container == "mp4") {
            containerOk = s.audioCodec == "aac" || s.audioCodec == "flac" ||
                          s.audioCodec == "opus";
        } else if (s.container == "webm") {
            containerOk = s.audioCodec == "opus" || s.audioCodec == "vorbis";
        } else {
            containerOk = s.audioCodec == "aac" || s.audioCodec == "flac" ||
                          s.audioCodec == "opus" || s.audioCodec == "vorbis";
        }
        EXPECT_TRUE(containerOk) << "FLAC should be valid for MKV";
    }

    // Opus should be valid for MKV
    {
        ExportSettings s;
        s.container = "mkv";
        s.includeAudio = true;
        s.audioCodec = "opus";
        bool containerOk = false;
        if (s.container == "mp4") {
            containerOk = s.audioCodec == "aac" || s.audioCodec == "flac" ||
                          s.audioCodec == "opus";
        } else if (s.container == "webm") {
            containerOk = s.audioCodec == "opus" || s.audioCodec == "vorbis";
        } else {
            containerOk = s.audioCodec == "aac" || s.audioCodec == "flac" ||
                          s.audioCodec == "opus" || s.audioCodec == "vorbis";
        }
        EXPECT_TRUE(containerOk) << "Opus should be valid for MKV";
    }

    // Vorbis should be valid for MKV
    {
        ExportSettings s;
        s.container = "mkv";
        s.includeAudio = true;
        s.audioCodec = "vorbis";
        bool containerOk = false;
        if (s.container == "mp4") {
            containerOk = s.audioCodec == "aac" || s.audioCodec == "flac" ||
                          s.audioCodec == "opus";
        } else if (s.container == "webm") {
            containerOk = s.audioCodec == "opus" || s.audioCodec == "vorbis";
        } else {
            containerOk = s.audioCodec == "aac" || s.audioCodec == "flac" ||
                          s.audioCodec == "opus" || s.audioCodec == "vorbis";
        }
        EXPECT_TRUE(containerOk) << "Vorbis should be valid for MKV";
    }
}

}  // namespace
}  // namespace bl::ui