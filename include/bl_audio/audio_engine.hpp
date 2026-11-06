#pragma once

#include <bl_audio/device_output.hpp>
#include <bl_audio/types.hpp>
#include <bl_core/result.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace bl {

class AudioEngine {
public:
    static Result<AudioEngine> create(std::unique_ptr<IDeviceOutput> output);

    AudioEngine(AudioEngine&&) noexcept;
    AudioEngine& operator=(AudioEngine&&) noexcept;
    ~AudioEngine();

    Result<void> start(const AudioMixConfig& config);
    void stop();

    // Queue planar frames for a track. Returns the number of frames accepted
    // (0 when the engine is stopped or the track ring is full).
    uint32_t pushSamples(uint32_t trackIndex, AudioBuffer buffer);
    Result<void> pullMix(AudioBuffer& output, uint32_t sampleCount);

    // Drop everything queued so far without stopping the device. Used when
    // the playhead jumps and the feeder starts over from a new position.
    void flush();

    const AudioMixConfig& config() const;
    bool isRunning() const;
    bool deviceBlocksOnWrite() const;

private:
    AudioEngine() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bl
