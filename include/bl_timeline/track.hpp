#pragma once

#include <bl_timeline/clip.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace bl {

enum class TrackKind { Video, Audio };

struct Marker {
    Time position{};
    std::string label;
    uint32_t color{0};

    Marker() = default;
    Marker(Time pos, std::string lbl, uint32_t col = 0)
        : position(pos), label(std::move(lbl)), color(col) {}
};

inline bool operator==(const Marker& a, const Marker& b) {
    return a.position == b.position && a.label == b.label && a.color == b.color;
}

template <typename ClipType>
class Track {
public:
    explicit Track(std::string trackName = "",
                   TrackKind kind = TrackKind::Video)
        : name_(std::move(trackName)), kind_(kind) {}

    const std::string& name() const noexcept { return name_; }
    TrackKind kind() const noexcept { return kind_; }
    bool muted() const noexcept { return muted_; }
    bool soloed() const noexcept { return soloed_; }
    bool locked() const noexcept { return locked_; }
    int32_t height() const noexcept { return height_; }

    void setName(std::string n) { name_ = std::move(n); }
    void setMuted(bool m) { muted_ = m; }
    void setSoloed(bool s) { soloed_ = s; }
    void setLocked(bool l) { locked_ = l; }
    void setHeight(int32_t h) { height_ = h; }

    const std::vector<ClipType>& clips() const noexcept { return clips_; }

    std::optional<size_t> findClipById(const ClipId& id) const {
        for (size_t i = 0; i < clips_.size(); ++i) {
            if (clips_[i].id == id) return i;
        }
        return std::nullopt;
    }

    bool addClip(const ClipType& clip) {
        if (overlapsExisting(clip)) return false;
        auto it = std::lower_bound(
            clips_.begin(), clips_.end(), clip,
            [](const ClipType& a, const ClipType& b) {
                return a.timelineStart < b.timelineStart;
            });
        clips_.insert(it, clip);
        pruneInvalidTransitions();
        return true;
    }

    bool removeClip(const ClipId& id) {
        auto idx = findClipById(id);
        if (!idx) return false;
        clips_.erase(clips_.begin() + static_cast<ptrdiff_t>(*idx));
        pruneInvalidTransitions();
        return true;
    }

    bool moveClip(const ClipId& id, Time newStart) {
        auto idx = findClipById(id);
        if (!idx) return false;
        ClipType original = clips_[*idx];
        clips_.erase(clips_.begin() + static_cast<ptrdiff_t>(*idx));
        ClipType moved = original;
        moved.timelineStart = newStart;
        if (overlapsExisting(moved)) {
            clips_.insert(clips_.begin() + static_cast<ptrdiff_t>(*idx),
                          original);
            return false;
        }
        auto it = std::lower_bound(
            clips_.begin(), clips_.end(), moved,
            [](const ClipType& a, const ClipType& b) {
                return a.timelineStart < b.timelineStart;
            });
        clips_.insert(it, moved);
        pruneInvalidTransitions();
        return true;
    }

    std::optional<ClipType> splitClip(const ClipId& id, Time splitPoint) {
        auto idx = findClipById(id);
        if (!idx) return std::nullopt;
        ClipType& original = clips_[*idx];
        if (splitPoint <= original.timelineStart) return std::nullopt;
        Time clipEnd = original.timelineStart + original.timelineDuration;
        if (splitPoint >= clipEnd) return std::nullopt;

        ClipType right = original;
        right.id = ClipId();
        right.name = original.name + " (R)";

        Duration leftDur = splitPoint - original.timelineStart;
        Duration rightDur = clipEnd - splitPoint;

        original.timelineDuration = leftDur;
        right.timelineStart = splitPoint;
        right.timelineDuration = rightDur;
        right.source.sourceIn = advanceSourceTime(
            original.source.sourceIn, leftDur, original.speed);
        if (original.keyframes) {
            right.keyframes =
                keyframeSetSuffixRebased(*original.keyframes, leftDur);
            original.keyframes =
                keyframeSetPrefix(*original.keyframes, leftDur);
        }

        auto it = clips_.begin() + static_cast<ptrdiff_t>(*idx) + 1;
        clips_.insert(it, right);
        pruneInvalidTransitions();
        return right;
    }

