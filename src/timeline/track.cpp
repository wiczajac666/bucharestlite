#include <bl_timeline/track.hpp>

namespace bl {

void to_json(nlohmann::json& j, const VideoTrack& t) {
    j = {
        {"name", t.name()},
        {"kind", "video"},
        {"muted", t.muted()},
        {"soloed", t.soloed()},
        {"locked", t.locked()},
        {"gain", t.gain()},
        {"pan", t.pan()},
        {"height", t.height()},
        {"clips", t.clips()}
    };
}

void from_json(const nlohmann::json& j, VideoTrack& t) {
    t.setName(j.value("name", ""));
    t.setMuted(j.value("muted", false));
    t.setSoloed(j.value("soloed", false));
    t.setLocked(j.value("locked", false));
    t.setGain(j.value("gain", 1.0));
    t.setPan(j.value("pan", 0.0));
    t.setHeight(j.value("height", 60));
    if (j.contains("clips")) {
        const auto& arr = j.at("clips");
        for (const auto& item : arr) {
            Clip c;
            from_json(item, c);
            t.addClip(c);
        }
    }
}

void to_json(nlohmann::json& j, const Marker& m) {
    j = {{"position",
          {{"ticks", m.position.ticks},
           {"rate", {{"num", m.position.rate.num},
                     {"den", m.position.rate.den}}}}},
         {"label", m.label},
         {"color", m.color}};
}

void from_json(const nlohmann::json& j, Marker& m) {
    m.position.ticks = j.at("position").at("ticks").get<int64_t>();
    m.position.rate.num =
        j.at("position").at("rate").at("num").get<int64_t>();
    m.position.rate.den =
        j.at("position").at("rate").at("den").get<int64_t>();
    m.label = j.value("label", "");
    m.color = j.value("color", 0u);
}

} // namespace bl
