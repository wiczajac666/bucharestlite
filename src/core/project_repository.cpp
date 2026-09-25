#include <bl_core/project_repository.hpp>

#include <bl_core/logger.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

namespace bl {

namespace fs = std::filesystem;

namespace {

using json = nlohmann::json;

json mediaItemToJson(const MediaBinItem& item,
                     const fs::path& projectDir) {
    json j;
    j["id"] = item.id;
    j["name"] = item.name;

    std::error_code ec;
    const fs::path absolute = fs::weakly_canonical(item.path, ec);
    if (!ec && !projectDir.empty()) {
        const fs::path relative = fs::relative(absolute, projectDir, ec);
        if (!ec && !relative.empty() &&
            relative.generic_string().find("..") == std::string::npos) {
            j["path"] = relative.generic_string();
            return j;
        }
    }
    j["path"] = item.path;
    return j;
}

std::string resolveMediaPath(const std::string& stored,
                             const fs::path& projectDir) {
    if (stored.empty()) return stored;
    if (fs::path(stored).is_absolute()) return stored;
    if (projectDir.empty()) return stored;
    return (projectDir / stored).generic_string();
}

json settingsToJson(const ProjectSettings& s) {
    json j;
    j["name"] = s.name;
    j["author"] = s.author;
    j["created"] = s.createdIso8601;
    return j;
}

json buildDocument(const ProjectData& data,
                   const std::string& projectFilePath) {
    const fs::path projectDir =
        projectFilePath.empty()
            ? fs::path()
            : fs::absolute(projectFilePath).parent_path();

    json doc;
    doc["schemaVersion"] = ProjectRepository::kSchemaVersion;
    doc["settings"] = settingsToJson(data.settings);

    json bin = json::array();
    for (const auto& item : data.mediaBin) {
        bin.push_back(mediaItemToJson(item, projectDir));
    }
    doc["mediaBin"] = std::move(bin);
    doc["extensions"] = data.extensions.is_null()
                            ? json::object()
                            : data.extensions;
    return doc;
}

ProjectData parseDocument(const json& doc, const fs::path& projectDir,
                          std::vector<std::string>& missingMedia) {
    ProjectData data;

    const json& settings = doc.at("settings");
    data.settings.name = settings.value("name", std::string());
    data.settings.author = settings.value("author", std::string());
    data.settings.createdIso8601 = settings.value("created", std::string());

    if (doc.contains("mediaBin") && doc["mediaBin"].is_array()) {
        for (const auto& entry : doc["mediaBin"]) {
            MediaBinItem item;
            item.id = entry.value("id", std::string());
            item.name = entry.value("name", std::string());
            const std::string stored = entry.value("path", std::string());
            const std::string resolved =
                resolveMediaPath(stored, projectDir);

            item.path = resolved;
            data.mediaBin.push_back(item);

            std::error_code ec;
            if (!fs::exists(resolved, ec)) {
                missingMedia.push_back(resolved);
                BL_LOG_WARN("project",
                            "media missing: " + resolved);
            }
        }
    }

    if (doc.contains("extensions")) {
        data.extensions = doc["extensions"];
        if (data.extensions.is_null()) data.extensions = json::object();
    }

    return data;
}

json migrateV0ToV1(const json& old) {
    json doc;
    doc["schemaVersion"] = 1;
    json settings;
    settings["name"] = old.value("projectName", std::string());
    settings["author"] = old.value("author", std::string());
    settings["created"] = old.value("created", std::string());
    doc["settings"] = std::move(settings);
    doc["mediaBin"] = json::array();
    doc["extensions"] = json::object();
    return doc;
}

const std::map<uint32_t, json (*)(const json&)>& migratorChain() {
    static const std::map<uint32_t, json (*)(const json&)> chain = {
        {0, &migrateV0ToV1},
    };
    return chain;
}

Result<json> normalizeDocumentVersion(json doc) {
    for (;;) {
        if (!doc.contains("schemaVersion") ||
            !doc["schemaVersion"].is_number_integer()) {
            return Result<json>::err(Err::JsonError,
                                     "document has no valid schemaVersion");
        }
        const uint32_t version = doc["schemaVersion"].get<uint32_t>();

        if (version > ProjectRepository::kSchemaVersion) {
            return Result<json>::err(
                Err::JsonError,
                "document schema v" + std::to_string(version) +
                    " is newer than supported v" +
                    std::to_string(ProjectRepository::kSchemaVersion));
        }
        if (version == ProjectRepository::kSchemaVersion) {
            return Result<json>::ok(std::move(doc));
        }

        auto it = migratorChain().find(version);
        if (it == migratorChain().end()) {
            return Result<json>::err(
                Err::JsonError,
                "no migrator for schema v" + std::to_string(version));
        }
        doc = it->second(doc);
    }
}

} // namespace

std::string ProjectRepository::toDocument(const ProjectData& data,
                                          const std::string& projectFilePath) {
    return toJson(data, projectFilePath).dump(2);
}

nlohmann::json ProjectRepository::toJson(const ProjectData& data,
                                         const std::string& projectFilePath) {
    return buildDocument(data, projectFilePath);
}

Result<LoadReport> ProjectRepository::fromDocument(
    const std::string& jsonText, const std::string& projectFilePath) {
    json doc;
    try {
        doc = json::parse(jsonText);
    } catch (const json::parse_error& e) {
        return Result<LoadReport>::err(
            Err::JsonError,
            std::string("parse error at byte ") + std::to_string(e.byte) +
                ": " + e.what());
    }

    auto normalized = normalizeDocumentVersion(std::move(doc));
    if (!normalized.ok()) {
        return Result<LoadReport>::err(normalized.code(),
                                       normalized.message());
    }

    const fs::path projectDir =
        projectFilePath.empty()
            ? fs::path()
            : fs::absolute(projectFilePath).parent_path();

    LoadReport report;
    report.project =
        parseDocument(*normalized, projectDir, report.missingMedia);
    return Result<LoadReport>::ok(std::move(report));
}

Result<void> ProjectRepository::save(const ProjectData& data,
                                     const std::string& path) const {
    if (path.empty()) {
        return Result<void>::err(Err::InvalidArgument, "empty save path");
    }

    std::error_code ec;
    const fs::path target(path);
    const fs::path parent = target.parent_path();
    if (!parent.empty() && !fs::exists(parent, ec)) {
        fs::create_directories(parent, ec);
        if (ec) {
            return Result<void>::err(Err::IoError,
                                     "cannot create directory '" +
                                         parent.string() + "'");
        }
    }

    const std::string document =
        buildDocument(data, path).dump(2);

    const fs::path temp = target.string() + ".tmp";

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Result<void>::err(Err::IoError,
                                     "cannot open temp file '" +
                                         temp.string() + "' for writing");
        }
        out.write(document.data(),
                  static_cast<std::streamsize>(document.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::remove(temp.string().c_str());
            return Result<void>::err(Err::IoError,
                                     "write failed for '" + temp.string() +
                                         "'");
        }
    }

    fs::rename(temp, target, ec);
    if (ec) {
        std::remove(temp.string().c_str());
        return Result<void>::err(Err::IoError,
                                 "atomic rename failed for '" + path +
                                     "': " + ec.message());
    }

    BL_LOG_INFO("project", "saved '" + path + "'");
    return Result<void>();
}

Result<LoadReport> ProjectRepository::load(const std::string& path) const {
    if (path.empty()) {
        return Result<LoadReport>::err(Err::InvalidArgument,
                                       "empty load path");
    }

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return Result<LoadReport>::err(Err::FileNotFound,
                                       "project file not found: " + path);
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<LoadReport>::err(Err::IoError,
                                       "cannot open '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    auto parsed = fromDocument(text, path);
    if (!parsed.ok()) {
        return parsed;
    }

    return parsed;
}

} // namespace bl