#pragma once

#include <bl_timeline/sequence_settings.hpp>
#include <bl_timeline/track.hpp>

#include <string>
#include <vector>

namespace bl {

struct Sequence {
    std::string name;
    SequenceSettings settings;
    std::vector<VideoTrack> videoTracks;
    std::vector<AudioTrack> audioTracks;
    std::vector<Marker> markers;

    VideoTrack& addVideoTrack(const std::string& name = "") {
        videoTracks.emplace_back(name, TrackKind::Video);
        return videoTracks.back();
    }

    AudioTrack& addAudioTrack(const std::string& name = "") {
        audioTracks.emplace_back(name, TrackKind::Audio);
        return audioTracks.back();
    }

    void addMarker(const Marker& m) { markers.push_back(m); }

    std::optional<Clip> insertClipInVideoTrack(size_t t, const Clip& clip,
                                               Time at) {
        if (t >= videoTracks.size()) return std::nullopt;
        return videoTracks[t].insertClip(clip, at);
    }

    std::optional<Clip> overwriteClipInVideoTrack(size_t t, const Clip& clip,
                                                  Time at) {
        if (t >= videoTracks.size()) return std::nullopt;
        return videoTracks[t].overwriteClip(clip, at);
    }

    std::optional<Clip> appendClipToVideoTrack(size_t t, const Clip& clip) {
        if (t >= videoTracks.size()) return std::nullopt;
        return videoTracks[t].appendClip(clip);
    }

    std::optional<Clip> insertClipInAudioTrack(size_t t, const Clip& clip,
                                               Time at) {
        if (t >= audioTracks.size()) return std::nullopt;
        return audioTracks[t].insertClip(clip, at);
    }

    std::optional<Clip> overwriteClipInAudioTrack(size_t t, const Clip& clip,
                                                  Time at) {
        if (t >= audioTracks.size()) return std::nullopt;
        return audioTracks[t].overwriteClip(clip, at);
    }

    std::optional<Clip> appendClipToAudioTrack(size_t t, const Clip& clip) {
        if (t >= audioTracks.size()) return std::nullopt;
        return audioTracks[t].appendClip(clip);
    }

    std::optional<Clip> setClipSpeedInVideoTrack(size_t t, const ClipId& id,
                                                 SpeedRemap speed) {
        if (t >= videoTracks.size()) return std::nullopt;
        return videoTracks[t].setClipSpeed(id, speed);
    }

    std::optional<Clip> setClipSpeedInAudioTrack(size_t t, const ClipId& id,
                                                 SpeedRemap speed) {
        if (t >= audioTracks.size()) return std::nullopt;
        return audioTracks[t].setClipSpeed(id, speed);
    }

    bool setClipKeyframeInVideoTrack(size_t t, const ClipId& id,
                                     KeyChannel channel, Time at,
                                     double value, Interpolation interp) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setClipKeyframe(id, channel, at, value, interp);
    }

