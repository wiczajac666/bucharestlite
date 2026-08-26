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

TEST(ClipEffectiveDurationTest, ConsumesDoubleSourceMaterial) {
    Clip c;
    c.timelineDuration = Duration::fromFrames(10, fps24());
    c.speed = SpeedRemap{2, 1, false};
    auto eff = c.effectiveDuration();
    EXPECT_EQ(eff.ticks, c.timelineDuration.ticks * 2);
}

TEST(ClipEffectiveDurationTest, ConsumesHalfSourceMaterial) {
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

Clip makeClip(std::string id, int64_t startFrames, int64_t durFrames) {
    Clip c;
    c.id = std::move(id);
    c.timelineStart = Time::fromFrame(startFrames, fps24());
    c.timelineDuration = Duration::fromFrames(durFrames, fps24());
    return c;
}

bool sortedByStart(const Track<Clip>& track) {
    for (size_t i = 1; i < track.clips().size(); ++i) {
        if (track.clips()[i - 1].timelineStart > track.clips()[i].timelineStart)
            return false;
    }
    return true;
}

TEST(InsertClipTest, EmptyTrackPlacesAtPoint) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c = makeClip("c1", 100, 4);
    auto placed = track.insertClip(c, Time::fromFrame(8, fps24()));
    ASSERT_TRUE(placed.has_value());
    ASSERT_EQ(track.clips().size(), 1u);
    EXPECT_EQ(track.clips()[0].id, "c1");
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(8, fps24()));
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(4, fps24()));
}

TEST(InsertClipTest, RipplesLaterClips) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 10, 5)));
    ASSERT_TRUE(track.addClip(makeClip("b", 20, 5)));

    Clip n = makeClip("n", 0, 4);
    auto placed = track.insertClip(n, Time::fromFrame(0, fps24()));
    ASSERT_TRUE(placed.has_value());
    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[0].id, "n");
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(0, fps24()));
    EXPECT_EQ(track.clips()[1].id, "a");
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(14, fps24()));
    EXPECT_EQ(track.clips()[2].id, "b");
    EXPECT_EQ(track.clips()[2].timelineStart, Time::fromFrame(24, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(InsertClipTest, SplitsStraddlingClip) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 10);
    a.name = "A";
    a.source.sourceIn = Time::fromFrame(100, fps24());
    a.source.sourceOut = Time::fromFrame(200, fps24());
    ASSERT_TRUE(track.addClip(a));

    Clip n = makeClip("n", 999, 4);
    auto placed = track.insertClip(n, Time::fromFrame(6, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[0].id, "a");
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(0, fps24()));
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(6, fps24()));
    EXPECT_EQ(track.clips()[1].id, "n");
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(6, fps24()));
    const Clip& frag = track.clips()[2];
    EXPECT_NE(frag.id, "a");
    EXPECT_EQ(frag.name, "A (R)");
    EXPECT_EQ(frag.timelineStart, Time::fromFrame(10, fps24()));
    EXPECT_EQ(frag.timelineDuration, Duration::fromFrames(4, fps24()));
    EXPECT_EQ(frag.source.sourceIn, Time::fromFrame(106, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(InsertClipTest, SplitFragmentSpeedAware) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 20);
    a.name = "A";
    a.source.sourceIn = Time::fromFrame(100, fps24());
    a.speed = SpeedRemap{2, 1, false};
    ASSERT_TRUE(track.addClip(a));

    Clip n = makeClip("n", 0, 2);
    auto placed = track.insertClip(n, Time::fromFrame(8, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(8, fps24()));
    const Clip& frag = track.clips()[2];
    EXPECT_EQ(frag.timelineStart, Time::fromFrame(10, fps24()));
    EXPECT_EQ(frag.timelineDuration, Duration::fromFrames(12, fps24()));
    EXPECT_EQ(frag.source.sourceIn, Time::fromFrame(116, fps24()));
}

TEST(InsertClipTest, AtClipBoundaryDoesNotSplit) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 10)));
    ASSERT_TRUE(track.addClip(makeClip("b", 10, 10)));

    Clip n = makeClip("n", 0, 3);
    auto placed = track.insertClip(n, Time::fromFrame(10, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[0].id, "a");
    EXPECT_EQ(track.clips()[1].id, "n");
    EXPECT_EQ(track.clips()[2].id, "b");
    EXPECT_EQ(track.clips()[2].timelineStart, Time::fromFrame(13, fps24()));
    for (const auto& c : track.clips()) {
        EXPECT_EQ(c.name.find("(R)"), std::string::npos);
    }
}

