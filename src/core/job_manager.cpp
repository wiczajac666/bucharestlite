#include <bl_core/job_manager.hpp>

#include <algorithm>
#include <utility>

namespace bl {

namespace {

int priorityRank(JobPriority p) {
    switch (p) {
        case JobPriority::High: return 0;
        case JobPriority::Normal: return 1;
        case JobPriority::Low: return 2;
    }
    return 1;
}

} // namespace

JobManager::JobManager(unsigned workerCount) {
    unsigned count = workerCount;
    if (count == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        count = hw > 4 ? 2u : (hw > 0 ? 1u : 2u);
    }
    workers_.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

JobManager::~JobManager() { shutdown(ShutdownMode::Abandon); }

JobId JobManager::enqueue(std::unique_ptr<IJob> job) {
    if (!job) return 0;

    std::unique_ptr<CancelToken> token = std::make_unique<CancelToken>();
    Entry entry;
    entry.job = std::move(job);
    entry.token = std::move(token);

    JobEvent queued{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shuttingDown_) return 0;

        entry.id = nextId_.fetch_add(1, std::memory_order_relaxed);
        entry.sequence = sequenceCounter_++;
        entry.priority = entry.job->priority();

        queue_.push_back(std::move(entry));
        std::push_heap(queue_.begin(), queue_.end(), higherPriorityThan);

        queued.id = queue_.back().id;
        queued.type = JobEventType::Queued;
        queued.jobName = queue_.back().job->name();
    }

    cv_.notify_one();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        emitLocked(queued);
    }
    return queued.id;
}

bool JobManager::cancel(JobId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->id == id) {
            JobEvent event{id, JobEventType::Cancelled, it->job->name(), 0.f, {}};
            queue_.erase(it);
            std::make_heap(queue_.begin(), queue_.end(), higherPriorityThan);
            emitLocked(event);
            cv_.notify_all();
            return true;
        }
    }

    for (auto& active : active_) {
        if (active.first == id) {
            active.second->cancel();
            return true;
        }
    }
    return false;
}

size_t JobManager::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

size_t JobManager::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeCount_;
}

void JobManager::onEvent(std::function<void(const JobEvent&)> handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    eventHandler_ = std::move(handler);
}

void JobManager::shutdown(ShutdownMode mode) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shuttingDown_) return;
        shuttingDown_ = true;

        if (mode == ShutdownMode::Abandon) {
            for (auto& entry : queue_) {
                emitLocked(JobEvent{entry.id, JobEventType::Cancelled,
                                    entry.job->name(), 0.f, {}});
            }
            queue_.clear();
            for (auto& [id, token] : active_) {
                (void)id;
                token->cancel();
            }
        } else {
            drainingNow_ = true;
        }
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

void JobManager::workerLoop() {
    for (;;) {
        Entry entry;
        {
            std::unique_lock<std::mutex> lock(mutex_);

            cv_.wait(lock, [this] {
                if (!queue_.empty()) return true;
                if (shuttingDown_) {
                    if (!drainingNow_) return true;
                    return activeCount_ == 0 && queue_.empty();
                }
                return false;
            });

            bool abandonAll =
                shuttingDown_ && !drainingNow_;

            if (queue_.empty()) {
                if (abandonAll || (shuttingDown_ && drainingNow_ &&
                                   activeCount_ == 0)) {
                    return;
                }
                continue;
            }

            if (abandonAll) {
                while (!queue_.empty()) {
                    auto& front = queue_.front();
                    emitLocked(JobEvent{front.id, JobEventType::Cancelled,
                                        front.job->name(), 0.f, {}});
                    std::pop_heap(queue_.begin(), queue_.end(),
                                  higherPriorityThan);
                    queue_.pop_back();
                }
                continue;
            }

            std::pop_heap(queue_.begin(), queue_.end(), higherPriorityThan);
            entry = std::move(queue_.back());
            queue_.pop_back();

            ++activeCount_;
        }

        emitLocked(JobEvent{entry.id, JobEventType::Started,
                            entry.job->name(), 0.f, {}});

        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_.emplace_back(entry.id, entry.token);
        }

        Result<void> outcome = entry.job->run(
            [&](float ratio, const std::string& message) {
                std::lock_guard<std::mutex> lock(mutex_);
                emitLocked(JobEvent{entry.id, JobEventType::Progress,
                                    entry.job->name(), ratio, message});
            },
            *entry.token);

        JobEventType endType =
            outcome.ok()
                ? (entry.token->cancelled() ? JobEventType::Cancelled
                                            : JobEventType::Done)
                : (outcome.code() == Err::Cancelled ? JobEventType::Cancelled
                                                    : JobEventType::Failed);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = active_.begin(); it != active_.end(); ++it) {
                if (it->first == entry.id) {
                    active_.erase(it);
                    break;
                }
            }
            --activeCount_;
            emitLocked(JobEvent{entry.id, endType, entry.job->name(), 1.f,
                                outcome.message()});
            cv_.notify_all();
        }
    }
}

void JobManager::emitLocked(const JobEvent& event) {
    if (eventHandler_) eventHandler_(event);
}

bool JobManager::higherPriorityThan(const Entry& a, const Entry& b) {
    const int ra = priorityRank(a.priority);
    const int rb = priorityRank(b.priority);
    if (ra != rb) return ra > rb;
    return a.sequence > b.sequence;
}

} // namespace bl