#include <bl_timeline/timeline.hpp>
#include <gtest/gtest.h>

namespace bl {
namespace {

Rational fps24() { return Rational{24000, 1001}; }

TEST(TrackAddClipTest, SortedByStartTime) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(10, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(0, fps24());
    c2.timelineDuration = Duration::fromFrames(8, fps24());

    ASSERT_TRUE(track.addClip(c2));
    ASSERT_TRUE(track.addClip(c1));
    EXPECT_EQ(track.clips().size(), 2u);
    EXPECT_EQ(track.clips()[0].id, "c2");
    EXPECT_EQ(track.clips()[1].id, "c1");
}

TEST(TrackAddClipTest, RejectOverlapping) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(10, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(5, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());

    ASSERT_TRUE(track.addClip(c1));
    EXPECT_FALSE(track.addClip(c2));
    EXPECT_EQ(track.clips().size(), 1u);
}

TEST(TrackAddClipTest, AdjacentClipsAllowed) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(5, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());

    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));
    EXPECT_EQ(track.clips().size(), 2u);
}

TEST(TrackFindClipTest, FoundAndNotFound) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "abc";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c));

    auto idx = track.findClipById("abc");
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(track.clips()[*idx].id, "abc");
    EXPECT_FALSE(track.findClipById("nonexistent").has_value());
}

TEST(TrackRemoveClipTest, RemoveExisting) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c));
    EXPECT_TRUE(track.removeClip("c1"));
    EXPECT_TRUE(track.clips().empty());
}

TEST(TrackRemoveClipTest, RemoveNonexistentFails) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.removeClip("nope"));
}

TEST(TrackMoveClipTest, MoveToFreeSpace) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c));

    Time newStart = Time::fromFrame(20, fps24());
    EXPECT_TRUE(track.moveClip("c1", newStart));
    EXPECT_EQ(track.clips()[0].timelineStart, newStart);
}

TEST(TrackMoveClipTest, MoveFailsIfOverlap) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(10, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(20, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));

    Time target = Time::fromFrame(8, fps24());
    EXPECT_FALSE(track.moveClip("c2", target));
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(20, fps24()));
}

TEST(TrackMoveClipTest, MoveNonexistentFails) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.moveClip("nope", Time::fromFrame(0, fps24())));
}

TEST(TrackSplitClipTest, SplitMidClip) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.source.sourceIn = Time::fromFrame(0, fps24());
    ASSERT_TRUE(track.addClip(c));

    Time splitPoint = Time::fromFrame(4, fps24());
    auto right = track.splitClip("c1", splitPoint);
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(track.clips().size(), 2u);
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(4, fps24()));
    EXPECT_EQ(right->timelineStart, splitPoint);
    EXPECT_EQ(right->timelineDuration,
              Duration::fromFrames(6, fps24()));
    EXPECT_EQ(right->source.sourceIn, Time::fromFrame(4, fps24()));
}

TEST(TrackSplitClipTest, SplitAtStartFails) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(5, fps24());
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.source.sourceIn = Time::fromFrame(0, fps24());
    ASSERT_TRUE(track.addClip(c));

    Time beforeStart = Time::fromFrame(3, fps24());
    EXPECT_FALSE(track.splitClip("c1", beforeStart).has_value());
}

TEST(TrackSplitClipTest, SplitAtEndFails) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(5, fps24());
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.source.sourceIn = Time::fromFrame(0, fps24());
    ASSERT_TRUE(track.addClip(c));

    Time atEnd = Time::fromFrame(15, fps24());
    EXPECT_FALSE(track.splitClip("c1", atEnd).has_value());
}

TEST(TrackSplitClipTest, SplitNonexistentFails) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(
        track.splitClip("nope", Time::fromFrame(0, fps24())).has_value());
}

