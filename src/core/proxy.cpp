#include <bl_core/proxy.hpp>

#include <bl_core/job_manager.hpp>
#include <bl_core/media_source.hpp>
#include <bl_core/project_data.hpp>
#include <bl_core/result.hpp>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace bl {

namespace {

// Helper to compute proxy output path
std::string makeProxyPath(const std::string& projectDir,
                          const std::string& proxyDir,
                          const std::string& mediaItemId,
                          const std::string& codec) {
    std::string ext = ".mp4"; // H.264 in MP4 container
    if (codec == "h265" || codec == "hevc") ext = ".mp4";
    else if (codec == "vp9") ext = ".webm";

    std::string dir;
    if (proxyDir.empty()) {
        dir = projectDir + "/proxy/";
    } else {
        dir = proxyDir + "/";
    }

    std::filesystem::create_directories(dir);
    return dir + mediaItemId + "_proxy" + ext;
}

// Helper to compute proxy dimensions
void computeProxyDimensions(uint32_t sourceWidth, uint32_t sourceHeight,
                            float factor, uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = std::max<uint32_t>(1, static_cast<uint32_t>(sourceWidth * factor));
    outHeight = std::max<uint32_t>(1, static_cast<uint32_t>(sourceHeight * factor));

    // Ensure even dimensions for H.264
    outWidth = (outWidth + 1) & ~1u;
    outHeight = (outHeight + 1) & ~1u;
}

} // namespace

// ProxyGenerator implementation
ProxyGenerator::ProxyGenerator(JobManager& jobManager, ProxyConfig config)
    : jobManager_(jobManager), config_(std::move(config)), activeCount_(0) {
    // Register event handler for job completion
    jobManager_.onEvent([this](const JobEvent& event) {
        onJobEvent(event);
    });
}

ProxyGenerator::~ProxyGenerator() {
    cancelAll();
}

void ProxyGenerator::setConfig(const ProxyConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

JobId ProxyGenerator::queueProxy(const ProxyJobInput& input) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!config_.enabled) return 0;

    // Check if already generating or has proxy
    if (mediaIdToJob_.count(input.mediaItemId)) return 0;

    // Check concurrent job limit
    if (activeCount_.load(std::memory_order_relaxed) >= config_.maxConcurrentJobs) {
        // Could queue it for later, but for now just skip
        // In a full implementation, we'd add to a pending queue
        return 0;
    }

    // Increment active count
    activeCount_.fetch_add(1, std::memory_order_relaxed);

    // Create the proxy generation job
    auto job = std::make_unique<LambdaJob>(
        "proxy:" + input.mediaItemId,
        [this, input](const ProgressFn& report, const ICancelToken& cancel) -> Result<void> {
            // Report progress start
            report(0.0f, "Starting proxy generation for " + input.mediaItemId);

            // Build FFmpeg command for proxy generation
            // Using libx264 with CRF, scaling, fast preset for proxy
            std::vector<std::string> args = {
                "-y",                           // overwrite
                "-i", input.sourcePath,         // input
                "-c:v", "libx264",              // H.264 encoder
                "-preset", "fast",              // fast encoding
                "-crf", std::to_string(input.crf),
                "-vf", "scale=" + std::to_string(input.proxyWidth) + ":" +
                          std::to_string(input.proxyHeight) + ":flags=lanczos",
                "-an",                          // no audio for proxy
                input.proxyPath                 // output
            };

            report(0.1f, "Encoding proxy...");

            // Execute FFmpeg (this is a simplified version - in production use
            // proper FFmpeg API or a wrapper)
            // For now, we'll use a system call approach
            std::string cmd = "ffmpeg ";
            for (const auto& arg : args) {
                cmd += " \"" + arg + "\"";
            }

            int result = std::system(cmd.c_str());

            if (cancel.cancelled()) {
                // Clean up partial file
                std::filesystem::remove(input.proxyPath);
                return Result<void>::err(Err::Cancelled, "Proxy generation cancelled");
            }

            report(0.9f, "Finalizing...");

            if (result != 0) {
                std::filesystem::remove(input.proxyPath);
                return Result<void>::err(Err::Internal, "FFmpeg proxy generation failed");
            }

            report(1.0f, "Proxy generation complete");
            return {};
        },
        JobPriority::Low
    );

    JobId jobId = jobManager_.enqueue(std::move(job));

    // Track the job
    mediaIdToJob_[input.mediaItemId] = jobId;
    jobToMediaId_[jobId] = input.mediaItemId;

    return jobId;
}

bool ProxyGenerator::cancelJob(JobId id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = jobToMediaId_.find(id);
    if (it == jobToMediaId_.end()) return false;

    std::string mediaId = it->second;
    bool cancelled = jobManager_.cancel(id);

    if (cancelled) {
        mediaIdToJob_.erase(mediaId);
        jobToMediaId_.erase(id);
    }

    return cancelled;
}

void ProxyGenerator::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [id, mediaId] : jobToMediaId_) {
        jobManager_.cancel(id);
    }
    jobToMediaId_.clear();
    mediaIdToJob_.clear();
}

size_t ProxyGenerator::pendingCount() const {
    // JobManager doesn't expose pending count directly, approximate
    return jobToMediaId_.size();
}

size_t ProxyGenerator::activeCount() const {
    return activeCount_.load(std::memory_order_relaxed);
}

bool ProxyGenerator::isGenerating(const std::string& mediaItemId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mediaIdToJob_.count(mediaItemId) > 0;
}

void ProxyGenerator::onJobEvent(const JobEvent& event) {
    if (event.type != JobEventType::Done &&
        event.type != JobEventType::Failed &&
        event.type != JobEventType::Cancelled) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = jobToMediaId_.find(event.id);
    if (it == jobToMediaId_.end()) return;

    std::string mediaId = it->second;
    jobToMediaId_.erase(it);
    mediaIdToJob_.erase(mediaId);

    // Decrement active count
    activeCount_.fetch_sub(1, std::memory_order_relaxed);

    // Report result via callback
    if (progressCallback_) {
        ProxyJobResult result;
        result.mediaItemId = mediaId;
        result.success = (event.type == JobEventType::Done);
        result.errorMessage = event.message;

        if (result.success) {
            // We need to get the proxy path/dimensions from somewhere
            // In a full implementation, this would be stored in the job result
        }

        progressCallback_(result);
    }
}

bool ProxyGenerator::shouldAutoProxy(const MediaBinItem& item, const ProxyConfig& config) {
    if (!config.enabled) return false;
    if (!item.proxyPath.empty()) return false; // Already has proxy
    // Only skip if proxy is ready or currently generating
    if (item.proxyStatus == "ready" || item.proxyStatus == "generating") return false;
    return item.sourceWidth >= config.autoProxyMinWidth;
}

} // namespace bl