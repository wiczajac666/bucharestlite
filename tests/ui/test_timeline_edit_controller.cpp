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
// Same as makeClip but with an explicit start time — clip ends accumulate
// frame rounding (end = start + duration), so an absolute frames(n) start can
// land 1µs before the previous clip's end. Layouts built for exact adjacency
// must chain off the previous end.
Clip makeClipAt(const ClipId& id, const Time& start, int64_t durFrames,
                int64_t sourceInFrame = 0) {
    Clip c = makeClip(id, 0, durFrames, sourceInFrame);
    c.timelineStart = start;
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

TEST(TimelineEditController, addClipGeneratesIdAndIsUndoable) {
    Fixture f;
    Clip c = makeClip("", 10, 20);
    ASSERT_TRUE(f.editor.addClip(kVideo0, c));

    const Clip* placed = nullptr;
    for (const auto& clip : f.timeline.sequence().videoTracks[0].clips()) {
        if (clip.source.mediaItemId == c.source.mediaItemId) placed = &clip;
    }
    ASSERT_NE(placed, nullptr);
    EXPECT_FALSE(placed->id.empty());
    EXPECT_TIME_EQ(placed->timelineStart, 10);
    EXPECT_DUR_EQ(placed->timelineDuration, 20);
    EXPECT_TRUE(f.undoStack.canUndo());
    EXPECT_EQ(f.undoStack.undoText(), "Add clip");

    const ClipId generatedId = placed->id;
    f.undoStack.undo();
    EXPECT_EQ(f.clip(kVideo0, generatedId), nullptr);

    f.undoStack.redo();
    const Clip* restored = f.clip(kVideo0, generatedId);
    ASSERT_NE(restored, nullptr);
    EXPECT_TIME_EQ(restored->timelineStart, 10);
}

TEST(TimelineEditController, addClipKeepsExplicitId) {
    Fixture f;
    Clip c = makeClip("explicit", 0, 10);
    ASSERT_TRUE(f.editor.addClip(kVideo0, c));
    EXPECT_NE(f.clip(kVideo0, "explicit"), nullptr);
}

TEST(TimelineEditController, addClipRejectsOverlap) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    Clip c = makeClip("b", 5, 10);
    EXPECT_FALSE(f.editor.addClip(kVideo0, c));
    EXPECT_FALSE(f.undoStack.canUndo());
    EXPECT_EQ(f.clip(kVideo0, "b"), nullptr);
}

TEST(TimelineEditController, addClipOnAudioTrack) {
    Fixture f;
    Clip c = makeClip("a", 5, 8);
    c.source.mediaItemId = "audio-source";
    ASSERT_TRUE(f.editor.addClip(kAudio0, c));
    EXPECT_NE(f.clip(kAudio0, "a"), nullptr);

    f.undoStack.undo();
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

// ---------------------------------------------------------------------------
// P1: Kdenlive-parity controller primitives — lock enforcement, refusal
// reporting, slip/roll/groups/A-V split/markers/track ops/zone edits.
// ---------------------------------------------------------------------------

TEST(TimelineEditControllerLock, LockedTrackRefusesLayoutEdits) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));
    f.timeline.sequence().videoTracks[0].setLocked(true);

    const auto expectLocked = [&](bool result) {
        EXPECT_FALSE(result);
        EXPECT_EQ(f.editor.lastError(), "Track is locked");
    };

    expectLocked(f.editor.addClip(kVideo0, makeClip("c", 40, 5)));
    expectLocked(f.editor.moveClip(kVideo0, kVideo0, "a", frames(35)));
    expectLocked(f.editor.groupMove(
        {{"a", kVideo0, kVideo0, frames(0), frames(35)}}));
    expectLocked(f.editor.trimLeft(kVideo0, "a", frames(2), false));
    expectLocked(f.editor.trimRight(kVideo0, "a", frames(8), false));
    expectLocked(f.editor.splitClip(kVideo0, "a", frames(5)));
    expectLocked(f.editor.removeClip(kVideo0, "a", false));
    expectLocked(f.editor.setClipEnabled(kVideo0, "a", false));
    expectLocked(
        f.editor.slipClip(kVideo0, "a", Duration::fromFrames(1, kFps)));
    expectLocked(f.editor.rollTrim(kVideo0, "a", frames(15)));
    expectLocked(f.editor.splitAudioVideo(kVideo0, "a", kAudio0));
    expectLocked(f.editor.removeTrack(true, 0, true));

    // Refusals never push commands.
    EXPECT_FALSE(f.undoStack.canUndo());
}