TEST(TrackPropertiesTest, MuteSoloLockHeight) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.muted());
    EXPECT_FALSE(track.soloed());
    EXPECT_FALSE(track.locked());
    EXPECT_EQ(track.height(), 60);

    track.setMuted(true);
    track.setSoloed(true);
    track.setLocked(true);
    track.setHeight(80);

    EXPECT_TRUE(track.muted());
    EXPECT_TRUE(track.soloed());
    EXPECT_TRUE(track.locked());
    EXPECT_EQ(track.height(), 80);
}

TEST(ClipEffectiveDurationTest, Identity) {
    Clip c;
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.speed = SpeedRemap{1, 1, false};
    EXPECT_EQ(c.effectiveDuration(), c.timelineDuration);
}

TEST(ClipEffectiveDurationTest, SlowMotion2x) {
    Clip c;
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.speed = SpeedRemap{2, 1, false};
    auto eff = c.effectiveDuration();
    EXPECT_EQ(eff.ticks, c.timelineDuration.ticks * 2);
}

TEST(ClipEffectiveDurationTest, FastMotionHalf) {
    Clip c;
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.speed = SpeedRemap{1, 2, false};
    auto eff = c.effectiveDuration();
    EXPECT_EQ(eff.ticks, c.timelineDuration.ticks / 2);
}

TEST(SpeedRemapTest, IsIdentityTrue) {
    EXPECT_TRUE(SpeedRemap{}.isIdentity());
}

TEST(SpeedRemapTest, IsIdentitySpeedChanged) {
    SpeedRemap sr{2, 1, false};
    EXPECT_FALSE(sr.isIdentity());
}

TEST(SpeedRemapTest, IsIdentityReversed) {
    SpeedRemap sr{1, 1, true};
    EXPECT_FALSE(sr.isIdentity());
}

TEST(TimelineSnapshotTest, Immutability) {
    Sequence seq;
    seq.name = "Test Seq";
    seq.addVideoTrack("V1");
    Timeline tl(std::move(seq));

    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());

    auto snap = tl.snapshot();
    EXPECT_TRUE(snap.sequence().videoTracks[0].clips().empty());

    EXPECT_TRUE(tl.addClipToVideoTrack(0, c));
    EXPECT_FALSE(tl.sequence().videoTracks[0].clips().empty());
    EXPECT_TRUE(snap.sequence().videoTracks[0].clips().empty());
}

TEST(TimelineTest, AddAndRemoveClip) {
    Timeline tl;
    tl.sequence().addVideoTrack("V1");

    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());

    EXPECT_TRUE(tl.addClipToVideoTrack(0, c));
    EXPECT_FALSE(tl.sequence().videoTracks[0].clips().empty());
    EXPECT_TRUE(tl.removeClipFromVideoTrack(0, "c1"));
    EXPECT_TRUE(tl.sequence().videoTracks[0].clips().empty());
}

TEST(TimelineTest, AddClipInvalidTrackIndex) {
    Timeline tl;
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());
    EXPECT_FALSE(tl.addClipToVideoTrack(0, c));
    EXPECT_FALSE(tl.addClipToAudioTrack(0, c));
}

TEST(TimelineTest, RemoveClipInvalidTrackIndex) {
    Timeline tl;
    EXPECT_FALSE(tl.removeClipFromVideoTrack(0, "c1"));
    EXPECT_FALSE(tl.removeClipFromAudioTrack(0, "c1"));
}

TEST(TimelineTest, MoveClipInvalidTrackIndex) {
    Timeline tl;
    EXPECT_FALSE(tl.moveClipInVideoTrack(
        0, "c1", Time::fromFrame(0, fps24())));
}

TEST(TimelineTest, SplitClipInvalidTrackIndex) {
    Timeline tl;
    EXPECT_FALSE(tl.splitClipInVideoTrack(
                     0, "c1", Time::fromFrame(0, fps24()))
                     .has_value());
}

TEST(TimelineTest, AudioTrackOperations) {
    Timeline tl;
    tl.sequence().addAudioTrack("A1");

    Clip c;
    c.id = "a1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());

    EXPECT_TRUE(tl.addClipToAudioTrack(0, c));
    EXPECT_FALSE(tl.sequence().audioTracks[0].clips().empty());
    EXPECT_TRUE(tl.removeClipFromAudioTrack(0, "a1"));
}

