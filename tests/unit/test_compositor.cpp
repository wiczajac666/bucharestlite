#include <bl_render/compositor.hpp>
#include <bl_render/effect.hpp>
#include <bl_timeline/timeline.hpp>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
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
using bl::TransitionAlignment;
using bl::TransitionKind;
using bl::TransitionSpec;
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
        uint8_t a = defaultA_;
        if (colorMap_.count(mediaItemId)) {
            auto& c = colorMap_[mediaItemId];
            r = c[0]; g = c[1]; b = c[2];
        }
        if (alphaMap_.count(mediaItemId)) {
            a = alphaMap_[mediaItemId];
        }

        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = f.data + y * f.linesize;
            for (uint32_t x = 0; x < width; ++x) {
                row[x * 4 + 0] = b;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = r;
                row[x * 4 + 3] = a;
            }
        }
        return bl::Result<Frame>::ok(std::move(f));
    }

    void setColor(const std::string& id, uint8_t r, uint8_t g, uint8_t b) {
        colorMap_[id] = {r, g, b};
    }

    void setAlpha(const std::string& id, uint8_t a) { alphaMap_[id] = a; }

private:
    uint8_t defaultR_, defaultG_, defaultB_;
    uint8_t defaultA_{255};
    std::unordered_map<std::string, std::array<uint8_t, 3>> colorMap_;
    std::unordered_map<std::string, uint8_t> alphaMap_;
};

// Records the source times the compositor requests per media item.
class RecordingProvider : public StubDecodeProvider {
public:
    std::vector<bl::Time> requested;

    bl::Result<Frame> getFrame(const std::string& mediaItemId, Time sourceTime,
                               uint32_t width, uint32_t height) override {
        requested.push_back(sourceTime);
        return StubDecodeProvider::getFrame(mediaItemId, sourceTime,
                                            width, height);
    }
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

TEST_F(CompositorTest, OpaqueAlphaBlendMatchesOpacity) {
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
    clip0.keyframes = bl::KeyframeTrackSet();
    clip0.keyframes->ensure(bl::KeyChannel::Opacity);
    clip0.keyframes->track(bl::KeyChannel::Opacity)
        ->set(frame(0), 0.5, bl::Interpolation::Linear);
    track0.addClip(clip0);
    seq.videoTracks.push_back(std::move(track0));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 200, 100, 50);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();

    // Opaque 200/100/50 at 50% over transparent black → 100/50/25,
    // with blended alpha 127 (premultiplied accumulation, as before).
    EXPECT_NEAR(result->data[2], 100, 2);  // R
    EXPECT_NEAR(result->data[1], 50, 2);   // G
    EXPECT_NEAR(result->data[0], 25, 2);   // B
    EXPECT_NEAR(result->data[3], 127, 2);  // A
}

TEST_F(CompositorTest, TransparentTopTrackShowsThrough) {
    Sequence seq;
    seq.settings.width = 1;
    seq.settings.height = 1;

    // Bottom track: opaque red.
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

    // Top track: yellow with per-pixel alpha 0 (as a chroma-keyed frame).
    VideoTrack track1("V1");
    Clip clip1;
    clip1.id = "c1";
    clip1.source.mediaItemId = "yellow.mp4";
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
    provider.setColor("yellow.mp4", 255, 255, 0);
    provider.setAlpha("yellow.mp4", 0);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();

    // Keyed-out top layer contributes nothing → underlying red stays.
    EXPECT_EQ(result->data[2], 255u);  // R
    EXPECT_EQ(result->data[1], 0u);    // G
    EXPECT_EQ(result->data[0], 0u);    // B
    EXPECT_EQ(result->data[3], 255u);  // A
}