TEST(TimelineEditControllerLock,
     GroupingMarkersAndTrackStructuresAreNotBlocked) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));
    f.timeline.sequence().videoTracks[0].setLocked(true);

    // Grouping moves nothing, so the lock does not apply.
    ASSERT_TRUE(f.editor.groupClips({{kVideo0, "a"}, {kVideo0, "b"}}));
    EXPECT_EQ(f.timeline.sequence().groupMembers("a").size(), 1u);
    ASSERT_TRUE(f.editor.ungroupClips(kVideo0, "a"));
    EXPECT_TRUE(f.timeline.sequence().groups.empty());

    // Markers and track-structure edits are sequence-level too.
    ASSERT_TRUE(f.editor.addTimelineMarker(bl::Marker(frames(10), "m", 1)));
    ASSERT_TRUE(f.editor.insertTrack(true, 1, "V2"));
    EXPECT_EQ(f.timeline.sequence().videoTracks.size(), 2u);
}

TEST(TimelineEditControllerErrors, RefusalSetsErrorAndSuccessClearsIt) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    EXPECT_FALSE(f.editor.moveClip(kVideo0, kVideo0, "missing", frames(5)));
    EXPECT_EQ(f.editor.lastError(), "Clip not found");

    EXPECT_FALSE(f.editor.moveClip(kVideo0, kVideo0, "a", frames(20)));
    EXPECT_EQ(f.editor.lastError(), "That spot is occupied");

    EXPECT_FALSE(f.editor.moveClip(kVideo0, kVideo0, "a", frames(0)));
    EXPECT_EQ(f.editor.lastError(), "No change");

    // A successful mutator leaves the error channel empty.
    ASSERT_TRUE(f.editor.moveClip(kVideo0, kVideo0, "a", frames(9)));
    EXPECT_TRUE(f.editor.lastError().empty());
    EXPECT_TRUE(f.undoStack.canUndo());
}

TEST(TimelineEditControllerEnable, ToggleDisableClipAndUndo) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.setClipEnabled(kVideo0, "a", false));
    EXPECT_FALSE(f.clip(kVideo0, "a")->enabled);

    // Toggling to the same value is a no-op refusal.
    EXPECT_FALSE(f.editor.setClipEnabled(kVideo0, "a", false));
    EXPECT_EQ(f.editor.lastError(), "No change");

    f.undoStack.undo();
    EXPECT_TRUE(f.clip(kVideo0, "a")->enabled);
    f.undoStack.redo();
    EXPECT_FALSE(f.clip(kVideo0, "a")->enabled);
}

TEST(TimelineEditControllerSlip, ShiftsSourceWindowOnly) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.slipClip(kVideo0, "a", Duration::fromFrames(3, kFps)));
    const Clip* c = f.clip(kVideo0, "a");
    ASSERT_NE(c, nullptr);
    EXPECT_TIME_EQ(c->source.sourceIn, 3);
    EXPECT_TIME_EQ(c->source.sourceOut, 13);
    // Timeline geometry is untouched.
    EXPECT_TIME_EQ(c->timelineStart, 0);
    EXPECT_DUR_EQ(c->timelineDuration, 10);

    f.undoStack.undo();
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->source.sourceIn, 0);
    EXPECT_TIME_EQ(f.clip(kVideo0, "a")->source.sourceOut, 10);
}

