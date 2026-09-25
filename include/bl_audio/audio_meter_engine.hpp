#pragma once

#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_core/job_manager.hpp>
#include <bl_core/plugin_loader.hpp>

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

// Background audio analysis + peak/RMS envelope cache backing the live mixer
// meters. Qt-free and thread-safe: decoding happens on JobManager worker
// threads, results are handed off through onAnalyzed (invoked on whichever
// thread finished the job, so UI layers must marshal back to their event
// loop). envelopeFor() may be called from any thread.
class AudioMeterEngine {
public:
    explicit AudioMeterEngine(unsigned workerCount = 1);
    ~AudioMeterEngine();

    AudioMeterEngine(const AudioMeterEngine&) = delete;
    AudioMeterEngine& operator=(const AudioMeterEngine&) = delete;

    // Extra plugin roots scanned on top of registerBuiltins(); matches the
    // MediaDecodeSource configuration the preview renderer uses.
    void configure(std::vector<std::pair<std::string, PluginOrigin>> pluginDirs);

    // Enqueue analysis for one media item (idempotent: skipped when cached or
    // already pending).
    void analyze(const std::string& mediaItemId, const std::string& path);
    // Enqueue analysis for every media-card item with a resolvable path.
    void analyzeMedia(const std::vector<MediaBinItem>& mediaBin);

    // Every media id currently cached or pending analysis (for reconciling the
    // cache against the media bin after items are removed).
    std::vector<std::string> mediaIds() const;

    // Drop a cached/pending item (e.g. removed from the media bin).
    void remove(const std::string& mediaItemId);
    // Cancel all pending jobs and drop the whole cache.
    void clear();

    std::shared_ptr<const AudioMeterEnvelope> envelopeFor(
        const std::string& mediaItemId) const;
    bool isAnalyzing(const std::string& mediaItemId) const;

    // Called after a job settles (decoded, failed or cancelled) so callers can
    // drop "analyzing" state. Runs on the engine's internal bookkeeping
    // thread, never the JobManager worker pool.
    std::function<void(const std::string& mediaItemId)> onAnalyzed;

private:
    // Consumes Failed/Cancelled JobEvents on a dedicated thread. Event dispatch
    // is decoupled from the JobManager mutex (its handlers run while that mutex
    // is held) so cache bookkeeping can never deadlock against a caller that
    // holds the cache lock while cancelling a job.
    void eventLoop();
    void settle(const std::string& mediaItemId,
                std::shared_ptr<AudioMeterEnvelope> envelope);

    std::vector<std::pair<std::string, PluginOrigin>> pluginDirs_;

    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, std::shared_ptr<AudioMeterEnvelope>> cache_;
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