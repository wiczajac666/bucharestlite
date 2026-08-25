#include "paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace bl::platform {

std::string dynamicLibrarySuffix() noexcept {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

std::string userDataRoot() {
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        return std::string(appdata) + "\\BucharestLite";
    }
    return "BucharestLite";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/Library/Application Support/BucharestLite";
    }
    return "BucharestLite";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/bucharest-lite";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.local/share/bucharest-lite";
    }
    return "bucharest-lite";
#endif
}

std::vector<std::string> pluginRootDirectories() {
    std::vector<std::string> roots;
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        roots.push_back(std::string(appdata) + "\\BucharestLite\\plugins");
    }
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        roots.push_back(std::string(home) +
                        "/Library/Application Support/BucharestLite/plugins");
    }
#else
    roots.push_back("/usr/share/bucharest-lite/plugins");
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        roots.push_back(std::string(xdg) + "/bucharest-lite/plugins");
    } else {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            roots.push_back(std::string(home) +
                            "/.local/share/bucharest-lite/plugins");
        }
    }
#endif
    return roots;
}

std::vector<std::string> pluginScanSubdirectories() noexcept {
    return {"video", "audio"};
}

} // namespace bl::platform
