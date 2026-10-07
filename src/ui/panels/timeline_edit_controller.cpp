#include "panels/timeline_edit_controller.hpp"

#include <bl_core/command.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace bl::ui {

namespace {

const Rational kTimelineRate{1'000'000, 1};

using TrackT = Track<Clip>;

// Mutable counterpart of TimelineEditController::trackAt; callers must have
// bounds-checked `flat` first (ensureEditable does).
TrackT& mutableTrackAt(Sequence& seq, int flat) {
    if (flat < static_cast<int>(seq.videoTracks.size())) {
        return seq.videoTracks[static_cast<size_t>(flat)];
    }
    return seq.audioTracks[static_cast<size_t>(
        flat - static_cast<int>(seq.videoTracks.size()))];
}

} // namespace

const Clip* TimelineEditController::findClip(const Sequence& seq, int flat,
                                             const ClipId& id) const {
    for (const auto& c : trackAt(seq, flat).clips()) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const TrackT& TimelineEditController::trackAt(const Sequence& seq,
                                              int flat) const {
    if (isVideoTrack(seq, flat)) {
        return seq.videoTracks[static_cast<size_t>(flat)];
    }
    return seq.audioTracks[static_cast<size_t>(kindIndex(seq, flat))];
}

bool TimelineEditController::canPlaceAt(const Sequence& seq, int flatTrack,
                                        const Clip& clip, Time newStart) const {
    const TimeRange candidate{newStart, clip.effectiveDuration()};
    for (const auto& existing : trackAt(seq, flatTrack).clips()) {
        if (existing.id == clip.id) continue;
        const TimeRange other{existing.timelineStart, existing.timelineDuration};
        if (other.overlaps(candidate)) return false;
    }
    return true;
}

bool TimelineEditController::validTrimLeft(const Sequence& seq, int flatTrack,
                                           const ClipId& id, Time newStart,
                                           bool ripple) const {
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (newStart >= clip->timelineStart + clip->timelineDuration) return false;
    if (newStart == clip->timelineStart) return false;
    if (ripple) return true;
    // Gap-aware: growing left must not collide with the previous clip.
    if (newStart < clip->timelineStart) {
        Time prevEnd{};
        for (const auto& c : trackAt(seq, flatTrack).clips()) {
            if (c.id == id) continue;
            const Time end = c.timelineStart + c.timelineDuration;
            if (c.timelineStart <= clip->timelineStart && end > prevEnd) {
                prevEnd = end;
            }
        }
        if (prevEnd > newStart) return false;
    }
    return true;
}

bool TimelineEditController::validTrimRight(const Sequence& seq, int flatTrack,
                                            const ClipId& id, Time newEnd,
                                            bool ripple) const {
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (newEnd <= clip->timelineStart) return false;
    if (newEnd == clip->timelineStart + clip->timelineDuration) return false;
    if (ripple) return true;
    // Gap-aware: growing right must not collide with the next clip.
    if (newEnd > clip->timelineStart + clip->timelineDuration) {
        std::optional<Time> nextStart;
        for (const auto& c : trackAt(seq, flatTrack).clips()) {
            if (c.id == id) continue;
            if (c.timelineStart > clip->timelineStart &&
                (!nextStart || c.timelineStart < *nextStart)) {
                nextStart = c.timelineStart;
            }
        }
        if (nextStart && newEnd > *nextStart) return false;
    }
    return true;
}

Time TimelineEditController::snapToFrame(const Sequence& seq, Time t) const {
    const Rational fps = seq.settings.fps;
    const double frames = t.toSeconds() * static_cast<double>(fps.num) /
                          static_cast<double>(fps.den);
    const int64_t frame = static_cast<int64_t>(std::llround(frames));
    return Time::fromFrameAt(std::max<int64_t>(frame, 0), fps, kTimelineRate);
}

Time TimelineEditController::snapToNearest(const Sequence& seq, Time t,
                                           int maxDistanceFrames,
                                           const ClipId& dragging) const {
    const Rational fps = seq.settings.fps;

    auto distanceInFrames = [fps](const Time& a, const Time& b) {
        Duration d = a - b;
        if (d < Duration{}) d = -d;
        return d.toFramesAt(fps);
    };

    // Fallback: plain frame-grid rounding.
    Time best = snapToFrame(seq, t);
    if (maxDistanceFrames < 0) return best;

    // Nearest clip edge or marker within the threshold wins; the frame-grid
    // rounding above is only the fallback. (Comparing against the rounded
    // frame instead would make targets one frame away unreachable: their
    // distance rounds to 0 for any sub-frame cursor offset.)
    bool found = false;
    int64_t bestDist = 0;
    const auto consider = [&](const Time& cand, int64_t threshold) {
        const int64_t dist = distanceInFrames(cand, t);
        if (dist >= 0 && dist <= threshold && (!found || dist < bestDist)) {
            best = cand;
            bestDist = dist;
            found = true;
        }
    };

    for (const auto& track : seq.videoTracks) {
        for (const auto& clip : track.clips()) {
            if (clip.id == dragging) continue;
            consider(clip.timelineStart, maxDistanceFrames);
            consider(clip.timelineStart + clip.timelineDuration,
                     maxDistanceFrames);
        }
    }
    for (const auto& track : seq.audioTracks) {
        for (const auto& clip : track.clips()) {
            if (clip.id == dragging) continue;
            consider(clip.timelineStart, maxDistanceFrames);
            consider(clip.timelineStart + clip.timelineDuration,
                     maxDistanceFrames);
        }
    }
    for (const auto& marker : seq.markers) {
        consider(marker.position, maxDistanceFrames);
    }
    return best;
}

bool TimelineEditController::addClipAt(int flatTrack, const Clip& clip) {
    const Sequence& seq = timeline_->sequence();
    if (!valid()) return false;
    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    return video ? timeline_->addClipToVideoTrack(idx, clip)
                 : timeline_->addClipToAudioTrack(idx, clip);
}

void TimelineEditController::removeClipAt(int flatTrack, const ClipId& id) {
    const Sequence& seq = timeline_->sequence();
    if (!valid()) return;
    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    if (video) {
        timeline_->removeClipFromVideoTrack(idx, id);
    } else {
        timeline_->removeClipFromAudioTrack(idx, id);
    }
}

bool TimelineEditController::addClip(int flatTrack, Clip clip) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    const Sequence& seq = timeline_->sequence();
    if (clip.id.empty()) clip.id = "ui:" + std::to_string(++idSeed_);
    if (!canPlaceAt(seq, flatTrack, clip, clip.timelineStart)) {
        lastError_ = "That spot is occupied";
        return false;
    }

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Add clip",
        [this, flatTrack, clip] { addClipAt(flatTrack, clip); },
        [this, flatTrack, clip] { removeClipAt(flatTrack, clip.id); }));
    return true;
}

