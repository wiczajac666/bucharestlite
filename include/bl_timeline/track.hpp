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
        return true;
    }

    bool removeClip(const ClipId& id) {
        auto idx = findClipById(id);
        if (!idx) return false;
        clips_.erase(clips_.begin() + static_cast<ptrdiff_t>(*idx));
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
        right.source.sourceIn = original.source.sourceIn + leftDur;

        auto it = clips_.begin() + static_cast<ptrdiff_t>(*idx) + 1;
        clips_.insert(it, right);
        return right;
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
};

using VideoTrack = Track<Clip>;
using AudioTrack = Track<Clip>;

void to_json(nlohmann::json& j, const VideoTrack& t);
void from_json(const nlohmann::json& j, VideoTrack& t);
void to_json(nlohmann::json& j, const Marker& m);
void from_json(const nlohmann::json& j, Marker& m);

} // namespace bl
