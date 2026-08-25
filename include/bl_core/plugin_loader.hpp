#pragma once

#include <bl_core/result.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

struct BlCodecPlugin;

namespace bl {

enum class PluginOrigin : unsigned char { System, User };

class PluginHandle {
public:
    PluginHandle(std::string path, PluginOrigin origin, BlCodecPlugin* plugin);
    ~PluginHandle();

    PluginHandle(PluginHandle&&) noexcept;
    PluginHandle& operator=(PluginHandle&&) noexcept;

    PluginHandle(const PluginHandle&) = delete;
    PluginHandle& operator=(const PluginHandle&) = delete;

    const BlCodecPlugin* plugin() const noexcept { return plugin_; }
    const std::string& path() const noexcept { return path_; }
    PluginOrigin origin() const noexcept { return origin_; }

private:
    struct Impl;
    Impl* impl_;
    std::string path_;
    PluginOrigin origin_;
    BlCodecPlugin* plugin_{nullptr};

    friend class PluginLoader;
};

struct SkippedPlugin {
    std::string path;
    std::string reason;
};

class PluginLoader {
public:
    struct ScanReport {
        std::vector<std::unique_ptr<PluginHandle>> plugins;
        std::vector<SkippedPlugin> skipped;
    };

    Result<std::unique_ptr<PluginHandle>> load(const std::string& path,
                                               PluginOrigin origin);

    Result<ScanReport> scanDirectory(const std::string& dir, PluginOrigin origin);

    Result<ScanReport> scanDirectories(
        const std::vector<std::pair<std::string, PluginOrigin>>& dirs);

    Result<ScanReport> scanDefaultLocations();
};

} // namespace bl
