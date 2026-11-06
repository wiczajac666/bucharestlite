#pragma once

#include <bl_audio/types.hpp>
#include <bl_core/result.hpp>

#include <cstdint>
#include <memory>

namespace bl {

// PCM sink boundary. Buffers handed to write() are *planar* (see
// AudioBuffer::channelData); each backend converts to whatever layout the
// platform device expects.
class IDeviceOutput {
public:
    virtual ~IDeviceOutput() = default;
    virtual Result<void> open(const AudioMixConfig& config) = 0;
    virtual void write(const AudioBuffer& buffer) = 0;
    virtual void close() = 0;

    // True when write() blocks until the device has consumed the frames, in
    // which case the playback loop must not add its own pacing sleep.
    virtual bool blocksOnWrite() const noexcept { return false; }
};

class NullDeviceOutput : public IDeviceOutput {
public:
    Result<void> open(const AudioMixConfig& /*config*/) override { return Result<void>{}; }
    void write(const AudioBuffer& /*buffer*/) override {}
    void close() override {}
};

// Convert a planar buffer into frame-interleaved floats: out contains
// sampleCount * channels values ordered L,R,L,R,... `out` must have room for
// sampleCount * channels floats.
void interleaveTo(const AudioBuffer& buffer, float* out);

// Platform-default sink. Windows: WinMM waveOut. Linux: ALSA (loaded at
// runtime, so no development headers are required) with a null-sink fallback.
// Everywhere else: null sink. Never returns null.
//
// The ALSA backend honours the BL_AUDIO_DEVICE environment variable as the
// PCM device name; it defaults to "default". Tests set it to "null".
std::unique_ptr<IDeviceOutput> createDefaultAudioOutput();

} // namespace bl