TEST(InsertClipTest, RejectsNonPositiveDuration) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip c = makeClip("c1", 0, 0);
    EXPECT_FALSE(track.insertClip(c, Time::fromFrame(0, fps24())));
    EXPECT_TRUE(track.clips().empty());
}

TEST(OverwriteClipTest, ErasesFullyCoveredClip) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 10)));

    Clip n = makeClip("n", 0, 12);
    auto placed = track.overwriteClip(n, Time::fromFrame(0, fps24()));
    ASSERT_TRUE(placed.has_value());
    ASSERT_EQ(track.clips().size(), 1u);
    EXPECT_EQ(track.clips()[0].id, "n");
}

TEST(OverwriteClipTest, KeepsLeftRemnantWhenCoveringTail) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 10);
    a.source.sourceIn = Time::fromFrame(100, fps24());
    a.source.sourceOut = Time::fromFrame(200, fps24());
    ASSERT_TRUE(track.addClip(a));

    Clip n = makeClip("n", 0, 10);
    auto placed = track.overwriteClip(n, Time::fromFrame(5, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 2u);
    EXPECT_EQ(track.clips()[0].id, "a");
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(0, fps24()));
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(5, fps24()));
    EXPECT_EQ(track.clips()[0].source.sourceOut,
              Time::fromFrame(195, fps24()));
    EXPECT_EQ(track.clips()[1].id, "n");
}

TEST(OverwriteClipTest, KeepsRightRemnantWhenCoveringHead) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 10, 10);
    a.source.sourceIn = Time::fromFrame(100, fps24());
    a.source.sourceOut = Time::fromFrame(200, fps24());
    ASSERT_TRUE(track.addClip(a));

    Clip n = makeClip("n", 0, 10);
    auto placed = track.overwriteClip(n, Time::fromFrame(5, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 2u);
    EXPECT_EQ(track.clips()[0].id, "n");
    const Clip& remnant = track.clips()[1];
    EXPECT_EQ(remnant.id, "a");
    EXPECT_EQ(remnant.timelineStart, Time::fromFrame(15, fps24()));
    EXPECT_EQ(remnant.timelineDuration, Duration::fromFrames(5, fps24()));
    EXPECT_EQ(remnant.source.sourceIn, Time::fromFrame(105, fps24()));
    EXPECT_EQ(remnant.source.sourceOut, Time::fromFrame(200, fps24()));
}