TEST_F(CompositorTest, ReversedClipSamplesSourceBackwards) {
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
    clip.timelineDuration = dur(20);
    clip.speed = SpeedRemap{1, 1, true};
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    RecordingProvider provider;
    provider.setColor("color.mp4", 10, 20, 30);

    auto result = comp->renderFrame(snap, frame(0), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    ASSERT_EQ(provider.requested.size(), 1u);
    EXPECT_EQ(provider.requested[0], frame(20));  // starts at sourceOut

    provider.requested.clear();
    result = comp->renderFrame(snap, frame(10), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    EXPECT_EQ(provider.requested[0], frame(10));

    provider.requested.clear();
    result = comp->renderFrame(snap, frame(19), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    EXPECT_EQ(provider.requested[0], frame(1));  // approaches sourceIn
}

// ---------------------------------------------------------------------------
// P1: Kdenlive-parity gating — disabled clips, audio-only halves, and hidden
// tracks render nothing.
// ---------------------------------------------------------------------------

namespace {

// One red clip on a single video track, with a tweak applied first.
Sequence singleRedSequence(const std::function<void(Clip&)>& tweak = {}) {
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
    if (tweak) tweak(clip);
    track.addClip(clip);
    seq.videoTracks.push_back(std::move(track));
    return seq;
}

} // namespace

TEST_F(CompositorTest, DisabledClipSkipped) {
    TimelineSnapshot snap(singleRedSequence([](Clip& c) { c.enabled = false; }));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    // Disabled clip → black.
    EXPECT_EQ(result->data[0], 0u);
    EXPECT_EQ(result->data[1], 0u);
    EXPECT_EQ(result->data[2], 0u);
}

TEST_F(CompositorTest, AudioOnlyClipSkipped) {
    TimelineSnapshot snap(
        singleRedSequence([](Clip& c) { c.hasVideo = false; }));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    // Audio-only half → black.
    EXPECT_EQ(result->data[0], 0u);
    EXPECT_EQ(result->data[1], 0u);
    EXPECT_EQ(result->data[2], 0u);
}

TEST_F(CompositorTest, HiddenTrackSkipped) {
    Sequence seq = singleRedSequence();
    seq.videoTracks[0].setHidden(true);
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    // Hidden track → black.
    EXPECT_EQ(result->data[0], 0u);
    EXPECT_EQ(result->data[1], 0u);
    EXPECT_EQ(result->data[2], 0u);
}

TEST_F(CompositorTest, DisabledTopClipRevealsLowerTrack) {
    Sequence seq = singleRedSequence(); // red on V0

    VideoTrack top("V1");
    Clip blue;
    blue.id = "c2";
    blue.source.mediaItemId = "blue.mp4";
    blue.source.sourceIn = frame(0);
    blue.source.sourceOut = frame(10);
    blue.timelineStart = frame(0);
    blue.timelineDuration = dur(10);
    blue.enabled = false;
    top.addClip(blue);
    seq.videoTracks.push_back(std::move(top));

    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    auto result = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    // The disabled blue clip on the top track must not cover the red one.
    EXPECT_EQ(result->data[0], 0u);   // B
    EXPECT_EQ(result->data[1], 0u);   // G
    EXPECT_EQ(result->data[2], 255u); // R
}

// ---------------------------------------------------------------------------
// Transition rendering. Preview and export both composite through the single
// Compositor::renderFrame path, so these assertions cover both surfaces.
// ---------------------------------------------------------------------------

namespace {

// Two adjacent clips on one video track: A (red) then B (blue), both starting
// at source frame 0. `tweak` runs after both clips exist so it can call
// addTransition() (addClip's prune sweep would drop a hand-set transitionOut).
Sequence twoClipSequence(int64_t aDur, int64_t bDur, uint32_t w, uint32_t h,
                         const std::function<void(VideoTrack&)>& tweak = {}) {
    Sequence seq;
    seq.settings.width = w;
    seq.settings.height = h;

    VideoTrack track("V1");
    Clip a;
    a.id = "a";
    a.name = "A";
    a.source.mediaItemId = "red.mp4";
    a.source.sourceIn = frame(0);
    a.source.sourceOut = frame(aDur);
    a.timelineStart = frame(0);
    a.timelineDuration = dur(aDur);

    Clip b;
    b.id = "b";
    b.name = "B";
    b.source.mediaItemId = "blue.mp4";
    b.source.sourceIn = frame(0);
    b.source.sourceOut = frame(bDur);
    b.timelineStart = frame(aDur);
    b.timelineDuration = dur(bDur);

    EXPECT_TRUE(track.addClip(a));
    EXPECT_TRUE(track.addClip(b));
    if (tweak) tweak(track);
    seq.videoTracks.push_back(std::move(track));
    return seq;
}

TransitionSpec makeSpec(TransitionKind kind, TransitionAlignment alignment,
                        int64_t durFrames,
                        nlohmann::json params = nlohmann::json::object()) {
    TransitionSpec s;
    s.kind = kind;
    s.alignment = alignment;
    s.duration = dur(durFrames);
    s.params = std::move(params);
    return s;
}

// Asserts one BGRA pixel (column `x` of a single-row result) plus its alpha.
void expectPixel(const CompositorResult& r, uint32_t x, uint8_t b, uint8_t g,
                 uint8_t rr, uint8_t a = 255) {
    EXPECT_EQ(r.data[x * 4 + 0], b) << "col " << x << " B";
    EXPECT_EQ(r.data[x * 4 + 1], g) << "col " << x << " G";
    EXPECT_EQ(r.data[x * 4 + 2], rr) << "col " << x << " R";
    EXPECT_EQ(r.data[x * 4 + 3], a) << "col " << x << " A";
}

} // namespace

TEST_F(CompositorTest, CrossfadeMidpointBlendsHalfway) {
    // A red (255,0,0) → B blue (0,0,255), crossfade duration 4, Left-aligned
    // to the outgoing tail: span is [aEnd-4, aEnd) = [6, 10).
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Crossfade, TransitionAlignment::Left,
                          4)));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok()) << comp.message();

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // Midpoint of the 4-frame span (frame 8) is 50/50. Truncated per channel:
    // A red contributes 255*0.5 = 127 to R, B blue contributes 255*0.5 = 127
    // to B, so the blend is (B=127, G=0, R=127). A contributes, so this is a
    // real blend rather than an overwrite toward B.
    auto result = comp->renderFrame(snap, frame(8), provider);
    ASSERT_TRUE(result.ok()) << result.message();
    expectPixel(*result, 0, 127, 0, 127);
    expectPixel(*result, 3, 127, 0, 127);
}

