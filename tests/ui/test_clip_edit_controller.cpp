#include <bl_core/undo_stack.hpp>
#include <bl_timeline/sequence.hpp>

#include <panels/clip_edit_controller.hpp>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

namespace {

using bl::Clip;
using bl::ClipId;
using bl::Duration;
using bl::EffectInstance;
using bl::Interpolation;
using bl::KeyChannel;
using bl::Rational;
using bl::Sequence;
using bl::Time;
using bl::Timeline;
using bl::UndoStack;
using bl::ui::ClipEditController;

constexpr int kVideo0 = 0;
constexpr int kAudio0 = 1; // after the default 1 video track

const Rational kRate{1'000'000, 1};
const Rational kFps{24000, 1001};

Time frames(int64_t n) {
    return Time::fromFrameAt(n, kFps, kRate);
}

Clip makeClip(const ClipId& id, int64_t startFrame, int64_t durFrames) {
    Clip c;
    c.id = id;
    c.name = id;
    c.timelineStart = frames(startFrame);
    c.timelineDuration = Duration::fromFrames(durFrames, kFps);
    c.source.sourceIn = frames(0);
    c.source.sourceOut = c.source.sourceIn + c.timelineDuration;
    c.source.mediaItemId = id;
    return c;
}

EffectInstance makeEffect(const std::string& id, int radius = 1,
                          bool enabled = true) {
    EffectInstance e;
    e.effectId = id;
    e.enabled = enabled;
    e.params["radius"] = radius;
    return e;
}

struct Fixture {
    Sequence sequence;
    Timeline timeline;
    UndoStack undoStack;
    ClipEditController editor;

    Fixture() {
        sequence.name = "Test";
        sequence.addVideoTrack("V1");
        sequence.addAudioTrack("A1");
        timeline = Timeline(sequence);
        editor.reset(timeline, undoStack);
    }

    const Clip* clip(int flat, const ClipId& id) const {
        const auto& track =
            flat < 1 ? timeline.sequence().videoTracks[flat]
                     : timeline.sequence().audioTracks[flat - 1];
        for (const auto& c : track.clips()) {
            if (c.id == id) return &c;
        }
        return nullptr;
    }
};

} // namespace

TEST(ClipEditController, RenameIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.setClipName(kVideo0, "a", "New name"));
    EXPECT_EQ(f.clip(kVideo0, "a")->name, "New name");

    // Same name is a no-op: nothing pushed.
    EXPECT_FALSE(f.editor.setClipName(kVideo0, "a", "New name"));
    f.undoStack.undo();
    EXPECT_EQ(f.clip(kVideo0, "a")->name, "a");
    f.undoStack.redo();
    EXPECT_EQ(f.clip(kVideo0, "a")->name, "New name");
}

TEST(ClipEditController, RenameRejectsMissingClip) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    EXPECT_FALSE(f.editor.setClipName(kVideo0, "nope", "X"));
    EXPECT_FALSE(f.editor.setClipName(99, "a", "X"));
}

TEST(ClipEditController, ColorLabelIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.setColorLabel(kVideo0, "a", 4u));
    EXPECT_EQ(f.clip(kVideo0, "a")->colorLabel, 4u);

    f.undoStack.undo();
    EXPECT_EQ(f.clip(kVideo0, "a")->colorLabel, 0u);
}

TEST(ClipEditController, SourceRangeIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.setSourceRange(kVideo0, "a", frames(2), frames(8)));
    EXPECT_EQ(f.clip(kVideo0, "a")->source.sourceIn, frames(2));
    EXPECT_EQ(f.clip(kVideo0, "a")->source.sourceOut, frames(8));

    // Reversed range is rejected and pushes nothing.
    EXPECT_FALSE(f.editor.setSourceRange(kVideo0, "a", frames(8), frames(2)));

    f.undoStack.undo();
    EXPECT_EQ(f.clip(kVideo0, "a")->source.sourceIn, frames(0));
}

TEST(ClipEditController, GainAndPanAreUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.setGain(kVideo0, "a", 0.5));
    ASSERT_TRUE(f.editor.setPan(kVideo0, "a", -0.25));
    EXPECT_DOUBLE_EQ(f.clip(kVideo0, "a")->audio.gain, 0.5);
    EXPECT_DOUBLE_EQ(f.clip(kVideo0, "a")->audio.pan, -0.25);

    f.undoStack.undo();
    EXPECT_DOUBLE_EQ(f.clip(kVideo0, "a")->audio.pan, 0.0);
    f.undoStack.undo();
    EXPECT_DOUBLE_EQ(f.clip(kVideo0, "a")->audio.gain, 1.0);
}

TEST(ClipEditController, AddEffectIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.addEffect(kVideo0, "a", makeEffect("blur.box", 2)));
    ASSERT_EQ(f.clip(kVideo0, "a")->effects.size(), 1u);
    EXPECT_EQ(f.clip(kVideo0, "a")->effects[0].effectId, "blur.box");

    f.undoStack.undo();
    EXPECT_TRUE(f.clip(kVideo0, "a")->effects.empty());

    f.undoStack.redo();
    ASSERT_EQ(f.clip(kVideo0, "a")->effects.size(), 1u);
    EXPECT_EQ(f.clip(kVideo0, "a")->effects[0].params["radius"].get<int>(), 2);
}