TEST(TimelineEditControllerSlip, RefusesToLeaveSourceRange) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    // Before the source head.
    EXPECT_FALSE(
        f.editor.slipClip(kVideo0, "a", Duration::fromFrames(-1, kFps)));
    EXPECT_EQ(f.editor.lastError(), "Slip exceeds the start of the source");

    // No delta at all.
    EXPECT_FALSE(f.editor.slipClip(kVideo0, "a", Duration{}));
    EXPECT_EQ(f.editor.lastError(), "No change");

    // With a known media length, past-the-end slips are refused too. (The
    // same slip succeeds without a provider: bounds degrade gracefully.)
    f.editor.setMediaDurationProvider(
        [](const std::string& id) -> std::optional<Duration> {
            if (id == "a") return Duration::fromFrames(10, kFps);
            return std::nullopt;
        });
    EXPECT_FALSE(
        f.editor.slipClip(kVideo0, "a", Duration::fromFrames(2, kFps)));
    EXPECT_EQ(f.editor.lastError(), "Slip exceeds the end of the source");

    // None of the refusals pushed a command.
    EXPECT_FALSE(f.undoStack.canUndo());
}

TEST(TimelineEditControllerRoll, SeamMovesLeftAcrossBothClips) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 10, 10, 5)));

    ASSERT_TRUE(f.editor.rollTrim(kVideo0, "a", frames(7)));
    const Clip* a = f.clip(kVideo0, "a");
    const Clip* b = f.clip(kVideo0, "b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_DUR_EQ(a->timelineDuration, 7);
    EXPECT_TIME_EQ(a->source.sourceOut, 7); // left window shrinks with seam
    EXPECT_TIME_EQ(b->timelineStart, 7);
    EXPECT_DUR_EQ(b->timelineDuration, 13); // right grows by the same amount
    EXPECT_TIME_EQ(b->source.sourceIn, 2);  // 5 - 3

    f.undoStack.undo();
    a = f.clip(kVideo0, "a");
    b = f.clip(kVideo0, "b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_DUR_EQ(a->timelineDuration, 10);
    EXPECT_TIME_EQ(a->source.sourceOut, 10);
    EXPECT_TIME_EQ(b->timelineStart, 10);
    EXPECT_DUR_EQ(b->timelineDuration, 10);
    EXPECT_TIME_EQ(b->source.sourceIn, 5);
}

TEST(TimelineEditControllerRoll, SeamMovesRightAcrossBothClips) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 10, 10, 5)));

    ASSERT_TRUE(f.editor.rollTrim(kVideo0, "a", frames(13)));
    const Clip* a = f.clip(kVideo0, "a");
    const Clip* b = f.clip(kVideo0, "b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_DUR_EQ(a->timelineDuration, 13);
    EXPECT_TIME_EQ(a->source.sourceOut, 13);
    EXPECT_TIME_EQ(b->timelineStart, 13);
    EXPECT_DUR_EQ(b->timelineDuration, 7);
    EXPECT_TIME_EQ(b->source.sourceIn, 8); // 5 + 3
}

