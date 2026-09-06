#include <bl_render/preview_engine.hpp>

#include <bl_core/project_data.hpp>
#include <bl_timeline/timeline.hpp>
#include "test_media_utils.hpp"
#include "test_plugin_utils.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

namespace {

using bl::Clip;
using bl::Duration;
using bl::Err;
using bl::MediaBinItem;
using bl::PreviewEngine;
using bl::Rational;
using bl::Sequence;
using bl::SourceRef;
using bl::Time;
using bl::TimelineSnapshot;
using bl::VideoTrack;

constexpr Rational kFps{24000, 1001};

static Time frame(int64_t f) { return Time::fromFrame(f, kFps); }
static Duration dur(int64_t f) { return Duration::fromFrames(f, kFps); }

// Points MediaDecodeSource at the staged codec plugins (video/audio subdirs
// are found by the loader automatically).
bl::PreviewEngine::Config makeConfig(uint32_t w = 160, uint32_t h = 120) {
    bl::PreviewEngine::Config cfg;
    cfg.outputWidth = w;
    cfg.outputHeight = h;
    cfg.plugins.pluginDirs.emplace_back(
        std::string(BL_TEST_PLUGIN_DIR) + "/plugins", bl::PluginOrigin::User);
    return cfg;
}

std::vector<MediaBinItem> mediaBinWithTheora() {
    return {MediaBinItem{"theora", bltest::mediaPath("test_video_theora.ogv"),
                         "theora.ogv"}};
}

TimelineSnapshot snapshotWithTheoraClip() {
    Sequence seq;
    seq.settings.width = 160;
    seq.settings.height = 120;

    VideoTrack track("V1");
    Clip clip;
    clip.id = "c1";
    clip.source.mediaItemId = "theora";
    clip.source.sourceIn = frame(0);
    clip.source.sourceOut = frame(24);
    clip.timelineStart = frame(0);
    clip.timelineDuration = dur(24);
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));

    return TimelineSnapshot(std::move(seq));
}

size_t differingBytes(const std::vector<uint8_t>& a,
                      const std::vector<uint8_t>& b) {
    const size_t n = std::min(a.size(), b.size());
    size_t diff = 0;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) ++diff;
    }
    return a.size() != b.size() ? diff + (a.size() > b.size()
                                              ? a.size() - b.size()
                                              : b.size() - a.size())
                                : diff;
}

TEST(PreviewEngineTest, CreateRejectsInvalidOutputSize) {
    auto cfg = makeConfig();
    cfg.outputWidth = 0;
    auto result = PreviewEngine::create(cfg, mediaBinWithTheora());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::InvalidArgument);

    auto cfg2 = makeConfig();
    cfg2.outputHeight = 5000;
    result = PreviewEngine::create(cfg2, mediaBinWithTheora());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::InvalidArgument);
}

TEST(PreviewEngineTest, EmptyTimelineRendersTransparent) {
    auto engine = PreviewEngine::create(makeConfig(), {});
    ASSERT_TRUE(engine.ok()) << engine.message();

    Sequence seq;
    seq.settings.width = 160;
    seq.settings.height = 120;
    auto out = engine.value().runAt(TimelineSnapshot(std::move(seq)), frame(0));
    ASSERT_TRUE(out.ok()) << out.message();
    EXPECT_EQ(out->width, 160u);
    EXPECT_EQ(out->height, 120u);
    EXPECT_EQ(out->linesize, 160u * 4u);
    EXPECT_TRUE(std::all_of(out->data.begin(), out->data.end(),
                            [](uint8_t b) { return b == 0; }));
}

TEST(PreviewEngineTest, DecodesContainerFixtureThroughFullPipeline) {
    auto result = PreviewEngine::create(makeConfig(), mediaBinWithTheora());
    ASSERT_TRUE(result.ok()) << result.message();
    PreviewEngine engine = std::move(result.value());

    auto out = engine.runAt(snapshotWithTheoraClip(), frame(0));
    ASSERT_TRUE(out.ok()) << out.message();
    EXPECT_EQ(out->width, 160u);
    EXPECT_EQ(out->height, 120u);

    // testsrc is not black: the frame must carry real pixels.
    const auto notBlack = [](const std::vector<uint8_t>& d) {
        return std::any_of(d.begin(), d.end(), [](uint8_t b) { return b != 0; });
    };
    EXPECT_TRUE(notBlack(out->data));
}

TEST(PreviewEngineTest, LaterFramesDifferFromEarlyFrame) {
    auto result = PreviewEngine::create(makeConfig(), mediaBinWithTheora());
    ASSERT_TRUE(result.ok()) << result.message();
    PreviewEngine engine = std::move(result.value());

    auto early = engine.runAt(snapshotWithTheoraClip(), frame(0));
    auto late = engine.runAt(snapshotWithTheoraClip(), frame(12));
    ASSERT_TRUE(early.ok());
    ASSERT_TRUE(late.ok());

    // Moving testsrc + a real codec: frames at different times must not be
    // byte-identical.
    EXPECT_GT(differingBytes(early->data, late->data), 0u);
}

TEST(PreviewEngineTest, RepeatRequestReturnsCachedFrame) {
    auto result = PreviewEngine::create(makeConfig(), mediaBinWithTheora());
    ASSERT_TRUE(result.ok()) << result.message();
    PreviewEngine engine = std::move(result.value());

    auto snap = snapshotWithTheoraClip();
    auto first = engine.runAt(snap, frame(5));
    auto second = engine.runAt(snap, frame(5));
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first->data, second->data);

    // A different position must re-decode (no stale-frame aliasing).
    auto other = engine.runAt(snap, frame(11));
    ASSERT_TRUE(other.ok());
    EXPECT_GT(differingBytes(first->data, other->data), 0u);
}

TEST(PreviewEngineTest, ResetClosesSourcesAndReopensOnDemand) {
    auto result = PreviewEngine::create(makeConfig(), mediaBinWithTheora());
    ASSERT_TRUE(result.ok()) << result.message();
    PreviewEngine engine = std::move(result.value());

    auto snap = snapshotWithTheoraClip();
    ASSERT_TRUE(engine.runAt(snap, frame(0)).ok());

    engine.reset();
    auto after = engine.runAt(snap, frame(1));
    ASSERT_TRUE(after.ok()) << after.message();
    EXPECT_EQ(after->width, 160u);
    EXPECT_EQ(after->height, 120u);
}

TEST(PreviewEngineTest, UnknownMediaItemRendersBlackNotError) {
    auto result = PreviewEngine::create(makeConfig(), mediaBinWithTheora());
    ASSERT_TRUE(result.ok()) << result.message();
    PreviewEngine engine = std::move(result.value());

    Sequence seq;
    seq.settings.width = 160;
    seq.settings.height = 120;
    VideoTrack track("V1");
    Clip clip;
    clip.id = "c1";
    clip.source.mediaItemId = "missing.mp4";
    clip.source.sourceIn = frame(0);
    clip.source.sourceOut = frame(24);
    clip.timelineStart = frame(0);
    clip.timelineDuration = dur(24);
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));

    auto out = engine.runAt(TimelineSnapshot(std::move(seq)), frame(0));
    ASSERT_TRUE(out.ok()) << out.message();
    EXPECT_TRUE(std::all_of(out->data.begin(), out->data.end(),
                            [](uint8_t b) { return b == 0; }));
}

} // namespace