    bool trimClipLeft(const ClipId& id, Time newStart) {
        auto idx = findClipById(id);
        if (!idx) return false;
        ClipType& c = clips_[*idx];
        if (newStart >= c.timelineStart + c.timelineDuration) return false;
        if (newStart > c.timelineStart) {
            Time clipEnd = c.timelineStart + c.timelineDuration;
            c.source.sourceIn = advanceSourceTime(
                c.source.sourceIn, newStart - c.timelineStart, c.speed);
            c.timelineDuration = clipEnd - newStart;
            c.timelineStart = newStart;
            pruneInvalidTransitions();
            return true;
        }
        if (newStart < c.timelineStart) {
            if (idx > 0) {
                Time prevEnd = clips_[*idx - 1].timelineStart +
                               clips_[*idx - 1].timelineDuration;
                if (newStart < prevEnd) return false;
            }
            Time clipEnd = c.timelineStart + c.timelineDuration;
            Duration growDelta = c.timelineStart - newStart;
            c.source.sourceIn = advanceSourceTime(
                c.source.sourceIn, -growDelta, c.speed);
            c.timelineStart = newStart;
            c.timelineDuration = clipEnd - newStart;
            pruneInvalidTransitions();
            return true;
        }
        return false;
    }

    bool trimClipRight(const ClipId& id, Time newEnd) {
        auto idx = findClipById(id);
        if (!idx) return false;
        ClipType& c = clips_[*idx];
        if (newEnd <= c.timelineStart) return false;
        if (*idx + 1 < clips_.size()) {
            Time nextStart = clips_[*idx + 1].timelineStart;
            if (newEnd > nextStart) return false;
        }
        Duration newDur = newEnd - c.timelineStart;
        if (newDur == c.timelineDuration) return false;
        Duration growDelta = newDur - c.timelineDuration;
        c.source.sourceOut = advanceSourceTime(
            c.source.sourceOut, growDelta, c.speed);
        c.timelineDuration = newDur;
        pruneInvalidTransitions();
        return true;
    }

    bool rippleDelete(const ClipId& id) {
        auto idx = findClipById(id);
        if (!idx) return false;
        ClipType removed = clips_[*idx];
        clips_.erase(clips_.begin() + static_cast<ptrdiff_t>(*idx));
        for (size_t i = *idx; i < clips_.size(); ++i) {
            clips_[i].timelineStart = clips_[i].timelineStart - removed.timelineDuration;
        }
        pruneInvalidTransitions();
        return true;
    }

    bool rippleTrimLeft(const ClipId& id, Time newStart) {
        auto idx = findClipById(id);
        if (!idx) return false;
        ClipType& c = clips_[*idx];
        if (newStart >= c.timelineStart + c.timelineDuration) return false;
        if (newStart == c.timelineStart) return false;
        Duration delta = newStart - c.timelineStart;
        c.source.sourceIn = advanceSourceTime(
            c.source.sourceIn, delta, c.speed);
        Time clipEnd = c.timelineStart + c.timelineDuration;
        c.timelineStart = newStart;
        c.timelineDuration = clipEnd - newStart;
        for (size_t i = 0; i < *idx; ++i) {
            clips_[i].timelineStart = clips_[i].timelineStart + delta;
        }
        pruneInvalidTransitions();
        return true;
    }

    bool rippleTrimRight(const ClipId& id, Time newEnd) {
        auto idx = findClipById(id);
        if (!idx) return false;
        ClipType& c = clips_[*idx];
        if (newEnd <= c.timelineStart) return false;
        if (newEnd == c.timelineStart + c.timelineDuration) return false;
        Duration newDur = newEnd - c.timelineStart;
        Duration delta = newDur - c.timelineDuration;
        c.source.sourceOut = advanceSourceTime(
            c.source.sourceOut, delta, c.speed);
        c.timelineDuration = newDur;
        for (size_t i = *idx + 1; i < clips_.size(); ++i) {
            clips_[i].timelineStart = clips_[i].timelineStart + delta;
        }
        pruneInvalidTransitions();
        return true;
    }

    std::optional<ClipType> insertClip(const ClipType& incoming, Time at) {
        if (incoming.timelineDuration <= Duration{}) return std::nullopt;
        size_t first = 0;
        while (first < clips_.size() && clips_[first].timelineStart < at) {
            ++first;
        }
        if (first > 0) {
            ClipType& prev = clips_[first - 1];
            Time prevEnd = prev.timelineStart + prev.timelineDuration;
            if (prevEnd > at) {
                ClipType right = prev;
                right.id = ClipId();
                right.name = prev.name + " (R)";
                right.source.sourceIn = advanceSourceTime(
                    prev.source.sourceIn, at - prev.timelineStart, prev.speed);
                right.timelineStart = at;
                right.timelineDuration = prevEnd - at;
                if (prev.keyframes) {
                    right.keyframes = keyframeSetSuffixRebased(
                        *prev.keyframes, at - prev.timelineStart);
                    prev.keyframes = keyframeSetPrefix(
                        *prev.keyframes, at - prev.timelineStart);
                }
                prev.timelineDuration = at - prev.timelineStart;
                clips_.insert(clips_.begin() + static_cast<ptrdiff_t>(first),
                              right);
            }
            // `first` still points at the split-off fragment: it belongs to
            // the pushed region and must ripple with the clips after it.
        }
        for (size_t i = first; i < clips_.size(); ++i) {
            clips_[i].timelineStart =
                clips_[i].timelineStart + incoming.timelineDuration;
        }
        ClipType placed = incoming;
        placed.timelineStart = at;
        auto it = clips_.begin() + static_cast<ptrdiff_t>(first);
        it = clips_.insert(it, placed);
        pruneInvalidTransitions();
        return *it;
    }

