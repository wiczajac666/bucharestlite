#include <bl_audio/device_output.hpp>

#include <memory>

namespace bl {

namespace detail {
std::unique_ptr<IDeviceOutput> createWinMmOutput();
std::unique_ptr<IDeviceOutput> createAlsaOutput();
} // namespace detail

void interleaveTo(const AudioBuffer& buffer, float* out) {
    const uint32_t frames = buffer.sampleCount;
    const uint32_t channels = buffer.channels;
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t c = 0; c < channels; ++c) {
            out[static_cast<size_t>(i) * channels + c] = buffer.channelData(c)[i];
        }
    }
}

std::unique_ptr<IDeviceOutput> createDefaultAudioOutput() {
#if defined(_WIN32)
    return detail::createWinMmOutput();
#elif defined(__linux__)
    if (auto alsa = detail::createAlsaOutput()) {
        return alsa;
    }
    return std::make_unique<NullDeviceOutput>();
#else
    return std::make_unique<NullDeviceOutput>();
#endif
}

} // namespace bl
