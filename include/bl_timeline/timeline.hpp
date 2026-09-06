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

    std::optional<Clip> setClipSpeedInVideoTrack(size_t trackIndex,
                                                 const ClipId& clipId,
                                                 SpeedRemap speed) {
        if (trackIndex >= sequence_.videoTracks.size()) return std::nullopt;
        return sequence_.videoTracks[trackIndex].setClipSpeed(clipId, speed);
    }

    std::optional<Clip> setClipSpeedInAudioTrack(size_t trackIndex,
                                                 const ClipId& clipId,
                                                 SpeedRemap speed) {
        if (trackIndex >= sequence_.audioTracks.size()) return std::nullopt;
        return sequence_.audioTracks[trackIndex].setClipSpeed(clipId, speed);
    }

    bool setClipKeyframeInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                     KeyChannel channel, Time at,
                                     double value, Interpolation interp) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setClipKeyframe(
            clipId, channel, at, value, interp);
    }

    bool setClipKeyframeInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                     KeyChannel channel, Time at,
                                     double value, Interpolation interp) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setClipKeyframe(
            clipId, channel, at, value, interp);
    }

    bool removeClipKeyframeInVideoTrack(size_t trackIndex,
                                        const ClipId& clipId,
                                        KeyChannel channel, Time at) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].removeClipKeyframe(
            clipId, channel, at);
    }

    bool removeClipKeyframeInAudioTrack(size_t trackIndex,
                                         const ClipId& clipId,
                                         KeyChannel channel, Time at) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].removeClipKeyframe(
            clipId, channel, at);
    }

    bool addTransitionInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                    TransitionSpec spec) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].addTransition(
            clipId, std::move(spec));
    }

    bool addTransitionInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                    TransitionSpec spec) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].addTransition(
            clipId, std::move(spec));
    }

    bool removeTransitionInVideoTrack(size_t trackIndex, const ClipId& clipId) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].removeTransition(clipId);
    }

    bool removeTransitionInAudioTrack(size_t trackIndex, const ClipId& clipId) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].removeTransition(clipId);
    }

    bool setSubtitleTextInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                     std::string text) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setSubtitleText(
            clipId, std::move(text));
    }

    bool setSubtitleTextInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                     std::string text) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setSubtitleText(
            clipId, std::move(text));
    }

    bool setSubtitleStyleInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                      SubtitleStyle style) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setSubtitleStyle(
            clipId, std::move(style));
    }

    bool setSubtitleStyleInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                      SubtitleStyle style) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setSubtitleStyle(
            clipId, std::move(style));
    }

    bool setClipNameInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                 std::string name) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setClipName(
            clipId, std::move(name));
    }
    bool setClipNameInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                 std::string name) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setClipName(
            clipId, std::move(name));
    }

    bool setClipColorLabelInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                       uint32_t label) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setClipColorLabel(clipId,
                                                                   label);
    }
    bool setClipColorLabelInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                       uint32_t label) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setClipColorLabel(clipId,
                                                                   label);
    }

    bool setClipSourceRangeInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                        Time sourceIn, Time sourceOut) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setClipSourceRange(
            clipId, sourceIn, sourceOut);
    }
    bool setClipSourceRangeInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                        Time sourceIn, Time sourceOut) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setClipSourceRange(
            clipId, sourceIn, sourceOut);
    }

    bool setClipGainInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                 double gain) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setClipGain(clipId, gain);
    }
    bool setClipGainInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                 double gain) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setClipGain(clipId, gain);
    }

    bool setClipPanInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                double pan) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setClipPan(clipId, pan);
    }
    bool setClipPanInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                double pan) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setClipPan(clipId, pan);
    }

    bool addClipEffectInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                   EffectInstance effect) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].addClipEffect(
            clipId, std::move(effect));
    }
    bool addClipEffectInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                   EffectInstance effect) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].addClipEffect(
            clipId, std::move(effect));
    }

    bool removeClipEffectInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                      size_t index) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].removeClipEffect(clipId,
                                                                  index);
    }
    bool removeClipEffectInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                      size_t index) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].removeClipEffect(clipId,
                                                                  index);
    }

    bool reorderClipEffectInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                       size_t from, size_t to) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].reorderClipEffect(clipId, from,
                                                                  to);
    }
    bool reorderClipEffectInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                       size_t from, size_t to) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].reorderClipEffect(clipId, from,
                                                                   to);
    }

    bool setEffectEnabledInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                      size_t index, bool enabled) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setEffectEnabled(
            clipId, index, enabled);
    }
    bool setEffectEnabledInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                      size_t index, bool enabled) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setEffectEnabled(
            clipId, index, enabled);
    }

    bool setEffectParamsInVideoTrack(size_t trackIndex, const ClipId& clipId,
                                     size_t index, nlohmann::json params) {
        if (trackIndex >= sequence_.videoTracks.size()) return false;
        return sequence_.videoTracks[trackIndex].setEffectParams(
            clipId, index, std::move(params));
    }
    bool setEffectParamsInAudioTrack(size_t trackIndex, const ClipId& clipId,
                                     size_t index, nlohmann::json params) {
        if (trackIndex >= sequence_.audioTracks.size()) return false;
        return sequence_.audioTracks[trackIndex].setEffectParams(
            clipId, index, std::move(params));
    }

private:
    Sequence sequence_;
};

void to_json(nlohmann::json& j, const Timeline& t);
void from_json(const nlohmann::json& j, Timeline& t);

} // namespace bl
