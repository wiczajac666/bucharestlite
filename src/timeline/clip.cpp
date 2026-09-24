#include <bl_timeline/clip.hpp>

#include <cassert>
#include <cmath>

#if defined(__SIZEOF_INT128__)
#define BL_HAS_INT128 1
#else
#define BL_HAS_INT128 0
#endif

namespace bl {

#if BL_HAS_INT128

__extension__ typedef __int128 Wide;

static int64_t roundHalfEvenWide(Wide n, Wide d) {
    assert(d != 0);
    if (d < 0) { n = -n; d = -d; }
    Wide q = n / d;
    Wide r = n % d;
    if (r == 0) return static_cast<int64_t>(q);
    Wide twice = r * 2;
    if (twice > d || twice < -d)
        return static_cast<int64_t>(q + (r > 0 ? 1 : -1));
    if (q % 2 != 0)
        return static_cast<int64_t>(q + (r > 0 ? 1 : -1));
    return static_cast<int64_t>(q);
}

static Time advanceSourceTimeWide(Time sourcePos, Duration timelineDelta,
                                  SpeedRemap speed) {
    Wide num = static_cast<Wide>(timelineDelta.ticks) *
               static_cast<Wide>(timelineDelta.rate.den) *
               static_cast<Wide>(speed.rateNum) *
               static_cast<Wide>(sourcePos.rate.num);
    Wide den = static_cast<Wide>(timelineDelta.rate.num) *
               static_cast<Wide>(speed.rateDen) *
               static_cast<Wide>(sourcePos.rate.den);
    Time result = sourcePos;
    result.ticks += roundHalfEvenWide(num, den);
    return result;
}

#else

static int64_t roundHalfEvenDouble(double v) {
    double fl = std::floor(v);
    double frac = v - fl;
    if (frac > 0.5) return static_cast<int64_t>(fl + 1.0);
    if (frac < 0.5) return static_cast<int64_t>(fl);
    return (static_cast<int64_t>(fl) % 2 == 0)
               ? static_cast<int64_t>(fl)
               : static_cast<int64_t>(fl + 1.0);
}

#endif

Duration Clip::effectiveDuration() const noexcept {
    if (speed.isIdentity()) return timelineDuration;
    int64_t effTicks = timelineDuration.ticks * speed.rateNum / speed.rateDen;
    return Duration::fromTicks(effTicks, timelineDuration.rate);
}

void to_json(nlohmann::json& j, TransitionKind kind) {
    switch (kind) {
        case TransitionKind::Crossfade: j = "crossfade"; break;
        case TransitionKind::Wipe:      j = "wipe";      break;
        case TransitionKind::Dissolve:  j = "dissolve";  break;
        case TransitionKind::Slide:     j = "slide";     break;
    }
}

void from_json(const nlohmann::json& j, TransitionKind& kind) {
    std::string s = j.get<std::string>();
    if      (s == "crossfade") kind = TransitionKind::Crossfade;
    else if (s == "wipe")      kind = TransitionKind::Wipe;
    else if (s == "dissolve")  kind = TransitionKind::Dissolve;
    else                       kind = TransitionKind::Slide;
}

void to_json(nlohmann::json& j, TransitionAlignment a) {
    switch (a) {
        case TransitionAlignment::Center: j = "center"; break;
        case TransitionAlignment::Left:   j = "left";   break;
        case TransitionAlignment::Right:  j = "right";  break;
    }
}

void from_json(const nlohmann::json& j, TransitionAlignment& a) {
    std::string s = j.get<std::string>();
    if      (s == "center") a = TransitionAlignment::Center;
    else if (s == "left")   a = TransitionAlignment::Left;
    else                    a = TransitionAlignment::Right;
}

void to_json(nlohmann::json& j, const TransitionSpec& s) {
    j = {
        {"kind",      s.kind},
        {"duration",  {{"ticks", s.duration.ticks},
                       {"rate",  {{"num", s.duration.rate.num},
                                  {"den", s.duration.rate.den}}}}},
        {"alignment", s.alignment},
        {"params",    s.params}
    };
}

void from_json(const nlohmann::json& j, TransitionSpec& s) {
    from_json(j.at("kind"), s.kind);
    s.duration.ticks = j.at("duration").at("ticks").get<int64_t>();
    s.duration.rate.num = j.at("duration").at("rate").at("num").get<int64_t>();
    s.duration.rate.den = j.at("duration").at("rate").at("den").get<int64_t>();
    from_json(j.at("alignment"), s.alignment);
    s.params = j.value("params", nlohmann::json::object());
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

void to_json(nlohmann::json& j, SubtitleStyle::Position p) {
    switch (p) {
        case SubtitleStyle::Position::Bottom: j = "bottom"; break;
        case SubtitleStyle::Position::Top:    j = "top";    break;
        case SubtitleStyle::Position::Center: j = "center"; break;
    }
}

void from_json(const nlohmann::json& j, SubtitleStyle::Position& p) {
    std::string s = j.get<std::string>();
    if      (s == "top")    p = SubtitleStyle::Position::Top;
    else if (s == "center") p = SubtitleStyle::Position::Center;
    else                    p = SubtitleStyle::Position::Bottom;
}

void to_json(nlohmann::json& j, const SubtitleStyle& s) {
    j = {
        {"font",     s.font},
        {"fontSize", s.fontSize},
        {"color",    s.color},
        {"position", s.position}
    };
}

void from_json(const nlohmann::json& j, SubtitleStyle& s) {
    s.font     = j.value("font", "sans-serif");
    s.fontSize = j.value("fontSize", 24);
    s.color    = j.value("color", "#FFFFFF");
    from_json(j.at("position"), s.position);
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
    if (c.keyframes) j["keyframes"] = *c.keyframes;
    if (c.transitionOut) j["transitionOut"] = *c.transitionOut;
    if (c.subtitleText) j["subtitleText"] = *c.subtitleText;
    if (c.subtitleStyle) j["subtitleStyle"] = *c.subtitleStyle;
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
    if (j.contains("keyframes")) {
        from_json(j.at("keyframes"), c.keyframes.emplace());
    }
    if (j.contains("transitionOut")) {
        from_json(j.at("transitionOut"), c.transitionOut.emplace());
    }
    if (j.contains("effects")) {
        const auto& arr = j.at("effects");
        c.effects.resize(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            from_json(arr[i], c.effects[i]);
        }
    }
    if (j.contains("audio")) from_json(j.at("audio"), c.audio);
    if (j.contains("subtitleText")) {
        c.subtitleText = j.at("subtitleText").get<std::string>();
    }
    if (j.contains("subtitleStyle")) {
        from_json(j.at("subtitleStyle"), c.subtitleStyle.emplace());
    }
}

Time advanceSourceTime(Time sourcePos, Duration timelineDelta,
                       SpeedRemap speed) {
#if BL_HAS_INT128
    return advanceSourceTimeWide(sourcePos, timelineDelta, speed);
#else
    Time result = sourcePos;
    double tlSec = static_cast<double>(timelineDelta.ticks) *
                   static_cast<double>(timelineDelta.rate.den) /
                   static_cast<double>(timelineDelta.rate.num);
    double factor = static_cast<double>(speed.rateNum) /
                    static_cast<double>(speed.rateDen);
    double srcTicks = tlSec * factor *
                      static_cast<double>(sourcePos.rate.num) /
                      static_cast<double>(sourcePos.rate.den);
    result.ticks += roundHalfEvenDouble(srcTicks);
    return result;
#endif
}

#if BL_HAS_INT128
static Duration scaleSourceSpanToTimelineWide(Duration span,
                                              SpeedRemap speed) {
    Wide num = static_cast<Wide>(span.ticks) *
               static_cast<Wide>(speed.rateDen);
    Wide den = static_cast<Wide>(speed.rateNum);
    return Duration::fromTicks(roundHalfEvenWide(num, den), span.rate);
}
#endif

Duration retimedTimelineDuration(Duration timelineDuration, SpeedRemap from,
                                 SpeedRemap to) {
#if BL_HAS_INT128
    Wide num = static_cast<Wide>(timelineDuration.ticks) *
               static_cast<Wide>(to.rateDen) *
               static_cast<Wide>(from.rateNum);
    Wide den = static_cast<Wide>(from.rateDen) *
               static_cast<Wide>(to.rateNum);
    return Duration::fromTicks(roundHalfEvenWide(num, den),
                               timelineDuration.rate);
#else
    double factor =
        (static_cast<double>(to.rateDen) / static_cast<double>(to.rateNum)) *
        (static_cast<double>(from.rateNum) / static_cast<double>(from.rateDen));
    return Duration::fromTicks(
        roundHalfEvenDouble(static_cast<double>(timelineDuration.ticks) *
                            factor),
        timelineDuration.rate);
#endif
}

std::optional<Clip> makeThreePointClip(const SourceRef& source,
                                       SpeedRemap speed, std::string name) {
    if (!(source.sourceIn < source.sourceOut)) return std::nullopt;
    Clip clip;
    clip.name = std::move(name);
    clip.source = source;
    clip.speed = speed;
    Duration span = source.sourceOut - source.sourceIn;
#if BL_HAS_INT128
    clip.timelineDuration = scaleSourceSpanToTimelineWide(span, speed);
#else
    double factor = static_cast<double>(speed.rateDen) /
                    static_cast<double>(speed.rateNum);
    clip.timelineDuration = Duration::fromTicks(
        roundHalfEvenDouble(static_cast<double>(span.ticks) * factor),
        span.rate);
#endif
    return clip;
}

} // namespace bl
