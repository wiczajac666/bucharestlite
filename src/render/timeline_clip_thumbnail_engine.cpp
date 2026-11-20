#include <bl_render/timeline_clip_thumbnail_engine.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/result.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bl {

size_t clipThumbnailCellCount(double clipWidthPx, double targetCellPx) noexcept {
    if (!(targetCellPx > 0.0) || !(clipWidthPx > 0.0)) {
        return 1;
    }
    const double raw = clipWidthPx / targetCellPx;
    const double rounded = std::floor(raw + 0.5);
    if (rounded <= 1.0) {
        return 1;
    }
    if (rounded >= static_cast<double>(kMaxStripCells)) {
        return kMaxStripCells;
    }
    return static_cast<size_t>(rounded);
}

std::vector<Time> clipThumbnailSampleTimes(Time sourceIn, Time sourceOut,
                                           size_t count) {
    std::vector<Time> out;
    if (count == 0) {
        return out;
    }
    out.reserve(count);
    const double inSec = sourceIn.toSeconds();
    const double dur = sourceOut.toSeconds() - inSec;
    for (size_t i = 0; i < count; ++i) {
        // Sample the in-point of cell i, so the last cell starts before the
        // out-point. A single cell always samples the in-point.
        const double t = (dur > 0.0 && count > 1)
                             ? inSec + dur * static_cast<double>(i) /
                                           static_cast<double>(count)
                             : inSec;
        out.push_back(Time::fromSeconds(t, sourceIn.rate));
    }
    return out;
}

TimelineClipThumbnailEngine::TimelineClipThumbnailEngine(size_t maxEntries,
                                                        unsigned workerCount)
    : maxEntries_(std::max<size_t>(maxEntries, 1)),
      registry_(std::make_unique<CodecRegistry>()),
      jobs_(std::make_unique<JobManager>(workerCount)) {
    // JobManager event handlers run synchronously under its mutex; queue the
    // terminal events and drain them on our own bookkeeping thread instead of
    // touching the cache from a worker/canceller thread.
    jobs_->onEvent([this](const JobEvent& event) {
        if (event.type != JobEventType::Failed &&
            event.type != JobEventType::Cancelled) {
            return;
        }
        std::lock_guard<std::mutex> lock(eventMutex_);
        eventQueue_.emplace_back(event.id, event.type);
        eventCv_.notify_one();
    });
    eventThread_ = std::thread([this] { eventLoop(); });
}

TimelineClipThumbnailEngine::~TimelineClipThumbnailEngine() {
    jobs_->shutdown();
    {
        std::lock_guard<std::mutex> lock(eventMutex_);
        eventThreadStopping_ = true;
    }
    eventCv_.notify_all();
    if (eventThread_.joinable()) {
        eventThread_.join();
    }
}

void TimelineClipThumbnailEngine::configure(
    std::vector<std::pair<std::string, PluginOrigin>> pluginDirs) {
    pluginDirs_ = std::move(pluginDirs);
}

void TimelineClipThumbnailEngine::request(const std::string& mediaItemId,
                                          const std::string& path,
                                          Time sourceTime, uint32_t maxWidth) {
    if (path.empty() || mediaItemId.empty() || maxWidth == 0) {
        return;
    }
    const ClipThumbnailKey key{mediaItemId, sourceTime, maxWidth};
    auto job = std::make_unique<LambdaJob>(
        "clip thumbnail " + mediaItemId,
        [this, key, path](const ProgressFn& progress,
                          const ICancelToken& cancel) {
            (void)progress;
            std::lock_guard<std::mutex> lock(registryMutex_);
            if (!registryLoaded_) {
                if (auto r = registerBuiltins(*registry_); !r.ok()) {
                    return r;
                }
                PluginLoader loader;
                if (auto report = loader.scanDirectories(pluginDirs_);
                    report.ok()) {
                    for (auto& handle : report.value().plugins) {
                        auto reg = registry_->registerPlugin(
                            const_cast<BlCodecPlugin*>(handle->plugin()));
                        if (reg.ok()) {
                            handles_.push_back(std::move(handle));
                        }
                    }
                }
                registryLoaded_ = true;
            }

            auto thumb = readThumbnailAt(path, key.sourceTime, *registry_,
                                         key.maxWidth);
            if (cancel.cancelled()) {
                return Result<void>::err(Err::Cancelled,
                                         "clip thumbnail decode cancelled");
            }
            if (!thumb.ok()) {
                return Result<void>::err(thumb.code(), thumb.message());
            }
            settle(key, std::make_shared<MediaThumbnail>(std::move(thumb.value())));
            return Result<void>();
        },
        JobPriority::Low);

    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (cache_.find(key) != cache_.end() ||
        pendingByKey_.find(key) != pendingByKey_.end()) {
        return;  // cached or already enqueued
    }
    const JobId id = jobs_->enqueue(std::move(job));
    if (id == 0) {
        return;
    }
    pendingByKey_[key] = id;
    keyByJob_[id] = key;
}

