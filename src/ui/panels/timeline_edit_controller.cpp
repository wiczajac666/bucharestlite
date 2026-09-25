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

    Time best = snapToFrame(seq, t);
    if (maxDistanceFrames < 0 || distanceInFrames(best, t) == 0) return best;

    const auto consider = [&](const Time& cand, int64_t threshold) {
        const int64_t dist = distanceInFrames(cand, t);
        if (dist >= 0 && dist <= threshold && (cand != best) &&
            dist < distanceInFrames(best, t)) {
            best = cand;
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
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    if (clip.id.empty()) clip.id = "ui:" + std::to_string(++idSeed_);
    if (!canPlaceAt(seq, flatTrack, clip, clip.timelineStart)) return false;

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Add clip",
        [this, flatTrack, clip] { addClipAt(flatTrack, clip); },
        [this, flatTrack, clip] { removeClipAt(flatTrack, clip.id); }));
    return true;
}

bool TimelineEditController::moveClip(int fromTrack, int toTrack,
                                      const ClipId& id, Time newStart) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, fromTrack, id);
    if (!clip) return false;
    if (fromTrack == toTrack && clip->timelineStart == newStart) return false;

    Clip moved = *clip;
    moved.timelineStart = newStart;
    if (!canPlaceAt(seq, toTrack, moved, newStart)) return false;

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
    if (!valid() || entries.empty()) return false;
    const Sequence& seq = timeline_->sequence();

    struct EntryData {
        MoveEntry e;
        Clip original;
        Clip moved;
    };
    std::vector<EntryData> data;
    for (const MoveEntry& e : entries) {
        const Clip* clip = findClip(seq, e.fromTrack, e.id);
        if (!clip) return false;
        Clip m = *clip;
        m.timelineStart = e.newStart;
        if (!canPlaceAt(seq, e.toTrack, m, e.newStart)) return false;
        for (const auto& d : data) {
            const TimeRange other{d.moved.timelineStart, d.moved.effectiveDuration()};
            const TimeRange cand{e.newStart, m.effectiveDuration()};
            if (other.overlaps(cand)) return false;
        }
        data.push_back({e, *clip, std::move(m)});
    }

    bool anyChange = false;
    for (const auto& d : data) {
        if (d.e.fromTrack != d.e.toTrack || d.e.oldStart != d.e.newStart) {
            anyChange = true;
        }
    }
    if (!anyChange) return false;

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
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (!validTrimLeft(seq, flatTrack, id, newStart, ripple)) return false;

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
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (!validTrimRight(seq, flatTrack, id, newEnd, ripple)) return false;

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
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (at <= clip->timelineStart ||
        at >= clip->timelineStart + clip->timelineDuration) {
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
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;

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

} // namespace bl::ui