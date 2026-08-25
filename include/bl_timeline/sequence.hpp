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
};

void to_json(nlohmann::json& j, const Sequence& s);
void from_json(const nlohmann::json& j, Sequence& s);
void to_json(nlohmann::json& j, const SequenceSettings& s);
void from_json(const nlohmann::json& j, SequenceSettings& s);

} // namespace bl