bool TimelineEditController::moveClip(int fromTrack, int toTrack,
                                      const ClipId& id, Time newStart) {
    lastError_.clear();
    if (!ensureEditable(fromTrack)) return false;
    if (!ensureEditable(toTrack)) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, fromTrack, id);
    if (!clip) {
        lastError_ = "Clip not found";
        return false;
    }
    if (fromTrack == toTrack && clip->timelineStart == newStart) {
        lastError_ = "No change";
        return false;
    }

    Clip moved = *clip;
    moved.timelineStart = newStart;
    if (!canPlaceAt(seq, toTrack, moved, newStart)) {
        lastError_ = "That spot is occupied";
        return false;
    }

    const Clip original = *clip;
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Move clip",
        [this, fromTrack, toTrack, id, moved] {
            removeClipAt(fromTrack, id);
            addClipAt(toTrack, moved);
        },
        [this, fromTrack, toTrack, id, original] {
            removeClipAt(toTrack, id);
            addClipAt(fromTrack, original);
        }));
    return true;
}

bool TimelineEditController::groupMove(const std::vector<MoveEntry>& entries) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    if (entries.empty()) {
        lastError_ = "Nothing to move";
        return false;
    }
    const Sequence& seq = timeline_->sequence();

    struct EntryData {
        MoveEntry e;
        Clip original;
        Clip moved;
    };
    std::vector<EntryData> data;
    for (const MoveEntry& e : entries) {
        if (!ensureEditable(e.fromTrack)) return false;
        if (!ensureEditable(e.toTrack)) return false;
        const Clip* clip = findClip(seq, e.fromTrack, e.id);
        if (!clip) {
            lastError_ = "Clip not found";
            return false;
        }
        Clip m = *clip;
        m.timelineStart = e.newStart;
        if (!canPlaceAt(seq, e.toTrack, m, e.newStart)) {
            lastError_ = "That spot is occupied";
            return false;
        }
        for (const auto& d : data) {
            const TimeRange other{d.moved.timelineStart, d.moved.effectiveDuration()};
            const TimeRange cand{e.newStart, m.effectiveDuration()};
            if (other.overlaps(cand)) {
                lastError_ = "Those clips would overlap";
                return false;
            }
        }
        data.push_back({e, *clip, std::move(m)});
    }

    bool anyChange = false;
    for (const auto& d : data) {
        if (d.e.fromTrack != d.e.toTrack || d.e.oldStart != d.e.newStart) {
            anyChange = true;
        }
    }
    if (!anyChange) {
        lastError_ = "No change";
        return false;
    }

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Move clips",
        [this, data] {
            for (const auto& d : data) removeClipAt(d.e.fromTrack, d.e.id);
            for (const auto& d : data) addClipAt(d.e.toTrack, d.moved);
        },
        [this, data] {
            for (const auto& d : data) removeClipAt(d.e.toTrack, d.e.id);
            for (const auto& d : data) addClipAt(d.e.fromTrack, d.original);
        }));
    return true;
}