TEST(TimelineTest, SplitClipInVideoTrack) {
    Timeline tl;
    tl.sequence().addVideoTrack("V1");

    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.source.sourceIn = Time::fromFrame(0, fps24());

    ASSERT_TRUE(tl.addClipToVideoTrack(0, c));
    auto right = tl.splitClipInVideoTrack(
        0, "c1", Time::fromFrame(4, fps24()));
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(tl.sequence().videoTracks[0].clips().size(), 2u);
}

TEST(TimelineTest, MoveClipInVideoTrack) {
    Timeline tl;
    tl.sequence().addVideoTrack("V1");

    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());

    ASSERT_TRUE(tl.addClipToVideoTrack(0, c));
    Time target = Time::fromFrame(20, fps24());
    EXPECT_TRUE(tl.moveClipInVideoTrack(0, "c1", target));
    EXPECT_EQ(tl.sequence().videoTracks[0].clips()[0].timelineStart, target);
}

TEST(SequenceTest, AddTracksAndMarkers) {
    Sequence seq;
    seq.name = "Seq1";
    seq.settings.width = 3840;
    seq.settings.height = 2160;
    seq.settings.fps = Rational{30000, 1001};

    auto& vt = seq.addVideoTrack("V1");
    EXPECT_EQ(vt.name(), "V1");
    EXPECT_EQ(seq.videoTracks.size(), 1u);

    auto& at = seq.addAudioTrack("A1");
    EXPECT_EQ(at.name(), "A1");
    EXPECT_EQ(seq.audioTracks.size(), 1u);

    seq.addMarker(Marker(Time::fromFrame(100, fps24()), "Ch1"));
    EXPECT_EQ(seq.markers.size(), 1u);
    EXPECT_EQ(seq.markers[0].label, "Ch1");
}

TEST(MarkerTest, DefaultAndParameterized) {
    Marker m;
    EXPECT_TRUE(m.label.empty());
    EXPECT_EQ(m.color, 0u);

    Marker m2(Time::fromFrame(50, fps24()), "Act1", 0xFF0000);
    EXPECT_EQ(m2.position, Time::fromFrame(50, fps24()));
    EXPECT_EQ(m2.label, "Act1");
    EXPECT_EQ(m2.color, 0xFF0000);
}

TEST(JsonRoundTrip, Clip) {
    Clip c;
    c.id = "clip-001";
    c.name = "Intro";
    c.colorLabel = 3;
    c.source.mediaItemId = "media-42";
    c.source.sourceIn = Time::fromFrame(100, fps24());
    c.source.sourceOut = Time::fromFrame(200, fps24());
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(50, fps24());
    c.speed = SpeedRemap{2, 1, false};
    c.effects.push_back({"blur.gaussian", true, {{"radius", 5.0}}});
    c.audio.gain = 0.8;
    c.audio.pan = -0.3;

    nlohmann::json j = c;
    Clip c2 = j.get<Clip>();

    EXPECT_EQ(c.id, c2.id);
    EXPECT_EQ(c.name, c2.name);
    EXPECT_EQ(c.colorLabel, c2.colorLabel);
    EXPECT_EQ(c.source.mediaItemId, c2.source.mediaItemId);
    EXPECT_EQ(c.source.sourceIn, c2.source.sourceIn);
    EXPECT_EQ(c.source.sourceOut, c2.source.sourceOut);
    EXPECT_EQ(c.timelineStart, c2.timelineStart);
    EXPECT_EQ(c.timelineDuration, c2.timelineDuration);
    EXPECT_EQ(c.speed, c2.speed);
    EXPECT_EQ(c.effects.size(), 1u);
    EXPECT_EQ(c.effects[0].effectId, "blur.gaussian");
    EXPECT_EQ(c.effects[0].params["radius"], 5.0);
    EXPECT_DOUBLE_EQ(c.audio.gain, c2.audio.gain);
    EXPECT_DOUBLE_EQ(c.audio.pan, c2.audio.pan);
}

