#include <bl_timeline/sequence.hpp>

#include <algorithm>
#include <utility>

namespace bl {

void to_json(nlohmann::json& j, const SequenceSettings& s) {
    j = {
        {"width", s.width},
        {"height", s.height},
        {"fps",
         {{"num", s.fps.num}, {"den", s.fps.den}}},
        {"sampleRate", s.sampleRate},
        {"channelLayout", s.channelLayout},
        {"masterGain", s.masterGain},
        {"masterPan", s.masterPan}
    };
}

void from_json(const nlohmann::json& j, SequenceSettings& s) {
    s.width = j.value("width", 1920);
    s.height = j.value("height", 1080);
    s.fps.num = j.at("fps").at("num").get<int64_t>();
    s.fps.den = j.at("fps").at("den").get<int64_t>();
    s.sampleRate = j.value("sampleRate", 48000);
    s.channelLayout = j.value("channelLayout", 2);
    s.masterGain = j.value("masterGain", 1.0);
    s.masterPan = j.value("masterPan", 0.0);
}

void to_json(nlohmann::json& j, const Sequence& s) {
    j = {
        {"name", s.name},
        {"settings", s.settings},
        {"videoTracks", s.videoTracks},
        {"audioTracks", s.audioTracks},
        {"markers", s.markers},
        {"groups", s.groups}
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
    // Old projects predate groups: default to none.
    s.groups =
        j.value("groups", std::vector<std::vector<ClipId>>{});
}

// --- Markers ---

std::optional<size_t> Sequence::findMarkerAt(const Time& position) const {
    for (size_t i = 0; i < markers.size(); ++i) {
        if (markers[i].position == position) return i;
    }
    return std::nullopt;
}

bool Sequence::removeMarkerAt(size_t index) {
    if (index >= markers.size()) return false;
    markers.erase(markers.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

bool Sequence::setMarkerAt(size_t index, const Marker& revised) {
    if (index >= markers.size()) return false;
    markers[index] = revised;
    return true;
}

// --- Clip groups ---

bool Sequence::containsClip(const ClipId& id) const {
    const auto scan = [&id](const auto& tracks) {
        for (const auto& track : tracks) {
            if (track.findClipById(id)) return true;
        }
        return false;
    };
    return scan(videoTracks) || scan(audioTracks);
}

std::vector<ClipId> Sequence::groupMembers(const ClipId& id) const {
    for (const auto& group : groups) {
        if (std::find(group.begin(), group.end(), id) == group.end()) {
            continue;
        }
        std::vector<ClipId> out;
        for (const auto& member : group) {
            if (member != id && containsClip(member)) out.push_back(member);
        }
        return out;
    }
    return {};
}

bool Sequence::formGroup(const std::vector<ClipId>& ids) {
    std::vector<ClipId> wanted;
    for (const auto& id : ids) {
        if (id.empty() || !containsClip(id)) return false;
        if (std::find(wanted.begin(), wanted.end(), id) == wanted.end()) {
            wanted.push_back(id);
        }
    }
    if (wanted.size() < 2) return false;

    std::vector<ClipId> target = wanted;
    std::vector<std::vector<ClipId>> others;
    for (const auto& group : groups) {
        bool overlaps = false;
        for (const auto& id : group) {
            if (std::find(target.begin(), target.end(), id) != target.end()) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) {
            others.push_back(group);
            continue;
        }
        for (const auto& id : group) {
            if (std::find(target.begin(), target.end(), id) == target.end()) {
                target.push_back(id);
            }
        }
    }

    std::vector<std::vector<ClipId>> merged;
    merged.push_back(std::move(target));
    for (auto& group : others) merged.push_back(std::move(group));

    // Groups are unordered sets: compare canonically so re-forming an
    // identical group is reported as "no change".
    const auto canon = [](std::vector<std::vector<ClipId>> gs) {
        for (auto& g : gs) std::sort(g.begin(), g.end());
        std::sort(gs.begin(), gs.end());
        return gs;
    };
    if (canon(merged) == canon(groups)) return false;
    groups = std::move(merged);
    return true;
}

bool Sequence::ungroup(const ClipId& id) {
    const size_t before = groups.size();
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [&id](const std::vector<ClipId>& g) {
                                    return std::find(g.begin(), g.end(), id) !=
                                           g.end();
                                }),
                 groups.end());
    return groups.size() != before;
}

void Sequence::pruneGroups() {
    std::vector<std::vector<ClipId>> kept;
    kept.reserve(groups.size());
    for (auto& group : groups) {
        std::vector<ClipId> live;
        for (const auto& id : group) {
            if (containsClip(id)) live.push_back(id);
        }
        if (live.size() >= 2) kept.push_back(std::move(live));
    }
    groups = std::move(kept);
}

// --- Track structure ---

bool Sequence::insertVideoTrack(size_t index, std::string trackName) {
    if (index > videoTracks.size()) return false;
    videoTracks.emplace(videoTracks.begin() + static_cast<ptrdiff_t>(index),
                        std::move(trackName), TrackKind::Video);
    return true;
}

bool Sequence::insertAudioTrack(size_t index, std::string trackName) {
    if (index > audioTracks.size()) return false;
    audioTracks.emplace(audioTracks.begin() + static_cast<ptrdiff_t>(index),
                        std::move(trackName), TrackKind::Audio);
    return true;
}

bool Sequence::removeVideoTrack(size_t index) {
    if (index >= videoTracks.size()) return false;
    // A sequence always keeps at least one video track (preview/export
    // assume one exists; main_window recreates a default if it does not).
    if (videoTracks.size() == 1) return false;
    videoTracks.erase(videoTracks.begin() + static_cast<ptrdiff_t>(index));
    pruneGroups();
    return true;
}

bool Sequence::removeAudioTrack(size_t index) {
    if (index >= audioTracks.size()) return false;
    audioTracks.erase(audioTracks.begin() + static_cast<ptrdiff_t>(index));
    pruneGroups();
    return true;
}

bool Sequence::moveVideoTrack(size_t from, size_t to) {
    if (from >= videoTracks.size() || to >= videoTracks.size() || from == to) {
        return false;
    }
    VideoTrack track = std::move(videoTracks[from]);
    videoTracks.erase(videoTracks.begin() + static_cast<ptrdiff_t>(from));
    videoTracks.insert(videoTracks.begin() + static_cast<ptrdiff_t>(to),
                       std::move(track));
    return true;
}

bool Sequence::moveAudioTrack(size_t from, size_t to) {
    if (from >= audioTracks.size() || to >= audioTracks.size() || from == to) {
        return false;
    }
    AudioTrack track = std::move(audioTracks[from]);
    audioTracks.erase(audioTracks.begin() + static_cast<ptrdiff_t>(from));
    audioTracks.insert(audioTracks.begin() + static_cast<ptrdiff_t>(to),
                       std::move(track));
    return true;
}

} // namespace bl
