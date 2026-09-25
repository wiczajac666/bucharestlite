#include <bl_audio/audio_meter_engine.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/project_data.hpp>
#include <bl_core/result.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bl {

AudioMeterEngine::AudioMeterEngine(unsigned workerCount)
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

AudioMeterEngine::~AudioMeterEngine() {
    jobs_->shutdown();
    {
        std::lock_guard<std::mutex> lock(eventMutex_);
        eventThreadStopping_ = true;
    }
    eventCv_.notify_all();
    if (eventThread_.joinable()) eventThread_.join();
}

void AudioMeterEngine::configure(
    std::vector<std::pair<std::string, PluginOrigin>> pluginDirs) {
    pluginDirs_ = std::move(pluginDirs);
}

void AudioMeterEngine::analyze(const std::string& mediaItemId,
                               const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cache_.find(mediaItemId) != cache_.end() ||
            pendingByMedia_.find(mediaItemId) != pendingByMedia_.end()) {
            return;
        }
    }

    auto job = std::make_unique<LambdaJob>(
        "analyze audio " + mediaItemId,
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

            auto analyzed = analyzeAudio(path, *registry_);
            if (cancel.cancelled()) {
                return Result<void>::err(Err::Cancelled,
                                         "audio analysis cancelled");
            }
            if (!analyzed.ok()) {
                return Result<void>::err(analyzed.code(), analyzed.message());
            }
            settle(mediaItemId, std::make_shared<AudioMeterEnvelope>(
                                    std::move(analyzed.value())));
            return Result<void>();
        },
        JobPriority::Low);

    const JobId id = jobs_->enqueue(std::move(job));
    std::lock_guard<std::mutex> lock(cacheMutex_);
    pendingByMedia_[mediaItemId] = id;
    mediaByJob_[id] = mediaItemId;
}

void AudioMeterEngine::analyzeMedia(
    const std::vector<MediaBinItem>& mediaBin) {
    for (const auto& item : mediaBin) {
        if (!item.path.empty()) {
            analyze(item.id, item.path);
        }
    }
}

void AudioMeterEngine::remove(const std::string& mediaItemId) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto pendingIt = pendingByMedia_.find(mediaItemId);
    if (pendingIt != pendingByMedia_.end()) {
        jobs_->cancel(pendingIt->second);
        mediaByJob_.erase(pendingIt->second);
        pendingByMedia_.erase(pendingIt);
    }
    cache_.erase(mediaItemId);
}

void AudioMeterEngine::clear() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (const auto& [mediaId, jobId] : pendingByMedia_) {
        jobs_->cancel(jobId);
    }
    pendingByMedia_.clear();
    mediaByJob_.clear();
    cache_.clear();
}

std::shared_ptr<const AudioMeterEnvelope> AudioMeterEngine::envelopeFor(
    const std::string& mediaItemId) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(mediaItemId);
    if (it == cache_.end() || !it->second || it->second->empty()) {
        return nullptr;
    }
    return it->second;
}

bool AudioMeterEngine::isAnalyzing(const std::string& mediaItemId) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return pendingByMedia_.find(mediaItemId) != pendingByMedia_.end();
}

std::vector<std::string> AudioMeterEngine::mediaIds() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    std::vector<std::string> ids;
    ids.reserve(cache_.size() + pendingByMedia_.size());
    for (const auto& [id, envelope] : cache_) {
        (void)envelope;
        ids.push_back(id);
    }
    for (const auto& [id, jobId] : pendingByMedia_) {
        (void)jobId;
        ids.push_back(id);
    }
    return ids;
}

void AudioMeterEngine::eventLoop() {
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
            if (onAnalyzed) onAnalyzed(mediaId);
        }
        batch.clear();
    }
}

void AudioMeterEngine::settle(
    const std::string& mediaItemId,
    std::shared_ptr<AudioMeterEnvelope> envelope) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto pendingIt = pendingByMedia_.find(mediaItemId);
        if (pendingIt == pendingByMedia_.end()) return;  // removed meanwhile
        mediaByJob_.erase(pendingIt->second);
        pendingByMedia_.erase(pendingIt);
        if (envelope && !envelope->empty()) {
            cache_[mediaItemId] = std::move(envelope);
        }
    }
    if (onAnalyzed) onAnalyzed(mediaItemId);
}

} // namespace bl