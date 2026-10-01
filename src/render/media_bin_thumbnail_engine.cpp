#include <bl_render/media_bin_thumbnail_engine.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/project_data.hpp>
#include <bl_core/result.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bl {

MediaBinThumbnailEngine::MediaBinThumbnailEngine(unsigned workerCount)
    : registry_(std::make_unique<CodecRegistry>()),
      jobs_(std::make_unique<JobManager>(workerCount)) {
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

MediaBinThumbnailEngine::~MediaBinThumbnailEngine() {
    jobs_->shutdown();
    {
        std::lock_guard<std::mutex> lock(eventMutex_);
        eventThreadStopping_ = true;
    }
    eventCv_.notify_all();
    if (eventThread_.joinable()) eventThread_.join();
}

void MediaBinThumbnailEngine::configure(
    std::vector<std::pair<std::string, PluginOrigin>> pluginDirs) {
    pluginDirs_ = std::move(pluginDirs);
}

void MediaBinThumbnailEngine::request(const std::string& mediaItemId,
                                      const std::string& path) {
    // Hold the cache lock across enqueue + registration so a job that fails
    // instantly (e.g. missing file) can never settle before its bookkeeping
    // exists. settle()/eventLoop() take this same lock, and neither enqueue
    // nor the event handler takes it, so there is no lock-order inversion.
    std::lock_guard<std::mutex> lock(cacheMutex_);
    if (cache_.find(mediaItemId) != cache_.end() ||
        pendingByMedia_.find(mediaItemId) != pendingByMedia_.end()) {
        return;
    }

    auto job = std::make_unique<LambdaJob>(
        "thumbnail " + mediaItemId,
        [this, mediaItemId, path](const ProgressFn& progress,
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

            auto thumb = readThumbnail(path, *registry_);
            if (cancel.cancelled()) {
                return Result<void>::err(Err::Cancelled,
                                         "thumbnail decode cancelled");
            }
            if (!thumb.ok()) {
                return Result<void>::err(thumb.code(), thumb.message());
            }
            settle(mediaItemId,
                   std::make_shared<MediaThumbnail>(std::move(thumb.value())));
            return Result<void>();
        },
        JobPriority::Low);

    const JobId id = jobs_->enqueue(std::move(job));
    pendingByMedia_[mediaItemId] = id;
    mediaByJob_[id] = mediaItemId;
}

void MediaBinThumbnailEngine::requestMedia(
    const std::vector<MediaBinItem>& mediaBin) {
    for (const auto& item : mediaBin) {
        if (!item.path.empty()) {
            request(item.id, item.path);
        }
    }
}

std::vector<std::string> MediaBinThumbnailEngine::mediaIds() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    std::vector<std::string> ids;
    ids.reserve(cache_.size() + pendingByMedia_.size());
    for (const auto& [id, thumb] : cache_) {
        (void)thumb;
        ids.push_back(id);
    }
    for (const auto& [id, jobId] : pendingByMedia_) {
        (void)jobId;
        ids.push_back(id);
    }
    return ids;
}

void MediaBinThumbnailEngine::remove(const std::string& mediaItemId) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto pendingIt = pendingByMedia_.find(mediaItemId);
    if (pendingIt != pendingByMedia_.end()) {
        jobs_->cancel(pendingIt->second);
        mediaByJob_.erase(pendingIt->second);
        pendingByMedia_.erase(pendingIt);
    }
    cache_.erase(mediaItemId);
}

void MediaBinThumbnailEngine::clear() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (const auto& [mediaId, jobId] : pendingByMedia_) {
        (void)mediaId;
        jobs_->cancel(jobId);
    }
    pendingByMedia_.clear();
    mediaByJob_.clear();
    cache_.clear();
}

std::shared_ptr<const MediaThumbnail> MediaBinThumbnailEngine::thumbnailFor(
    const std::string& mediaItemId) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(mediaItemId);
    if (it == cache_.end() || !it->second) {
        return nullptr;
    }
    return it->second;
}

bool MediaBinThumbnailEngine::isLoading(
    const std::string& mediaItemId) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return pendingByMedia_.find(mediaItemId) != pendingByMedia_.end();
}

void MediaBinThumbnailEngine::eventLoop() {
    std::deque<std::pair<JobId, JobEventType>> batch;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(eventMutex_);
            eventCv_.wait(lock, [this] {
                return !eventQueue_.empty() || eventThreadStopping_;
            });
            if (eventQueue_.empty()) {
                if (eventThreadStopping_) return;
                continue;
            }
            batch.swap(eventQueue_);
        }

        for (const auto& [jobId, type] : batch) {
            (void)type;
            std::string mediaId;
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                auto it = mediaByJob_.find(jobId);
                if (it == mediaByJob_.end()) continue;
                mediaId = it->second;
                mediaByJob_.erase(it);
                pendingByMedia_.erase(mediaId);
            }
            if (onReady) onReady(mediaId);
        }
        batch.clear();
    }
}

void MediaBinThumbnailEngine::settle(
    const std::string& mediaItemId,
    std::shared_ptr<MediaThumbnail> thumbnail) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto pendingIt = pendingByMedia_.find(mediaItemId);
        if (pendingIt == pendingByMedia_.end()) return;  // removed meanwhile
        mediaByJob_.erase(pendingIt->second);
        pendingByMedia_.erase(pendingIt);
        if (thumbnail) {
            cache_[mediaItemId] = std::move(thumbnail);
        }
    }
    if (onReady) onReady(mediaItemId);
}

} // namespace bl
