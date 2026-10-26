#include <bl_core/proxy.hpp>

#include <bl_core/job_manager.hpp>
#include <bl_core/media_source.hpp>
#include <bl_core/process.hpp>
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

    std::filesystem::path dir;
    if (!proxyDir.empty()) {
        dir = proxyDir;
    } else if (!projectDir.empty()) {
        dir = std::filesystem::path(projectDir) / "proxy";
    } else {
        dir = std::filesystem::temp_directory_path() /
              "bucharest-lite-proxy";
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / (mediaItemId + "_proxy" + ext)).string();
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
    // Detach the event handler first: it captures `this`, and worker threads
    // may still be finishing cancelled jobs after this destructor returns.
    jobManager_.onEvent(nullptr);
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
        [input](const ProgressFn& report, const ICancelToken& cancel) -> Result<void> {
            // Report progress start
            report(0.0f, "Starting proxy generation for " + input.mediaItemId);

            // Build the FFmpeg invocation. runProcess() execs it directly, so
            // paths with spaces or shell metacharacters are passed through as
            // arguments rather than being interpreted by a shell.
            std::string encoder = "libx264";
            if (input.codec == "h265" || input.codec == "hevc") {
                encoder = "libx265";
            } else if (input.codec == "vp9") {
                encoder = "libvpx-vp9";
            } else if (input.codec == "av1") {
                encoder = "libaom-av1";
            }

            std::vector<std::string> args = {
                "ffmpeg",
                "-hide_banner",                 // quieter, machine-readable log
                "-loglevel", "error",
                "-nostdin",
                "-y",                           // overwrite
                "-i", input.sourcePath,         // input
                "-c:v", encoder,
            };
            // Encoder-specific speed knobs: libx264/libx265 use -preset, while
            // the VP9 and AV1 wrappers reject it and expose -cpu-used instead.
            if (encoder == "libvpx-vp9") {
                args.insert(args.end(),
                            {"-deadline", "realtime", "-cpu-used", "5"});
            } else if (encoder == "libaom-av1") {
                args.insert(args.end(), {"-cpu-used", "8"});
            } else {
                args.insert(args.end(), {"-preset", "fast"});
            }
            args.insert(args.end(),
                        {
                            "-crf", std::to_string(input.crf),
                            "-vf", "scale=" + std::to_string(input.proxyWidth) +
                                       ":" + std::to_string(input.proxyHeight) +
                                       ":flags=lanczos",
                            "-pix_fmt", "yuv420p",
                            "-c:a", "aac",      // keep audio for A/V preview
                            "-b:a", "128k",
                            input.proxyPath,    // output
                        });

            report(0.1f, "Encoding proxy...");

            const ProcessResult process =
                runProcess(args, [&cancel] { return cancel.cancelled(); });

            if (process.cancelled || cancel.cancelled()) {
                std::filesystem::remove(input.proxyPath);
                return Result<void>::err(Err::Cancelled, "Proxy generation cancelled");
            }

            report(0.9f, "Finalizing...");

            if (!process.started || process.exitCode != 0) {
                std::filesystem::remove(input.proxyPath);
                std::string message = "FFmpeg proxy generation failed";
                if (!process.started) {
                    message += ": could not start ffmpeg (is it installed and on PATH?)";
                } else if (!process.output.empty()) {
                    message += ": " + process.output;
                }
                return Result<void>::err(Err::Internal, message);
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
    jobInputs_[jobId] = input;

    return jobId;
}

bool ProxyGenerator::cancelJob(JobId id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (jobToMediaId_.find(id) == jobToMediaId_.end()) return false;
    }
    // Cancel outside the lock: JobManager dispatches the Cancelled event
    // synchronously, which re-enters onJobEvent().
    return jobManager_.cancel(id);
}

void ProxyGenerator::cancelAll() {
    std::vector<JobId> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ids.reserve(jobToMediaId_.size());
        for (const auto& [id, mediaId] : jobToMediaId_) {
            (void)mediaId;
            ids.push_back(id);
        }
    }
    for (const JobId id : ids) {
        jobManager_.cancel(id);
    }
}

size_t ProxyGenerator::pendingCount() const {
    // Everything tracked minus what is currently running.
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t tracked = jobToMediaId_.size();
    const size_t active = activeCount_.load(std::memory_order_relaxed);
    return tracked > active ? tracked - active : 0;
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

    ProxyJobResult result;
    ProxyProgressFn callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = jobToMediaId_.find(event.id);
        if (it == jobToMediaId_.end()) return;

        const std::string mediaId = it->second;
        const auto inputIt = jobInputs_.find(event.id);

        jobToMediaId_.erase(it);
        mediaIdToJob_.erase(mediaId);

        // Decrement active count
        activeCount_.fetch_sub(1, std::memory_order_relaxed);

        result.mediaItemId = mediaId;
        result.success = (event.type == JobEventType::Done);
        result.errorMessage = event.message;

        if (result.success && inputIt != jobInputs_.end()) {
            // The job knows where it wrote (and at what size), so the media
            // bin can record a usable proxy without re-probing the file.
            result.proxyPath = inputIt->second.proxyPath;
            result.proxyWidth = inputIt->second.proxyWidth;
            result.proxyHeight = inputIt->second.proxyHeight;
        }
        if (inputIt != jobInputs_.end()) {
            jobInputs_.erase(inputIt);
        }
        callback = progressCallback_;
    }

    // Invoke outside the lock: the callback may query the generator (counts)
    // or re-enter the job manager.
    if (callback) {
        callback(result);
    }
}

bool ProxyGenerator::shouldAutoProxy(const MediaBinItem& item, const ProxyConfig& config) {
    if (!config.enabled) return false;
    if (!item.proxyPath.empty()) return false; // Already has proxy
    // Skip jobs that are done, running, or already failed. Retrying failures
    // automatically would spin: every failed job emits projectChanged, which
    // re-runs the queue scan.
    if (item.proxyStatus == "ready" || item.proxyStatus == "generating" ||
        item.proxyStatus == "failed") {
        return false;
    }
    return item.sourceWidth >= config.autoProxyMinWidth;
}

std::string ProxyGenerator::proxyPathFor(const ProxyConfig& config,
                                         const std::string& projectDir,
                                         const std::string& mediaItemId) {
    return makeProxyPath(projectDir, config.proxyDir, mediaItemId, config.codec);
}

} // namespace bl