TEST(TimelineEditControllerRoll, Refusals) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 10, 10, 5)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("c", 40, 10)));

    EXPECT_FALSE(f.editor.rollTrim(kVideo0, "nope", frames(5)));
    EXPECT_EQ(f.editor.lastError(), "Clip not found");

    EXPECT_FALSE(f.editor.rollTrim(kVideo0, "c", frames(45)));
    EXPECT_EQ(f.editor.lastError(),
              "There is no clip to the right of this one");

    // b and c are separated by a gap.
    EXPECT_FALSE(f.editor.rollTrim(kVideo0, "b", frames(15)));
    EXPECT_EQ(f.editor.lastError(), "Clips are not adjacent");

    EXPECT_FALSE(f.editor.rollTrim(kVideo0, "a", frames(10)));
    EXPECT_EQ(f.editor.lastError(), "No change");

    EXPECT_FALSE(f.editor.rollTrim(kVideo0, "a", frames(0)));
    EXPECT_EQ(f.editor.lastError(), "Roll trim would delete a clip");
    // Seam exactly on b's end (chain-computed: frames(n) drifts 1µs from it).
    const Clip* rb = f.clip(kVideo0, "b");
    ASSERT_NE(rb, nullptr);
    EXPECT_FALSE(f.editor.rollTrim(
        kVideo0, "a", rb->timelineStart + rb->timelineDuration));
    EXPECT_EQ(f.editor.lastError(), "Roll trim would delete a clip");

    // Seam to 1 would pull b's source window to tick -4.
    EXPECT_FALSE(f.editor.rollTrim(kVideo0, "a", frames(1)));
    EXPECT_EQ(f.editor.lastError(),
              "Roll trim exceeds the start of the source");

    // Known media length: seam to 13 would push a's source-out past 10 s.
    f.editor.setMediaDurationProvider(
        [](const std::string& id) -> std::optional<Duration> {
            if (id == "a") return Duration::fromFrames(10, kFps);
            return std::nullopt;
        });
    EXPECT_FALSE(f.editor.rollTrim(kVideo0, "a", frames(13)));
    EXPECT_EQ(f.editor.lastError(),
              "Roll trim exceeds the end of the left clip's source");

    EXPECT_FALSE(f.undoStack.canUndo());
}

TEST(TimelineEditControllerGroups, GroupsAcrossTracksAndUndo) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));
    ASSERT_TRUE(
        f.timeline.addClipToAudioTrack(0, makeClip("aud", 0, 10)));

    ASSERT_TRUE(
        f.editor.groupClips({{kVideo0, "a"}, {kVideo0, "b"}, {kAudio0, "aud"}}));
    EXPECT_EQ(f.timeline.sequence().groupMembers("a").size(), 2u);
    EXPECT_EQ(f.timeline.sequence().groupMembers("aud").size(), 2u);

    f.undoStack.undo();
    EXPECT_TRUE(f.timeline.sequence().groups.empty());
    f.undoStack.redo();
    EXPECT_EQ(f.timeline.sequence().groupMembers("a").size(), 2u);

    ASSERT_TRUE(f.editor.ungroupClips(kVideo0, "b"));
    EXPECT_TRUE(f.timeline.sequence().groups.empty());
    f.undoStack.undo();
    EXPECT_EQ(f.timeline.sequence().groupMembers("a").size(), 2u);
}

TEST(TimelineEditControllerGroups, Refusals) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 20, 10)));

    EXPECT_FALSE(f.editor.groupClips({{kVideo0, "a"}}));
    EXPECT_EQ(f.editor.lastError(), "Select at least two clips");

    EXPECT_FALSE(f.editor.groupClips({{kVideo0, "a"}, {kVideo0, "missing"}}));
    EXPECT_EQ(f.editor.lastError(), "Clip not found");

    EXPECT_FALSE(f.editor.ungroupClips(kVideo0, "a"));
    EXPECT_EQ(f.editor.lastError(), "Clip is not in a group");

    ASSERT_TRUE(f.editor.groupClips({{kVideo0, "a"}, {kVideo0, "b"}}));
    EXPECT_FALSE(f.editor.groupClips({{kVideo0, "a"}, {kVideo0, "b"}}));
    EXPECT_EQ(f.editor.lastError(), "Clips are already grouped");
}

TEST(TimelineEditControllerSplitAV, SplitsLinksAndUndoes) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.splitAudioVideo(kVideo0, "a", kAudio0));
    const Clip* video = f.clip(kVideo0, "a");
    ASSERT_NE(video, nullptr);
    EXPECT_TRUE(video->hasVideo);
    EXPECT_FALSE(video->hasAudio);

    const auto& audioClips = f.timeline.sequence().audioTracks[0].clips();
    ASSERT_EQ(audioClips.size(), 1u);
    const Clip& audio = audioClips[0];
    EXPECT_TRUE(audio.hasAudio);
    EXPECT_FALSE(audio.hasVideo);
    EXPECT_TIME_EQ(audio.timelineStart, 0);
    EXPECT_DUR_EQ(audio.timelineDuration, 10);
    EXPECT_TIME_EQ(audio.source.sourceIn, 0);

    // Both halves are linked so selection/expansion can follow them.
    const auto members = f.timeline.sequence().groupMembers("a");
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(members[0], audio.id);

    f.undoStack.undo();
    EXPECT_TRUE(f.clip(kVideo0, "a")->hasAudio);
    EXPECT_TRUE(f.timeline.sequence().audioTracks[0].clips().empty());
    EXPECT_TRUE(f.timeline.sequence().groups.empty());
}

