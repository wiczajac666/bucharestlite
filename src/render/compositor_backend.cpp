#include <bl_render/compositor_backend.hpp>

#include <algorithm>
#include <cstddef>

namespace bl {

void alphaBlendCpu(std::vector<uint8_t>& dst, uint32_t dstWidth,
                   uint32_t dstHeight, uint32_t dstLinesize,
                   const std::vector<uint8_t>& src, uint32_t srcWidth,
                   uint32_t srcHeight, uint32_t srcLinesize, double opacity) {
    if (opacity <= 0.0) return;
    if (opacity > 1.0) opacity = 1.0;

    const uint32_t copyW = std::min(dstWidth, srcWidth);
    const uint32_t copyH = std::min(dstHeight, srcHeight);

    for (uint32_t y = 0; y < copyH; ++y) {
        uint8_t* dstRow = dst.data() + static_cast<size_t>(y) * dstLinesize;
        const uint8_t* srcRow =
            src.data() + static_cast<size_t>(y) * srcLinesize;
        for (uint32_t x = 0; x < copyW; ++x) {
            const uint32_t i = x * 4;
            uint8_t* dstP = dstRow + i;
            const uint8_t* srcP = srcRow + i;
            // Premultiplied accumulation on the output canvas (in-app output is
            // alpha-less RGB, so coverage scales RGB directly). Per-pixel alpha
            // from the source frame participates: for opaque frames this is
            // byte-identical to the previous opacity-only blend, and keyed-out
            // (alpha 0) pixels fall through to whatever is already on the
            // canvas.
            const double op = static_cast<double>(srcP[3]) / 255.0 * opacity;
            if (op <= 0.0) continue;
            const double invOp = 1.0 - op;
            for (int c = 0; c < 3; ++c) {
                dstP[c] = static_cast<uint8_t>(
                    static_cast<double>(srcP[c]) * op +
                    static_cast<double>(dstP[c]) * invOp);
            }
            dstP[3] = static_cast<uint8_t>(
                static_cast<double>(srcP[3]) * op +
                static_cast<double>(dstP[3]) * invOp);
        }
    }
}

namespace {

// Portable backend: transparent canvas, then bottom-to-top source-over blends.
// This mirrors the original in-line compositor exactly, so preview and export
// stay byte-identical when no GPU backend is supplied.
class CpuCompositorBackend final : public ICompositorBackend {
public:
    std::string_view name() const override { return "cpu"; }

    void composite(std::vector<uint8_t>& out, uint32_t outWidth,
                   uint32_t outHeight, uint32_t outLinesize,
                   const std::vector<CompositeLayer>& layers) override {
        std::fill(out.begin(), out.end(), 0);
        for (const CompositeLayer& layer : layers) {
            alphaBlendCpu(out, outWidth, outHeight, outLinesize, layer.data,
                          layer.width, layer.height, layer.linesize,
                          layer.opacity);
        }
    }
};

} // namespace

std::unique_ptr<ICompositorBackend> makeCpuCompositorBackend() {
    return std::make_unique<CpuCompositorBackend>();
}

} // namespace bl