    bool setClipKeyframeInAudioTrack(size_t t, const ClipId& id,
                                     KeyChannel channel, Time at,
                                     double value, Interpolation interp) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setClipKeyframe(id, channel, at, value, interp);
    }

    bool removeClipKeyframeInVideoTrack(size_t t, const ClipId& id,
                                        KeyChannel channel, Time at) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].removeClipKeyframe(id, channel, at);
    }

    bool removeClipKeyframeInAudioTrack(size_t t, const ClipId& id,
                                         KeyChannel channel, Time at) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].removeClipKeyframe(id, channel, at);
    }

    bool addTransitionInVideoTrack(size_t t, const ClipId& id,
                                    TransitionSpec spec) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].addTransition(id, std::move(spec));
    }

    bool addTransitionInAudioTrack(size_t t, const ClipId& id,
                                    TransitionSpec spec) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].addTransition(id, std::move(spec));
    }

    bool removeTransitionInVideoTrack(size_t t, const ClipId& id) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].removeTransition(id);
    }

    bool removeTransitionInAudioTrack(size_t t, const ClipId& id) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].removeTransition(id);
    }

    bool setSubtitleTextInVideoTrack(size_t t, const ClipId& id,
                                     std::string text) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setSubtitleText(id, std::move(text));
    }

    bool setSubtitleTextInAudioTrack(size_t t, const ClipId& id,
                                     std::string text) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setSubtitleText(id, std::move(text));
    }

    bool setSubtitleStyleInVideoTrack(size_t t, const ClipId& id,
                                      SubtitleStyle style) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setSubtitleStyle(id, std::move(style));
    }

    bool setSubtitleStyleInAudioTrack(size_t t, const ClipId& id,
                                      SubtitleStyle style) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setSubtitleStyle(id, std::move(style));
    }

    bool setClipNameInVideoTrack(size_t t, const ClipId& id, std::string name) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setClipName(id, std::move(name));
    }
    bool setClipNameInAudioTrack(size_t t, const ClipId& id, std::string name) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setClipName(id, std::move(name));
    }

    bool setClipColorLabelInVideoTrack(size_t t, const ClipId& id,
                                       uint32_t label) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setClipColorLabel(id, label);
    }
    bool setClipColorLabelInAudioTrack(size_t t, const ClipId& id,
                                       uint32_t label) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setClipColorLabel(id, label);
    }

    bool setClipSourceRangeInVideoTrack(size_t t, const ClipId& id,
                                        Time sourceIn, Time sourceOut) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setClipSourceRange(id, sourceIn, sourceOut);
    }
    bool setClipSourceRangeInAudioTrack(size_t t, const ClipId& id,
                                        Time sourceIn, Time sourceOut) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setClipSourceRange(id, sourceIn, sourceOut);
    }

    bool setClipGainInVideoTrack(size_t t, const ClipId& id, double gain) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setClipGain(id, gain);
    }
    bool setClipGainInAudioTrack(size_t t, const ClipId& id, double gain) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setClipGain(id, gain);
    }

    bool setClipPanInVideoTrack(size_t t, const ClipId& id, double pan) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setClipPan(id, pan);
    }
    bool setClipPanInAudioTrack(size_t t, const ClipId& id, double pan) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setClipPan(id, pan);
    }

    bool addClipEffectInVideoTrack(size_t t, const ClipId& id,
                                   EffectInstance effect) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].addClipEffect(id, std::move(effect));
    }
    bool addClipEffectInAudioTrack(size_t t, const ClipId& id,
                                   EffectInstance effect) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].addClipEffect(id, std::move(effect));
    }

    bool removeClipEffectInVideoTrack(size_t t, const ClipId& id,
                                      size_t index) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].removeClipEffect(id, index);
    }
    bool removeClipEffectInAudioTrack(size_t t, const ClipId& id,
                                      size_t index) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].removeClipEffect(id, index);
    }

    bool reorderClipEffectInVideoTrack(size_t t, const ClipId& id,
                                       size_t from, size_t to) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].reorderClipEffect(id, from, to);
    }
    bool reorderClipEffectInAudioTrack(size_t t, const ClipId& id,
                                       size_t from, size_t to) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].reorderClipEffect(id, from, to);
    }

    bool setEffectEnabledInVideoTrack(size_t t, const ClipId& id,
                                      size_t index, bool enabled) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setEffectEnabled(id, index, enabled);
    }
    bool setEffectEnabledInAudioTrack(size_t t, const ClipId& id,
                                      size_t index, bool enabled) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setEffectEnabled(id, index, enabled);
    }

    bool setEffectParamsInVideoTrack(size_t t, const ClipId& id, size_t index,
                                     nlohmann::json params) {
        if (t >= videoTracks.size()) return false;
        return videoTracks[t].setEffectParams(id, index, std::move(params));
    }
    bool setEffectParamsInAudioTrack(size_t t, const ClipId& id, size_t index,
                                     nlohmann::json params) {
        if (t >= audioTracks.size()) return false;
        return audioTracks[t].setEffectParams(id, index, std::move(params));
    }
};

void to_json(nlohmann::json& j, const Sequence& s);
void from_json(const nlohmann::json& j, Sequence& s);
void to_json(nlohmann::json& j, const SequenceSettings& s);
void from_json(const nlohmann::json& j, SequenceSettings& s);

} // namespace bl
