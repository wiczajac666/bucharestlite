#include <bl_audio/audio_engine.hpp>
#include <bl_audio/mixer.hpp>
#include <bl_audio/track_strip.hpp>
#include <bl_core/logger.hpp>

#include <algorithm>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace bl {

// Fixed-capacity ring of *planar* float frames. The public AudioBuffer /
// AudioSpan types are planar (channel c starts at data + c*linesize), so the
// ring stores one contiguous plane per channel to avoid a transpose on every
// push/read.
struct RingBuffer {
    std::vector<float> data;
    uint32_t channels{0};
    uint32_t writePos{0};
    uint32_t count{0};
    uint32_t capacity{0};

    void init(uint32_t cap, uint32_t ch) {
        channels = ch;
        capacity = cap;
        data.assign(static_cast<size_t>(cap) * ch, 0.0f);
        writePos = 0;
        count = 0;
    }

    uint32_t push(const AudioSpan& src, uint32_t frames) {
        frames = std::min(frames, capacity - count);
        for (uint32_t i = 0; i < frames; ++i) {
            const uint32_t slot = (writePos + i) % capacity;
            for (uint32_t c = 0; c < channels; ++c) {
                data[static_cast<size_t>(c) * capacity + slot] = src.channelData(c)[i];
            }
        }
        writePos = (writePos + frames) % capacity;
        count += frames;
        return frames;
    }

    void read(AudioSpan out, uint32_t frames) {
        frames = std::min(frames, count);
        const uint32_t readPos = (writePos + capacity - count) % capacity;
        for (uint32_t i = 0; i < frames; ++i) {
            const uint32_t slot = (readPos + i) % capacity;
            for (uint32_t c = 0; c < channels; ++c) {
                out.channelData(c)[i] = data[static_cast<size_t>(c) * capacity + slot];
            }
        }
        count -= frames;
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
    if (!res.ok()) {
        impl_->running = false;
        return res;
    }

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

uint32_t AudioEngine::pushSamples(uint32_t trackIndex, AudioBuffer buffer) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->running) return 0;
    if (buffer.channels != impl_->config.channels) return 0;

    auto& rb = impl_->ringBuffers[trackIndex];
    if (rb.capacity == 0) {
        rb.init(impl_->config.sampleRate * 2, impl_->config.channels);
    }

    return rb.push(buffer.span(), buffer.sampleCount);
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

    // Reserve so push_back never reallocates: `inputs` holds spans pointing
    // into `trackBuffers`, which would dangle after a move.
    trackBuffers.reserve(impl_->ringBuffers.size());
    inputs.reserve(impl_->ringBuffers.size());
    gains.reserve(impl_->ringBuffers.size());
    pans.reserve(impl_->ringBuffers.size());

    for (auto& [idx, rb] : impl_->ringBuffers) {
        if (rb.available() == 0) continue;

        AudioBuffer tmp;
        tmp.resize(sampleCount, impl_->config.channels);
        tmp.clear();
        const uint32_t toRead = std::min(sampleCount, rb.available());
        rb.read(tmp.span(), toRead);

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

void AudioEngine::flush() {
    std::lock_guard lock(impl_->mutex);
    for (auto& [index, ring] : impl_->ringBuffers) {
        (void)index;
        ring.count = 0;
    }
}

const AudioMixConfig& AudioEngine::config() const { return impl_->config; }
bool AudioEngine::isRunning() const { return impl_->running; }
bool AudioEngine::deviceBlocksOnWrite() const { return impl_->output->blocksOnWrite(); }

} // namespace bl
