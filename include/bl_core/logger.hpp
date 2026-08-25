#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#if defined(__GNUC__) || defined(__clang__)
#define BL_PRINTF_FMT(fmtIdx, argIdx) __attribute__((format(printf, fmtIdx, argIdx)))
#else
#define BL_PRINTF_FMT(fmtIdx, argIdx)
#endif

namespace bl {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

const char* toString(LogLevel level) noexcept;

class Logger {
public:
    struct Options {
        LogLevel minLevel{LogLevel::Info};
        LogLevel consoleLevel{LogLevel::Debug};
        bool consoleEnabled{true};
        std::string filePath;
        uint64_t maxFileBytes{8ull * 1024ull * 1024ull};
        int maxRotations{3};
        size_t maxQueuedLines{8192};
    };

    static Logger& get();

    void configure(const Options& options);
    void setLevel(LogLevel level);
    bool enabled(LogLevel level) const;

    void log(LogLevel level, std::string_view category, std::string message);
    void logf(LogLevel level, std::string_view category, const char* fmt,
              ...) BL_PRINTF_FMT(4, 5);

    void flush();
    void shutdown();

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct Impl;
    Impl* impl_;
};

} // namespace bl

#define BL_LOG_TRACE(category, message) \
    ::bl::Logger::get().log(::bl::LogLevel::Trace, category, message)
#define BL_LOG_DEBUG(category, message) \
    ::bl::Logger::get().log(::bl::LogLevel::Debug, category, message)
#define BL_LOG_INFO(category, message) \
    ::bl::Logger::get().log(::bl::LogLevel::Info, category, message)
#define BL_LOG_WARN(category, message) \
    ::bl::Logger::get().log(::bl::LogLevel::Warn, category, message)
#define BL_LOG_ERROR(category, message) \
    ::bl::Logger::get().log(::bl::LogLevel::Error, category, message)
