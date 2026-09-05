#include <bl_audio/audio_engine.hpp>
#include <bl_audio/mixer.hpp>
#include <bl_audio/track_strip.hpp>
#include <bl_core/logger.hpp>

#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace bl {

struct RingBuffer {
    std::vector<float> data;
    uint32_t channels{0};
    uint32_t writePos{0};
    uint32_t count{0};
    uint32_t capacity{0};

    void init(uint32_t cap, uint32_t ch) {
        channels = ch;
        capacity = cap;
        data.resize(static_cast<size_t>(cap) * ch, 0.0f);
        writePos = 0;
        count = 0;
    }

    void push(const float* samples, uint32_t frames) {
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t c = 0; c < channels; ++c) {
                data[static_cast<size_t>((writePos + i) % capacity) * channels + c] =
                    samples[i * channels + c];
            }
        }
        writePos = (writePos + frames) % capacity;
        count = std::min(count + frames, capacity);
    }

    void read(float* out, uint32_t frames) {
        uint32_t readPos = (writePos + capacity - count) % capacity;
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t c = 0; c < channels; ++c) {
                out[i * channels + c] =
                    data[static_cast<size_t>((readPos + i) % capacity) * channels + c];
            }
        }
        count = (count > frames) ? count - frames : 0;
    }

    uint32_t available() const { return count; }
};

struct AudioEngine::Impl {
    AudioMixConfig config;
    std::unique_ptr<IDeviceOutput> output;
    std::unordered_map<uint32_t, RingBuffer> ringBuffers;
    Mixer mixer;
    bool running{false};
    std::mutex mutex;

    explicit Impl(std::unique_ptr<IDeviceOutput> out)
        : output(std::move(out)), mixer(AudioMixConfig{}) {}
};

Result<AudioEngine> AudioEngine::create(std::unique_ptr<IDeviceOutput> output) {
    if (!output) {
        return Result<AudioEngine>::err(Err::InvalidArgument, "null output device");
    }

    AudioEngine engine;
    engine.impl_ = std::make_unique<Impl>(std::move(output));
    return Result<AudioEngine>::ok(std::move(engine));
}

AudioEngine::~AudioEngine() = default;
AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;
AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

Result<void> AudioEngine::start(const AudioMixConfig& config) {
    std::lock_guard lock(impl_->mutex);
    impl_->config = config;
    impl_->mixer = Mixer(config);
    impl_->running = true;

    auto res = impl_->output->open(config);
    if (!res.ok()) return res;

    BL_LOG_INFO("audio", "engine started " + std::to_string(config.sampleRate) + "Hz " +
                             std::to_string(config.channels) + "ch");
    return Result<void>{};
}

void AudioEngine::stop() {
    std::lock_guard lock(impl_->mutex);
    impl_->running = false;
    impl_->output->close();
    impl_->ringBuffers.clear();
    BL_LOG_INFO("audio", "engine stopped");
}

bool AudioEngine::pushSamples(uint32_t trackIndex, AudioBuffer buffer) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->running) return false;

    auto& rb = impl_->ringBuffers[trackIndex];
    if (rb.capacity == 0) {
        rb.init(impl_->config.sampleRate * 2, impl_->config.channels);
    }

    AudioSpan span = buffer.span();
    uint32_t frames = std::min(span.sampleCount, rb.capacity - rb.available());
    if (frames == 0) return false;

    rb.push(span.data, frames);
    return true;
}

Result<void> AudioEngine::pullMix(AudioBuffer& output, uint32_t sampleCount) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->running) {
        return Result<void>::err(Err::InvalidArgument, "engine not running");
    }

    output.resize(sampleCount, impl_->config.channels);
    output.clear();

    std::vector<AudioBuffer> trackBuffers;
    std::vector<AudioSpan> inputs;
    std::vector<double> gains;
    std::vector<double> pans;

    for (auto& [idx, rb] : impl_->ringBuffers) {
        if (rb.available() == 0) continue;

        AudioBuffer tmp;
        tmp.resize(sampleCount, impl_->config.channels);
        uint32_t toRead = std::min(sampleCount, rb.available());
        rb.read(tmp.data.data(), toRead);

        trackBuffers.push_back(std::move(tmp));
        inputs.push_back(trackBuffers.back().span());
        gains.push_back(1.0);
        pans.push_back(0.0);
    }

    if (!inputs.empty()) {
        impl_->mixer.mix(output.span(), inputs, gains, pans);
    }

    impl_->output->write(output);
    return Result<void>{};
}

const AudioMixConfig& AudioEngine::config() const { return impl_->config; }
bool AudioEngine::isRunning() const { return impl_->running; }

} // namespace bl
