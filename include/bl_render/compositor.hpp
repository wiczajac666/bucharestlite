#pragma once

#include <bl_core/decoder_bridge.hpp>
#include <bl_core/result.hpp>
#include <bl_core/time.hpp>
#include <bl_render/compositor_backend.hpp>
#include <bl_timeline/timeline.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace bl {

class IDecodeProvider {
public:
    virtual ~IDecodeProvider() = default;
    virtual Result<Frame> getFrame(const std::string& mediaItemId, Time sourceTime,
                                   uint32_t width, uint32_t height) = 0;
};

struct CompositorConfig {
    uint32_t outputWidth{1920};
    uint32_t outputHeight{1080};
};

struct CompositorResult {
    std::vector<uint8_t> data;
    uint32_t width{0};
    uint32_t height{0};
    uint32_t linesize{0};
    Time presentationTime{};
};

class Compositor {
public:
    static Result<Compositor> create(const CompositorConfig& config);
    // Creates a compositor that delegates layer blending to `backend`. A null
    // backend selects the portable CPU implementation; the UI injects an
    // OpenGL backend when the user opts into GPU compositing.
    static Result<Compositor> create(
        const CompositorConfig& config,
        std::unique_ptr<ICompositorBackend> backend);

    Compositor(Compositor&&) noexcept;
    Compositor& operator=(Compositor&&) noexcept;
    ~Compositor();
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    Result<CompositorResult> renderFrame(const TimelineSnapshot& snapshot, Time t,
                                         IDecodeProvider& provider);

    const CompositorConfig& config() const noexcept;

    // Name of the active backend ("cpu", "opengl", ...) and whether it runs on
    // the GPU. Exposed for diagnostics and tests.
    std::string_view backendName() const noexcept;
    bool usingGpu() const noexcept;

private:
    Compositor() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bl
