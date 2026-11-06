#include <bl_audio/device_output.hpp>
#include <bl_core/result.hpp>

#if defined(__linux__)

#include <dlfcn.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

namespace bl::detail {
namespace {

// The small slice of the ALSA ABI this backend needs, spelled out so the file
// compiles without alsa/asoundlib.h. libasound is dlopen'ed at runtime, which
// keeps the build dependency-free and lets the app fall back to a null sink on
// systems without ALSA.
constexpr int kStreamPlayback = 0;      // SND_PCM_STREAM_PLAYBACK
constexpr int kAccessRwInterleaved = 3; // SND_PCM_ACCESS_RW_INTERLEAVED
constexpr int kFormatFloatLE = 14;      // SND_PCM_FORMAT_FLOAT_LE
constexpr long kEpipe = -32;            // -EPIPE
constexpr long kEstrpipe = -86;         // -ESTRPIPE

struct AlsaApi {
    using OpenFn = int (*)(void**, const char*, int, int);
    using SetParamsFn = int (*)(void*, int, int, unsigned, unsigned, int, unsigned);
    using WriteFn = long (*)(void*, const void*, unsigned long);
    using PrepareFn = int (*)(void*);
    using DrainFn = int (*)(void*);
    using CloseFn = int (*)(void*);
    using StrErrorFn = const char* (*)(int);

    void* library{nullptr};
    OpenFn open{nullptr};
    SetParamsFn setParams{nullptr};
    WriteFn write{nullptr};
    PrepareFn prepare{nullptr};
    DrainFn drain{nullptr};
    CloseFn close{nullptr};
    StrErrorFn strerror{nullptr};

    bool load() {
        if (library) return true;

        library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!library) return false;

        open = reinterpret_cast<OpenFn>(dlsym(library, "snd_pcm_open"));
        setParams = reinterpret_cast<SetParamsFn>(dlsym(library, "snd_pcm_set_params"));
        write = reinterpret_cast<WriteFn>(dlsym(library, "snd_pcm_writei"));
        prepare = reinterpret_cast<PrepareFn>(dlsym(library, "snd_pcm_prepare"));
        drain = reinterpret_cast<DrainFn>(dlsym(library, "snd_pcm_drain"));
        close = reinterpret_cast<CloseFn>(dlsym(library, "snd_pcm_close"));
        strerror = reinterpret_cast<StrErrorFn>(dlsym(library, "snd_strerror"));

        if (!open || !setParams || !write || !prepare || !drain || !close) {
            dlclose(library);
            library = nullptr;
            open = nullptr;
            setParams = nullptr;
            write = nullptr;
            prepare = nullptr;
            drain = nullptr;
            close = nullptr;
            strerror = nullptr;
            return false;
        }
        return true;
    }

    std::string message(int code) const {
        if (strerror) {
            const char* text = strerror(code);
            if (text) return text;
        }
        return "ALSA error " + std::to_string(code);
    }
};

// Process-lifetime singleton: the handle is intentionally never dlclose'd, so
// a sink destructor running during static teardown can still call close().
AlsaApi& alsa() {
    static AlsaApi api;
    return api;
}

class AlsaOutput final : public IDeviceOutput {
public:
    ~AlsaOutput() override { close(); }

    Result<void> open(const AudioMixConfig& config) override {
        close();

        if (!alsa().load()) {
            return Result<void>::err(Err::Internal, "libasound.so.2 not available");
        }

        const char* configured = std::getenv("BL_AUDIO_DEVICE");
        const std::string device = (configured && *configured) ? configured : "default";

        int rc = alsa().open(&pcm_, device.c_str(), kStreamPlayback, 0);
        if (rc < 0) {
            pcm_ = nullptr;
            return Result<void>::err(Err::Internal,
                                     "snd_pcm_open(" + device + "): " +
                                         alsa().message(rc));
        }

        rc = alsa().setParams(pcm_, kFormatFloatLE, kAccessRwInterleaved,
                              config.channels, config.sampleRate, 1, 100000);
        if (rc < 0) {
            const std::string message = alsa().message(rc);
            alsa().close(pcm_);
            pcm_ = nullptr;
            return Result<void>::err(Err::Internal,
                                     "snd_pcm_set_params: " + message);
        }

        config_ = config;
        return Result<void>{};
    }

    void write(const AudioBuffer& buffer) override {
        if (!pcm_ || buffer.sampleCount == 0) return;

        scratch_.resize(static_cast<size_t>(buffer.sampleCount) * buffer.channels);
        interleaveTo(buffer, scratch_.data());

        const float* cursor = scratch_.data();
        long remaining = static_cast<long>(buffer.sampleCount);
        int recoveries = 0;

        while (remaining > 0) {
            const long written = alsa().write(pcm_, cursor,
                                              static_cast<unsigned long>(remaining));
            if (written == kEpipe || written == kEstrpipe) {
                if (++recoveries > 3) return;
                if (alsa().prepare(pcm_) < 0) return;
                continue;
            }
            if (written < 0) {
                if (++recoveries > 3) return;
                if (alsa().prepare(pcm_) < 0) return;
                continue;
            }
            if (written == 0) return;

            cursor += written * buffer.channels;
            remaining -= written;
        }
    }

    void close() override {
        if (pcm_) {
            alsa().drain(pcm_);
            alsa().close(pcm_);
            pcm_ = nullptr;
        }
        scratch_.clear();
    }

    bool blocksOnWrite() const noexcept override { return true; }

private:
    void* pcm_{nullptr};
    AudioMixConfig config_{};
    std::vector<float> scratch_;
};

} // namespace

std::unique_ptr<IDeviceOutput> createAlsaOutput() {
    return std::make_unique<AlsaOutput>();
}

} // namespace bl::detail

#endif // __linux__
