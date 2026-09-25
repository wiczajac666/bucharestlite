#include <bl_core/platform/paths.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

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

std::string executableDirectory() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    for (DWORD i = len; i > 0; --i) {
        if (buf[i - 1] == L'\\' || buf[i - 1] == L'/') {
            std::wstring dir(buf, i - 1);
            return std::string(dir.begin(), dir.end());
        }
    }
    return {};
#else
    return {};
#endif
}

std::vector<std::string> pluginRootDirectories() {
    std::vector<std::string> roots;
#if defined(_WIN32)
    const std::string exeDir = executableDirectory();
    if (!exeDir.empty()) {
        roots.push_back(exeDir + "\\plugins");
    }
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
