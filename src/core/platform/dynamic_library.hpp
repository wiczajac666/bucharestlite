#pragma once

#include <bl_core/result.hpp>

#include <string>

namespace bl::platform {

class DynamicLibrary {
public:
    static Result<DynamicLibrary> open(const std::string& path);

    DynamicLibrary() noexcept = default;
    ~DynamicLibrary();

    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    void* symbol(const char* name) const noexcept;
    bool loaded() const noexcept { return handle_ != nullptr; }
    void close() noexcept;

private:
    explicit DynamicLibrary(void* handle) noexcept : handle_(handle) {}
    void* handle_{nullptr};
};

} // namespace bl::platform
