#include <bl_core/plugin_loader.hpp>

#include <bl_core/logger.hpp>
#include <bl_plugins/codec_plugin.h>
#include "platform/dynamic_library.hpp"
#include "platform/paths.hpp"
#include "plugin_validation.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <utility>

namespace bl {

namespace fs = std::filesystem;

namespace {

constexpr const char* kEntryPoint = "bl_get_codec_plugin";

bool hasLibrarySuffix(const fs::path& p) {
    return p.extension() == platform::dynamicLibrarySuffix();
}

} // namespace

struct PluginHandle::Impl {
    platform::DynamicLibrary library;
};

PluginHandle::PluginHandle(std::string path, PluginOrigin origin,
                           BlCodecPlugin* plugin)
    : impl_(new Impl()), path_(std::move(path)), origin_(origin),
      plugin_(plugin) {}

PluginHandle::~PluginHandle() { delete impl_; }

PluginHandle::PluginHandle(PluginHandle&& other) noexcept
    : impl_(other.impl_), path_(std::move(other.path_)),
      origin_(other.origin_), plugin_(other.plugin_) {
    other.impl_ = nullptr;
    other.plugin_ = nullptr;
}

PluginHandle& PluginHandle::operator=(PluginHandle&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        path_ = std::move(other.path_);
        origin_ = other.origin_;
        plugin_ = other.plugin_;
        other.impl_ = nullptr;
        other.plugin_ = nullptr;
    }
    return *this;
}

Result<std::unique_ptr<PluginHandle>> PluginLoader::load(const std::string& path,
                                                         PluginOrigin origin) {
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) {
        return Result<std::unique_ptr<PluginHandle>>::err(
            Err::FileNotFound, "plugin file not found: " + path);
    }

    auto opened = platform::DynamicLibrary::open(path);
    if (!opened.ok()) {
        return Result<std::unique_ptr<PluginHandle>>::err(opened.code(),
                                                          opened.message());
    }

    void* sym = opened.value().symbol(kEntryPoint);
    if (!sym) {
        return Result<std::unique_ptr<PluginHandle>>::err(
            Err::InvalidArgument,
            path + ": missing entry point " + kEntryPoint);
    }

    using EntryPointFn = BlCodecPlugin* (*)();
    EntryPointFn entry = reinterpret_cast<EntryPointFn>(sym);
    BlCodecPlugin* plugin = entry();
    if (!plugin) {
        return Result<std::unique_ptr<PluginHandle>>::err(
            Err::InvalidArgument, path + ": " + kEntryPoint + " returned null");
    }

    if (plugin->abi_version != BL_PLUGIN_ABI_VERSION) {
        return Result<std::unique_ptr<PluginHandle>>::err(
            Err::PluginAbiMismatch,
            path + ": ABI version " +
                std::to_string(plugin->abi_version) + " != expected " +
                std::to_string(BL_PLUGIN_ABI_VERSION));
    }

    auto shape = detail::validatePluginShape(plugin);
    if (shape != detail::PluginValidation::Valid) {
        return Result<std::unique_ptr<PluginHandle>>::err(
            Err::InvalidArgument,
            path + ": " + detail::validationMessage(shape));
    }

    auto handle = std::unique_ptr<PluginHandle>(
        new PluginHandle(path, origin, plugin));
    handle->impl_->library = std::move(opened).value();
    return Result<std::unique_ptr<PluginHandle>>::ok(std::move(handle));
}

Result<PluginLoader::ScanReport> PluginLoader::scanDirectory(
    const std::string& dir, PluginOrigin origin) {
    return scanDirectories({{dir, origin}});
}

Result<PluginLoader::ScanReport> PluginLoader::scanDirectories(
    const std::vector<std::pair<std::string, PluginOrigin>>& dirs) {
    ScanReport merged;
    std::map<std::string, std::unique_ptr<PluginHandle>> byName;

    for (const auto& [dir, origin] : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;

        std::vector<fs::path> searchRoots{fs::path(dir)};
        for (const auto& sub : platform::pluginScanSubdirectories()) {
            searchRoots.push_back(fs::path(dir) / sub);
        }

        for (const auto& root : searchRoots) {
            fs::directory_iterator it(
                root, fs::directory_options::skip_permission_denied, ec);
            if (ec) continue;
            for (const auto& entry : it) {
                if (!entry.is_regular_file(ec) || ec) continue;
                const fs::path& p = entry.path();
                if (!hasLibrarySuffix(p)) continue;

                auto loaded = load(p.string(), origin);
                if (!loaded.ok()) {
                    merged.skipped.push_back({p.string(), loaded.message()});
                    BL_LOG_WARN("plugins", loaded.message());
                    continue;
                }
                std::string name = loaded.value()->plugin()->name;
                auto existing = byName.find(name);
                if (existing != byName.end()) {
                    bool userOverrides =
                        existing->second->origin() == PluginOrigin::System &&
                        origin == PluginOrigin::User;
                    if (userOverrides) {
                        merged.skipped.push_back(
                            {existing->second->path(),
                             "overridden by user-local plugin '" + name + "'"});
                        BL_LOG_INFO("plugins",
                                    "user-local plugin overrides system plugin '" +
                                        name + "'");
                        existing->second = std::move(loaded.value());
                    } else {
                        merged.skipped.push_back(
                            {p.string(),
                             "duplicate plugin name '" + name + "'"});
                        BL_LOG_WARN("plugins",
                                    "skipping duplicate plugin '" + name +
                                        "' at " + p.string());
                    }
                    continue;
                }
                byName.emplace(std::move(name), std::move(loaded.value()));
            }
        }
    }

    for (auto& kv : byName) {
        merged.plugins.push_back(std::move(kv.second));
    }
    std::sort(merged.plugins.begin(), merged.plugins.end(),
              [](const std::unique_ptr<PluginHandle>& a,
                 const std::unique_ptr<PluginHandle>& b) {
                  return a->path() < b->path();
              });
    return Result<ScanReport>::ok(std::move(merged));
}

Result<PluginLoader::ScanReport> PluginLoader::scanDefaultLocations() {
    std::vector<std::pair<std::string, PluginOrigin>> dirs;
    auto roots = platform::pluginRootDirectories();
    for (size_t i = 0; i < roots.size(); ++i) {
        dirs.emplace_back(roots[i],
                          i == 0 ? PluginOrigin::System : PluginOrigin::User);
    }
    return scanDirectories(dirs);
}

} // namespace bl
