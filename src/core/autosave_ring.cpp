#include <bl_core/autosave_ring.hpp>

#include <bl_core/logger.hpp>
#include <bl_core/platform/paths.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace bl {

namespace fs = std::filesystem;

namespace {

int64_t modificationKey(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return static_cast<int64_t>(t.time_since_epoch().count());
}

uint64_t fileSizeOf(const fs::path& p) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    return ec ? 0u : static_cast<uint64_t>(size);
}

std::string fileContentsOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return std::string();
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

Result<void> writeAtomic(const fs::path& target,
                         const std::string& contents) {
    std::error_code ec;
    const fs::path temp = target.string() + ".tmp";

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Result<void>::err(Err::IoError,
                                     "cannot open '" + temp.string() +
                                         "' for writing");
        }
        out.write(contents.data(),
                  static_cast<std::streamsize>(contents.size()));
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
                                 "atomic rename failed for '" +
                                     target.string() + "': " + ec.message());
    }
    return Result<void>();
}

} // namespace

AutosaveRing::AutosaveRing(std::string projectName, size_t capacity,
                           std::string rootOverride)
    : capacity_(capacity == 0 ? kDefaultCapacity : capacity) {
    const std::string sanitized =
        projectName.empty() ? std::string("untitled") : projectName;
    const std::string root =
        rootOverride.empty() ? platform::userDataRoot() : rootOverride;
    dir_ = (fs::path(root) / "autosave" / sanitized).generic_string();
}

std::string AutosaveRing::slotPath(size_t oneBasedSlot) const {
    char name[32];
    std::snprintf(name, sizeof(name), "autosave_%02zu.blproj", oneBasedSlot);
    return (fs::path(dir_) / name).generic_string();
}

Result<std::string> AutosaveRing::rotate(const nlohmann::json& document) {
    if (!document.is_object()) {
        return Result<std::string>::err(Err::InvalidArgument,
                                        "autosave document must be an object");
    }

    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec && !fs::is_directory(dir_, ec)) {
        return Result<std::string>::err(
            Err::IoError, "cannot create autosave directory '" + dir_ + "'");
    }

    size_t chosenSlot = 0;
    int64_t oldestKey = std::numeric_limits<int64_t>::max();

    for (size_t slot = 1; slot <= capacity_; ++slot) {
        const std::string candidate = slotPath(slot);
        if (!fs::exists(candidate, ec)) {
            chosenSlot = slot;
            break;
        }
        const int64_t key = modificationKey(candidate);
        if (key < oldestKey) {
            oldestKey = key;
            chosenSlot = slot;
        }
    }

    const std::string path = slotPath(chosenSlot);
    const std::string contents = document.dump(2);

    auto written = writeAtomic(path, contents);
    if (!written.ok()) {
        return Result<std::string>::err(written.code(), written.message());
    }

    BL_LOG_DEBUG("autosave", "rotated slot " +
                                 std::to_string(chosenSlot) + " -> " + path);
    return Result<std::string>::ok(path);
}

Result<nlohmann::json> AutosaveRing::read(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<nlohmann::json>::err(Err::FileNotFound,
                                           "cannot open '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    try {
        return Result<nlohmann::json>::ok(nlohmann::json::parse(buffer.str()));
    } catch (const nlohmann::json::parse_error& e) {
        return Result<nlohmann::json>::err(
            Err::JsonError,
            std::string("corrupt autosave at byte ") +
                std::to_string(e.byte) + ": " + e.what());
    }
}

std::vector<AutosaveEntry> AutosaveRing::list() const {
    std::vector<AutosaveEntry> entries;
    std::error_code ec;
    if (!fs::is_directory(dir_, ec)) return entries;

    for (const auto& entry : fs::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!entry.path().extension().empty() &&
            entry.path().filename().string().rfind("autosave_", 0) != 0) {
            continue;
        }
        if (entry.path().extension() != ".blproj") continue;

        AutosaveEntry item;
        item.path = entry.path().generic_string();
        item.modifiedKey = modificationKey(item.path);
        item.sizeBytes = fileSizeOf(item.path);
        entries.push_back(std::move(item));
    }

    std::sort(entries.begin(), entries.end(),
              [](const AutosaveEntry& a, const AutosaveEntry& b) {
                  return a.modifiedKey > b.modifiedKey;
              });
    return entries;
}

Result<AutosaveEntry> AutosaveRing::newest() const {
    const auto entries = list();
    if (entries.empty()) {
        return Result<AutosaveEntry>::err(Err::FileNotFound,
                                          "no autosaves in '" + dir_ + "'");
    }
    return Result<AutosaveEntry>::ok(entries.front());
}

void AutosaveRing::removeAll() {
    std::error_code ec;
    if (!fs::is_directory(dir_, ec)) return;

    for (const auto& entry : fs::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (entry.path().extension() == ".blproj" ||
            entry.path().extension() == ".tmp") {
            fs::remove(entry.path(), ec);
        }
    }
}

bool isAutosaveNewerThan(const std::string& autosavePath,
                         const std::string& mainPath) {
    std::error_code ec;
    if (!fs::exists(autosavePath, ec)) return false;
    if (!fs::exists(mainPath, ec)) return true;

    const int64_t autosaveKey = modificationKey(autosavePath);
    const int64_t mainKey = modificationKey(mainPath);
    if (autosaveKey > mainKey) return true;
    if (autosaveKey < mainKey) return false;

    // Modification times landed in the same tick (NTFS caches and refreshes
    // timestamps lazily, so rapid consecutive writes frequently tie even when
    // the autosave happened after the manual save). Break the tie on content:
    // offer recovery only when the snapshot actually holds changes the saved
    // file does not.
    return fileContentsOf(autosavePath) != fileContentsOf(mainPath);
}

} // namespace bl