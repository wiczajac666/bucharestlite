#pragma once

#include <bl_core/project_data.hpp>
#include <bl_core/result.hpp>
#include <bl_core/time.hpp>
#include <bl_render/compositor.hpp>
#include <bl_render/media_decode_source.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace bl {

// Drives full-pipeline preview: renders the current timeline snapshot at a
// requested time through the compositor, decoding source frames on demand via
// MediaDecodeSource (cached in FrameCache). The compositor uses the CPU
// backend unless an optional GPU backend is supplied by the caller.
//
// Single-threaded: call runAt() from one thread only (the UI thread). A GPU
// backend's OpenGL context is therefore created and used on the same thread.
class PreviewEngine {
public:
    struct Config {
        uint32_t outputWidth{320};
        uint32_t outputHeight{240};
        size_t cacheBytes{64 * 1024 * 1024};
        MediaDecodeSource::Spec plugins;
    };

    static Result<PreviewEngine> create(Config config,
                                        std::vector<MediaBinItem> mediaBin,
                                        std::unique_ptr<ICompositorBackend>
                                            backend = nullptr);

    PreviewEngine(PreviewEngine&&) noexcept;
    PreviewEngine& operator=(PreviewEngine&&) noexcept;
    ~PreviewEngine();

    PreviewEngine(const PreviewEngine&) = delete;
    PreviewEngine& operator=(const PreviewEngine&) = delete;

    Result<CompositorResult> runAt(const TimelineSnapshot& snapshot, Time t);

    // Drops open source handles and cached frames (e.g. after a project
    // change or media-bin mutation).
    void reset();

    // Global proxy toggle for the decode source.
    void setUseProxies(bool use);
    bool useProxies() const;

    // Per-media-item proxy override for the decode source.
    void setMediaProxyOverride(const std::string& mediaItemId, std::optional<bool> useProxy);
    std::optional<bool> mediaProxyOverride(const std::string& mediaItemId) const;

    const CompositorConfig& config() const noexcept;

    // Active compositor backend ("cpu", "opengl", ...) and whether it runs on
    // the GPU.
    std::string_view backendName() const noexcept;
    bool usingGpu() const noexcept;

private:
    PreviewEngine() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bl