TEST(TimelineEditControllerSplitAV, Refusals) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(
        f.timeline.addClipToAudioTrack(0, makeClip("aud", 0, 10)));

    EXPECT_FALSE(f.editor.splitAudioVideo(kAudio0, "aud", kVideo0));
    EXPECT_EQ(f.editor.lastError(), "Not a video track");

    EXPECT_FALSE(f.editor.splitAudioVideo(kVideo0, "a", kVideo0));
    EXPECT_EQ(f.editor.lastError(), "Not an audio track");

    // The target audio lane is occupied over the clip's range.
    EXPECT_FALSE(f.editor.splitAudioVideo(kVideo0, "a", kAudio0));
    EXPECT_EQ(f.editor.lastError(), "That spot is occupied");

    EXPECT_FALSE(f.editor.splitAudioVideo(kVideo0, "nope", kAudio0));
    EXPECT_EQ(f.editor.lastError(), "Clip not found");

    // An already-split half has no audio left to split off.
    Fixture g;
    ASSERT_TRUE(g.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(g.editor.splitAudioVideo(kVideo0, "a", kAudio0));
    EXPECT_FALSE(g.editor.splitAudioVideo(kVideo0, "a", kAudio0));
    EXPECT_EQ(g.editor.lastError(), "Clip has no audio to split off");
    EXPECT_EQ(g.timeline.sequence().audioTracks[0].clips().size(), 1u);
}

TEST(TimelineEditControllerMarkers, AddEditRemoveWithUndo) {
    Fixture f;
    ASSERT_TRUE(f.editor.addTimelineMarker(bl::Marker(frames(10), "m", 1)));
    ASSERT_EQ(f.timeline.sequence().markers.size(), 1u);

    EXPECT_FALSE(f.editor.addTimelineMarker(bl::Marker(frames(10), "dup", 2)));
    EXPECT_EQ(f.editor.lastError(), "A marker already exists there");

    EXPECT_FALSE(f.editor.editTimelineMarker(
        frames(99), bl::Marker(frames(99), "x", 0)));
    EXPECT_EQ(f.editor.lastError(), "No marker at that position");

    // Move to a free position and rename in one command.
    ASSERT_TRUE(f.editor.editTimelineMarker(
        frames(10), bl::Marker(frames(20), "moved", 3)));
    EXPECT_TIME_EQ(f.timeline.sequence().markers[0].position, 20);
    EXPECT_EQ(f.timeline.sequence().markers[0].label, "moved");

    ASSERT_TRUE(f.editor.addTimelineMarker(bl::Marker(frames(30), "other", 0)));
    EXPECT_FALSE(f.editor.editTimelineMarker(
        frames(20), bl::Marker(frames(30), "clash", 0)));
    EXPECT_EQ(f.editor.lastError(), "A marker already exists there");

    ASSERT_TRUE(f.editor.removeTimelineMarker(frames(20)));
    ASSERT_EQ(f.timeline.sequence().markers.size(), 1u);
    EXPECT_FALSE(f.editor.removeTimelineMarker(frames(20)));
    EXPECT_EQ(f.editor.lastError(), "No marker at that position");

    // Undoing the removal brings the marker back.
    f.undoStack.undo();
    ASSERT_EQ(f.timeline.sequence().markers.size(), 2u);
    EXPECT_EQ(f.timeline.sequence().markers[0].label, "moved");
}