TEST(ClipEditController, RemoveEffectRestoresPositionOnUndo) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    const ClipId kId = "a";
    f.editor.addEffect(kVideo0, kId, makeEffect("blur.box"));
    f.editor.addEffect(kVideo0, kId, makeEffect("greyscale"));
    f.editor.addEffect(kVideo0, kId, makeEffect("transform_2d"));
    ASSERT_EQ(f.clip(kVideo0, kId)->effects.size(), 3u);

    ASSERT_TRUE(f.editor.removeEffect(kVideo0, kId, 1));
    ASSERT_EQ(f.clip(kVideo0, kId)->effects.size(), 2u);
    EXPECT_EQ(f.clip(kVideo0, kId)->effects[1].effectId, "transform_2d");

    f.undoStack.undo();
    ASSERT_EQ(f.clip(kVideo0, kId)->effects.size(), 3u);
    EXPECT_EQ(f.clip(kVideo0, kId)->effects[1].effectId, "greyscale");
}

TEST(ClipEditController, RemoveEffectRejectsBadIndex) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    EXPECT_FALSE(f.editor.removeEffect(kVideo0, "a", 0));
}

TEST(ClipEditController, ReorderEffectIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    const ClipId kId = "a";
    f.editor.addEffect(kVideo0, kId, makeEffect("blur.box"));
    f.editor.addEffect(kVideo0, kId, makeEffect("greyscale"));
    f.editor.addEffect(kVideo0, kId, makeEffect("transform_2d"));

    ASSERT_TRUE(f.editor.reorderEffect(kVideo0, kId, 0, 2));
    ASSERT_EQ(f.clip(kVideo0, kId)->effects.size(), 3u);
    EXPECT_EQ(f.clip(kVideo0, kId)->effects[0].effectId, "greyscale");
    EXPECT_EQ(f.clip(kVideo0, kId)->effects[2].effectId, "blur.box");

    f.undoStack.undo();
    ASSERT_EQ(f.clip(kVideo0, kId)->effects.size(), 3u);
    EXPECT_EQ(f.clip(kVideo0, kId)->effects[0].effectId, "blur.box");
    EXPECT_EQ(f.clip(kVideo0, kId)->effects[2].effectId, "transform_2d");
}

TEST(ClipEditController, ReorderRejectsBadIndices) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    f.editor.addEffect(kVideo0, "a", makeEffect("blur.box"));
    EXPECT_FALSE(f.editor.reorderEffect(kVideo0, "a", 0, 0));
    EXPECT_FALSE(f.editor.reorderEffect(kVideo0, "a", 0, 1));
    EXPECT_FALSE(f.editor.reorderEffect(kVideo0, "a", 3, 0));
}

TEST(ClipEditController, ToggleEffectIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    f.editor.addEffect(kVideo0, "a", makeEffect("blur.box"));

    ASSERT_TRUE(f.editor.setEffectEnabled(kVideo0, "a", 0, false));
    EXPECT_FALSE(f.clip(kVideo0, "a")->effects[0].enabled);

    // Same state is a no-op.
    EXPECT_FALSE(f.editor.setEffectEnabled(kVideo0, "a", 0, false));

    f.undoStack.undo();
    EXPECT_TRUE(f.clip(kVideo0, "a")->effects[0].enabled);
    f.undoStack.redo();
    EXPECT_FALSE(f.clip(kVideo0, "a")->effects[0].enabled);
}

TEST(ClipEditController, EffectParamsAreUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    f.editor.addEffect(kVideo0, "a", makeEffect("blur.box", 1));

    nlohmann::json params;
    params["radius"] = 7;
    ASSERT_TRUE(f.editor.setEffectParams(kVideo0, "a", 0, params));
    EXPECT_EQ(f.clip(kVideo0, "a")->effects[0].params["radius"].get<int>(), 7);

    f.undoStack.undo();
    EXPECT_EQ(f.clip(kVideo0, "a")->effects[0].params["radius"].get<int>(), 1);
}

TEST(ClipEditController, EffectParamsRejectNonObject) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    f.editor.addEffect(kVideo0, "a", makeEffect("blur.box"));
    EXPECT_FALSE(f.editor.setEffectParams(kVideo0, "a", 0, nlohmann::json{5}));
    EXPECT_EQ(f.undoStack.count(), 1u); // no additional command pushed
}

