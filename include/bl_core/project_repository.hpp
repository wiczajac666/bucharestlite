#pragma once

#include <bl_core/project_data.hpp>
#include <bl_core/result.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace bl {

struct LoadReport {
    ProjectData project;
    std::vector<std::string> missingMedia;
};

class ProjectRepository {
public:
    static constexpr uint32_t kSchemaVersion = 1;

    Result<void> save(const ProjectData& data, const std::string& path) const;

    Result<LoadReport> load(const std::string& path) const;

    static std::string toDocument(const ProjectData& data,
                                  const std::string& projectFilePath);
    static nlohmann::json toJson(const ProjectData& data,
                                 const std::string& projectFilePath);
    static Result<LoadReport> fromDocument(
        const std::string& jsonText, const std::string& projectFilePath);
};

} // namespace bl