bool TimelineEditController::trimLeft(int flatTrack, const ClipId& id,
                                      Time newStart, bool ripple) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) {
        lastError_ = "Clip not found";
        return false;
    }
    if (!validTrimLeft(seq, flatTrack, id, newStart, ripple)) {
        lastError_ = "Cannot trim here";
        return false;
    }

    const Time oldStart = clip->timelineStart;
    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Trim (left)",
        [this, video, idx, id, newStart, ripple] {
            if (video) {
                if (ripple) {
                    timeline_->rippleTrimLeftInVideoTrack(idx, id, newStart);
                } else {
                    timeline_->trimClipLeftInVideoTrack(idx, id, newStart);
                }
            } else {
                if (ripple) {
                    timeline_->rippleTrimLeftInAudioTrack(idx, id, newStart);
                } else {
                    timeline_->trimClipLeftInAudioTrack(idx, id, newStart);
                }
            }
        },
        [this, video, idx, id, oldStart, ripple] {
            if (video) {
                if (ripple) {
                    timeline_->rippleTrimLeftInVideoTrack(idx, id, oldStart);
                } else {
                    timeline_->trimClipLeftInVideoTrack(idx, id, oldStart);
                }
            } else {
                if (ripple) {
                    timeline_->rippleTrimLeftInAudioTrack(idx, id, oldStart);
                } else {
                    timeline_->trimClipLeftInAudioTrack(idx, id, oldStart);
                }
            }
        }));
    return true;
}

bool TimelineEditController::trimRight(int flatTrack, const ClipId& id,
                                       Time newEnd, bool ripple) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) {
        lastError_ = "Clip not found";
        return false;
    }
    if (!validTrimRight(seq, flatTrack, id, newEnd, ripple)) {
        lastError_ = "Cannot trim here";
        return false;
    }

    const Time oldEnd = clip->timelineStart + clip->timelineDuration;
    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Trim (right)",
        [this, video, idx, id, newEnd, ripple] {
            if (video) {
                if (ripple) {
                    timeline_->rippleTrimRightInVideoTrack(idx, id, newEnd);
                } else {
                    timeline_->trimClipRightInVideoTrack(idx, id, newEnd);
                }
            } else {
                if (ripple) {
                    timeline_->rippleTrimRightInAudioTrack(idx, id, newEnd);
                } else {
                    timeline_->trimClipRightInAudioTrack(idx, id, newEnd);
                }
            }
        },
        [this, video, idx, id, oldEnd, ripple] {
            if (video) {
                if (ripple) {
                    timeline_->rippleTrimRightInVideoTrack(idx, id, oldEnd);
                } else {
                    timeline_->trimClipRightInVideoTrack(idx, id, oldEnd);
                }
            } else {
                if (ripple) {
                    timeline_->rippleTrimRightInAudioTrack(idx, id, oldEnd);
                } else {
                    timeline_->trimClipRightInAudioTrack(idx, id, oldEnd);
                }
            }
        }));
    return true;
}

