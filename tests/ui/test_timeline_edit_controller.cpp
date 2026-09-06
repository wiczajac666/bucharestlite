#include <bl_core/undo_stack.hpp>
#include <bl_timeline/sequence.hpp>

#include <panels/timeline_edit_controller.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace {

using bl::Clip;
using bl::ClipId;
using bl::Duration;
using bl::Rational;
using bl::Sequence;
using bl::Time;
using bl::Timeline;
using bl::UndoStack;
using bl::ui::TimelineEditController;

constexpr int kVideo0 = 0;
constexpr int kAudio0 = 1; // after the default 1 video track

const Rational kRate{1'000'000, 1};
const Rational kFps{24000, 1001};

Time frames(int64_t n) {
    return Time::fromFrameAt(n, kFps, kRate);
}

// The model sits on a microsecond tick grid while clip starts are derived
// from a 24000/1001 frame grid; exact Time equality is a rounding trap after
// ripple shuffles. Compare at the frame level instead.
int64_t frameOf(const Time& t) {
    return static_cast<int64_t>(
        std::llround(t.toSeconds() * static_cast<double>(kFps.num) /
                     static_cast<double>(kFps.den)));
}
int64_t frameOf(const Duration& d) {
    return static_cast<int64_t>(
        std::llround(d.toSeconds() * static_cast<double>(kFps.num) /
                     static_cast<double>(kFps.den)));
}
int64_t frameOf(int64_t plainFrames) { return plainFrames; }
#define EXPECT_TIME_EQ(a, b) EXPECT_EQ(frameOf(a), frameOf(b))
#define EXPECT_DUR_EQ(a, b) EXPECT_EQ(frameOf(a), frameOf(b))

Clip makeClip(const ClipId& id, int64_t startFrame, int64_t durFrames,
              int64_t sourceInFrame = 0) {
    Clip c;
    c.id = id;
    c.name = id;
    c.timelineStart = frames(startFrame);
    c.timelineDuration = Duration::fromFrames(durFrames, kFps);
    c.source.sourceIn = frames(sourceInFrame);
    c.source.sourceOut = c.source.sourceIn + c.timelineDuration;
    c.source.mediaItemId = id;
    return c;
}

struct Fixture {
    Sequence sequence;
    Timeline timeline;
    UndoStack undoStack;
    TimelineEditController editor;

    Fixture() {
        sequence.name = "Test";
        sequence.addVideoTrack("V1");
        sequence.addAudioTrack("A1");
        timeline = Timeline(sequence);
        editor.reset(timeline, undoStack);
    }

    const Clip* clip(int flat, const ClipId& id) const {
        const auto& track = flat < 1 ? timeline.sequence().videoTracks[flat]
                                     : timeline.sequence().audioTracks[flat - 1];
        for (const auto& c : track.clips()) {
            if (c.id == id) return &c;
        }
        return nullptr;
    }
};

} // namespace

TEST(TimelineEditController, moveClipIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    ASSERT_TRUE(f.editor.moveClip(kVideo0, kVideo0, "a", frames(30)));
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 30);
    EXPECT_TRUE(f.undoStack.canUndo());

    f.undoStack.undo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);

    f.undoStack.redo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 30);
}

TEST(TimelineEditController, moveClipRejectsOverlap) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    EXPECT_FALSE(f.editor.moveClip(kVideo0, kVideo0, "a", frames(15)));
    EXPECT_FALSE(f.undoStack.canUndo());
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);
}

TEST(TimelineEditController, moveClipAcrossTracksIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.moveClip(kVideo0, kAudio0, "a", frames(5)));
    EXPECT_TIME_EQ(f.clip(kAudio0, "a")->timelineStart, 5);
    EXPECT_EQ(f.clip(kVideo0, "a"), nullptr);

    f.undoStack.undo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);
    EXPECT_EQ(f.clip(kAudio0, "a"), nullptr);
}

TEST(TimelineEditController, groupMovePreservesOffsets) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    std::vector<TimelineEditController::MoveEntry> entries;
    entries.push_back({"a", kVideo0, kVideo0, frames(0), frames(60)});
    entries.push_back({"b", kVideo0, kVideo0, frames(20), frames(80)});
    ASSERT_TRUE(f.editor.groupMove(entries));

    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 60);
    EXPECT_TIME_EQ(f.clip(kVideo0, "b")->timelineStart, 80);

    f.undoStack.undo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);
    EXPECT_TIME_EQ(f.clip(kVideo0, "b")->timelineStart, 20);

    f.undoStack.redo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 60);
}

TEST(TimelineEditController, gapAwareTrimLeftIsUndoableAndBound) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    // Shrink "a" from its left edge.
    ASSERT_TRUE(f.editor.trimLeft(kVideo0, "a", frames(5), /*ripple=*/false));
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 5);
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->source.sourceIn, 5);

    // Growing back left is allowed within the open gap, but not past 0.
    EXPECT_TRUE(f.editor.validTrimLeft(f.timeline.sequence(), kVideo0, "a",
                                       frames(2), false));
    ASSERT_TRUE(f.editor.trimLeft(kVideo0, "a", frames(2), /*ripple=*/false));
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 2);

    // Reaching the end boundary is invalid.
    EXPECT_FALSE(f.editor.validTrimLeft(f.timeline.sequence(), kVideo0, "a",
                                        frames(30), false));

    f.undoStack.undo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 5);
    f.undoStack.undo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->source.sourceIn, 0);
}