TEST_F(CompositorTest, LeftAlignmentSpanIsOutgoingTail) {
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Crossfade, TransitionAlignment::Left,
                          4)));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // Frame 5: before the span → the normal path renders pure A.
    auto r = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 255);

    // Frame 6 == span start: progress 0 → pure A.
    r = comp->renderFrame(snap, frame(6), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 255);

    // Frame 9 (last tick in [6,10)): progress 3/4 → R 255*0.25 = 63,
    // B 255*0.75 = 191 (truncated).
    r = comp->renderFrame(snap, frame(9), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 191, 0, 63);

    // Frame 10 == aEnd: span end is exclusive, so the normal path renders B.
    r = comp->renderFrame(snap, frame(10), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 255, 0, 0);
}

TEST_F(CompositorTest, CenterAlignmentStraddlesCut) {
    // Dissolve (the standard alpha crossfade) duration 4, Center: half = 2,
    // span = [aEnd-2, aEnd+2) = [8, 12).
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Dissolve,
                          TransitionAlignment::Center, 4)));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // Frame 8 == span start: pure A.
    auto r = comp->renderFrame(snap, frame(8), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 255);

    // Frame 10 == aEnd: progress 0.5 → exact 50/50. B's offset is clamped to
    // its first frame here (t < b.timelineStart).
    r = comp->renderFrame(snap, frame(10), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 127, 0, 127);

    // Frame 11 (just before aEnd+dur/2): progress 3/4 → R 63, B 191.
    r = comp->renderFrame(snap, frame(11), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 191, 0, 63);

    // Frame 12 == span end (exclusive) → normal path renders pure B.
    r = comp->renderFrame(snap, frame(12), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 255, 0, 0);
}