TEST(ClipEditController, AddKeyframeIsUndoable) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));

    ASSERT_TRUE(f.editor.setKeyframe(kVideo0, "a", KeyChannel::Opacity,
                                     frames(2), 0.5, Interpolation::Linear));
    auto* kf = f.clip(kVideo0, "a")->keyframes
                   ? f.clip(kVideo0, "a")->keyframes->track(KeyChannel::Opacity)
                   : nullptr;
    ASSERT_NE(kf, nullptr);
    ASSERT_EQ(kf->samples().size(), 1u);
    EXPECT_DOUBLE_EQ(kf->samples()[0].value, 0.5);

    f.undoStack.undo();
    const auto* afterUndo =
        f.clip(kVideo0, "a")->keyframes
            ? f.clip(kVideo0, "a")->keyframes->track(KeyChannel::Opacity)
            : nullptr;
    EXPECT_TRUE(afterUndo == nullptr || afterUndo->samples().empty());

    f.undoStack.redo();
    kf = f.clip(kVideo0, "a")->keyframes->track(KeyChannel::Opacity);
    ASSERT_EQ(kf->samples().size(), 1u);
}

TEST(ClipEditController, UpdateKeyframeRestoresOldValueOnUndo) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    const ClipId kId = "a";
    f.editor.setKeyframe(kVideo0, kId, KeyChannel::Scale, frames(1), 1.0,
                         Interpolation::Linear);
    f.editor.setKeyframe(kVideo0, kId, KeyChannel::Scale, frames(5), 2.0,
                         Interpolation::Linear);

    ASSERT_TRUE(f.editor.setKeyframe(kVideo0, kId, KeyChannel::Scale, frames(1),
                                     9.0, Interpolation::Hold));
    auto* track =
        f.clip(kVideo0, kId)->keyframes->track(KeyChannel::Scale);
    ASSERT_EQ(track->samples().size(), 2u);
    EXPECT_DOUBLE_EQ(track->samples()[0].value, 9.0);

    f.undoStack.undo();
    track = f.clip(kVideo0, kId)->keyframes->track(KeyChannel::Scale);
    ASSERT_EQ(track->samples().size(), 2u);
    EXPECT_DOUBLE_EQ(track->samples()[0].value, 1.0);
    EXPECT_EQ(track->samples()[0].interpolation, Interpolation::Linear);
}

TEST(ClipEditController, KeyframeNoOpWhenUnchanged) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    ASSERT_TRUE(f.editor.setKeyframe(kVideo0, "a", KeyChannel::PosX, frames(3),
                                     4.0, Interpolation::Linear));
    EXPECT_EQ(f.undoStack.count(), 1u);
    EXPECT_FALSE(f.editor.setKeyframe(kVideo0, "a", KeyChannel::PosX, frames(3),
                                      4.0, Interpolation::Linear));
    EXPECT_EQ(f.undoStack.count(), 1u);
}

TEST(ClipEditController, RemoveKeyframeRestoresOnUndo) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    const ClipId kId = "a";
    ASSERT_TRUE(f.editor.setKeyframe(kVideo0, kId, KeyChannel::Volume, frames(1),
                                     0.25, Interpolation::Hold));
    ASSERT_TRUE(f.editor.setKeyframe(kVideo0, kId, KeyChannel::Volume, frames(5),
                                     0.75, Interpolation::Linear));

    ASSERT_TRUE(f.editor.removeKeyframe(kVideo0, kId, KeyChannel::Volume,
                                        frames(1)));
    EXPECT_EQ(f.clip(kVideo0, kId)->keyframes->track(KeyChannel::Volume)
                  ->samples()
                  .size(),
              1u);

    f.undoStack.undo();
    auto* track =
        f.clip(kVideo0, kId)->keyframes->track(KeyChannel::Volume);
    ASSERT_EQ(track->samples().size(), 2u);
    EXPECT_DOUBLE_EQ(track->samples()[0].value, 0.25);
    EXPECT_EQ(track->samples()[0].interpolation, Interpolation::Hold);
}

TEST(ClipEditController, RemoveKeyframeRejectsMissing) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToVideoTrack(0, makeClip("a", 0, 10)));
    EXPECT_FALSE(f.editor.removeKeyframe(kVideo0, "a", KeyChannel::Volume,
                                         frames(1)));
}

TEST(ClipEditController, AudioTrackEditingWorks) {
    Fixture f;
    ASSERT_TRUE(f.timeline.addClipToAudioTrack(0, makeClip("amix", 0, 10)));

    ASSERT_TRUE(f.editor.setGain(kAudio0, "amix", 0.25));
    ASSERT_TRUE(f.editor.setPan(kAudio0, "amix", 0.75));
    ASSERT_TRUE(f.editor.setColorLabel(kAudio0, "amix", 2u));
    ASSERT_TRUE(f.editor.setClipName(kAudio0, "amix", "Voiceover"));
    EXPECT_DOUBLE_EQ(f.clip(kAudio0, "amix")->audio.gain, 0.25);
    EXPECT_DOUBLE_EQ(f.clip(kAudio0, "amix")->audio.pan, 0.75);
    EXPECT_EQ(f.clip(kAudio0, "amix")->colorLabel, 2u);
    EXPECT_EQ(f.clip(kAudio0, "amix")->name, "Voiceover");

    f.editor.addEffect(kAudio0, "amix", makeEffect("brightness_contrast_gamma"));
    ASSERT_EQ(f.clip(kAudio0, "amix")->effects.size(), 1u);
    f.undoStack.undo();
    EXPECT_TRUE(f.clip(kAudio0, "amix")->effects.empty());
}