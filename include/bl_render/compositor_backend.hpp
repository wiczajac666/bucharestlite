#pragma once

#include <bl_render/composite_layer.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace bl {

// Pluggable compositing backend. The CPU implementation is always available;
// the UI optionally supplies a GPU backend (OpenGL) that blends the same
// layers with a shader. Compositors fall back to the CPU backend when no GPU
// context can be created.
class ICompositorBackend {
public:
    virtual ~ICompositorBackend() = default;

    virtual std::string_view name() const = 0;
    virtual bool gpu() const { return false; }

    // Blends `layers` bottom-to-top into `out` (BGRA32, `outLinesize` bytes per
    // row, already sized to outWidth * outHeight * 4 or larger).
    virtual void composite(std::vector<uint8_t>& out, uint32_t outWidth,
                           uint32_t outHeight, uint32_t outLinesize,
                           const std::vector<CompositeLayer>& layers) = 0;
};

// The portable CPU compositor (straight-alpha "source over" into a transparent
// canvas).
std::unique_ptr<ICompositorBackend> makeCpuCompositorBackend();

// Source-over blend used by the CPU backend and by transition rendering.
// `src` is straight-alpha BGRA32.
void alphaBlendCpu(std::vector<uint8_t>& dst, uint32_t dstWidth,
                   uint32_t dstHeight, uint32_t dstLinesize,
                   const std::vector<uint8_t>& src, uint32_t srcWidth,
                   uint32_t srcHeight, uint32_t srcLinesize, double opacity);

} // namespace bl