TEST(TimelineEditControllerTracks, InsertMoveRemoveAndUndo) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.insertTrack(true, 1, "V2"));
    ASSERT_EQ(f.timeline.sequence().videoTracks.size(), 2u);
    EXPECT_EQ(f.timeline.sequence().videoTracks[1].name(), "V2");

    ASSERT_TRUE(f.editor.moveTrack(true, 1, 0));
    EXPECT_EQ(f.timeline.sequence().videoTracks[0].name(), "V2");
    EXPECT_EQ(f.timeline.sequence().videoTracks[1].name(), "V1");

    // V1 (now at index 1) holds a clip: force is required.
    EXPECT_FALSE(f.editor.removeTrack(true, 1, false));
    EXPECT_EQ(f.editor.lastError(), "Track still has clips");
    ASSERT_TRUE(f.editor.removeTrack(true, 1, true));
    EXPECT_EQ(f.timeline.sequence().videoTracks.size(), 1u);

    // The last video track always survives...
    EXPECT_FALSE(f.editor.removeTrack(true, 0, true));
    EXPECT_EQ(f.editor.lastError(), "Cannot remove the last video track");
    // ...and out-of-range/no-op moves refuse too.
    EXPECT_FALSE(f.editor.removeTrack(true, 7, true));
    EXPECT_EQ(f.editor.lastError(), "No such track");
    EXPECT_FALSE(f.editor.moveTrack(true, 0, 0));
    EXPECT_EQ(f.editor.lastError(), "No change");

    // Audio wrapper works the same way.
    ASSERT_TRUE(f.editor.insertTrack(false, 1, "A2"));
    EXPECT_EQ(f.timeline.sequence().audioTracks.size(), 2u);
    ASSERT_TRUE(f.editor.moveTrack(false, 1, 0));
    EXPECT_EQ(f.timeline.sequence().audioTracks[0].name(), "A2");

    // Undoing the removals restores V1 with its clip, in order (three
    // commands: video insert, video move, video remove — plus audio insert
    // and audio move on top).
    f.undoStack.undo(); // audio move
    f.undoStack.undo(); // audio insert
    f.undoStack.undo(); // video remove
    ASSERT_EQ(f.timeline.sequence().videoTracks.size(), 2u);
    EXPECT_EQ(f.timeline.sequence().videoTracks[1].name(), "V1");
    ASSERT_EQ(f.timeline.sequence().videoTracks[1].clips().size(), 1u);
    EXPECT_EQ(f.timeline.sequence().videoTracks[1].clips()[0].id, "a");
}

TEST(TimelineEditControllerZones, LiftClearsRangeAndKeepsOutside) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 10, 10)));
    ASSERT_TRUE(
        f.timeline.addClipToAudioTrack(0, makeClip("aud", 0, 20)));

    // Empty zone refuses outright.
    EXPECT_FALSE(f.editor.liftZone(frames(5), Duration{}));
    EXPECT_EQ(f.editor.lastError(), "Empty zone");

    // Nothing in [50, 60): lift refuses without pushing a command.
    EXPECT_FALSE(
        f.editor.liftZone(frames(50), Duration::fromFrames(10, kFps)));
    EXPECT_EQ(f.editor.lastError(), "Nothing to lift");
    EXPECT_FALSE(f.undoStack.canUndo());

    // Zone [5, 15) cuts through both video clips and the audio clip.
    ASSERT_TRUE(
        f.editor.liftZone(frames(5), Duration::fromFrames(10, kFps)));

    const auto& vids = f.timeline.sequence().videoTracks[0].clips();
    ASSERT_EQ(vids.size(), 2u);
    EXPECT_TIME_EQ(vids[0].timelineStart, 0);
    EXPECT_DUR_EQ(vids[0].timelineDuration, 5); // a: head kept
    EXPECT_TIME_EQ(vids[1].timelineStart, 15);
    EXPECT_DUR_EQ(vids[1].timelineDuration, 5); // b: tail starts at zone end
    EXPECT_TIME_EQ(vids[1].source.sourceIn, 5); // source advanced by 5

    const auto& auds = f.timeline.sequence().audioTracks[0].clips();
    ASSERT_EQ(auds.size(), 2u);
    EXPECT_DUR_EQ(auds[0].timelineDuration, 5); // aud head [0, 5)
    EXPECT_TIME_EQ(auds[1].timelineStart, 15);
    EXPECT_DUR_EQ(auds[1].timelineDuration, 5);
    EXPECT_TIME_EQ(auds[1].source.sourceIn, 15);

    f.undoStack.undo();
    const auto& vidsAfter = f.timeline.sequence().videoTracks[0].clips();
    ASSERT_EQ(vidsAfter.size(), 2u);
    EXPECT_DUR_EQ(vidsAfter[0].timelineDuration, 10);
    EXPECT_TIME_EQ(vidsAfter[1].timelineStart, 10);
    EXPECT_EQ(f.timeline.sequence().audioTracks[0].clips().size(), 1u);
    EXPECT_TIME_EQ(f.timeline.sequence().audioTracks[0].clips()[0]
                       .timelineDuration,
                   20);
}

