#pragma once

#include <bl_core/job_manager.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_core/time.hpp>

#include <bl_render/thumbnail.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bl {

// Identifies one filmstrip cell: a media item sampled at a source time and
// rendered at a bounded width. Trimming a clip changes the sampled times (new
// keys), moving it does not (the same source frames still apply).
struct ClipThumbnailKey {
    std::string mediaItemId;
    Time sourceTime;
    uint32_t maxWidth{0};

    bool operator==(const ClipThumbnailKey& other) const noexcept {
        return mediaItemId == other.mediaItemId &&
               sourceTime == other.sourceTime &&
               maxWidth == other.maxWidth;
    }
};

struct ClipThumbnailKeyHash {
    size_t operator()(const ClipThumbnailKey& key) const noexcept {
        size_t h = std::hash<std::string>{}(key.mediaItemId);
        h ^= std::hash<int64_t>{}(key.sourceTime.ticks) + 0x9e3779b9u +
             (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(key.sourceTime.rate.num) + 0x85ebca6bu +
             (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(key.sourceTime.rate.den) + 0xc2b2ae35u +
             (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(key.maxWidth) + 0x27d4eb2fu +
             (h << 6) + (h >> 2);
        return h;
    }
};

// Decode width used when the caller does not specify one (matches the media
// bin's first-frame thumbnails).
constexpr uint32_t kDefaultClipThumbnailWidth = 96;

// A clip's filmstrip: `sampleTimes` defines the grid (one cell per sample,
// evenly spaced across the clipped source window) and `frames` holds the
// resolved thumbnails in the same order. A null frame means "still loading" —
// painters draw a placeholder for it and repaint when onReady fires.
struct ClipThumbnailStrip {
    std::vector<Time> sampleTimes;
    std::vector<std::shared_ptr<const MediaThumbnail>> frames;
};

// What a clip needs from the thumbnail engine. Built by the UI from a clip's
// source window and its on-screen width; kept Qt-free so it is testable.
struct ClipThumbnailSpec {
    std::string mediaItemId;
    Time sourceIn;
    Time sourceOut;
    size_t sampleCount{1};
    uint32_t maxWidth{kDefaultClipThumbnailWidth};
};

// Upper bound on filmstrip cells drawn for a single clip, regardless of zoom.
constexpr size_t kMaxStripCells = 12;

// Cells to draw for a clip `clipWidthPx` wide, using a target cell width.
// Clamped to [1, kMaxStripCells] so zooming in adds detail up to a cap and
// zooming out never collapses below one frame.
size_t clipThumbnailCellCount(double clipWidthPx,
                              double targetCellPx = 64.0) noexcept;

// `count` evenly spaced source times across [sourceIn, sourceOut), including
// the in-point. A degenerate window yields the in-point repeated `count` times.
std::vector<Time> clipThumbnailSampleTimes(Time sourceIn, Time sourceOut,
                                           size_t count);

// Background source-time frame decode + bounded LRU cache backing the video
// filmstrips drawn on timeline clips. Qt-free and thread-safe: decoding runs
// on JobManager worker threads; results are handed off through onReady (which
// may run on a worker or the engine's bookkeeping thread, so UI layers must
// marshal back to their event loop). thumbnailFor()/isLoading() may be called
// from any thread.
class TimelineClipThumbnailEngine {
public:
    static constexpr uint32_t kDefaultWidth = kDefaultClipThumbnailWidth;
    static constexpr size_t kDefaultMaxEntries = 256;

    explicit TimelineClipThumbnailEngine(
        size_t maxEntries = kDefaultMaxEntries, unsigned workerCount = 1);
    ~TimelineClipThumbnailEngine();

    TimelineClipThumbnailEngine(const TimelineClipThumbnailEngine&) = delete;
    TimelineClipThumbnailEngine& operator=(const TimelineClipThumbnailEngine&) =
        delete;

    // Extra plugin roots scanned on top of registerBuiltins(); matches the
    // MediaDecodeSource configuration the preview renderer uses.
    void configure(std::vector<std::pair<std::string, PluginOrigin>> pluginDirs);

    // Enqueue a source-time decode (idempotent: skipped when cached or already
    // pending). `path` is resolved from the media bin by the caller.
    void request(const std::string& mediaItemId, const std::string& path,
                 Time sourceTime, uint32_t maxWidth = kDefaultWidth);

    // Drop every cached/pending cell for a media item (source change).
    void remove(const std::string& mediaItemId);
    // Drop cached/pending cells whose media item is not in `keep` (media-bin
    // reconciliation or clip source edits).
    void retain(const std::vector<std::string>& keep);
    // Cancel all pending jobs and clear the cache.
    void clear();

    std::shared_ptr<const MediaThumbnail> thumbnailFor(
        const std::string& mediaItemId, Time sourceTime,
        uint32_t maxWidth = kDefaultWidth) const;
    bool isLoading(const std::string& mediaItemId, Time sourceTime,
                   uint32_t maxWidth = kDefaultWidth) const;

    size_t cachedCount() const;
    size_t pendingCount() const;
    size_t maxEntries() const noexcept { return maxEntries_; }

    // Fired after a job settles (decoded, failed or cancelled) with the exact
    // cell that settled, so a UI can repaint just the affected clips.
    std::function<void(const std::string& mediaItemId, Time sourceTime,
                       uint32_t maxWidth)>
        onReady;

private:
    struct CacheEntry {
        std::shared_ptr<MediaThumbnail> thumbnail;
        std::list<ClipThumbnailKey>::iterator lruIt;
    };

    void eventLoop();
    void settle(const ClipThumbnailKey& key,
                std::shared_ptr<MediaThumbnail> thumbnail);
    void eraseLocked(std::unordered_map<ClipThumbnailKey, CacheEntry,
                                        ClipThumbnailKeyHash>::iterator it);

    const size_t maxEntries_;
    std::vector<std::pair<std::string, PluginOrigin>> pluginDirs_;

    mutable std::mutex cacheMutex_;
    // Mutable so thumbnailFor() can promote an entry on access (logically
    // const: the cached value itself is unchanged).
    mutable std::unordered_map<ClipThumbnailKey, CacheEntry,
                               ClipThumbnailKeyHash> cache_;
    mutable std::list<ClipThumbnailKey> lru_;  // front = most recently used
    std::unordered_map<ClipThumbnailKey, JobId, ClipThumbnailKeyHash> pendingByKey_;
    std::unordered_map<JobId, ClipThumbnailKey> keyByJob_;

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
