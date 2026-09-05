#include <bl_render/compositor.hpp>
#include <bl_render/effect.hpp>
#include <bl_timeline/timeline.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using bl::Clip;
using bl::Compositor;
using bl::CompositorConfig;
using bl::CompositorResult;
using bl::Duration;
using bl::EffectInstance;
using bl::Err;
using bl::Frame;
using bl::IDecodeProvider;
using bl::Rational;
using bl::Sequence;
using bl::SequenceSettings;
using bl::SourceRef;
using bl::SpeedRemap;
using bl::Time;
using bl::TimelineSnapshot;
using bl::Track;
using bl::TrackKind;
using bl::VideoTrack;

static const Rational kFps{24000, 1001};

static Time frame(int64_t f) { return Time::fromFrame(f, kFps); }
static Duration dur(int64_t f) { return Duration::fromFrames(f, kFps); }

// Stub decode provider: returns solid-color BGRA32 frames.
// Color is derived from mediaItemId hash.
class StubDecodeProvider : public IDecodeProvider {
public:
    explicit StubDecodeProvider(uint8_t r = 128, uint8_t g = 128, uint8_t b = 128)
        : defaultR_(r), defaultG_(g), defaultB_(b) {}

    bl::Result<Frame> getFrame(const std::string& mediaItemId, Time /*sourceTime*/,
                           uint32_t width, uint32_t height) override {
        Frame f;
        f.type = Frame::Type::Video;
        f.width = width;
        f.height = height;
        f.linesize = width * 4;
        f.dataSize = width * height * 4;
        f.data = new uint8_t[f.dataSize];

        uint8_t r = defaultR_, g = defaultG_, b = defaultB_;
        if (colorMap_.count(mediaItemId)) {
            auto& c = colorMap_[mediaItemId];
            r = c[0]; g = c[1]; b = c[2];
        }

        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = f.data + y * f.linesize;
            for (uint32_t x = 0; x < width; ++x) {
                row[x * 4 + 0] = b;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = r;
                row[x * 4 + 3] = 255;
            }
        }
        return bl::Result<Frame>::ok(std::move(f));
    }

    void setColor(const std::string& id, uint8_t r, uint8_t g, uint8_t b) {
        colorMap_[id] = {r, g, b};
    }

private:
    uint8_t defaultR_, defaultG_, defaultB_;
    std::unordered_map<std::string, std::array<uint8_t, 3>> colorMap_;
};

struct CompositorTest : ::testing::Test {
    void SetUp() override {
        bl::EffectRegistry::instance().clear();
        bl::registerBuiltinEffects();
    }
};

TEST_F(CompositorTest, EmptySequenceReturnsBlack) {
    Sequence seq;
    seq.settings.width = 4;
    seq.settings.height = 4;
    TimelineSnapshot snap(std::move(seq));

    auto comp = Compositor::create({4, 4});
    ASSERT_TRUE(comp.ok()) << comp.message();

    StubDecodeProvider provider;
    auto result = comp->renderFrame(snap, frame(0), provider);
    ASSERT_TRUE(result.ok()) << result.message();

    EXPECT_EQ(result->width, 4u);
    EXPECT_EQ(result->height, 4u);
    // All pixels should be transparent black (0)
    for (size_t i = 0; i < result->data.size(); ++i) {
        EXPECT_EQ(result->data[i], 0) << "byte " << i;
    }
}