bool TimelineEditController::splitClip(int flatTrack, const ClipId& id,
                                       Time at) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) {
        lastError_ = "Clip not found";
        return false;
    }
    if (at <= clip->timelineStart ||
        at >= clip->timelineStart + clip->timelineDuration) {
        lastError_ = "Cannot split outside the clip";
        return false;
    }

    const Clip original = *clip;
    const Duration leftDur = at - clip->timelineStart;
    const Duration rightDur = clip->timelineStart + clip->timelineDuration - at;

    Clip left = original;
    left.timelineDuration = leftDur;
    left.source.sourceIn =
        advanceSourceTime(original.source.sourceIn, leftDur, original.speed);

    Clip right = original;
    right.id = "ui:" + std::to_string(++idSeed_);
    right.name = original.name + " (R)";
    right.timelineStart = at;
    right.timelineDuration = rightDur;
    right.source.sourceIn = left.source.sourceIn;

    if (original.keyframes) {
        right.keyframes = keyframeSetSuffixRebased(*original.keyframes, leftDur);
        left.keyframes = keyframeSetPrefix(*original.keyframes, leftDur);
    }

    const ClipId rightId = right.id;
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Split clip",
        [this, flatTrack, id, left, right] {
            removeClipAt(flatTrack, id);
            addClipAt(flatTrack, left);
            addClipAt(flatTrack, right);
        },
        [this, flatTrack, id, rightId, original] {
            removeClipAt(flatTrack, rightId);
            removeClipAt(flatTrack, id);
            addClipAt(flatTrack, original);
        }));
    return true;
}

bool TimelineEditController::removeClip(int flatTrack, const ClipId& id,
                                        bool ripple) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) {
        lastError_ = "Clip not found";
        return false;
    }

    const Clip original = *clip;
    if (!ripple) {
        undoStack_->push(std::make_unique<FunctionCommand>(
            "Delete clip",
            [this, flatTrack, id] { removeClipAt(flatTrack, id); },
            [this, flatTrack, original] { addClipAt(flatTrack, original); }));
        return true;
    }

    // Ripple delete: capture the following clips (they get shifted left) so
    // undo can restore them exactly.
    std::vector<Clip> followers;
    for (const auto& c : trackAt(seq, flatTrack).clips()) {
        if (c.id == id) continue;
        if (c.timelineStart >= original.timelineStart) {
            followers.push_back(c);
        }
    }

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Ripple delete",
        [this, flatTrack, id, original, followers] {
            for (const auto& f : followers) removeClipAt(flatTrack, f.id);
            removeClipAt(flatTrack, id);
            for (const auto& f : followers) {
                Clip shifted = f;
                shifted.timelineStart = f.timelineStart - original.timelineDuration;
                addClipAt(flatTrack, std::move(shifted));
            }
        },
        [this, flatTrack, id, original, followers] {
            for (const auto& f : followers) removeClipAt(flatTrack, f.id);
            addClipAt(flatTrack, original);
            for (const auto& f : followers) addClipAt(flatTrack, f);
        }));
    return true;
}

bool TimelineEditController::ensureEditable(int flat) {
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    const Sequence& seq = timeline_->sequence();
    if (flat < 0 || flat >= trackCount(seq)) {
        lastError_ = "No such track";
        return false;
    }
    if (trackAt(seq, flat).locked()) {
        lastError_ = "Track is locked";
        return false;
    }
    return true;
}

bool TimelineEditController::runSequenceEdit(
    const std::string& label, const std::function<bool(Sequence&)>& fn) {
    if (!valid()) return false;
    Sequence before = timeline_->sequence();
    if (!fn(timeline_->sequence())) return false;
    Sequence after = timeline_->sequence();
    undoStack_->push(std::make_unique<FunctionCommand>(
        label,
        [this, after] { timeline_->sequence() = after; },
        [this, before] { timeline_->sequence() = before; }));
    return true;
}

// --- Kdenlive-parity primitives ---