TEST(OverwriteClipTest, SplitsStraddledClipKeepsBothRemnants) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 10);
    a.name = "A";
    a.source.sourceIn = Time::fromFrame(100, fps24());
    a.source.sourceOut = Time::fromFrame(200, fps24());
    ASSERT_TRUE(track.addClip(a));

    Clip n = makeClip("n", 0, 4);
    auto placed = track.overwriteClip(n, Time::fromFrame(3, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 3u);
    const Clip& left = track.clips()[0];
    EXPECT_EQ(left.id, "a");
    EXPECT_EQ(left.timelineStart, Time::fromFrame(0, fps24()));
    EXPECT_EQ(left.timelineDuration, Duration::fromFrames(3, fps24()));
    EXPECT_EQ(left.source.sourceOut, Time::fromFrame(193, fps24()));
    EXPECT_EQ(track.clips()[1].id, "n");
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(3, fps24()));
    const Clip& right = track.clips()[2];
    EXPECT_NE(right.id, "a");
    EXPECT_EQ(right.name, "A (R)");
    EXPECT_EQ(right.timelineStart, Time::fromFrame(7, fps24()));
    EXPECT_EQ(right.timelineDuration, Duration::fromFrames(3, fps24()));
    EXPECT_EQ(right.source.sourceIn, Time::fromFrame(107, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(OverwriteClipTest, CoversAcrossTwoClipsLeavesEnds) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 10);
    a.source.sourceIn = Time::fromFrame(10, fps24());
    a.source.sourceOut = Time::fromFrame(20, fps24());
    Clip b = makeClip("b", 10, 10);
    b.source.sourceIn = Time::fromFrame(20, fps24());
    b.source.sourceOut = Time::fromFrame(30, fps24());
    ASSERT_TRUE(track.addClip(a));
    ASSERT_TRUE(track.addClip(b));

    Clip n = makeClip("n", 0, 8);
    auto placed = track.overwriteClip(n, Time::fromFrame(4, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[0].id, "a");
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(4, fps24()));
    EXPECT_EQ(track.clips()[0].source.sourceOut,
              Time::fromFrame(14, fps24()));
    EXPECT_EQ(track.clips()[1].id, "n");
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(4, fps24()));
    const Clip& bRemnant = track.clips()[2];
    EXPECT_EQ(bRemnant.id, "b");
    EXPECT_EQ(bRemnant.timelineStart, Time::fromFrame(12, fps24()));
    EXPECT_EQ(bRemnant.source.sourceIn, Time::fromFrame(22, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(OverwriteClipTest, SpeedAwareRemnantTrims) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 20);
    a.source.sourceIn = Time::fromFrame(100, fps24());
    a.source.sourceOut = Time::fromFrame(300, fps24());
    a.speed = SpeedRemap{2, 1, false};
    ASSERT_TRUE(track.addClip(a));

    Clip n = makeClip("n", 0, 10);
    auto placed = track.overwriteClip(n, Time::fromFrame(5, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(5, fps24()));
    EXPECT_EQ(track.clips()[0].source.sourceOut,
              Time::fromFrame(270, fps24()));
    const Clip& remnant = track.clips()[2];
    EXPECT_EQ(remnant.timelineStart, Time::fromFrame(15, fps24()));
    EXPECT_EQ(remnant.timelineDuration, Duration::fromFrames(5, fps24()));
    EXPECT_EQ(remnant.source.sourceIn, Time::fromFrame(130, fps24()));
}

TEST(OverwriteClipTest, ReplacesExistingIdAndKeepsSort) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("p", 0, 5)));
    ASSERT_TRUE(track.addClip(makeClip("q", 10, 5)));
    ASSERT_TRUE(track.addClip(makeClip("x", 50, 5)));

    Clip x2 = makeClip("x", 999, 2);
    auto placed = track.overwriteClip(x2, Time::fromFrame(7, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[1].id, "x");
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(7, fps24()));
    auto found = track.findClipById("x");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, 1u);
    EXPECT_TRUE(sortedByStart(track));
}

TEST(OverwriteClipTest, LeavesOutsideClipsUntouched) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 5);
    Clip b = makeClip("b", 20, 5);
    ASSERT_TRUE(track.addClip(a));
    ASSERT_TRUE(track.addClip(b));

    Clip n = makeClip("n", 0, 4);
    auto placed = track.overwriteClip(n, Time::fromFrame(6, fps24()));
    ASSERT_TRUE(placed.has_value());

    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[0].id, "a");
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(5, fps24()));
    EXPECT_EQ(track.clips()[2].id, "b");
    EXPECT_EQ(track.clips()[2].timelineStart, Time::fromFrame(20, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(OverwriteClipTest, RejectsNonPositiveDuration) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 5)));
    Clip c = makeClip("c1", 0, 0);
    EXPECT_FALSE(track.overwriteClip(c, Time::fromFrame(2, fps24()))
                     .has_value());
    ASSERT_EQ(track.clips().size(), 1u);
    EXPECT_EQ(track.clips()[0].id, "a");
}

TEST(AppendClipTest, EmptyTrackAtZero) {
    Track<Clip> track("V1", TrackKind::Video);
    auto placed = track.appendClip(makeClip("c1", 40, 4));
    ASSERT_TRUE(placed.has_value());
    ASSERT_EQ(track.clips().size(), 1u);
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(0, fps24()));
}

TEST(AppendClipTest, AppendsAfterTailBeyondGap) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 10)));
    ASSERT_TRUE(track.addClip(makeClip("b", 30, 5)));

    auto placed = track.appendClip(makeClip("c", 0, 4));
    ASSERT_TRUE(placed.has_value());
    ASSERT_EQ(track.clips().size(), 3u);
    EXPECT_EQ(track.clips()[2].id, "c");
    EXPECT_EQ(track.clips()[2].timelineStart, Time::fromFrame(35, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(AppendClipTest, RejectsNonPositiveDuration) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(track.appendClip(makeClip("c1", 0, 0)).has_value());
    EXPECT_TRUE(track.clips().empty());
}

TEST(ThreePointTest, IdentitySpeed) {
    SourceRef src;
    src.mediaItemId = "m1";
    src.sourceIn = Time::fromFrame(100, fps24());
    src.sourceOut = Time::fromFrame(110, fps24());
    auto clip = makeThreePointClip(src, SpeedRemap{}, "hello");
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->name, "hello");
    EXPECT_EQ(clip->source, src);
    EXPECT_TRUE(clip->speed.isIdentity());
    EXPECT_EQ(clip->timelineDuration, Duration::fromFrames(10, fps24()));
}

