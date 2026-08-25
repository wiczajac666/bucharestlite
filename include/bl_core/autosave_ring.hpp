#pragma once

#include <bl_core/result.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

struct AutosaveEntry {
    std::string path;
    int64_t modifiedKey{0};
    uint64_t sizeBytes{0};
};

class AutosaveRing {
public:
    static constexpr size_t kDefaultCapacity = 10;

    explicit AutosaveRing(std::string projectName,
                          size_t capacity = kDefaultCapacity,
                          std::string rootOverride = {});

    const std::string& directory() const noexcept { return dir_; }
    size_t capacity() const noexcept { return capacity_; }

    Result<std::string> rotate(const nlohmann::json& document);

    Result<nlohmann::json> read(const std::string& path) const;

    std::vector<AutosaveEntry> list() const;

    Result<AutosaveEntry> newest() const;

    void removeAll();

private:
    std::string slotPath(size_t oneBasedSlot) const;

    std::string dir_;
    size_t capacity_;
};

bool isAutosaveNewerThan(const std::string& autosavePath,
                         const std::string& mainPath);

} // namespace bl