TEST(JsonRoundTrip, Sequence) {
    Sequence seq;
    seq.name = "Main";
    seq.settings.width = 1920;
    seq.settings.height = 1080;
    seq.settings.fps = Rational{24000, 1001};
    seq.settings.sampleRate = 48000;
    seq.settings.channelLayout = 2;

    seq.addVideoTrack("V1");
    seq.addAudioTrack("A1");

    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(10, fps24());
    seq.videoTracks[0].addClip(c);

    seq.addMarker(Marker(Time::fromFrame(0, fps24()), "Start", 0x00FF00));

    nlohmann::json j = seq;
    Sequence seq2 = j.get<Sequence>();

    EXPECT_EQ(seq2.name, "Main");
    EXPECT_EQ(seq2.settings.width, 1920);
    EXPECT_EQ(seq2.settings.height, 1080);
    EXPECT_EQ(seq2.videoTracks.size(), 1u);
    EXPECT_EQ(seq2.audioTracks.size(), 1u);
    EXPECT_EQ(seq2.videoTracks[0].clips().size(), 1u);
    EXPECT_EQ(seq2.markers.size(), 1u);
    EXPECT_EQ(seq2.markers[0].label, "Start");
}

TEST(JsonRoundTrip, Timeline) {
    Sequence seq;
    seq.name = "Seq";
    seq.settings.fps = Rational{30000, 1001};

    Timeline tl(std::move(seq));
    tl.sequence().addVideoTrack("V1");

    nlohmann::json j = tl;
    Timeline tl2 = j.get<Timeline>();

    EXPECT_EQ(tl2.sequence().name, "Seq");
    Rational expectedFps{30000, 1001};
    EXPECT_TRUE(tl2.sequence().settings.fps == expectedFps);
    EXPECT_EQ(tl2.sequence().videoTracks.size(), 1u);
}

TEST(TrimLeftTest, ShrinkFromRight) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.source.sourceIn = Time::fromFrame(0, fps24());
    ASSERT_TRUE(track.addClip(c));

    Time newStart = Time::fromFrame(3, fps24());
    EXPECT_TRUE(track.trimClipLeft("c1", newStart));
    EXPECT_EQ(track.clips()[0].timelineStart, newStart);
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(7, fps24()));
}

TEST(TrimLeftTest, GrowIntoGap) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(10, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));

    Time newStart = Time::fromFrame(7, fps24());
    EXPECT_TRUE(track.trimClipLeft("c2", newStart));
    EXPECT_EQ(track.clips()[1].timelineStart, newStart);
    EXPECT_EQ(track.clips()[1].timelineDuration,
              Duration::fromFrames(8, fps24()));
}

TEST(TrimLeftTest, RejectCrossPrev) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(10, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));

    Time target = Time::fromFrame(3, fps24());
    EXPECT_FALSE(track.trimClipLeft("c2", target));
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(10, fps24()));
}

TEST(TrimLeftTest, RejectVanish) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(5, fps24());
    c.timelineDuration = Duration::fromFrames(10, fps24());
    ASSERT_TRUE(track.addClip(c));

    Time target = Time::fromFrame(15, fps24());
    EXPECT_FALSE(track.trimClipLeft("c1", target));
}

TEST(TrimLeftTest, RejectNonexistent) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.trimClipLeft("nope", Time::fromFrame(0, fps24())));
}

TEST(TrimRightTest, ShrinkFromRight) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(10, fps24());
    ASSERT_TRUE(track.addClip(c));

    Time newEnd = Time::fromFrame(7, fps24());
    EXPECT_TRUE(track.trimClipRight("c1", newEnd));
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(7, fps24()));
}

TEST(TrimRightTest, GrowIntoGap) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(10, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));

    Time newEnd = Time::fromFrame(9, fps24());
    EXPECT_TRUE(track.trimClipRight("c1", newEnd));
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(9, fps24()));
}