TEST(ThreePointTest, DoubleSpeedHalvesDuration) {
    SourceRef src;
    src.mediaItemId = "m1";
    src.sourceIn = Time::fromFrame(100, fps24());
    src.sourceOut = Time::fromFrame(110, fps24());
    auto clip = makeThreePointClip(src, SpeedRemap{2, 1, false});
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->timelineDuration, Duration::fromFrames(5, fps24()));
}

TEST(ThreePointTest, HalfSpeedDoublesDuration) {
    SourceRef src;
    src.mediaItemId = "m1";
    src.sourceIn = Time::fromFrame(100, fps24());
    src.sourceOut = Time::fromFrame(110, fps24());
    auto clip = makeThreePointClip(src, SpeedRemap{1, 2, false});
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->timelineDuration, Duration::fromFrames(20, fps24()));
}

TEST(ThreePointTest, RejectsEmptyRange) {
    SourceRef src;
    src.mediaItemId = "m1";
    src.sourceIn = Time::fromFrame(100, fps24());
    src.sourceOut = Time::fromFrame(100, fps24());
    EXPECT_FALSE(makeThreePointClip(src, SpeedRemap{}).has_value());
}

TEST(ThreePointTest, RejectsReversedRange) {
    SourceRef src;
    src.mediaItemId = "m1";
    src.sourceIn = Time::fromFrame(110, fps24());
    src.sourceOut = Time::fromFrame(100, fps24());
    EXPECT_FALSE(makeThreePointClip(src, SpeedRemap{}).has_value());
}

TEST(ThreePointTest, ClipFeedsOverwriteEdit) {
    SourceRef src;
    src.mediaItemId = "m1";
    src.sourceIn = Time::fromFrame(100, fps24());
    src.sourceOut = Time::fromFrame(110, fps24());
    auto clip = makeThreePointClip(src, SpeedRemap{2, 1, false}, "fast");
    ASSERT_TRUE(clip.has_value());

    Track<Clip> track("V1", TrackKind::Video);
    auto placed = track.overwriteClip(*clip, Time::fromFrame(3, fps24()));
    ASSERT_TRUE(placed.has_value());
    ASSERT_EQ(track.clips().size(), 1u);
    EXPECT_EQ(track.clips()[0].name, "fast");
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(3, fps24()));
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(5, fps24()));
}

TEST(ThreePointTest, JsonRoundTrip) {
    SourceRef src;
    src.mediaItemId = "m1";
    src.sourceIn = Time::fromFrame(100, fps24());
    src.sourceOut = Time::fromFrame(110, fps24());
    auto clip = makeThreePointClip(src, SpeedRemap{2, 1, false}, "rt");
    ASSERT_TRUE(clip.has_value());

    nlohmann::json j = *clip;
    Clip parsed = j.get<Clip>();
    EXPECT_EQ(*clip, parsed);
}

TEST(TimelineThreePointWrappers, InvalidTrackIndex) {
    Timeline tl;
    Clip c = makeClip("c1", 0, 4);
    EXPECT_FALSE(tl.insertClipInVideoTrack(0, c, Time::fromFrame(0, fps24()))
                      .has_value());
    EXPECT_FALSE(tl.overwriteClipInVideoTrack(0, c, Time::fromFrame(0, fps24()))
                      .has_value());
    EXPECT_FALSE(tl.appendClipToVideoTrack(0, c).has_value());
    EXPECT_FALSE(tl.insertClipInAudioTrack(0, c, Time::fromFrame(0, fps24()))
                      .has_value());
    EXPECT_FALSE(tl.overwriteClipInAudioTrack(0, c, Time::fromFrame(0, fps24()))
                      .has_value());
    EXPECT_FALSE(tl.appendClipToAudioTrack(0, c).has_value());
}