TEST(TimelineEditControllerZones, ExtractClosesGapAndSkipsLockedTracks) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("b", 10, 10)));
    // Chain off b's end: an absolute frames(20) start would drift 1µs into b.
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(
        0, makeClipAt("c", frames(10) + Duration::fromFrames(10, kFps), 10)));
    ASSERT_TRUE(
        f.timeline.addClipToAudioTrack(0, makeClip("aud", 0, 30)));
    f.timeline.sequence().audioTracks[0].setLocked(true);

    // Empty zone refuses cleanly.
    EXPECT_FALSE(
        f.editor.extractZone(frames(60), Duration::fromFrames(5, kFps)));
    EXPECT_EQ(f.editor.lastError(), "Nothing to extract");

    // Zone [5, 15): lift on unlocked tracks, then close the gap everywhere
    // unlocked. The locked audio track stays byte-for-byte intact.
    ASSERT_TRUE(
        f.editor.extractZone(frames(5), Duration::fromFrames(10, kFps)));

    const auto& vids = f.timeline.sequence().videoTracks[0].clips();
    ASSERT_EQ(vids.size(), 3u);
    EXPECT_TIME_EQ(vids[0].timelineStart, 0);
    EXPECT_DUR_EQ(vids[0].timelineDuration, 5);  // a head
    EXPECT_TIME_EQ(vids[1].timelineStart, 5);    // b tail rippled into gap
    EXPECT_DUR_EQ(vids[1].timelineDuration, 5);
    EXPECT_TIME_EQ(vids[2].timelineStart, 10);   // c shifted left by the zone
    EXPECT_DUR_EQ(vids[2].timelineDuration, 10);

    const auto& auds = f.timeline.sequence().audioTracks[0].clips();
    ASSERT_EQ(auds.size(), 1u);
    EXPECT_TIME_EQ(auds[0].timelineStart, 0);
    EXPECT_DUR_EQ(auds[0].timelineDuration, 30); // locked: untouched

    f.undoStack.undo();
    const auto& vidsAfter = f.timeline.sequence().videoTracks[0].clips();
    ASSERT_EQ(vidsAfter.size(), 3u);
    EXPECT_DUR_EQ(vidsAfter[0].timelineDuration, 10);
    EXPECT_TIME_EQ(vidsAfter[1].timelineStart, 10);
    EXPECT_TIME_EQ(vidsAfter[2].timelineStart, 20);
}

TEST(TimelineEditControllerSnap, ConsidersMarkerPositions) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 10, 10)));
    ASSERT_TRUE(f.editor.addTimelineMarker(bl::Marker(frames(30), "cue", 0)));

    const Sequence& seq = f.timeline.sequence();
    // One frame before the marker: within reach of maxDistance=4.
    EXPECT_TIME_EQ(f.editor.snapToNearest(seq, frames(31), 4), 30);
    // Three frames away with maxDistance=2: falls back to the frame grid.
    EXPECT_TIME_EQ(f.editor.snapToNearest(seq, frames(33), 2), 33);
}