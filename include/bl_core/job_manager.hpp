#pragma once

#include <bl_core/result.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace bl {

using JobId = uint64_t;

enum class JobPriority : unsigned char { Low, Normal, High };

using ProgressFn = std::function<void(float ratio, const std::string& message)>;

class ICancelToken {
public:
    virtual ~ICancelToken() = default;
    virtual bool cancelled() const = 0;
};

class CancelToken final : public ICancelToken {
public:
    bool cancelled() const override {
        return flag_.load(std::memory_order_relaxed);
    }
    void cancel() { flag_.store(true, std::memory_order_relaxed); }

private:
    std::atomic<bool> flag_{false};
};

class IJob {
public:
    virtual ~IJob() = default;

    virtual std::string name() const = 0;
    virtual Result<void> run(const ProgressFn& report,
                             const ICancelToken& cancel) = 0;
    virtual JobPriority priority() const { return JobPriority::Normal; }
};

class LambdaJob final : public IJob {
public:
    using RunFn =
        std::function<Result<void>(const ProgressFn&, const ICancelToken&)>;

    LambdaJob(std::string name, RunFn runFn,
              JobPriority priority = JobPriority::Normal)
        : name_(std::move(name)), runFn_(std::move(runFn)),
          priority_(priority) {}

    std::string name() const override { return name_; }
    JobPriority priority() const override { return priority_; }

    Result<void> run(const ProgressFn& report,
                     const ICancelToken& cancel) override {
        if (!runFn_) return {};
        return runFn_(report, cancel);
    }

private:
    std::string name_;
    RunFn runFn_;
    JobPriority priority_;
};

enum class JobEventType : unsigned char {
    Queued,
    Started,
    Progress,
    Done,
    Failed,
    Cancelled,
};

struct JobEvent {
    JobId id;
    JobEventType type;
    std::string jobName;
    float ratio{0.f};
    std::string message;
};

class JobManager {
public:
    explicit JobManager(unsigned workerCount = 0);
    ~JobManager();

    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;

    JobId enqueue(std::unique_ptr<IJob> job);

    bool cancel(JobId id);

    size_t pendingCount() const;
    size_t activeCount() const;

    void onEvent(std::function<void(const JobEvent&)> handler);

    enum class ShutdownMode { Drain, Abandon };
    void shutdown(ShutdownMode mode = ShutdownMode::Drain);

private:
    struct Entry {
        int sequence;
        JobId id;
        JobPriority priority;
        std::unique_ptr<IJob> job;
        std::shared_ptr<CancelToken> token;
    };

    void workerLoop();
    void emitLocked(const JobEvent& event);
    static bool higherPriorityThan(const Entry& a, const Entry& b);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<Entry> queue_;
    std::vector<std::pair<JobId, std::shared_ptr<CancelToken>>> active_;
    std::atomic<JobId> nextId_{1};
    int sequenceCounter_{0};
    size_t activeCount_{0};
    bool shuttingDown_{false};
    bool drainingNow_{false};

    std::function<void(const JobEvent&)> eventHandler_;

    std::vector<std::thread> workers_;
};

} // namespace bl