bool TimelineEditController::setClipEnabled(int flatTrack, const ClipId& id,
                                            bool enabled) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) {
        lastError_ = "Clip not found";
        return false;
    }
    if (clip->enabled == enabled) {
        lastError_ = "No change";
        return false;
    }
    undoStack_->push(std::make_unique<FunctionCommand>(
        enabled ? "Enable clip" : "Disable clip",
        [this, flatTrack, id, enabled] {
            mutableTrackAt(timeline_->sequence(), flatTrack)
                .setClipEnabled(id, enabled);
        },
        [this, flatTrack, id, enabled] {
            mutableTrackAt(timeline_->sequence(), flatTrack)
                .setClipEnabled(id, !enabled);
        }));
    return true;
}

bool TimelineEditController::slipClip(int flatTrack, const ClipId& id,
                                      Duration delta) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    if (delta == Duration{}) {
        lastError_ = "No change";
        return false;
    }
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) {
        lastError_ = "Clip not found";
        return false;
    }

    // Slip slides the source window in source time (speed-scaled); the
    // timeline range stays put.
    const Time newIn =
        advanceSourceTime(clip->source.sourceIn, delta, clip->speed);
    const Time newOut =
        advanceSourceTime(clip->source.sourceOut, delta, clip->speed);
    if (newIn > newOut) {
        lastError_ = "Slip crosses the source window";
        return false;
    }
    if (newIn < Time::fromTicks(0, newIn.rate)) {
        lastError_ = "Slip exceeds the start of the source";
        return false;
    }
    if (mediaDuration_) {
        if (const auto mediaDur = mediaDuration_(clip->source.mediaItemId)) {
            const Time mediaEnd =
                Time::fromTicks(mediaDur->ticks, mediaDur->rate);
            if (newOut > mediaEnd) {
                lastError_ = "Slip exceeds the end of the source";
                return false;
            }
        }
    }

    const Time oldIn = clip->source.sourceIn;
    const Time oldOut = clip->source.sourceOut;
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Slip clip",
        [this, flatTrack, id, newIn, newOut] {
            mutableTrackAt(timeline_->sequence(), flatTrack)
                .setClipSourceRange(id, newIn, newOut);
        },
        [this, flatTrack, id, oldIn, oldOut] {
            mutableTrackAt(timeline_->sequence(), flatTrack)
                .setClipSourceRange(id, oldIn, oldOut);
        }));
    return true;
}

bool TimelineEditController::rollTrim(int flatTrack, const ClipId& leftId,
                                      Time newBoundary) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    const Sequence& seq = timeline_->sequence();
    const TrackT& track = trackAt(seq, flatTrack);
    auto leftIdx = track.findClipById(leftId);
    if (!leftIdx) {
        lastError_ = "Clip not found";
        return false;
    }
    if (*leftIdx + 1 >= track.clips().size()) {
        lastError_ = "There is no clip to the right of this one";
        return false;
    }
    const Clip left = track.clips()[*leftIdx];
    const Clip right = track.clips()[*leftIdx + 1];
    const Time leftEnd = left.timelineStart + left.timelineDuration;
    const Time rightEnd = right.timelineStart + right.timelineDuration;
    if (leftEnd != right.timelineStart) {
        lastError_ = "Clips are not adjacent";
        return false;
    }
    if (newBoundary == leftEnd) {
        lastError_ = "No change";
        return false;
    }
    if (newBoundary <= left.timelineStart || newBoundary >= rightEnd) {
        lastError_ = "Roll trim would delete a clip";
        return false;
    }

    // Both source windows must still hold content after the seam moves.
    const Time newLeftOut = advanceSourceTime(
        left.source.sourceOut, newBoundary - leftEnd, left.speed);
    const Time newRightIn = advanceSourceTime(
        right.source.sourceIn, newBoundary - right.timelineStart, right.speed);
    if (newLeftOut <= left.source.sourceIn) {
        lastError_ = "Roll trim exceeds the start of the left clip's source";
        return false;
    }
    if (newRightIn >= right.source.sourceOut) {
        lastError_ = "Roll trim exceeds the end of the right clip's source";
        return false;
    }
    if (newRightIn < Time::fromTicks(0, newRightIn.rate)) {
        lastError_ = "Roll trim exceeds the start of the source";
        return false;
    }
    if (mediaDuration_) {
        if (const auto mediaDur = mediaDuration_(left.source.mediaItemId)) {
            const Time mediaEnd =
                Time::fromTicks(mediaDur->ticks, mediaDur->rate);
            if (newLeftOut > mediaEnd) {
                lastError_ =
                    "Roll trim exceeds the end of the left clip's source";
                return false;
            }
        }
    }

    // Shrink the clip that loses range first so the other's trim never sees
    // an overlap; both orders are validated above.
    const bool seamMovesLeft = newBoundary < leftEnd;
    return runSequenceEdit("Roll trim", [&](Sequence& s) {
        TrackT& tr = mutableTrackAt(s, flatTrack);
        if (seamMovesLeft) {
            if (!tr.trimClipRight(leftId, newBoundary)) return false;
            if (!tr.trimClipLeft(right.id, newBoundary)) {
                tr.trimClipRight(leftId, leftEnd); // unreachable; restore
                return false;
            }
        } else {
            if (!tr.trimClipLeft(right.id, newBoundary)) return false;
            if (!tr.trimClipRight(leftId, newBoundary)) {
                tr.trimClipLeft(right.id, right.timelineStart); // restore
                return false;
            }
        }
        return true;
    });
}

