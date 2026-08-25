#include <bl_timeline/clip.hpp>

namespace bl {

Duration Clip::effectiveDuration() const noexcept {
    if (speed.isIdentity()) return timelineDuration;
    int64_t effTicks = timelineDuration.ticks * speed.rateNum / speed.rateDen;
    return Duration::fromTicks(effTicks, timelineDuration.rate);
}

void to_json(nlohmann::json& j, const SourceRef& s) {
    j = {
        {"mediaItemId", s.mediaItemId},
        {"sourceIn", {{"ticks", s.sourceIn.ticks},
                      {"rate", {{"num", s.sourceIn.rate.num},
                                {"den", s.sourceIn.rate.den}}}}},
        {"sourceOut", {{"ticks", s.sourceOut.ticks},
                       {"rate", {{"num", s.sourceOut.rate.num},
                                 {"den", s.sourceOut.rate.den}}}}}
    };
}

void from_json(const nlohmann::json& j, SourceRef& s) {
    s.mediaItemId = j.at("mediaItemId").get<std::string>();
    s.sourceIn.ticks = j.at("sourceIn").at("ticks").get<int64_t>();
    s.sourceIn.rate.num = j.at("sourceIn").at("rate").at("num").get<int64_t>();
    s.sourceIn.rate.den = j.at("sourceIn").at("rate").at("den").get<int64_t>();
    s.sourceOut.ticks = j.at("sourceOut").at("ticks").get<int64_t>();
    s.sourceOut.rate.num =
        j.at("sourceOut").at("rate").at("num").get<int64_t>();
    s.sourceOut.rate.den =
        j.at("sourceOut").at("rate").at("den").get<int64_t>();
}

void to_json(nlohmann::json& j, const SpeedRemap& s) {
    j = {{"rateNum", s.rateNum},
         {"rateDen", s.rateDen},
         {"reversed", s.reversed}};
}

void from_json(const nlohmann::json& j, SpeedRemap& s) {
    s.rateNum = j.value("rateNum", 1);
    s.rateDen = j.value("rateDen", 1);
    s.reversed = j.value("reversed", false);
}

void to_json(nlohmann::json& j, const EffectInstance& e) {
    j = {{"effectId", e.effectId},
         {"enabled", e.enabled},
         {"params", e.params}};
}

void from_json(const nlohmann::json& j, EffectInstance& e) {
    e.effectId = j.at("effectId").get<std::string>();
    e.enabled = j.value("enabled", true);
    e.params = j.value("params", nlohmann::json::object());
}

void to_json(nlohmann::json& j, const AudioClipProps& a) {
    j = {{"gain", a.gain}, {"pan", a.pan}};
}

void from_json(const nlohmann::json& j, AudioClipProps& a) {
    a.gain = j.value("gain", 1.0);
    a.pan = j.value("pan", 0.0);
}

void to_json(nlohmann::json& j, const Clip& c) {
    j = {
        {"id", c.id},
        {"name", c.name},
        {"colorLabel", c.colorLabel},
        {"source", c.source},
        {"timelineStart",
         {{"ticks", c.timelineStart.ticks},
          {"rate", {{"num", c.timelineStart.rate.num},
                    {"den", c.timelineStart.rate.den}}}}},
        {"timelineDuration",
         {{"ticks", c.timelineDuration.ticks},
          {"rate", {{"num", c.timelineDuration.rate.num},
                    {"den", c.timelineDuration.rate.den}}}}},
        {"speed", c.speed},
        {"effects", c.effects},
        {"audio", c.audio}
    };
}

void from_json(const nlohmann::json& j, Clip& c) {
    c.id = j.at("id").get<std::string>();
    c.name = j.value("name", "");
    c.colorLabel = j.value("colorLabel", 0u);
    from_json(j.at("source"), c.source);
    c.timelineStart.ticks =
        j.at("timelineStart").at("ticks").get<int64_t>();
    c.timelineStart.rate.num =
        j.at("timelineStart").at("rate").at("num").get<int64_t>();
    c.timelineStart.rate.den =
        j.at("timelineStart").at("rate").at("den").get<int64_t>();
    c.timelineDuration.ticks =
        j.at("timelineDuration").at("ticks").get<int64_t>();
    c.timelineDuration.rate.num =
        j.at("timelineDuration").at("rate").at("num").get<int64_t>();
    c.timelineDuration.rate.den =
        j.at("timelineDuration").at("rate").at("den").get<int64_t>();
    if (j.contains("speed")) from_json(j.at("speed"), c.speed);
    if (j.contains("effects")) {
        const auto& arr = j.at("effects");
        c.effects.resize(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            from_json(arr[i], c.effects[i]);
        }
    }
    if (j.contains("audio")) from_json(j.at("audio"), c.audio);
}

} // namespace bl
