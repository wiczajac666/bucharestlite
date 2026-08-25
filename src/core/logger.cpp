#include "bl_core/logger.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace bl {

namespace {

const char* levelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::string formatTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                  tmv.tm_min, tmv.tm_sec, static_cast<int>(ms.count()));
    return std::string(buf);
}

} // namespace

const char* toString(LogLevel level) noexcept { return levelName(level); }

struct Logger::Impl {
    Options opts;
    std::atomic<int> minLevel{static_cast<int>(LogLevel::Info)};

    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::pair<LogLevel, std::string>> queue;
    size_t outstanding{0};
    bool stopping{false};

    std::thread worker;
    std::FILE* file{nullptr};
    uint64_t fileBytes{0};

    ~Impl() {
        if (file) std::fclose(file);
    }

    void startWorker() {
        if (!worker.joinable()) {
            worker = std::thread([this] { run(); });
        }
    }

    void run() {
        for (;;) {
            std::vector<std::pair<LogLevel, std::string>> batch;
            {
                std::unique_lock<std::mutex> lk(mu);
                cv.wait(lk, [this] {
                    return (stopping && outstanding == 0) || !queue.empty();
                });
                if (queue.empty()) {
                    if (stopping) break;
                    continue;
                }
                batch.reserve(queue.size());
                while (!queue.empty()) {
                    batch.push_back(std::move(queue.front()));
                    queue.pop_front();
                }
            }
            writeBatch(batch);
            {
                std::lock_guard<std::mutex> lk(mu);
                outstanding -= batch.size();
                cv.notify_all();
            }
        }
    }

    void rotateIfNeeded(size_t incomingBytes) {
        if (!file || opts.maxFileBytes == 0) return;
        if (fileBytes + incomingBytes <= opts.maxFileBytes) return;
        std::fclose(file);
        file = nullptr;
        for (int k = opts.maxRotations - 1; k >= 1; --k) {
            std::string from = opts.filePath + "." + std::to_string(k);
            std::string to = opts.filePath + "." + std::to_string(k + 1);
            std::rename(from.c_str(), to.c_str());
        }
        std::rename(opts.filePath.c_str(), (opts.filePath + ".1").c_str());
        openFile();
    }

    void openFile() {
        file = std::fopen(opts.filePath.c_str(), "ab");
        fileBytes = 0;
        if (file) {
            if (std::fseek(file, 0, SEEK_END) == 0) {
                long sz = std::ftell(file);
                if (sz > 0) fileBytes = static_cast<uint64_t>(sz);
            }
            std::fseek(file, 0, SEEK_END);
        }
    }

    void writeBatch(const std::vector<std::pair<LogLevel, std::string>>& batch) {
        for (const auto& entry : batch) {
            const std::string& line = entry.second;
            if (opts.consoleEnabled && static_cast<int>(entry.first) >=
                                           static_cast<int>(opts.consoleLevel)) {
                std::FILE* out =
                    entry.first >= LogLevel::Warn ? stderr : stdout;
                std::fwrite(line.data(), 1, line.size(), out);
                std::fflush(out);
            }
            if (file && static_cast<int>(entry.first) >=
                            static_cast<int>(opts.minLevel)) {
                rotateIfNeeded(line.size());
                if (file) {
                    std::fwrite(line.data(), 1, line.size(), file);
                    std::fflush(file);
                    fileBytes += line.size();
                }
            }
        }
    }
};

Logger::Logger() : impl_(new Impl()) {}

Logger::~Logger() {
    shutdown();
    delete impl_;
}

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::configure(const Options& options) {
    Impl* impl = impl_;
    std::lock_guard<std::mutex> lk(impl->mu);
    bool pathChanged = impl->opts.filePath != options.filePath;
    impl->opts = options;
    impl->minLevel.store(static_cast<int>(options.minLevel),
                         std::memory_order_relaxed);
    if (!impl->worker.joinable()) impl->stopping = false;
    if (pathChanged || !impl->file) {
        if (impl->file) {
            std::fclose(impl->file);
            impl->file = nullptr;
        }
        if (!options.filePath.empty()) impl->openFile();
    } else if (impl->file &&
               options.maxFileBytes != 0 &&
               impl->fileBytes > options.maxFileBytes) {
        std::fclose(impl->file);
        impl->file = nullptr;
        impl->openFile();
    }
    impl->startWorker();
}

void Logger::setLevel(LogLevel level) {
    Impl* impl = impl_;
    std::lock_guard<std::mutex> lk(impl->mu);
    impl->opts.minLevel = level;
    impl->minLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

bool Logger::enabled(LogLevel level) const {
    Impl* impl = impl_;
    return static_cast<int>(level) >=
           impl->minLevel.load(std::memory_order_relaxed);
}

void Logger::log(LogLevel level, std::string_view category, std::string message) {
    Impl* impl = impl_;
    if (static_cast<int>(level) <
        impl->minLevel.load(std::memory_order_relaxed)) {
        return;
    }
    std::string line;
    line.reserve(category.size() + message.size() + 40);
    line += '[';
    line += formatTimestamp();
    line += "] [";
    line += levelName(level);
    line += "] [";
    line.append(category);
    line += "] ";
    line += message;
    line += '\n';
    {
        std::lock_guard<std::mutex> lk(impl->mu);
        if (impl->stopping) return;
        if (impl->queue.size() >= impl->opts.maxQueuedLines) {
            impl->queue.pop_front();
            --impl->outstanding;
        }
        impl->queue.emplace_back(level, std::move(line));
        ++impl->outstanding;
    }
    impl->cv.notify_one();
}

void Logger::logf(LogLevel level, std::string_view category, const char* fmt,
                  ...) {
    char stackBuf[512];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (static_cast<size_t>(n) < sizeof(stackBuf)) {
        log(level, category, std::string(stackBuf, static_cast<size_t>(n)));
        return;
    }
    std::string heap(static_cast<size_t>(n) + 1, '\0');
    va_start(args, fmt);
    std::vsnprintf(&heap[0], heap.size(), fmt, args);
    va_end(args);
    heap.resize(static_cast<size_t>(n));
    log(level, category, std::move(heap));
}

void Logger::flush() {
    Impl* impl = impl_;
    std::unique_lock<std::mutex> lk(impl->mu);
    impl->cv.wait(lk, [impl] { return impl->outstanding == 0; });
}

void Logger::shutdown() {
    Impl* impl = impl_;
    {
        std::lock_guard<std::mutex> lk(impl->mu);
        if (impl->stopping) return;
        impl->stopping = true;
    }
    impl->cv.notify_all();
    if (impl->worker.joinable()) impl->worker.join();
    std::lock_guard<std::mutex> lk(impl->mu);
    if (impl->file) {
        std::fclose(impl->file);
        impl->file = nullptr;
    }
}

} // namespace bl
