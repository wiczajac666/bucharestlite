#pragma once

#include <bl_core/undo_stack.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/timeline.hpp>

#include <cstddef>

namespace bl::ui {

// Qt-light mixer editing intents against a live bl::Timeline. Mirrors
// ClipEditController's flat-index addressing (video tracks 0..V-1 then
// audio V..V+A-1) and its validate-then-push-single-FunctionCommand model. All
// mutators validate state against the model before a command is pushed; invalid
// operations leave the document untouched and return false.
class MixerController {
public:
    MixerController() = default;
    MixerController(Timeline& timeline, UndoStack& undoStack)
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
    bool inBounds(const Sequence& seq, int flat) const {
        return flat >= 0 &&
               static_cast<size_t>(flat) <
                   seq.videoTracks.size() + seq.audioTracks.size();
    }

    // Track-level mutators (all validated; push one undoable command each).
    bool setTrackGain(int flatTrack, double gain);
    bool setTrackPan(int flatTrack, double pan);
    bool setTrackMuted(int flatTrack, bool muted);
    bool setTrackSoloed(int flatTrack, bool soloed);
    bool setTrackLocked(int flatTrack, bool locked);

    // Master-bus mutators against SequenceSettings.
    bool setMasterGain(double gain);
    bool setMasterPan(double pan);

private:
    using TrackT = Track<Clip>;
    const TrackT& trackAt(const Sequence& seq, int flat) const;
    TrackT& trackAt(Sequence& seq, int flat) const;

    Timeline* timeline_{nullptr};
    UndoStack* undoStack_{nullptr};
};

} // namespace bl::ui