TEST_F(CompositorTest, RightAlignmentSpanIsIncomingHead) {
    // Crossfade duration 4, Right: span = [aEnd, aEnd+4) = [10, 14).
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Crossfade,
                          TransitionAlignment::Right, 4)));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    RecordingProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // Frame 10 == span start: progress 0 → pure A, sourced from A's last frame
    // (offset 10 clamped to A's 10-frame span) while B is at its first frame.
    auto r = comp->renderFrame(snap, frame(10), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 255);
    ASSERT_EQ(provider.requested.size(), 2u);
    EXPECT_EQ(provider.requested[0], frame(10));  // A clamped to its end
    EXPECT_EQ(provider.requested[1], frame(0));   // B's first frame

    // Frame 12: progress 0.5 → exact 50/50; A stays clamped at its end.
    provider.requested.clear();
    r = comp->renderFrame(snap, frame(12), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 127, 0, 127);
    ASSERT_EQ(provider.requested.size(), 2u);
    EXPECT_EQ(provider.requested[0], frame(10));  // A clamped
    EXPECT_EQ(provider.requested[1], frame(2));   // B advances

    // Frame 14 == span end (exclusive) → normal path renders pure B.
    r = comp->renderFrame(snap, frame(14), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 255, 0, 0);
}

TEST_F(CompositorTest, WipeEdgePosition) {
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Wipe, TransitionAlignment::Left, 4,
                          nlohmann::json{{"direction", "left"}})));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // Frame 8 → p = 0.5, W = 4: the incoming blue is revealed over the first
    // half of the width (columns 0,1) and red remains on columns 2,3.
    auto r = comp->renderFrame(snap, frame(8), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 255, 0, 0);  // blue
    expectPixel(*r, 1, 255, 0, 0);  // blue
    expectPixel(*r, 2, 0, 0, 255);  // red
    expectPixel(*r, 3, 0, 0, 255);  // red
}

TEST_F(CompositorTest, WipeSoftnessRampsAcrossEdge) {
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Wipe, TransitionAlignment::Left, 4,
                          nlohmann::json{{"direction", "left"},
                                         {"softness", 2}})));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // p = 0.5 → edge at x = 2, a 2-pixel ramp straddling it: coverage
    // [1, 1, 0.5, 0] for columns 0..3 → blue, blue, 50/50, red.
    auto r = comp->renderFrame(snap, frame(8), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 255, 0, 0);  // full B
    expectPixel(*r, 1, 255, 0, 0);  // full B
    expectPixel(*r, 2, 127, 0, 127);  // ramp midpoint
    expectPixel(*r, 3, 0, 0, 255);  // full A
}

TEST_F(CompositorTest, SlideDirection) {
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Slide, TransitionAlignment::Left, 4,
                          nlohmann::json{{"direction", "left"}})));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // p = 0.5, slide from the left: the left half samples B, the right half
    // samples A (solid colours, so this is a pure copy).
    auto r = comp->renderFrame(snap, frame(8), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 255, 0, 0);  // B
    expectPixel(*r, 1, 255, 0, 0);  // B
    expectPixel(*r, 2, 0, 0, 255);  // A
    expectPixel(*r, 3, 0, 0, 255);  // A
}

TEST_F(CompositorTest, SlideVerticalDirection) {
    // 1x4 canvas to exercise the up/down sampling axes.
    Sequence seq = twoClipSequence(10, 10, 1, 4, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Slide, TransitionAlignment::Left, 4,
                          nlohmann::json{{"direction", "up"}})));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({1, 4});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // p = 0.5, slide up: rows 0,1 sample B; rows 2,3 sample A.
    auto r = comp->renderFrame(snap, frame(8), provider);
    ASSERT_TRUE(r.ok());
    auto checkRow = [&](uint32_t y, uint8_t b, uint8_t g, uint8_t rr) {
        const uint8_t* p = r->data.data() + y * r->linesize;
        EXPECT_EQ(p[0], b) << "row " << y << " B";
        EXPECT_EQ(p[1], g) << "row " << y << " G";
        EXPECT_EQ(p[2], rr) << "row " << y << " R";
        EXPECT_EQ(p[3], 255) << "row " << y << " A";
    };
    checkRow(0, 255, 0, 0);  // B
    checkRow(1, 255, 0, 0);  // B
    checkRow(2, 0, 0, 255);  // A
    checkRow(3, 0, 0, 255);  // A
}

