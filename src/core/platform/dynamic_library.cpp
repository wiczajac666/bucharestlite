#include "dynamic_library.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace bl::platform {

Result<DynamicLibrary> DynamicLibrary::open(const std::string& path) {
    if (path.empty()) {
        return Result<DynamicLibrary>::err(Err::InvalidArgument,
                                           "empty library path");
    }
#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(path.c_str());
    if (!handle) {
        DWORD code = GetLastError();
        return Result<DynamicLibrary>::err(
            Err::IoError, "LoadLibrary failed for " + path + " (error " +
                              std::to_string(code) + ")");
    }
    return Result<DynamicLibrary>::ok(DynamicLibrary(handle));
#else
    dlerror();
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        std::string msg = "dlopen failed for " + path;
        if (err) msg += std::string(": ") + err;
        return Result<DynamicLibrary>::err(Err::IoError, std::move(msg));
    }
    return Result<DynamicLibrary>::ok(DynamicLibrary(handle));
#endif
}

DynamicLibrary::~DynamicLibrary() { close(); }

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(other.handle_) {
    other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

void* DynamicLibrary::symbol(const char* name) const noexcept {
    if (!handle_ || !name) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    dlerror();
    return dlsym(handle_, name);
#endif
}

void DynamicLibrary::close() noexcept {
    if (!handle_) return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

} // namespace bl::platform
