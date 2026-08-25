#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace bl {

struct ProjectSettings {
    std::string name;
    std::string author;
    std::string createdIso8601;
};

struct MediaBinItem {
    std::string id;
    std::string path;
    std::string name;
};

struct ProjectData {
    ProjectSettings settings;
    std::vector<MediaBinItem> mediaBin;
    nlohmann::json extensions = nlohmann::json::object();
};

inline bool operator==(const ProjectSettings& a, const ProjectSettings& b) {
    return a.name == b.name && a.author == b.author &&
           a.createdIso8601 == b.createdIso8601;
}

inline bool operator==(const MediaBinItem& a, const MediaBinItem& b) {
    return a.id == b.id && a.path == b.path && a.name == b.name;
}

} // namespace bl