void TimelineClipThumbnailEngine::eraseLocked(
    std::unordered_map<ClipThumbnailKey, CacheEntry,
                       ClipThumbnailKeyHash>::iterator it) {
    lru_.erase(it->second.lruIt);
    cache_.erase(it);
}

void TimelineClipThumbnailEngine::remove(const std::string& mediaItemId) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (auto it = pendingByKey_.begin(); it != pendingByKey_.end();) {
        if (it->first.mediaItemId == mediaItemId) {
            jobs_->cancel(it->second);
            keyByJob_.erase(it->second);
            it = pendingByKey_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->first.mediaItemId == mediaItemId) {
            auto victim = it++;
            eraseLocked(victim);
        } else {
            ++it;
        }
    }
}

void TimelineClipThumbnailEngine::retain(const std::vector<std::string>& keep) {
    const std::unordered_set<std::string> keepSet(keep.begin(), keep.end());
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (auto it = pendingByKey_.begin(); it != pendingByKey_.end();) {
        if (keepSet.find(it->first.mediaItemId) == keepSet.end()) {
            jobs_->cancel(it->second);
            keyByJob_.erase(it->second);
            it = pendingByKey_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (keepSet.find(it->first.mediaItemId) == keepSet.end()) {
            auto victim = it++;
            eraseLocked(victim);
        } else {
            ++it;
        }
    }
}

void TimelineClipThumbnailEngine::clear() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (const auto& [key, jobId] : pendingByKey_) {
        (void)key;
        jobs_->cancel(jobId);
    }
    pendingByKey_.clear();
    keyByJob_.clear();
    cache_.clear();
    lru_.clear();
}

std::shared_ptr<const MediaThumbnail> TimelineClipThumbnailEngine::thumbnailFor(
    const std::string& mediaItemId, Time sourceTime, uint32_t maxWidth) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    const ClipThumbnailKey key{mediaItemId, sourceTime, maxWidth};
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        return nullptr;
    }
    // Promote on access so eviction is true LRU, not just decode order.
    auto lruIt = it->second.lruIt;  // copy: erase() needs a mutable iterator
    lru_.erase(lruIt);
    lru_.push_front(key);
    it->second.lruIt = lru_.begin();
    return it->second.thumbnail;
}

bool TimelineClipThumbnailEngine::isLoading(const std::string& mediaItemId,
                                            Time sourceTime,
                                            uint32_t maxWidth) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return pendingByKey_.find({mediaItemId, sourceTime, maxWidth}) !=
           pendingByKey_.end();
}

size_t TimelineClipThumbnailEngine::cachedCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cache_.size();
}

size_t TimelineClipThumbnailEngine::pendingCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return pendingByKey_.size();
}

void TimelineClipThumbnailEngine::eventLoop() {
    std::deque<std::pair<JobId, JobEventType>> batch;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(eventMutex_);
            eventCv_.wait(lock, [this] {
                return !eventQueue_.empty() || eventThreadStopping_;
            });
            if (eventQueue_.empty()) {
                if (eventThreadStopping_) {
                    return;
                }
                continue;
            }
            batch.swap(eventQueue_);
        }

        for (const auto& [jobId, type] : batch) {
            (void)type;
            ClipThumbnailKey key;
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                auto it = keyByJob_.find(jobId);
                if (it != keyByJob_.end()) {
                    key = it->second;
                    keyByJob_.erase(it);
                    pendingByKey_.erase(key);
                    found = true;
                }
            }
            if (found && onReady) {
                onReady(key.mediaItemId, key.sourceTime, key.maxWidth);
            }
        }
        batch.clear();
    }
}

void TimelineClipThumbnailEngine::settle(
    const ClipThumbnailKey& key, std::shared_ptr<MediaThumbnail> thumbnail) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto pendingIt = pendingByKey_.find(key);
        if (pendingIt == pendingByKey_.end()) {
            return;  // removed/cancelled meanwhile
        }
        keyByJob_.erase(pendingIt->second);
        pendingByKey_.erase(pendingIt);

        if (thumbnail && thumbnail->hasPixels()) {
            if (auto existing = cache_.find(key); existing != cache_.end()) {
                eraseLocked(existing);
            }
            lru_.push_front(key);
            cache_.emplace(key, CacheEntry{std::move(thumbnail), lru_.begin()});
            while (cache_.size() > maxEntries_) {
                const ClipThumbnailKey& oldest = lru_.back();
                auto victim = cache_.find(oldest);
                if (victim == cache_.end()) {
                    lru_.pop_back();
                } else {
                    eraseLocked(victim);
                }
            }
        }
    }
    if (onReady) {
        onReady(key.mediaItemId, key.sourceTime, key.maxWidth);
    }
}

} // namespace bl