TEST(TimelineEditController, rippleTrimLeftShiftsEarlierClips) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    // Shrink "b" from the left: it grows toward its end; earlier clips ripple.
    ASSERT_TRUE(f.editor.trimLeft(kVideo0, "b", frames(25), /*ripple=*/true));
    EXPECT_TIME_EQ(f.clip(kVideo0, "b")->timelineStart, 25);
    EXPECT_DUR_EQ(f.clip(kVideo0, "b")->timelineDuration, 5);
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 5);

    f.undoStack.undo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "b")->timelineStart, 20);
    EXPECT_DUR_EQ(f.clip(kVideo0, "b")->timelineDuration, 10);
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);
}

TEST(TimelineEditController, gapAwareTrimRightIsUndoableAndBound) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    // Gap-aware growth must not gore into the next clip.
    EXPECT_FALSE(f.editor.trimRight(kVideo0, "a", frames(25), /*ripple=*/false));

    ASSERT_TRUE(f.editor.trimRight(kVideo0, "a", frames(15), /*ripple=*/false));
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart +
                       f.clip(kVideo0, "a")->timelineDuration,
                   15);

    f.undoStack.undo();
    EXPECT_DUR_EQ(f.clip(kVideo0, "a")->timelineDuration, 10);
}

TEST(TimelineEditController, rippleTrimRightShiftsLaterClips) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("c", 40, 10)));

    // Extend "b" right: it grows into the gap; "c" ripples along by 5.
    ASSERT_TRUE(f.editor.trimRight(kVideo0, "b", frames(35), /*ripple=*/true));
    EXPECT_TIME_EQ(f.clip(kVideo0, "b")->timelineStart +
                       f.clip(kVideo0, "b")->timelineDuration,
                   35);
    EXPECT_TIME_EQ(f.clip(kVideo0, "c")->timelineStart, 45);

    f.undoStack.undo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "c")->timelineStart, 40);
    EXPECT_DUR_EQ(f.clip(kVideo0, "b")->timelineDuration, 10);
}

TEST(TimelineEditController, splitClipIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 20)));

    ASSERT_TRUE(f.editor.splitClip(kVideo0, "a", frames(10)));
    const auto& track = f.timeline.sequence().videoTracks[0].clips();
    ASSERT_EQ(track.size(), 2u);
    EXPECT_EQ(track[0].id, "a");
    EXPECT_TIME_EQ(track[0].timelineStart, 0);
    EXPECT_TIME_EQ(track[1].timelineStart, 10);

    f.undoStack.undo();
    ASSERT_EQ(f.timeline.sequence().videoTracks[0].clips().size(), 1u);
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);
    EXPECT_DUR_EQ(f.clip(kVideo0, "a")->timelineDuration, 20);

    f.undoStack.redo();
    ASSERT_EQ(f.timeline.sequence().videoTracks[0].clips().size(), 2u);
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);
}

TEST(TimelineEditController, splitAtEdgesRejected) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 20)));
    EXPECT_FALSE(f.editor.splitClip(kVideo0, "a", frames(0)));
    EXPECT_FALSE(f.editor.splitClip(kVideo0, "a", frames(20)));
    EXPECT_FALSE(f.editor.splitClip(kVideo0, "a", frames(25)));
}

TEST(TimelineEditController, deleteClipGapStays) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    ASSERT_TRUE(f.editor.removeClip(kVideo0, "a", /*ripple=*/false));
    EXPECT_EQ(f.clip(kVideo0, "a"), nullptr);
    EXPECT_TIME_EQ(f.clip(kVideo0, "b")->timelineStart, 20);

    f.undoStack.undo();
    ASSERT_NE(f.clip(kVideo0, "a"), nullptr);
    EXPECT_TIME_EQ(f.clip(kVideo0, "b")->timelineStart, 20);
}

TEST(TimelineEditController, rippleDeleteClosesGapAndUndoes) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("c", 40, 10)));

    ASSERT_TRUE(f.editor.removeClip(kVideo0, "b", /*ripple=*/true));
    EXPECT_EQ(f.clip(kVideo0, "b"), nullptr);
    EXPECT_TIME_EQ(f.clip(kVideo0, "c")->timelineStart, 30);
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->timelineStart, 0);

    f.undoStack.undo();
    EXPECT_NE(f.clip(kVideo0, "b"), nullptr);
    EXPECT_TIME_EQ(f.clip(kVideo0, "b")->timelineStart, 20);
    EXPECT_TIME_EQ(f.clip(kVideo0, "c")->timelineStart, 40);

    f.undoStack.redo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "c")->timelineStart, 30);
}

TEST(TimelineEditController, snappingSticksToFrameAndEdges) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 10, 10)));

    const Sequence& seq = f.timeline.sequence();
    // Mid-frame snap: rounds to the nearer integer frame boundary.
    const Time halfway = Time::fromSeconds(frames(5).toSeconds() + 0.00025, kRate);
    EXPECT_TIME_EQ(f.editor.snapToFrame(seq, halfway), 5);

    // A point just before clip "a"'s start sticks to the clip edge.
    const Time justBefore =
        Time::fromSeconds(frames(10).toSeconds() - 0.0001, kRate);
    EXPECT_TIME_EQ(f.editor.snapToNearest(seq, justBefore, 2), 10);
    // ... but three frames away stays snapped to the frame grid alone.
    const Time further = frames(7);
    EXPECT_TIME_EQ(f.editor.snapToNearest(seq, further, 2), 7);
}