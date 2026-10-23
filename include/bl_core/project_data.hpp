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
    // Id of the bin folder the item lives in, or "" for the project root.
    std::string folderId;

    // Proxy support (post-v1). When a proxy is generated, these fields track it.
    // If proxyPath is empty, no proxy exists or it's not yet generated.
    std::string proxyPath;
    uint32_t proxyWidth{0};
    uint32_t proxyHeight{0};
    // Proxy generation status: "pending" / "generating" / "ready" / "failed"
    std::string proxyStatus;
    // Original source dimensions (cached from probe) to avoid re-probing
    uint32_t sourceWidth{0};
    uint32_t sourceHeight{0};
};

inline bool operator==(const MediaBinItem& a, const MediaBinItem& b) {
    return a.id == b.id && a.path == b.path && a.name == b.name &&
           a.folderId == b.folderId && a.proxyPath == b.proxyPath &&
           a.proxyWidth == b.proxyWidth && a.proxyHeight == b.proxyHeight &&
           a.proxyStatus == b.proxyStatus && a.sourceWidth == b.sourceWidth &&
           a.sourceHeight == b.sourceHeight;
}

// A user-created bin folder (collection). Folders form a tree via `parentId`
// ("" = a top-level folder directly under the project root). Folders only
// exist inside a ProjectData; they carry no media themselves — each
// MediaBinItem names its folder through `folderId`.
struct MediaBinFolder {
    std::string id;
    std::string name;
    std::string parentId;
};

struct ProjectData {
    ProjectSettings settings;
    std::vector<MediaBinItem> mediaBin;
    std::vector<MediaBinFolder> folders;
    nlohmann::json extensions = nlohmann::json::object();
};

inline bool operator==(const ProjectSettings& a, const ProjectSettings& b) {
    return a.name == b.name && a.author == b.author &&
           a.createdIso8601 == b.createdIso8601;
}

inline bool operator==(const MediaBinFolder& a, const MediaBinFolder& b) {
    return a.id == b.id && a.name == b.name && a.parentId == b.parentId;
}

} // namespace bl
