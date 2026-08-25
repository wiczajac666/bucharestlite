#include <bl_timeline/sequence.hpp>

namespace bl {

void to_json(nlohmann::json& j, const SequenceSettings& s) {
    j = {
        {"width", s.width},
        {"height", s.height},
        {"fps",
         {{"num", s.fps.num}, {"den", s.fps.den}}},
        {"sampleRate", s.sampleRate},
        {"channelLayout", s.channelLayout}
    };
}

void from_json(const nlohmann::json& j, SequenceSettings& s) {
    s.width = j.value("width", 1920);
    s.height = j.value("height", 1080);
    s.fps.num = j.at("fps").at("num").get<int64_t>();
    s.fps.den = j.at("fps").at("den").get<int64_t>();
    s.sampleRate = j.value("sampleRate", 48000);
    s.channelLayout = j.value("channelLayout", 2);
}

void to_json(nlohmann::json& j, const Sequence& s) {
    j = {
        {"name", s.name},
        {"settings", s.settings},
        {"videoTracks", s.videoTracks},
        {"audioTracks", s.audioTracks},
        {"markers", s.markers}
    };
}

void from_json(const nlohmann::json& j, Sequence& s) {
    s.name = j.value("name", "");
    from_json(j.at("settings"), s.settings);
    if (j.contains("videoTracks")) {
        const auto& arr = j.at("videoTracks");
        s.videoTracks.resize(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            from_json(arr[i], s.videoTracks[i]);
        }
    }
    if (j.contains("audioTracks")) {
        const auto& arr = j.at("audioTracks");
        s.audioTracks.resize(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            from_json(arr[i], s.audioTracks[i]);
        }
    }
    if (j.contains("markers")) {
        const auto& arr = j.at("markers");
        s.markers.resize(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            from_json(arr[i], s.markers[i]);
        }
    }
}

} // namespace bl