bool TimelineEditController::groupClips(
    const std::vector<std::pair<int, ClipId>>& members) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    const Sequence& seq = timeline_->sequence();
    std::vector<ClipId> ids;
    for (const auto& entry : members) {
        if (entry.first < 0 || entry.first >= trackCount(seq)) {
            lastError_ = "No such track";
            return false;
        }
        if (!findClip(seq, entry.first, entry.second)) {
            lastError_ = "Clip not found";
            return false;
        }
        if (std::find(ids.begin(), ids.end(), entry.second) == ids.end()) {
            ids.push_back(entry.second);
        }
    }
    if (ids.size() < 2) {
        lastError_ = "Select at least two clips";
        return false;
    }
    // Grouping moves nothing, so locked tracks do not block it.
    if (!runSequenceEdit("Group clips",
                         [&](Sequence& s) { return s.formGroup(ids); })) {
        lastError_ = "Clips are already grouped";
        return false;
    }
    return true;
}

bool TimelineEditController::ungroupClips(int flatTrack, const ClipId& id) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    const Sequence& seq = timeline_->sequence();
    if (flatTrack < 0 || flatTrack >= trackCount(seq)) {
        lastError_ = "No such track";
        return false;
    }
    if (!findClip(seq, flatTrack, id)) {
        lastError_ = "Clip not found";
        return false;
    }
    if (!runSequenceEdit("Ungroup",
                         [&](Sequence& s) { return s.ungroup(id); })) {
        lastError_ = "Clip is not in a group";
        return false;
    }
    return true;
}

bool TimelineEditController::splitAudioVideo(int flatTrack, const ClipId& id,
                                             int audioFlat) {
    lastError_.clear();
    if (!ensureEditable(flatTrack)) return false;
    if (!ensureEditable(audioFlat)) return false;
    const Sequence& seq = timeline_->sequence();
    if (!isVideoTrack(seq, flatTrack)) {
        lastError_ = "Not a video track";
        return false;
    }
    if (isVideoTrack(seq, audioFlat)) {
        lastError_ = "Not an audio track";
        return false;
    }
    const Clip* found = findClip(seq, flatTrack, id);
    if (!found) {
        lastError_ = "Clip not found";
        return false;
    }
    if (!found->hasVideo) {
        lastError_ = "Clip has no video stream";
        return false;
    }
    if (!found->hasAudio) {
        lastError_ = "Clip has no audio to split off";
        return false;
    }

    // Copy everything now: `found` points into the live sequence, which the
    // edit below mutates (and invalidates).
    const Clip original = *found;
    Clip videoHalf = original;
    videoHalf.hasAudio = false;
    Clip audioHalf = original;
    audioHalf.id = ClipId("ui:" + std::to_string(++idSeed_));
    audioHalf.hasVideo = false;
    if (!canPlaceAt(seq, audioFlat, audioHalf, original.timelineStart)) {
        lastError_ = "That spot is occupied";
        return false;
    }

    const ClipId videoId = videoHalf.id;
    const ClipId audioId = audioHalf.id;
    return runSequenceEdit("Split audio/video", [&](Sequence& s) {
        mutableTrackAt(s, flatTrack).removeClip(videoId);
        if (!mutableTrackAt(s, flatTrack).addClip(videoHalf)) {
            mutableTrackAt(s, flatTrack).addClip(original); // restore
            return false;
        }
        if (!mutableTrackAt(s, audioFlat).addClip(audioHalf)) {
            mutableTrackAt(s, flatTrack).removeClip(videoId);
            mutableTrackAt(s, flatTrack).addClip(original); // restore
            return false;
        }
        // Link the halves (merges into an existing group when the clip was
        // already grouped). Both ids are live, so this cannot fail; the
        // split itself stands either way.
        s.formGroup({videoId, audioId});
        return true;
    });
}

