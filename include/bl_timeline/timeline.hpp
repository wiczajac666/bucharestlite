#pragma once

#include <bl_timeline/sequence.hpp>

#include <memory>
#include <optional>
#include <string>

namespace bl {

class TimelineSnapshot {
public:
    explicit TimelineSnapshot(Sequence seq)
        : sequence_(std::make_shared<Sequence>(std::move(seq))) {}

    const Sequence& sequence() const noexcept { return *sequence_; }

    bool operator==(const TimelineSnapshot& other) const {
        return sequence_ == other.sequence_;
    }

    bool operator!=(const TimelineSnapshot& other) const {
        return !(*this == other);
    }

private:
    std::shared_ptr<const Sequence> sequence_;
};

class Timeline {
public:
    Timeline() = default;
    explicit Timeline(Sequence seq) : sequence_(std::move(seq)) {}

    Sequence& sequence() noexcept { return sequence_; }
    const Sequence& sequence() const noexcept { return sequence_; }

    TimelineSnapshot snapshot() const {
        return TimelineSnapshot(sequence_);
    }

    bool addClipToVideoTrack(size_t trackIndex, const Clip& clip) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].addClip(clip);
    }

    bool addClipToAudioTrack(size_t trackIndex, const Clip& clip) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].addClip(clip);
    }

    bool removeClipFromVideoTrack(size_t trackIndex, const ClipId& clipId) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].removeClip(clipId);
    }

    bool removeClipFromAudioTrack(size_t trackIndex, const ClipId& clipId) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].removeClip(clipId);
    }

    bool moveClipInVideoTrack(size_t trackIndex, const ClipId& clipId,
                              Time newStart) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].moveClip(clipId, newStart);
    }

    bool moveClipInAudioTrack(size_t trackIndex, const ClipId& clipId,
                              Time newStart) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].moveClip(clipId, newStart);
    }

    std::optional<Clip> splitClipInVideoTrack(size_t trackIndex,
                                              const ClipId& clipId,
                                              Time splitPoint) {
        if (trackIndex >= sequence_.videoTracks.size()) return std::nullopt;
        return sequence_.videoTracks[trackIndex].splitClip(clipId, splitPoint);
    }

    std::optional<Clip> splitClipInAudioTrack(size_t trackIndex,
                                              const ClipId& clipId,
                                              Time splitPoint) {
        if (trackIndex >= sequence_.audioTracks.size()) return std::nullopt;
        return sequence_.audioTracks[trackIndex].splitClip(clipId, splitPoint);
    }

    void addMarker(const Marker& m) { sequence_.addMarker(m); }

    bool trimClipLeftInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                  Time newStart) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].trimClipLeft(clipId, newStart);
    }

    bool trimClipLeftInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                  Time newStart) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].trimClipLeft(clipId, newStart);
    }

    bool trimClipRightInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                   Time newEnd) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].trimClipRight(clipId, newEnd);
    }

    bool trimClipRightInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                   Time newEnd) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].trimClipRight(clipId, newEnd);
    }

    bool rippleDeleteFromVideoTrack(size_t trackIndex, const ClipId& clipId) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].rippleDelete(clipId);
    }

    bool rippleDeleteFromAudioTrack(size_t trackIndex, const ClipId& clipId) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].rippleDelete(clipId);
    }

    bool rippleTrimLeftInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                    Time newStart) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].rippleTrimLeft(clipId, newStart);
    }

    bool rippleTrimLeftInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                    Time newStart) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].rippleTrimLeft(clipId, newStart);
    }

    bool rippleTrimRightInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                     Time newEnd) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].rippleTrimRight(clipId, newEnd);
    }

    bool rippleTrimRightInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                     Time newEnd) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].rippleTrimRight(clipId, newEnd);
    }

    std::optional<Clip> insertClipInVideoTrack(size_t trackIndex,
                                               const Clip& clip, Time at) {
        if (trackIndex >= sequence_.videoTracks.size()) return std::nullopt;
        return sequence_.videoTracks[trackIndex].insertClip(clip, at);
    }

    std::optional<Clip> insertClipInAudioTrack(size_t trackIndex,
                                               const Clip& clip, Time at) {
        if (trackIndex >= sequence_.audioTracks.size()) return std::nullopt;
        return sequence_.audioTracks[trackIndex].insertClip(clip, at);
    }

    std::optional<Clip> overwriteClipInVideoTrack(size_t trackIndex,
                                                  const Clip& clip, Time at) {
        if (trackIndex >= sequence_.videoTracks.size()) return std::nullopt;
        return sequence_.videoTracks[trackIndex].overwriteClip(clip, at);
    }

    std::optional<Clip> overwriteClipInAudioTrack(size_t trackIndex,
                                                  const Clip& clip, Time at) {
        if (trackIndex >= sequence_.audioTracks.size()) return std::nullopt;
        return sequence_.audioTracks[trackIndex].overwriteClip(clip, at);
    }

    std::optional<Clip> appendClipToVideoTrack(size_t trackIndex,
                                               const Clip& clip) {
        if (trackIndex >= sequence_.videoTracks.size()) return std::nullopt;
        return sequence_.videoTracks[trackIndex].appendClip(clip);
    }

    std::optional<Clip> appendClipToAudioTrack(size_t trackIndex,
                                               const Clip& clip) {
        if (trackIndex >= sequence_.audioTracks.size()) return std::nullopt;
        return sequence_.audioTracks[trackIndex].appendClip(clip);
    }

private:
    Sequence sequence_;
};

void to_json(nlohmann::json& j, const Timeline& t);
void from_json(const nlohmann::json& j, Timeline& t);

} // namespace bl
