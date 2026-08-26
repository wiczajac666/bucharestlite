#include <bl_timeline/keyframes.hpp>

#include <cassert>
#include <cmath>
#include <functional>

namespace bl {

double KeyframeTrack::evaluate(Time t) const {
    if (samples_.empty()) return 0.0;
    if (t <= samples_.front().t) return samples_.front().value;
    if (t >= samples_.back().t) return samples_.back().value;
    size_t next = 0;
    while (next < samples_.size() && samples_[next].t <= t) {
        ++next;
    }
    const Keyframe& a = samples_[next - 1];
    const Keyframe& b = samples_[next];
    Duration span = b.t - a.t;
    if (span.ticks <= 0) return b.value;
    double u = (t - a.t).toSeconds() / span.toSeconds();
    switch (a.interpolation) {
        case Interpolation::Hold:
            return a.value;
        case Interpolation::Bezier: {
            double s = u * u * (3.0 - 2.0 * u);
            return a.value + (b.value - a.value) * s;
        }
        case Interpolation::Linear:
        default:
            return a.value + (b.value - a.value) * u;
    }
}

const KeyframeTrack* KeyframeTrackSet::track(KeyChannel channel) const {
    switch (channel) {
        case KeyChannel::Scale: return scale ? &*scale : nullptr;
        case KeyChannel::RotX: return rotX ? &*rotX : nullptr;
        case KeyChannel::RotY: return rotY ? &*rotY : nullptr;
        case KeyChannel::PosX: return posX ? &*posX : nullptr;
        case KeyChannel::PosY: return posY ? &*posY : nullptr;
        case KeyChannel::Opacity: return opacity ? &*opacity : nullptr;
        case KeyChannel::Volume: return volume ? &*volume : nullptr;
    }
    return nullptr;
}

KeyframeTrack* KeyframeTrackSet::track(KeyChannel channel) {
    return const_cast<KeyframeTrack*>(
        static_cast<const KeyframeTrackSet*>(this)->track(channel));
}

KeyframeTrack& KeyframeTrackSet::ensure(KeyChannel channel) {
    std::optional<KeyframeTrack>* slot = nullptr;
    switch (channel) {
        case KeyChannel::Scale: slot = &scale; break;
        case KeyChannel::RotX: slot = &rotX; break;
        case KeyChannel::RotY: slot = &rotY; break;
        case KeyChannel::PosX: slot = &posX; break;
        case KeyChannel::PosY: slot = &posY; break;
        case KeyChannel::Opacity: slot = &opacity; break;
        case KeyChannel::Volume: slot = &volume; break;
    }
    assert(slot != nullptr);
    if (!*slot) slot->emplace();
    return **slot;
}

bool KeyframeTrackSet::empty() const noexcept {
    for (const auto* tr : {&scale, &rotX, &rotY, &posX, &posY, &opacity,
                           &volume}) {
        if (*tr && !(*tr)->empty()) return false;
    }
    return true;
}

std::optional<double> evaluateChannel(const KeyframeTrackSet& set,
                                      KeyChannel channel, Time t) {
    const KeyframeTrack* tr = set.track(channel);
    if (!tr || tr->empty()) return std::nullopt;
    return tr->evaluate(t);
}

static void transformTracks(
    const KeyframeTrackSet& set,
    const std::function<KeyframeTrack(const KeyframeTrack&)>& fn,
    KeyframeTrackSet& out) {
    auto apply = [&](const std::optional<KeyframeTrack>& src,
                     std::optional<KeyframeTrack>& dst) {
        if (!src) return;
        KeyframeTrack mapped = fn(*src);
        if (!mapped.empty()) dst = std::move(mapped);
    };
    apply(set.scale, out.scale);
    apply(set.rotX, out.rotX);
    apply(set.rotY, out.rotY);
    apply(set.posX, out.posX);
    apply(set.posY, out.posY);
    apply(set.opacity, out.opacity);
    apply(set.volume, out.volume);
}

KeyframeTrackSet keyframeSetPrefix(const KeyframeTrackSet& set,
                                   Duration limit) {
    KeyframeTrackSet out;
    transformTracks(set,
                    [&](const KeyframeTrack& src) {
                        KeyframeTrack result;
                        for (const auto& k : src.samples()) {
                            if ((k.t).asDuration() >= limit) break;
                            result.set(k.t, k.value, k.interpolation);
                        }
                        return result;
                    },
                    out);
    return out;
}

KeyframeTrackSet keyframeSetSuffixRebased(const KeyframeTrackSet& set,
                                          Duration offset) {
    KeyframeTrackSet out;
    transformTracks(set,
                    [&](const KeyframeTrack& src) {
                        KeyframeTrack result;
                        for (const auto& k : src.samples()) {
                            Time rebased = k.t - offset;
                            if (rebased.ticks < 0) continue;
                            result.set(rebased, k.value, k.interpolation);
                        }
                        return result;
                    },
                    out);
    return out;
}

void to_json(nlohmann::json& j, Interpolation interp) {
    switch (interp) {
        case Interpolation::Hold: j = "hold"; break;
        case Interpolation::Bezier: j = "bezier"; break;
        case Interpolation::Linear:
        default: j = "linear"; break;
    }
}

void from_json(const nlohmann::json& j, Interpolation& interp) {
    std::string s = j.get<std::string>();
    if (s == "hold") interp = Interpolation::Hold;
    else if (s == "bezier") interp = Interpolation::Bezier;
    else interp = Interpolation::Linear;
}

void to_json(nlohmann::json& j, const Keyframe& k) {
    j = {{"t", {{"ticks", k.t.ticks},
                {"rate", {{"num", k.t.rate.num}, {"den", k.t.rate.den}}}}},
         {"value", k.value},
         {"interp", k.interpolation}};
}

void from_json(const nlohmann::json& j, Keyframe& k) {
    k.t.ticks = j.at("t").at("ticks").get<int64_t>();
    k.t.rate.num = j.at("t").at("rate").at("num").get<int64_t>();
    k.t.rate.den = j.at("t").at("rate").at("den").get<int64_t>();
    k.value = j.at("value").get<double>();
    if (j.contains("interp")) from_json(j.at("interp"), k.interpolation);
}

void to_json(nlohmann::json& j, const KeyframeTrack& track) {
    j = nlohmann::json::array();
    for (const auto& k : track.samples()) {
        nlohmann::json kj;
        to_json(kj, k);
        j.push_back(kj);
    }
}

void from_json(const nlohmann::json& j, KeyframeTrack& track) {
    for (const auto& kj : j) {
        Keyframe k;
        from_json(kj, k);
        track.set(k.t, k.value, k.interpolation);
    }
}

namespace {

const char* channelName(KeyChannel channel) {
    switch (channel) {
        case KeyChannel::Scale: return "scale";
        case KeyChannel::RotX: return "rotX";
        case KeyChannel::RotY: return "rotY";
        case KeyChannel::PosX: return "posX";
        case KeyChannel::PosY: return "posY";
        case KeyChannel::Opacity: return "opacity";
        case KeyChannel::Volume: return "volume";
    }
    return "";
}

struct ChannelEntry {
    KeyChannel channel;
    const std::optional<KeyframeTrack>* slot;
};

} // namespace

void to_json(nlohmann::json& j, const KeyframeTrackSet& set) {
    j = nlohmann::json::object();
    for (int i = 0; i < 7; ++i) {
        auto channel = static_cast<KeyChannel>(i);
        const KeyframeTrack* tr = set.track(channel);
        if (!tr || tr->empty()) continue;
        nlohmann::json tj;
        to_json(tj, *tr);
        j[channelName(channel)] = tj;
    }
}

void from_json(const nlohmann::json& j, KeyframeTrackSet& set) {
    for (int i = 0; i < 7; ++i) {
        auto channel = static_cast<KeyChannel>(i);
        if (!j.contains(channelName(channel))) continue;
        from_json(j.at(channelName(channel)), set.ensure(channel));
    }
}

} // namespace bl
