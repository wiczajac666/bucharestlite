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
};

void to_json(nlohmann::json& j, const Sequence& s);
void from_json(const nlohmann::json& j, Sequence& s);
void to_json(nlohmann::json& j, const SequenceSettings& s);
void from_json(const nlohmann::json& j, SequenceSettings& s);

} // namespace bl
