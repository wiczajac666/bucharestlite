#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace bl {

// Video scope data extracted from one composited (BGRA) frame. All three views
// are filled in a single pass by `computeScopes`; a panel picks one to draw.
// `step` subsamples the frame (1 = every pixel) so the per-frame cost can be
// capped for large canvases.
struct VideoScopes {
    // 256-bin histograms; luma uses Rec.709 weights.
    std::array<uint32_t, 256> luma{};
    std::array<uint32_t, 256> red{};
    std::array<uint32_t, 256> green{};
    std::array<uint32_t, 256> blue{};
    uint32_t histogramMax{0};

    // Luma waveform grid, `waveformColumns` x 256 entries; index is
    // `column * 256 + luma` (row 0 = luma 0).
    int waveformColumns{0};
    std::vector<uint32_t> waveform;
    uint32_t waveformMax{0};

    // Chroma vectorscope grid, `vectorscopeSize` x `vectorscopeSize` entries;
    // index is `cr * size + cb`, both chroma values mapped from [0, 255] across
    // the grid (so 128, the neutral chroma, lands at the centre).
    int vectorscopeSize{0};
    std::vector<uint32_t> vectorscope;
    uint32_t vectorscopeMax{0};

    // Number of pixels actually sampled (roughly frame pixels / step^2).
    uint64_t sampleCount{0};
};

// `bgra` must point at `height` rows of `linesize` bytes; each pixel is
// B,G,R,A. Returns zeroed scopes when the frame is empty, `linesize` is
// non-positive, or `step` < 1. `waveformColumns`/`vectorscopeSize` are clamped
// to at least 1.
VideoScopes computeScopes(const uint8_t* bgra, int width, int height,
                          int linesize, int waveformColumns = 256,
                          int vectorscopeSize = 256, int step = 1);

} // namespace bl
