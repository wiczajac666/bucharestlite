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
};

void to_json(nlohmann::json& j, const Sequence& s);
void from_json(const nlohmann::json& j, Sequence& s);
void to_json(nlohmann::json& j, const SequenceSettings& s);
void from_json(const nlohmann::json& j, SequenceSettings& s);

} // namespace bl