bool TimelineEditController::addTimelineMarker(const Marker& m) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    if (timeline_->sequence().findMarkerAt(m.position)) {
        lastError_ = "A marker already exists there";
        return false;
    }
    runSequenceEdit("Add marker", [&](Sequence& s) {
        s.addMarker(m);
        return true;
    });
    return true;
}

bool TimelineEditController::editTimelineMarker(Time position,
                                                const Marker& revised) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    const Sequence& seq = timeline_->sequence();
    if (!seq.findMarkerAt(position)) {
        lastError_ = "No marker at that position";
        return false;
    }
    if (revised.position != position && seq.findMarkerAt(revised.position)) {
        lastError_ = "A marker already exists there";
        return false;
    }
    if (!runSequenceEdit("Edit marker", [&](Sequence& s) {
            auto idx = s.findMarkerAt(position);
            if (!idx) return false;
            return s.setMarkerAt(*idx, revised);
        })) {
        lastError_ = "Could not edit marker";
        return false;
    }
    return true;
}

bool TimelineEditController::removeTimelineMarker(Time position) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    if (!timeline_->sequence().findMarkerAt(position)) {
        lastError_ = "No marker at that position";
        return false;
    }
    if (!runSequenceEdit("Remove marker", [&](Sequence& s) {
            auto idx = s.findMarkerAt(position);
            if (!idx) return false;
            return s.removeMarkerAt(*idx);
        })) {
        lastError_ = "Could not remove marker";
        return false;
    }
    return true;
}

bool TimelineEditController::insertTrack(bool video, size_t index,
                                         std::string name) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    if (!runSequenceEdit(video ? "Add video track" : "Add audio track",
                         [&](Sequence& s) {
                             return video ? s.insertVideoTrack(index, name)
                                          : s.insertAudioTrack(index, name);
                         })) {
        lastError_ = "No such position";
        return false;
    }
    return true;
}

bool TimelineEditController::removeTrack(bool video, size_t index,
                                         bool force) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    Sequence& seq = timeline_->sequence();
    const auto& tracks = video ? seq.videoTracks : seq.audioTracks;
    if (index >= tracks.size()) {
        lastError_ = "No such track";
        return false;
    }
    if (tracks[index].locked()) {
        lastError_ = "Track is locked";
        return false;
    }
    if (video && tracks.size() == 1) {
        lastError_ = "Cannot remove the last video track";
        return false;
    }
    if (!force && !tracks[index].clips().empty()) {
        lastError_ = "Track still has clips";
        return false;
    }
    if (!runSequenceEdit("Remove track", [&](Sequence& s) {
            return video ? s.removeVideoTrack(index) : s.removeAudioTrack(index);
        })) {
        lastError_ = "Could not remove track";
        return false;
    }
    return true;
}

bool TimelineEditController::moveTrack(bool video, size_t from, size_t to) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    if (from == to) {
        lastError_ = "No change";
        return false;
    }
    if (!runSequenceEdit("Move track", [&](Sequence& s) {
            return video ? s.moveVideoTrack(from, to) : s.moveAudioTrack(from, to);
        })) {
        lastError_ = "Cannot move track";
        return false;
    }
    return true;
}

bool TimelineEditController::liftZone(Time start, Duration dur) {
    return zoneEdit(start, dur, /*extract=*/false);
}

bool TimelineEditController::extractZone(Time start, Duration dur) {
    return zoneEdit(start, dur, /*extract=*/true);
}

