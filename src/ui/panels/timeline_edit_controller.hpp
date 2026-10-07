#pragma once

#include <bl_core/time.hpp>
#include <bl_core/undo_stack.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/timeline.hpp>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bl::ui {

// Qt-light editing intents against a live bl::Timeline. Every mutator is
// validated against the model BEFORE a command is pushed; invalid operations
// leave the document untouched and return false. All committed operations push
// a single FunctionCommand on the supplied UndoStack whose redo()/undo()
// re-apply the symmetric timeline primitive, so command execution is
// idempotent across redo/undo cycles. Multi-effect edits (zone ops, track
// ops, A/V split, roll trim, groups) instead snapshot the whole Sequence
// before mutation and restore the captured states on undo/redo.
//
// Tracks are addressed by "flat index": sequence video tracks first
// (0..V-1), then audio tracks (V..V+A-1) — matching the panel's lane layout.
//
// Refusals are never silent: every mutator clears lastError() on entry and
// sets it to a short reason before returning false.
//
// Track locks follow Kdenlive semantics: a locked track refuses *layout*
// edits (add/move/trim/split/delete/slip/roll/enable and the A/V split,
// plus deleting the track itself). Structural track edits (insert/move a
// track), grouping, and markers are not blocked by locks; property edits
// (gain/effects via ClipEditController/MixerController) are untouched.
// Zone edits (lift/extract) skip locked tracks instead of refusing.
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
    // frame grid or a neighbouring clip edge / marker when within
    // `maxDistanceFrames`.
    Time snapToFrame(const Sequence& seq, Time t) const;
    Time snapToNearest(const Sequence& seq, Time t, int maxDistanceFrames,
                       const ClipId& dragging = ClipId()) const;

    // Reason the most recently refused mutator returned false. Cleared at the
    // start of every mutator call; empty after a successful one.
    const std::string& lastError() const noexcept { return lastError_; }

    // Optional media-duration lookup (media id -> source length) used to
    // bound slip/roll source moves. Unset: source checks degrade to
    // "the source window stays non-negative and ordered".
    using MediaDurationFn =
        std::function<std::optional<Duration>(const std::string& mediaId)>;
    void setMediaDurationProvider(MediaDurationFn fn) {
        mediaDuration_ = std::move(fn);
    }

    // Mutators (all validated; push one undoable command each).
    bool moveClip(int fromTrack, int toTrack, const ClipId& id, Time newStart);
    bool groupMove(const std::vector<MoveEntry>& entries);
    // Adds a new clip, generating an id when the caller left it empty.
    bool addClip(int flatTrack, Clip clip);
    bool trimLeft(int flatTrack, const ClipId& id, Time newStart, bool ripple);
    bool trimRight(int flatTrack, const ClipId& id, Time newEnd, bool ripple);
    bool splitClip(int flatTrack, const ClipId& id, Time at);
    bool removeClip(int flatTrack, const ClipId& id, bool ripple);

    // Kdenlive-parity primitives (P1 foundation: no UI wired yet).
    // Toggle Clip::enabled (keeps timeline space, renders nothing).
    bool setClipEnabled(int flatTrack, const ClipId& id, bool enabled);
    // Slide the clip's source window by a timeline delta; the timeline range
    // is untouched. Fails when the window would leave [0, media duration].
    bool slipClip(int flatTrack, const ClipId& id, Duration delta);
    // Move the seam shared by two adjacent clips in one atomic step; the left
    // clip's right edge and the right clip's left edge both move to
    // `newBoundary` (leftId names the clip left of the seam).
    bool rollTrim(int flatTrack, const ClipId& leftId, Time newBoundary);
    // Groups/ungroups clips (flat, id) pairs. Not blocked by track locks:
    // grouping moves nothing.
    bool groupClips(const std::vector<std::pair<int, ClipId>>& members);
    bool ungroupClips(int flatTrack, const ClipId& id);
    // Split an A/V clip: the video half stays on `flatTrack` with
    // hasAudio = false, a new audio half (hasVideo = false) is placed on
    // `audioFlat`, and both are linked via Sequence::groups.
    bool splitAudioVideo(int flatTrack, const ClipId& id, int audioFlat);

    // Markers (sequence-level; identity is the marker position).
    bool addTimelineMarker(const Marker& m);
    bool editTimelineMarker(Time position, const Marker& revised);
    bool removeTimelineMarker(Time position);

    // Track structure. removeTrack refuses a locked track and, unless
    // `force`, a track that still holds clips; it never removes the last
    // video track.
    bool insertTrack(bool video, size_t index, std::string name = "");
    bool removeTrack(bool video, size_t index, bool force);
    bool moveTrack(bool video, size_t from, size_t to);

    // Zone edits over [start, start + dur): lift clears the range keeping
    // everything outside it; extract additionally closes the gap on every
    // unlocked track. Locked tracks are skipped, not refused.
    bool liftZone(Time start, Duration dur);
    bool extractZone(Time start, Duration dur);

private:
    // Shared implementation behind liftZone()/extractZone().
    bool zoneEdit(Time start, Duration dur, bool extract);
    using TrackT = Track<Clip>;
    const TrackT& trackAt(const Sequence& seq, int flat) const;

    bool addClipAt(int flatTrack, const Clip& clip);
    void removeClipAt(int flatTrack, const ClipId& id);

    // Clears lastError_ (call first) and checks bounds + lock for `flat`.
    bool ensureEditable(int flat);
    // Runs `fn` against the live sequence inside one snapshot command:
    // captures the sequence before and after, pushes a command that
    // reassigns those states. Returns false (no command) when `fn` does.
    bool runSequenceEdit(const std::string& label,
                         const std::function<bool(Sequence&)>& fn);

    // Removes [zoneStart, zoneEnd) content from one track (splits heads and
    // tails at the zone edges). Returns true when anything changed.
    bool liftRangeInTrack(TrackT& track, Time zoneStart, Time zoneEnd);
    // Ripple helper for extract: moves every clip starting at or after `from`
    // left by `by`.
    void shiftClipsAfter(TrackT& track, Time from, Duration by);

    Timeline* timeline_{nullptr};
    UndoStack* undoStack_{nullptr};
    size_t idSeed_{0};
    std::string lastError_;
    MediaDurationFn mediaDuration_;
};

} // namespace bl::ui
