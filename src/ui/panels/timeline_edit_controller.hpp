#pragma once

#include <bl_core/time.hpp>
#include <bl_core/undo_stack.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/timeline.hpp>

#include <optional>
#include <string>
#include <vector>

namespace bl::ui {

// Qt-light editing intents against a live bl::Timeline. Every mutator is
// validated against the model BEFORE a command is pushed; invalid operations
// leave the document untouched and return false. All committed operations push
// a single FunctionCommand on the supplied UndoStack whose redo()/undo()
// re-apply the symmetric timeline primitive, so command execution is
// idempotent across redo/undo cycles.
//
// Tracks are addressed by "flat index": sequence video tracks first
// (0..V-1), then audio tracks (V..V+A-1) — matching the panel's lane layout.
class TimelineEditController {
public:
    struct MoveEntry {
        ClipId id;
        int fromTrack{0};
        int toTrack{0};
        Time oldStart{};
        Time newStart{};
    };

    TimelineEditController() = default;
    TimelineEditController(Timeline& timeline, UndoStack& undoStack)
        : timeline_(&timeline), undoStack_(&undoStack) {}

    void reset(Timeline& timeline, UndoStack& undoStack) {
        timeline_ = &timeline;
        undoStack_ = &undoStack;
    }

    bool valid() const { return timeline_ != nullptr; }

    int trackCount(const Sequence& seq) const {
        return static_cast<int>(seq.videoTracks.size() + seq.audioTracks.size());
    }

    bool isVideoTrack(const Sequence& seq, int flat) const {
        return flat >= 0 && static_cast<size_t>(flat) < seq.videoTracks.size();
    }
    int kindIndex(const Sequence& seq, int flat) const {
        return isVideoTrack(seq, flat)
                   ? flat
                   : flat - static_cast<int>(seq.videoTracks.size());
    }

    const Clip* findClip(const Sequence& seq, int flat, const ClipId& id) const;

    bool canPlaceAt(const Sequence& seq, int flatTrack, const Clip& clip,
                    Time newStart) const;
    bool validTrimLeft(const Sequence& seq, int flatTrack, const ClipId& id,
                       Time newStart, bool ripple) const;
    bool validTrimRight(const Sequence& seq, int flatTrack, const ClipId& id,
                        Time newEnd, bool ripple) const;

    // Snap helpers: nearest whole frame at the sequence framerate; then the
    // frame grid or a neighbouring clip edge when within `maxDistanceFrames`.
    Time snapToFrame(const Sequence& seq, Time t) const;
    Time snapToNearest(const Sequence& seq, Time t, int maxDistanceFrames,
                       const ClipId& dragging = ClipId()) const;

    // Mutators (all validated; push one undoable command each).
    bool moveClip(int fromTrack, int toTrack, const ClipId& id, Time newStart);
    bool groupMove(const std::vector<MoveEntry>& entries);
    // Adds a new clip, generating an id when the caller left it empty.
    bool addClip(int flatTrack, Clip clip);
    bool trimLeft(int flatTrack, const ClipId& id, Time newStart, bool ripple);
    bool trimRight(int flatTrack, const ClipId& id, Time newEnd, bool ripple);
    bool splitClip(int flatTrack, const ClipId& id, Time at);
    bool removeClip(int flatTrack, const ClipId& id, bool ripple);

private:
    using TrackT = Track<Clip>;
    const TrackT& trackAt(const Sequence& seq, int flat) const;

    bool addClipAt(int flatTrack, const Clip& clip);
    void removeClipAt(int flatTrack, const ClipId& id);

    Timeline* timeline_{nullptr};
    UndoStack* undoStack_{nullptr};
    size_t idSeed_{0};
};

} // namespace bl::ui