TEST(TimelineThreePointWrappers, InsertAppendOverwriteHappyPath) {
    Timeline tl;
    tl.sequence().addVideoTrack("V1");
    tl.sequence().addAudioTrack("A1");

    ASSERT_TRUE(
        tl.insertClipInVideoTrack(0, makeClip("v1", 0, 4),
                                  Time::fromFrame(0, fps24())).has_value());
    ASSERT_TRUE(tl.appendClipToVideoTrack(0, makeClip("v2", 0, 3)).has_value());
    ASSERT_TRUE(
        tl.overwriteClipInVideoTrack(0, makeClip("v3", 0, 2),
                                     Time::fromFrame(2, fps24())).has_value());
    ASSERT_EQ(tl.sequence().videoTracks[0].clips().size(), 3u);
    EXPECT_EQ(tl.sequence().videoTracks[0].clips()[0].timelineStart,
              Time::fromFrame(0, fps24()));
    EXPECT_EQ(tl.sequence().videoTracks[0].clips()[1].id, "v3");
    EXPECT_EQ(tl.sequence().videoTracks[0].clips()[2].timelineStart,
              Time::fromFrame(4, fps24()));

    ASSERT_TRUE(
        tl.insertClipInAudioTrack(0, makeClip("a1", 0, 5),
                                  Time::fromFrame(0, fps24())).has_value());
    ASSERT_TRUE(tl.appendClipToAudioTrack(0, makeClip("a2", 0, 5)).has_value());
    ASSERT_TRUE(
        tl.overwriteClipInAudioTrack(0, makeClip("a3", 0, 2),
                                     Time::fromFrame(6, fps24())).has_value());
    ASSERT_EQ(tl.sequence().audioTracks[0].clips().size(), 4u);
    EXPECT_EQ(tl.sequence().audioTracks[0].clips()[1].id, "a2");
    EXPECT_EQ(tl.sequence().audioTracks[0].clips()[1].timelineStart,
              Time::fromFrame(5, fps24()));
    EXPECT_EQ(tl.sequence().audioTracks[0].clips()[2].id, "a3");
    EXPECT_EQ(tl.sequence().audioTracks[0].clips()[2].timelineStart,
              Time::fromFrame(6, fps24()));
    const Clip& a2Left = tl.sequence().audioTracks[0].clips()[1];
    EXPECT_EQ(a2Left.timelineDuration, Duration::fromFrames(1, fps24()));
    const Clip& a2Right = tl.sequence().audioTracks[0].clips()[3];
    EXPECT_NE(a2Right.id, "a2");
    EXPECT_EQ(a2Right.timelineStart, Time::fromFrame(8, fps24()));
    EXPECT_EQ(a2Right.timelineDuration, Duration::fromFrames(2, fps24()));
}

TEST(SetClipSpeedTest, DoubleHalvesSpanRipplesLater) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 20)));
    ASSERT_TRUE(track.addClip(makeClip("b", 20, 10)));

    auto placed = track.setClipSpeed("a", SpeedRemap{2, 1, false});
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(10, fps24()));
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(10, fps24()));
    EXPECT_EQ(track.clips()[1].timelineDuration,
              Duration::fromFrames(10, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(SetClipSpeedTest, HalfDoublesSpanRipplesLater) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 20)));
    ASSERT_TRUE(track.addClip(makeClip("b", 20, 5)));

    auto placed = track.setClipSpeed("a", SpeedRemap{1, 2, false});
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(40, fps24()));
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(40, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(SetClipSpeedTest, SourceRangeUntouched) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 20);
    a.source.sourceIn = Time::fromFrame(100, fps24());
    a.source.sourceOut = Time::fromFrame(200, fps24());
    ASSERT_TRUE(track.addClip(a));

    auto placed = track.setClipSpeed("a", SpeedRemap{4, 1, false});
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(track.clips()[0].source.sourceIn,
              Time::fromFrame(100, fps24()));
    EXPECT_EQ(track.clips()[0].source.sourceOut,
              Time::fromFrame(200, fps24()));
}

TEST(SetClipSpeedTest, ArbitraryRatioRetime) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 30);
    a.speed = SpeedRemap{3, 1, false};
    ASSERT_TRUE(track.addClip(a));

    auto placed = track.setClipSpeed("a", SpeedRemap{1, 2, false});
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(placed->speed, (SpeedRemap{1, 2, false}));
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(180, fps24()));
}