    std::optional<ClipType> overwriteClip(const ClipType& incoming, Time at) {
        if (incoming.timelineDuration <= Duration{}) return std::nullopt;
        Time end = at + incoming.timelineDuration;
        auto existing = findClipById(incoming.id);
        if (existing) {
            clips_.erase(clips_.begin() + static_cast<ptrdiff_t>(*existing));
        }
        for (size_t i = 0; i < clips_.size();) {
            ClipType& c = clips_[i];
            Time cStart = c.timelineStart;
            Time cEnd = cStart + c.timelineDuration;
            if (at >= cEnd || end <= cStart) {
                ++i;
                continue;
            }
            bool hasLeft = cStart < at;
            bool hasRight = end < cEnd;
            if (hasLeft && hasRight) {
                // The range lands strictly inside this clip: keep remnants
                // on both sides, matching splitClip conventions.
                ClipType rightPart = c;
                rightPart.id = ClipId();
                rightPart.name = c.name + " (R)";
                rightPart.source.sourceIn = advanceSourceTime(
                    c.source.sourceIn, end - cStart, c.speed);
                rightPart.timelineStart = end;
                rightPart.timelineDuration = cEnd - end;
                if (c.keyframes) {
                    rightPart.keyframes = keyframeSetSuffixRebased(
                        *c.keyframes, end - cStart);
                    c.keyframes = keyframeSetPrefix(*c.keyframes, at - cStart);
                }
                c.timelineDuration = at - cStart;
                c.source.sourceOut = advanceSourceTime(
                    c.source.sourceOut, at - cEnd, c.speed);
                clips_.insert(
                    clips_.begin() + static_cast<ptrdiff_t>(i) + 1,
                    rightPart);
                i += 2;
                continue;
            }
            if (hasLeft) {
                if (c.keyframes) {
                    c.keyframes =
                        keyframeSetPrefix(*c.keyframes, at - cStart);
                }
                c.timelineDuration = at - cStart;
                c.source.sourceOut = advanceSourceTime(
                    c.source.sourceOut, at - cEnd, c.speed);
            } else if (hasRight) {
                if (c.keyframes) {
                    c.keyframes = keyframeSetSuffixRebased(*c.keyframes,
                                                           end - cStart);
                }
                c.source.sourceIn = advanceSourceTime(
                    c.source.sourceIn, end - cStart, c.speed);
                c.timelineStart = end;
                c.timelineDuration = cEnd - end;
            } else {
                clips_.erase(clips_.begin() + static_cast<ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
        ClipType placed = incoming;
        placed.timelineStart = at;
        auto it = std::lower_bound(
            clips_.begin(), clips_.end(), placed,
            [](const ClipType& a, const ClipType& b) {
                return a.timelineStart < b.timelineStart;
            });
        clips_.insert(it, placed);
        pruneInvalidTransitions();
        return placed;
    }

    std::optional<ClipType> appendClip(const ClipType& incoming) {
        if (incoming.timelineDuration <= Duration{}) return std::nullopt;
        Time at{0, incoming.timelineStart.rate};
        for (const auto& c : clips_) {
            Time cEnd = c.timelineStart + c.timelineDuration;
            if (cEnd > at) at = cEnd;
        }
        return overwriteClip(incoming, at);
    }

    std::optional<ClipType> setClipSpeed(const ClipId& id, SpeedRemap speed) {
        if (speed.rateNum <= 0 || speed.rateDen <= 0) return std::nullopt;
        auto idx = findClipById(id);
        if (!idx) return std::nullopt;
        ClipType& c = clips_[*idx];
        Duration newDur =
            retimedTimelineDuration(c.timelineDuration, c.speed, speed);
        Duration delta = newDur - c.timelineDuration;
        c.timelineDuration = newDur;
        c.speed = speed;
        for (size_t i = *idx + 1; i < clips_.size(); ++i) {
            clips_[i].timelineStart = clips_[i].timelineStart + delta;
        }
        pruneInvalidTransitions();
        return c;
    }

    bool addTransition(const ClipId& id, TransitionSpec spec) {
        auto idx = findClipById(id);
        if (!idx || *idx + 1 >= clips_.size()) return false;
        Time curEnd = clips_[*idx].timelineStart + clips_[*idx].timelineDuration;
        Time nextStart = clips_[*idx + 1].timelineStart;
        if (curEnd != nextStart) return false;
        if (spec.duration <= Duration{}) return false;
        Duration minDur = std::min(clips_[*idx].timelineDuration,
                                   clips_[*idx + 1].timelineDuration);
        if (spec.duration > minDur) return false;
        clips_[*idx].transitionOut = std::move(spec);
        return true;
    }

    bool removeTransition(const ClipId& id) {
        auto idx = findClipById(id);
        if (!idx) return false;
        if (!clips_[*idx].transitionOut) return false;
        clips_[*idx].transitionOut = std::nullopt;
        return true;
    }

    std::optional<TransitionSpec> transitionAfter(const ClipId& id) const {
        auto idx = findClipById(id);
        if (!idx) return std::nullopt;
        return clips_[*idx].transitionOut;
    }

    std::optional<TransitionSpec> transitionBetween(const ClipId& idA,
                                                    const ClipId& idB) const {
        auto idxA = findClipById(idA);
        if (!idxA) return std::nullopt;
        if (*idxA + 1 >= clips_.size()) return std::nullopt;
        if (clips_[*idxA + 1].id != idB) return std::nullopt;
        return clips_[*idxA].transitionOut;
    }

    bool setSubtitleText(const ClipId& id, std::string text) {
        auto idx = findClipById(id);
        if (!idx) return false;
        clips_[*idx].subtitleText = std::move(text);
        return true;
    }

    bool setSubtitleStyle(const ClipId& id, SubtitleStyle style) {
        auto idx = findClipById(id);
        if (!idx) return false;
        clips_[*idx].subtitleStyle = std::move(style);
        return true;
    }

    std::optional<std::string> getSubtitleText(const ClipId& id) const {
        auto idx = findClipById(id);
        if (!idx) return std::nullopt;
        return clips_[*idx].subtitleText;
    }

    std::optional<SubtitleStyle> getSubtitleStyle(const ClipId& id) const {
        auto idx = findClipById(id);
        if (!idx) return std::nullopt;
        return clips_[*idx].subtitleStyle;
    }

    bool setClipKeyframe(const ClipId& id, KeyChannel channel, Time t,
                         double value, Interpolation interp) {
        auto idx = findClipById(id);
        if (!idx) return false;
        ClipType& c = clips_[*idx];
        if (!c.keyframes) c.keyframes = KeyframeTrackSet{};
        c.keyframes->ensure(channel).set(t, value, interp);
        return true;
    }

    bool removeClipKeyframe(const ClipId& id, KeyChannel channel, Time t) {
        auto idx = findClipById(id);
        if (!idx) return false;
        if (!clips_[*idx].keyframes) return false;
        KeyframeTrack* tr = clips_[*idx].keyframes->track(channel);
        if (!tr) return false;
        return tr->remove(t);
    }

    std::optional<double> evaluateClipChannel(const ClipId& id,
                                              KeyChannel channel,
                                              Time t) const {
        auto idx = findClipById(id);
        if (!idx || !clips_[*idx].keyframes) return std::nullopt;
        return evaluateChannel(*clips_[*idx].keyframes, channel, t);
    }

private:
    bool overlapsExisting(const ClipType& clip) const {
        Time clipEnd = clip.timelineStart + clip.timelineDuration;
        for (const auto& c : clips_) {
            Time cEnd = c.timelineStart + c.timelineDuration;
            if (clip.timelineStart < cEnd && clipEnd > c.timelineStart) {
                return true;
            }
        }
        return false;
    }

    std::string name_;
    TrackKind kind_{TrackKind::Video};
    bool muted_{false};
    bool soloed_{false};
    bool locked_{false};
    int32_t height_{60};
    std::vector<ClipType> clips_;

    void pruneInvalidTransitions() {
        for (size_t i = 0; i + 1 < clips_.size(); ++i) {
            ClipType& cur = clips_[i];
            if (!cur.transitionOut) continue;
            const ClipType& next = clips_[i + 1];
            Time curEnd = cur.timelineStart + cur.timelineDuration;
            if (curEnd != next.timelineStart ||
                cur.transitionOut->duration <= Duration{} ||
                cur.transitionOut->duration >
                    std::min(cur.timelineDuration, next.timelineDuration)) {
                cur.transitionOut = std::nullopt;
            }
        }
        if (!clips_.empty() && clips_.back().transitionOut) {
            clips_.back().transitionOut = std::nullopt;
        }
    }
};

using VideoTrack = Track<Clip>;
using AudioTrack = Track<Clip>;

void to_json(nlohmann::json& j, const VideoTrack& t);
void from_json(const nlohmann::json& j, VideoTrack& t);
void to_json(nlohmann::json& j, const Marker& m);
void from_json(const nlohmann::json& j, Marker& m);

} // namespace bl
