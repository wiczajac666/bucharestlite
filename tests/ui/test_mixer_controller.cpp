#include <bl_core/undo_stack.hpp>
#include <bl_timeline/sequence.hpp>

#include <panels/mixer_controller.hpp>

#include <gtest/gtest.h>

namespace {

using bl::Clip;
using bl::Sequence;
using bl::Timeline;
using bl::UndoStack;
using bl::ui::MixerController;
using bl::VideoTrack;

constexpr int kVideo0 = 0;
constexpr int kAudio0 = 1; // after the default 1 video track

struct Fixture {
    Sequence sequence;
    Timeline timeline;
    UndoStack undoStack;
    MixerController mixer;

    Fixture() {
        sequence.name = "Mix Test";
        sequence.addVideoTrack("V1");
        sequence.addAudioTrack("A1");
        timeline = Timeline(sequence);
        mixer.reset(timeline, undoStack);
    }
};

TEST(MixerController, TrackGainIsUndoable) {
    Fixture f;
    ASSERT_DOUBLE_EQ(f.timeline.sequence().videoTracks[0].gain(), 1.0);

    ASSERT_TRUE(f.mixer.setTrackGain(kVideo0, 0.5));
    EXPECT_DOUBLE_EQ(f.timeline.sequence().videoTracks[0].gain(), 0.5);

    // Same value is a no-op: nothing pushed.
    EXPECT_FALSE(f.mixer.setTrackGain(kVideo0, 0.5));

    f.undoStack.undo();
    EXPECT_DOUBLE_EQ(f.timeline.sequence().videoTracks[0].gain(), 1.0);
    f.undoStack.redo();
    EXPECT_DOUBLE_EQ(f.timeline.sequence().videoTracks[0].gain(), 0.5);
}

TEST(MixerController, AudioTrackGainAndPanAreUndoable) {
    Fixture f;
    ASSERT_TRUE(f.mixer.setTrackGain(kAudio0, 0.25));
    ASSERT_TRUE(f.mixer.setTrackPan(kAudio0, -0.5));

    VideoTrack& a = f.timeline.sequence().audioTracks[0];
    EXPECT_DOUBLE_EQ(a.gain(), 0.25);
    EXPECT_DOUBLE_EQ(a.pan(), -0.5);

    f.undoStack.undo();
    EXPECT_DOUBLE_EQ(a.pan(), 0.0);
    f.undoStack.undo();
    EXPECT_DOUBLE_EQ(a.gain(), 1.0);
}

TEST(MixerController, MuteAndSoloAreUndoable) {
    Fixture f;
    ASSERT_TRUE(f.mixer.setTrackMuted(kAudio0, true));
    ASSERT_TRUE(f.mixer.setTrackSoloed(kAudio0, true));

    VideoTrack& a = f.timeline.sequence().audioTracks[0];
    EXPECT_TRUE(a.muted());
    EXPECT_TRUE(a.soloed());

    f.undoStack.undo();
    EXPECT_FALSE(a.soloed());
    f.undoStack.undo();
    EXPECT_FALSE(a.muted());
}

TEST(MixerController, LockedIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.mixer.setTrackLocked(kAudio0, true));
    EXPECT_TRUE(f.timeline.sequence().audioTracks[0].locked());

    f.undoStack.undo();
    EXPECT_FALSE(f.timeline.sequence().audioTracks[0].locked());
}

TEST(MixerController, RejectsOutOfBoundsTrack) {
    Fixture f;
    EXPECT_FALSE(f.mixer.setTrackGain(99, 0.5));
    EXPECT_FALSE(f.mixer.setTrackGain(-1, 0.5));
    EXPECT_FALSE(f.mixer.setTrackMuted(99, true));
    EXPECT_EQ(f.undoStack.canUndo(), false);
    // Flat addressing spans video then audio: index 2 is out of bounds here.
    EXPECT_FALSE(f.mixer.setTrackPan(2, 0.1));
}

TEST(MixerController, MasterGainAndPanAreUndoable) {
    Fixture f;
    ASSERT_DOUBLE_EQ(f.timeline.sequence().settings.masterGain, 1.0);
    ASSERT_DOUBLE_EQ(f.timeline.sequence().settings.masterPan, 0.0);

    ASSERT_TRUE(f.mixer.setMasterGain(0.7));
    ASSERT_TRUE(f.mixer.setMasterPan(0.2));
    EXPECT_DOUBLE_EQ(f.timeline.sequence().settings.masterGain, 0.7);
    EXPECT_DOUBLE_EQ(f.timeline.sequence().settings.masterPan, 0.2);

    EXPECT_FALSE(f.mixer.setMasterGain(0.7)); // no-op, nothing pushed

    f.undoStack.undo();
    EXPECT_DOUBLE_EQ(f.timeline.sequence().settings.masterPan, 0.0);
    f.undoStack.undo();
    EXPECT_DOUBLE_EQ(f.timeline.sequence().settings.masterGain, 1.0);
}

TEST(MixerController, ResetSwitchesDocument) {
    Fixture a;
    Sequence otherSeq;
    otherSeq.name = "Other";
    otherSeq.addVideoTrack("V1");
    otherSeq.addAudioTrack("A1");
    otherSeq.audioTracks[0].setGain(2.0);
    Timeline other(otherSeq);
    UndoStack otherStack;
    MixerController otherMixer(other, otherStack);
    a.mixer.reset(other, otherStack);

    ASSERT_TRUE(a.mixer.setTrackGain(kAudio0, 0.1));
    EXPECT_DOUBLE_EQ(other.sequence().audioTracks[0].gain(), 0.1);
    EXPECT_DOUBLE_EQ(a.timeline.sequence().audioTracks[0].gain(), 1.0);
}

} // namespace