TEST(SetClipSpeedTest, NonexistentIdFails) {
    Track<Clip> track("V1", TrackKind::Video);
    EXPECT_FALSE(
        track.setClipSpeed("nope", SpeedRemap{2, 1, false}).has_value());
}

TEST(SetClipSpeedTest, RejectsInvalidRates) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 10)));

    EXPECT_FALSE(track.setClipSpeed("a", SpeedRemap{0, 1, false}).has_value());
    EXPECT_FALSE(track.setClipSpeed("a", SpeedRemap{1, 0, false}).has_value());
    EXPECT_FALSE(
        track.setClipSpeed("a", SpeedRemap{-2, 1, false}).has_value());
    EXPECT_FALSE(
        track.setClipSpeed("a", SpeedRemap{2, -1, false}).has_value());

    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(10, fps24()));
    EXPECT_TRUE(track.clips()[0].speed.isIdentity());
}

TEST(SetClipSpeedTest, IdentityNoOpKeepsNeighbors) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 10)));
    ASSERT_TRUE(track.addClip(makeClip("b", 10, 5)));

    auto placed = track.setClipSpeed("a", SpeedRemap{1, 1, false});
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(track.clips()[0].timelineStart, Time::fromFrame(0, fps24()));
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(10, fps24()));
}

TEST(SetClipSpeedTest, LastClipGrowsFreely) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 10)));

    auto placed = track.setClipSpeed("a", SpeedRemap{1, 2, false});
    ASSERT_TRUE(placed.has_value());
    ASSERT_EQ(track.clips().size(), 1u);
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(20, fps24()));
}

TEST(SetClipSpeedTest, PullLeftIntoGapStaysSorted) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 10)));
    ASSERT_TRUE(track.addClip(makeClip("b", 30, 5)));

    auto placed = track.setClipSpeed("a", SpeedRemap{2, 1, false});
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(track.clips()[1].timelineStart, Time::fromFrame(25, fps24()));
    EXPECT_TRUE(sortedByStart(track));
}

TEST(SetClipSpeedTest, ReverseFlagStoredWithoutRetime) {
    Track<Clip> track("V1", TrackKind::Video);
    ASSERT_TRUE(track.addClip(makeClip("a", 0, 10)));

    auto placed = track.setClipSpeed("a", SpeedRemap{1, 1, true});
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(track.clips()[0].timelineDuration,
              Duration::fromFrames(10, fps24()));
    EXPECT_TRUE(track.clips()[0].speed.reversed);
    EXPECT_FALSE(track.clips()[0].speed.isIdentity());
}

TEST(SetClipSpeedThenTrimCombo, TrimUsesNewSpeed) {
    Track<Clip> track("V1", TrackKind::Video);
    Clip a = makeClip("a", 0, 20);
    a.source.sourceIn = Time::fromFrame(100, fps24());
    ASSERT_TRUE(track.addClip(a));

    ASSERT_TRUE(track.setClipSpeed("a", SpeedRemap{2, 1, false}).has_value());
    EXPECT_TRUE(track.trimClipLeft("a", Time::fromFrame(5, fps24())));
    EXPECT_EQ(track.clips()[0].source.sourceIn,
              Time::fromFrame(110, fps24()));
}

TEST(TimelineSpeedWrappers, InvalidTrackIndex) {
    Timeline tl;
    EXPECT_FALSE(
        tl.setClipSpeedInVideoTrack(0, "c1", SpeedRemap{2, 1, false})
            .has_value());
    EXPECT_FALSE(
        tl.setClipSpeedInAudioTrack(0, "c1", SpeedRemap{2, 1, false})
            .has_value());
}

TEST(TimelineSpeedWrappers, HappyPathVideoAndAudio) {
    Timeline tl;
    tl.sequence().addVideoTrack("V1");
    tl.sequence().addAudioTrack("A1");
    ASSERT_TRUE(tl.addClipToVideoTrack(0, makeClip("v1", 0, 10)));
    ASSERT_TRUE(tl.addClipToAudioTrack(0, makeClip("a1", 0, 10)));

    auto v = tl.setClipSpeedInVideoTrack(0, "v1", SpeedRemap{2, 1, false});
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->timelineDuration, Duration::fromFrames(5, fps24()));

    auto a = tl.setClipSpeedInAudioTrack(0, "a1", SpeedRemap{1, 2, false});
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->timelineDuration, Duration::fromFrames(20, fps24()));
}

} // namespace
} // namespace bl