TEST_F(CompositorTest, SingleClipFullyCovers) {
    Sequence seq;
    seq.settings.width = 2;
    seq.settings.height = 1;

    VideoTrack track("V1");
    Clip clip;
    clip.id = "c1";
    clip.name = "clip1";
    clip.source.mediaItemId = "red.mp4";
    clip.source.sourceIn = frame(0);
    clip.source.sourceOut = frame(10);
    clip.timelineStart = frame(0);
    clip.timelineDuration = dur(10);
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({2, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();

    // All pixels should be red (BGRA: B=0, G=0, R=255, A=255)
    for (uint32_t x = 0; x < 2; ++x) {
        EXPECT_EQ(result->data[x * 4 + 0], 0u);   // B
        EXPECT_EQ(result->data[x * 4 + 1], 0u);   // G
        EXPECT_EQ(result->data[x * 4 + 2], 255u);  // R
        EXPECT_EQ(result->data[x * 4 + 3], 255u);  // A
    }
}

TEST_F(CompositorTest, TwoTrackAlphaBlend) {
    Sequence seq;
    seq.settings.width = 1;
    seq.settings.height = 1;

    // Track 0 (bottom): red
    VideoTrack track0("V0");
    Clip clip0;
    clip0.id = "c0";
    clip0.source.mediaItemId = "red.mp4";
    clip0.source.sourceIn = frame(0);
    clip0.source.sourceOut = frame(10);
    clip0.timelineStart = frame(0);
    clip0.timelineDuration = dur(10);
    track0.addClip(clip0);
    seq.videoTracks.push_back(std::move(track0));

    // Track 1 (top): blue
    VideoTrack track1("V1");
    Clip clip1;
    clip1.id = "c1";
    clip1.source.mediaItemId = "blue.mp4";
    clip1.source.sourceIn = frame(0);
    clip1.source.sourceOut = frame(10);
    clip1.timelineStart = frame(0);
    clip1.timelineDuration = dur(10);
    track1.addClip(clip1);
    seq.videoTracks.push_back(std::move(track1));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();

    // Track 0 red first, then track 1 blue on top → should be blue
    EXPECT_EQ(result->data[0], 255u);  // B (blue)
    EXPECT_EQ(result->data[1], 0u);    // G
    EXPECT_EQ(result->data[2], 0u);    // R
}

TEST_F(CompositorTest, MutedTrackSkipped) {
    Sequence seq;
    seq.settings.width = 1;
    seq.settings.height = 1;

    VideoTrack track0("V0");
    Clip clip0;
    clip0.id = "c0";
    clip0.source.mediaItemId = "red.mp4";
    clip0.source.sourceIn = frame(0);
    clip0.source.sourceOut = frame(10);
    clip0.timelineStart = frame(0);
    clip0.timelineDuration = dur(10);
    track0.addClip(clip0);
    track0.setMuted(true);
    seq.videoTracks.push_back(std::move(track0));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok());

    // Muted track → black
    EXPECT_EQ(result->data[0], 0u);
    EXPECT_EQ(result->data[1], 0u);
    EXPECT_EQ(result->data[2], 0u);
}

TEST_F(CompositorTest, KeyframeOpacityApplied) {
    Sequence seq;
    seq.settings.width = 1;
    seq.settings.height = 1;

    VideoTrack track("V1");
    Clip clip;
    clip.id = "c1";
    clip.source.mediaItemId = "red.mp4";
    clip.source.sourceIn = frame(0);
    clip.source.sourceOut = frame(10);
    clip.timelineStart = frame(0);
    clip.timelineDuration = dur(10);
    clip.keyframes = bl::KeyframeTrackSet();
    clip.keyframes->ensure(bl::KeyChannel::Opacity);
    clip.keyframes->track(bl::KeyChannel::Opacity)->set(frame(0), 0.5, bl::Interpolation::Linear);
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 200, 0, 0);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();

    // 50% opacity blend of red (200,0,0) on black (0,0,0)
    // R = 200 * 0.5 = 100
    EXPECT_NEAR(result->data[2], 100, 2);
}

TEST_F(CompositorTest, SpeedRemapMapping) {
    Sequence seq;
    seq.settings.width = 1;
    seq.settings.height = 1;

    VideoTrack track("V1");
    Clip clip;
    clip.id = "c1";
    clip.source.mediaItemId = "color.mp4";
    clip.source.sourceIn = frame(0);
    clip.source.sourceOut = frame(20);
    clip.timelineStart = frame(0);
    clip.timelineDuration = dur(10);  // 10 frames on timeline
    clip.speed = SpeedRemap{2, 1, false};  // 2x speed → source moves 2x
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("color.mp4", 100, 200, 50);

    // At timeline frame 5, source should be at frame 10 (2x speed)
    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok());

    // Should get the frame — color provider returns same color regardless of time
    EXPECT_EQ(result->data[2], 100u);  // R
    EXPECT_EQ(result->data[1], 200u);  // G
    EXPECT_EQ(result->data[0], 50u);   // B
}

TEST_F(CompositorTest, EffectChainApplied) {
    Sequence seq;
    seq.settings.width = 1;
    seq.settings.height = 1;

    VideoTrack track("V1");
    Clip clip;
    clip.id = "c1";
    clip.source.mediaItemId = "color.mp4";
    clip.source.sourceIn = frame(0);
    clip.source.sourceOut = frame(10);
    clip.timelineStart = frame(0);
    clip.timelineDuration = dur(10);

    EffectInstance fx;
    fx.effectId = "greyscale";
    fx.enabled = true;
    clip.effects.push_back(fx);

    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("color.mp4", 100, 150, 200);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();

    // Greyscale of (R=100,G=150,B=200) = 0.299*100 + 0.587*150 + 0.114*200
    // = 29.9 + 88.05 + 22.8 = 140.75 ≈ 141
    uint8_t expected = 141;
    EXPECT_NEAR(result->data[0], expected, 2);  // B
    EXPECT_NEAR(result->data[1], expected, 2);  // G
    EXPECT_NEAR(result->data[2], expected, 2);  // R
}

TEST_F(CompositorTest, ClipOutsideTimeRangeNotRendered) {
    Sequence seq;
    seq.settings.width = 1;
    seq.settings.height = 1;

    VideoTrack track("V1");
    Clip clip;
    clip.id = "c1";
    clip.source.mediaItemId = "red.mp4";
    clip.source.sourceIn = frame(0);
    clip.source.sourceOut = frame(5);
    clip.timelineStart = frame(0);
    clip.timelineDuration = dur(5);  // only frames 0-4
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);

    // Frame 10 is outside the clip's range
    auto result = comp->renderFrame(snap, frame(10), provider);
    ASSERT_TRUE(result.ok());

    // Should be black (no clip active)
    EXPECT_EQ(result->data[0], 0u);
    EXPECT_EQ(result->data[2], 0u);
}

} // namespace