TEST_F(CompositorTest, FadeThroughBlack) {
    // No dedicated Fade kind exists; a fade is a transition flagged
    // through-black. Duration 8, Left → span [2, 10).
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Crossfade, TransitionAlignment::Left,
                          8, nlohmann::json{{"throughBlack", true}})));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // Frame 2 == span start: progress 0 → pure A.
    auto r = comp->renderFrame(snap, frame(2), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 255);

    // Frame 5: progress 3/8 = 0.375 → first half dims A by (1 - 2p) = 0.25 →
    // R 255*0.25 = 63 (near black).
    r = comp->renderFrame(snap, frame(5), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 63);

    // Frame 6: progress 0.5 → the dip bottoms out exactly at black.
    r = comp->renderFrame(snap, frame(6), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 0);

    // Frame 7: progress 0.625 → B lifts from black by (2p - 1) = 0.25 →
    // B 255*0.25 = 63.
    r = comp->renderFrame(snap, frame(7), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 63, 0, 0);

    // Frame 10 == span end (exclusive) → normal path renders pure B.
    r = comp->renderFrame(snap, frame(10), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 255, 0, 0);
}

TEST_F(CompositorTest, FadeThroughBlackStringParam) {
    // The "fade": "through_black" spelling is equivalent to throughBlack:true.
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Crossfade, TransitionAlignment::Left,
                          8, nlohmann::json{{"fade", "through_black"}})));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    auto r = comp->renderFrame(snap, frame(6), provider);  // progress 0.5
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 0);
}

TEST_F(CompositorTest, DisabledIncomingClipSkipsTransition) {
    // B disabled → the pair is ignored, so the outgoing clip renders normally
    // through the per-clip loop at its own (keyframed) opacity.
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Crossfade, TransitionAlignment::Left,
                          4)));
        EXPECT_TRUE(t.setClipEnabled("b", false));
        EXPECT_TRUE(t.setClipKeyframe("a", bl::KeyChannel::Opacity, frame(0), 0.5,
                                      bl::Interpolation::Linear));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    // At frame 8 (inside the transition span) only A renders, at 50% opacity:
    // R = 255*0.5 = 127, alpha = 255*0.5 = 127.
    auto r = comp->renderFrame(snap, frame(8), provider);
    ASSERT_TRUE(r.ok());
    expectPixel(*r, 0, 0, 0, 127, 127);
}

TEST_F(CompositorTest, TransitionRenderIsDeterministic) {
    // Rendering the same transitioned frame twice through the single shared
    // Compositor::renderFrame entry point (used by both preview and export)
    // must be byte-identical.
    Sequence seq = twoClipSequence(10, 10, 4, 1, [](VideoTrack& t) {
        EXPECT_TRUE(t.addTransition(
            "a", makeSpec(TransitionKind::Wipe, TransitionAlignment::Center, 4,
                          nlohmann::json{{"direction", "right"},
                                         {"softness", 1}})));
    });
    TimelineSnapshot snap(std::move(seq));
    auto comp = Compositor::create({4, 1});
    ASSERT_TRUE(comp.ok());

    StubDecodeProvider provider;
    provider.setColor("red.mp4", 255, 0, 0);
    provider.setColor("blue.mp4", 0, 0, 255);

    auto first = comp->renderFrame(snap, frame(10), provider);
    ASSERT_TRUE(first.ok());
    auto second = comp->renderFrame(snap, frame(10), provider);
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first->data, second->data);
}

} // namespace
