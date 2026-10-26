#pragma once

#include <bl_core/job_manager.hpp>
#include <bl_core/media_source.hpp>
#include <bl_core/project_data.hpp>
#include <bl_core/result.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bl {

// Proxy configuration
struct ProxyConfig {
    // Minimum source width to auto-queue proxy generation (default: 3840 for 4K+)
    uint32_t autoProxyMinWidth = 3840;
    // Proxy resolution factor: 0.5 = half, 0.25 = quarter (default: 0.5)
    float resolutionFactor = 0.5f;
    // H.264 CRF for proxy encoding (lower = higher quality, default: 18)
    int crf = 18;
    // Proxy format codec (default: "h264")
    std::string codec = "h264";
    // Proxy output directory: empty = "<project_dir>/proxy/", absolute = central cache
    std::string proxyDir;
    // Maximum concurrent proxy jobs (default: 2)
    unsigned maxConcurrentJobs = 2;
    // Enable/disable proxy workflow globally
    bool enabled = true;
};

// Proxy generation job input
struct ProxyJobInput {
    std::string mediaItemId;
    std::string sourcePath;
    uint32_t sourceWidth;
    uint32_t sourceHeight;
    std::string proxyPath;
    uint32_t proxyWidth;
    uint32_t proxyHeight;
    int crf;
    std::string codec;
};

// Proxy generation result
struct ProxyJobResult {
    std::string mediaItemId;
    std::string proxyPath;
    uint32_t proxyWidth;
    uint32_t proxyHeight;
    bool success;
    std::string errorMessage;
};

// Callback for proxy generation progress/completion
using ProxyProgressFn = std::function<void(const ProxyJobResult&)>;

// Proxy generator engine: manages the queue of proxy generation jobs
class ProxyGenerator {
public:
    explicit ProxyGenerator(JobManager& jobManager, ProxyConfig config = {});
    ~ProxyGenerator();

    ProxyGenerator(const ProxyGenerator&) = delete;
    ProxyGenerator& operator=(const ProxyGenerator&) = delete;

    // Set/update configuration
    void setConfig(const ProxyConfig& config);
    const ProxyConfig& config() const { return config_; }

    // Set progress callback (called on job completion from worker thread)
    void setProgressCallback(ProxyProgressFn callback) {
        progressCallback_ = std::move(callback);
    }

    // Queue a proxy generation job for a media item
    // Returns the JobId, or 0 if the item already has a proxy / is being generated / disabled
    JobId queueProxy(const ProxyJobInput& input);

    // Cancel a specific proxy job
    bool cancelJob(JobId id);

    // Cancel all pending proxy jobs
    void cancelAll();

    // Get number of pending jobs in queue
    size_t pendingCount() const;

    // Get number of active jobs
    size_t activeCount() const;

    // Check if a media item currently has a proxy being generated
    bool isGenerating(const std::string& mediaItemId) const;

    // Called when a proxy job completes (from JobManager event handler)
    void onJobEvent(const JobEvent& event);

    // Determine if a source should auto-generate proxy based on config
    static bool shouldAutoProxy(const MediaBinItem& item, const ProxyConfig& config);

    // Destination path for a proxy: `<proxyDir>` when set, otherwise
    // `<projectDir>/proxy`. Creates the directory as a side effect.
    static std::string proxyPathFor(const ProxyConfig& config,
                                    const std::string& projectDir,
                                    const std::string& mediaItemId);

private:
    JobManager& jobManager_;
    ProxyConfig config_;

    mutable std::mutex mutex_;
    std::unordered_map<JobId, std::string> jobToMediaId_; // JobId -> mediaItemId
    std::unordered_map<std::string, JobId> mediaIdToJob_; // mediaItemId -> JobId
    std::unordered_map<JobId, ProxyJobInput> jobInputs_;  // JobId -> job parameters
    std::atomic<size_t> activeCount_{0};

    ProxyProgressFn progressCallback_;
};

} // namespace bl