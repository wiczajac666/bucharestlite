#pragma once

#include <bl_audio/types.hpp>
#include <bl_core/result.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace bl {

class IDeviceOutput {
public:
    virtual ~IDeviceOutput() = default;
    virtual Result<void> open(const AudioMixConfig& config) = 0;
    virtual void write(const AudioBuffer& buffer) = 0;
    virtual void close() = 0;
};

class NullDeviceOutput : public IDeviceOutput {
public:
    Result<void> open(const AudioMixConfig& /*config*/) override { return Result<void>{}; }
    void write(const AudioBuffer& /*buffer*/) override {}
    void close() override {}
};

class AudioEngine {
public:
    static Result<AudioEngine> create(std::unique_ptr<IDeviceOutput> output);

    AudioEngine(AudioEngine&&) noexcept;
    AudioEngine& operator=(AudioEngine&&) noexcept;
    ~AudioEngine();

    Result<void> start(const AudioMixConfig& config);
    void stop();

    bool pushSamples(uint32_t trackIndex, AudioBuffer buffer);
    Result<void> pullMix(AudioBuffer& output, uint32_t sampleCount);

    const AudioMixConfig& config() const;
    bool isRunning() const;

private:
    AudioEngine() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bl
