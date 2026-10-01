#pragma once

#include <bl_core/job_manager.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_render/thumbnail.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bl {

class CodecRegistry;
struct MediaBinItem;

// Background first-frame decode + media metadata cache backing the media-bin
// thumbnails and detail lines. Qt-free and thread-safe: decoding happens on
// JobManager worker threads; results are handed off through onReady (invoked
// on the engine's internal bookkeeping thread, so UI layers must marshal back
// to their event loop). thumbnailFor() may be called from any thread.
class MediaBinThumbnailEngine {
public:
    explicit MediaBinThumbnailEngine(unsigned workerCount = 1);
    ~MediaBinThumbnailEngine();

    MediaBinThumbnailEngine(const MediaBinThumbnailEngine&) = delete;
    MediaBinThumbnailEngine& operator=(const MediaBinThumbnailEngine&) = delete;

    // Extra plugin roots scanned on top of registerBuiltins(); matches the
    // MediaDecodeSource configuration the preview renderer uses.
    void configure(std::vector<std::pair<std::string, PluginOrigin>> pluginDirs);

    // Enqueue a first-frame decode for one media item (idempotent: skipped when
    // cached or already pending).
    void request(const std::string& mediaItemId, const std::string& path);
    // Enqueue a decode for every media-bin item with a resolvable path.
    void requestMedia(const std::vector<MediaBinItem>& mediaBin);

    // Every media id currently cached or pending (for reconciling the cache
    // against the media bin after items are removed).
    std::vector<std::string> mediaIds() const;

    // Drop a cached/pending item (e.g. removed from the media bin).
    void remove(const std::string& mediaItemId);
    // Cancel all pending jobs and drop the whole cache.
    void clear();

    std::shared_ptr<const MediaThumbnail> thumbnailFor(
        const std::string& mediaItemId) const;
    bool isLoading(const std::string& mediaItemId) const;

    // Called after a job settles (decoded, failed or cancelled) so callers can
    // clear per-row "loading" state. Runs on the engine's internal bookkeeping
    // thread, never the JobManager worker pool.
    std::function<void(const std::string& mediaItemId)> onReady;

private:
    // Consumes Failed/Cancelled JobEvents on a dedicated thread so cache
    // bookkeeping can never deadlock against a caller holding the cache lock
    // while cancelling a job.
    void eventLoop();
    void settle(const std::string& mediaItemId,
                std::shared_ptr<MediaThumbnail> thumbnail);

    std::vector<std::pair<std::string, PluginOrigin>> pluginDirs_;

    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, std::shared_ptr<MediaThumbnail>> cache_;
    std::unordered_map<std::string, JobId> pendingByMedia_;
    std::unordered_map<JobId, std::string> mediaByJob_;

    std::mutex eventMutex_;
    std::condition_variable eventCv_;
    std::deque<std::pair<JobId, JobEventType>> eventQueue_;
    bool eventThreadStopping_{false};
    std::thread eventThread_;

    std::unique_ptr<CodecRegistry> registry_;
    std::vector<std::unique_ptr<PluginHandle>> handles_;
    bool registryLoaded_{false};
    std::mutex registryMutex_;

    std::unique_ptr<JobManager> jobs_;
};

} // namespace bl
