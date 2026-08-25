#pragma once

#include <string>
#include <utility>
#include <variant>

namespace bl {

enum class Err : int {
    Ok = 0,
    FileNotFound,
    DecodeFailed,
    EncodeFailed,
    PluginAbiMismatch,
    RegistryDuplicate,
    InvalidArgument,
    OutOfMemory,
    Cancelled,
    IoError,
    JsonError,
    Internal,
};

const char* toString(Err code) noexcept;

struct Failure {
    Err code;
    std::string message;
};

template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : storage_(std::move(value)) {}
    Result(Err code, std::string message = std::string())
        : storage_(Failure{code, std::move(message)}) {}

    static Result ok(T value) { return Result(std::move(value)); }
    static Result err(Err code, std::string message = std::string()) {
        return Result(code, std::move(message));
    }

    bool ok() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    Err code() const noexcept { return ok() ? Err::Ok : std::get<Failure>(storage_).code; }

    const std::string& message() const noexcept {
        static const std::string empty;
        return ok() ? empty : std::get<Failure>(storage_).message;
    }

    T& value() & { return std::get<T>(storage_); }
    const T& value() const & { return std::get<T>(storage_); }
    T&& value() && { return std::get<T>(std::move(storage_)); }

    T& operator*() & { return value(); }
    const T& operator*() const & { return value(); }
    T&& operator*() && { return std::move(*this).value(); }
    T* operator->() noexcept { return &std::get<T>(storage_); }
    const T* operator->() const noexcept { return &std::get<T>(storage_); }

    T valueOr(T fallback) const {
        return ok() ? std::get<T>(storage_) : std::move(fallback);
    }

private:
    std::variant<T, Failure> storage_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() : failure_{Err::Ok, std::string()} {}
    Result(Err code, std::string message = std::string())
        : failure_{code, std::move(message)} {}

    static Result<void> err(Err code, std::string message = std::string()) {
        return Result<void>(code, std::move(message));
    }

    bool ok() const noexcept { return failure_.code == Err::Ok; }
    explicit operator bool() const noexcept { return ok(); }

    Err code() const noexcept { return failure_.code; }
    const std::string& message() const noexcept { return failure_.message; }

private:
    Failure failure_;
};

} // namespace bl
