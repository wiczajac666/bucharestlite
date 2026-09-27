#pragma once

#include <bl_core/time.hpp>
#include <bl_core/undo_stack.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/keyframes.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/timeline.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace bl::ui {

// Qt-light inspector editing intents against a live bl::Timeline. Mirrors
// TimelineEditController's flat-index addressing (video tracks 0..V-1 then
// audio V..V+A-1) and its validate-then-push-single-FunctionCommand model. All
// mutators validate state against the model before a command is pushed; invalid
// operations leave the document untouched and return false.
class ClipEditController {
public:
    ClipEditController() = default;
    ClipEditController(Timeline& timeline, UndoStack& undoStack)
        : timeline_(&timeline), undoStack_(&undoStack) {}

    void reset(Timeline& timeline, UndoStack& undoStack) {
        timeline_ = &timeline;
        undoStack_ = &undoStack;
    }

    bool valid() const { return timeline_ != nullptr; }

    bool isVideoTrack(const Sequence& seq, int flat) const {
        return flat >= 0 && static_cast<size_t>(flat) < seq.videoTracks.size();
    }
    int kindIndex(const Sequence& seq, int flat) const {
        return isVideoTrack(seq, flat)
                   ? flat
                   : flat - static_cast<int>(seq.videoTracks.size());
    }
    const Clip* findClip(const Sequence& seq, int flat,
                         const ClipId& id) const;

    // Mutators (all validated; push one undoable command each).
    bool setClipName(int flatTrack, const ClipId& id, const std::string& name);
    bool setColorLabel(int flatTrack, const ClipId& id, uint32_t label);
    bool setSourceRange(int flatTrack, const ClipId& id, Time sourceIn,
                        Time sourceOut);
    bool setGain(int flatTrack, const ClipId& id, double gain);
    bool setPan(int flatTrack, const ClipId& id, double pan);
    bool setSpeed(int flatTrack, const ClipId& id, SpeedRemap speed);
    bool setSubtitleText(int flatTrack, const ClipId& id,
                         const std::string& text);

    bool addEffect(int flatTrack, const ClipId& id, const EffectInstance& effect);
    bool removeEffect(int flatTrack, const ClipId& id, size_t index);
    bool reorderEffect(int flatTrack, const ClipId& id, size_t from, size_t to);
    bool setEffectEnabled(int flatTrack, const ClipId& id, size_t index,
                          bool enabled);
    bool setEffectParams(int flatTrack, const ClipId& id, size_t index,
                         const nlohmann::json& params);

    bool setKeyframe(int flatTrack, const ClipId& id, KeyChannel channel,
                     Time t, double value, Interpolation interp);
    bool removeKeyframe(int flatTrack, const ClipId& id, KeyChannel channel,
                        Time t);

private:
    using TrackT = Track<Clip>;
    const TrackT& trackAt(const Sequence& seq, int flat) const;

    Timeline* timeline_{nullptr};
    UndoStack* undoStack_{nullptr};
};

} // namespace bl::ui