bool TimelineEditController::zoneEdit(Time start, Duration dur, bool extract) {
    lastError_.clear();
    if (!valid()) {
        lastError_ = "No timeline";
        return false;
    }
    if (dur <= Duration{}) {
        lastError_ = "Empty zone";
        return false;
    }
    if (start.ticks < 0) {
        lastError_ = "Invalid zone";
        return false;
    }
    const Time zoneEnd = start + dur;

    bool changed = false;
    const bool ok = runSequenceEdit(extract ? "Extract zone" : "Lift zone",
                                    [&](Sequence& s) {
        changed = false;
        const auto liftIn = [&](auto& tracks) {
            for (auto& track : tracks) {
                if (track.locked()) continue;
                if (liftRangeInTrack(track, start, zoneEnd)) changed = true;
            }
        };
        liftIn(s.videoTracks);
        liftIn(s.audioTracks);
        if (!changed) return false;
        if (extract) {
            const auto shiftIn = [&](auto& tracks) {
                for (auto& track : tracks) {
                    if (track.locked()) continue;
                    shiftClipsAfter(track, zoneEnd, dur);
                }
            };
            shiftIn(s.videoTracks);
            shiftIn(s.audioTracks);
        }
        return true;
    });
    if (!ok) {
        lastError_ = extract ? "Nothing to extract" : "Nothing to lift";
        return false;
    }
    return true;
}

bool TimelineEditController::liftRangeInTrack(TrackT& track, Time zoneStart,
                                              Time zoneEnd) {
    // Snapshot ids first: the edits below mutate the clip vector.
    std::vector<ClipId> ids;
    ids.reserve(track.clips().size());
    for (const auto& c : track.clips()) ids.push_back(c.id);

    bool changed = false;
    for (const ClipId& id : ids) {
        auto idx = track.findClipById(id);
        if (!idx) continue;
        const Clip c = track.clips()[*idx];
        const Time clipStart = c.timelineStart;
        const Time clipEnd = c.timelineStart + c.timelineDuration;
        if (clipEnd <= zoneStart || clipStart >= zoneEnd) continue;
        changed = true;

        if (clipStart >= zoneStart && clipEnd <= zoneEnd) {
            track.removeClip(id); // entirely inside the zone
            continue;
        }
        if (clipStart < zoneStart && clipEnd > zoneEnd) {
            // Spans the zone: keep head and tail, drop the middle.
            Clip head = c;
            head.timelineDuration = zoneStart - clipStart;
            if (c.keyframes) {
                head.keyframes =
                    keyframeSetPrefix(*c.keyframes, zoneStart - clipStart);
            }
            Clip tail = c;
            tail.id = ClipId("ui:" + std::to_string(++idSeed_));
            tail.name = c.name + " (R)";
            tail.timelineStart = zoneEnd;
            tail.timelineDuration = clipEnd - zoneEnd;
            tail.source.sourceIn = advanceSourceTime(
                c.source.sourceIn, zoneEnd - clipStart, c.speed);
            if (c.keyframes) {
                tail.keyframes = keyframeSetSuffixRebased(
                    *c.keyframes, zoneEnd - clipStart);
            }
            track.removeClip(id);
            track.addClip(head);
            track.addClip(tail);
            continue;
        }
        const bool endsInZone = clipStart < zoneStart;
        Clip survivor = c;
        if (endsInZone) {
            survivor.timelineDuration = zoneStart - clipStart;
            if (c.keyframes) {
                survivor.keyframes =
                    keyframeSetPrefix(*c.keyframes, zoneStart - clipStart);
            }
        } else {
            survivor.timelineStart = zoneEnd;
            survivor.timelineDuration = clipEnd - zoneEnd;
            survivor.source.sourceIn = advanceSourceTime(
                c.source.sourceIn, zoneEnd - clipStart, c.speed);
            if (c.keyframes) {
                survivor.keyframes = keyframeSetSuffixRebased(
                    *c.keyframes, zoneEnd - clipStart);
            }
        }
        track.removeClip(id);
        track.addClip(survivor);
    }
    return changed;
}

void TimelineEditController::shiftClipsAfter(TrackT& track, Time from,
                                             Duration by) {
    std::vector<Clip> moving;
    for (const auto& c : track.clips()) {
        if (c.timelineStart >= from) moving.push_back(c);
    }
    if (moving.empty()) return;
    for (const auto& c : moving) track.removeClip(c.id);
    for (Clip c : moving) {
        c.timelineStart = c.timelineStart - by;
        track.addClip(c);
    }
}

} // namespace bl::ui