TEST(TrimRightTest, RejectCrossNext) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(10, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));

    Time target = Time::fromFrame(12, fps24());
    EXPECT_FALSE(track.trimClipRight("c1", target));
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(5, fps24()));
}

TEST(TrimRightTest, RejectNonexistent) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.trimClipRight("nope", Time::fromFrame(10, fps24())));
}

TEST(RippleDeleteTest, ClosesGap) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(10, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c3;
    c3.id = "c3";
    c3.timelineStart = Time::fromFrame(20, fps24());
    c3.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));
    ASSERT_TRUE(track.addClip(c3));

    EXPECT_TRUE(track.rippleDelete("c2"));
    EXPECT_EQ(track.clips().size(), 2u);
    EXPECT_EQ(track.clips()[0].id, "c1");
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(0, fps24()));
    EXPECT_EQ(track.clips()[1].id, "c3");
    Time expectedC3 = Time::fromFrame(15, fps24());
    EXPECT_EQ(track.clips()[1].timelineStart, expectedC3);
}

TEST(RippleDeleteTest, LastClip) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c));

    EXPECT_TRUE(track.rippleDelete("c1"));
    EXPECT_TRUE(track.clips().empty());
}

TEST(RippleDeleteTest, NonexistentFails) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.rippleDelete("nope"));
}

TEST(RippleTrimRightTest, ShiftsLater) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(10, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c3;
    c3.id = "c3";
    c3.timelineStart = Time::fromFrame(20, fps24());
    c3.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));
    ASSERT_TRUE(track.addClip(c3));

    Time newEnd = Time::fromFrame(14, fps24());
    EXPECT_TRUE(track.rippleTrimRight("c2", newEnd));
    EXPECT_EQ(track.clips()[1].timelineDuration,
              Duration::fromFrames(4, fps24()));
    Time expectedC3 = Time::fromFrame(19, fps24());
    EXPECT_EQ(track.clips()[2].timelineStart, expectedC3);
}

TEST(RippleTrimLeftTest, ShiftsEarlier) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(10, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c3;
    c3.id = "c3";
    c3.timelineStart = Time::fromFrame(20, fps24());
    c3.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));
    ASSERT_TRUE(track.addClip(c3));

    Time newStart = Time::fromFrame(12, fps24());
    EXPECT_TRUE(track.rippleTrimLeft("c2", newStart));
    EXPECT_EQ(track.clips()[1].timelineStart, newStart);
    EXPECT_EQ(track.clips()[1].timelineDuration,
              Duration::fromFrames(3, fps24()));
    Time expectedC1 = Time::fromFrame(2, fps24());
    EXPECT_EQ(track.clips()[0].timelineStart, expectedC1);
}

TEST(RippleTrimLeftTest, GrowShiftsEarlierBack) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(5, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(10, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));

    Time newStart = Time::fromFrame(7, fps24());
    EXPECT_TRUE(track.rippleTrimLeft("c2", newStart));
    EXPECT_EQ(track.clips()[1].timelineStart, newStart);
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(-3, fps24()));
}

TEST(RippleTrimRightTest, NonexistentFails) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.rippleTrimRight("nope", Time::fromFrame(10, fps24())));
}

TEST(RippleTrimLeftTest, NonexistentFails) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.rippleTrimLeft("nope", Time::fromFrame(0, fps24())));
}

TEST(SpeedTrimTest, TrimLeftSlowMo) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(20, fps24());
    c.source.sourceIn = Time::fromFrame(100, fps24());
    c.source.sourceOut = Time::fromFrame(300, fps24());
    c.speed = SpeedRemap{2, 1, false};
    ASSERT_TRUE(track.addClip(c));

    Time newStart = Time::fromFrame(5, fps24());
    EXPECT_TRUE(track.trimClipLeft("c1", newStart));
    Time expected = Time::fromFrame(110, fps24());
    EXPECT_EQ(track.clips()[0].source.sourceIn, expected);
}

TEST(SpeedTrimTest, TrimRightSlowMo) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(20, fps24());
    c.source.sourceIn = Time::fromFrame(100, fps24());
    c.source.sourceOut = Time::fromFrame(300, fps24());
    c.speed = SpeedRemap{2, 1, false};
    ASSERT_TRUE(track.addClip(c));

    Time newEnd = Time::fromFrame(15, fps24());
    EXPECT_TRUE(track.trimClipRight("c1", newEnd));
    Time expected = Time::fromFrame(290, fps24());
    EXPECT_EQ(track.clips()[0].source.sourceOut, expected);
}

TEST(SpeedSplitTest, SourceInSpeedAware) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c;
    c.id = "c1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(20, fps24());
    c.source.sourceIn = Time::fromFrame(100, fps24());
    c.speed = SpeedRemap{2, 1, false};
    ASSERT_TRUE(track.addClip(c));

    Time splitPoint = Time::fromFrame(8, fps24());
    auto right = track.splitClip("c1", splitPoint);
    ASSERT_TRUE(right.has_value());
    Time expected = Time::fromFrame(116, fps24());
    EXPECT_EQ(right->source.sourceIn, expected);
}

TEST(SpeedRippleTest, RippleDeleteSlowMo) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c1;
    c1.id = "c1";
    c1.timelineStart = Time::fromFrame(0, fps24());
    c1.timelineDuration = Duration::fromFrames(10, fps24());
    Clip c2;
    c2.id = "c2";
    c2.timelineStart = Time::fromFrame(20, fps24());
    c2.timelineDuration = Duration::fromFrames(5, fps24());
    c2.speed = SpeedRemap{2, 1, false};
    ASSERT_TRUE(track.addClip(c1));
    ASSERT_TRUE(track.addClip(c2));

    EXPECT_TRUE(track.rippleDelete("c1"));
    Time expectedC2 = Time::fromFrame(10, fps24());
    EXPECT_EQ(track.clips()[0].timelineStart, expectedC2);
}

TEST(TimelineEditWrappers, InvalidTrackIndex) {
    Timeline tl;
    EXPECT_FALSE(tl.trimClipLeftInVideoTrack(0, "c1", Time::fromFrame(0, fps24())));
    EXPECT_FALSE(tl.trimClipRightInVideoTrack(0, "c1", Time::fromFrame(10, fps24())));
    EXPECT_FALSE(tl.rippleDeleteFromVideoTrack(0, "c1"));
    EXPECT_FALSE(tl.rippleTrimLeftInVideoTrack(0, "c1", Time::fromFrame(0, fps24())));
    EXPECT_FALSE(tl.rippleTrimRightInVideoTrack(0, "c1", Time::fromFrame(10, fps24())));
    EXPECT_FALSE(tl.trimClipLeftInAudioTrack(0, "c1", Time::fromFrame(0, fps24())));
    EXPECT_FALSE(tl.trimClipRightInAudioTrack(0, "c1", Time::fromFrame(10, fps24())));
    EXPECT_FALSE(tl.rippleDeleteFromAudioTrack(0, "c1"));
    EXPECT_FALSE(tl.rippleTrimLeftInAudioTrack(0, "c1", Time::fromFrame(0, fps24())));
    EXPECT_FALSE(tl.rippleTrimRightInAudioTrack(0, "c1", Time::fromFrame(10, fps24())));
}

TEST(TimelineEditWrappers, RippleDeleteAudioTrack) {
    Timeline tl;
    tl.sequence().addAudioTrack("A1");
    Clip c;
    c.id = "a1";
    c.timelineStart = Time::fromFrame(0, fps24());
    c.timelineDuration = Duration::fromFrames(5, fps24());
    ASSERT_TRUE(tl.addClipToAudioTrack(0, c));
    EXPECT_TRUE(tl.rippleDeleteFromAudioTrack(0, "a1"));
    EXPECT_TRUE(tl.sequence().audioTracks[0].clips().empty());
}

} // namespace
} // namespace bl
