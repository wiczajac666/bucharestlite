#include <bl_render/scopes.hpp>

#include <algorithm>
#include <cmath>

namespace bl {
namespace {

uint8_t to8(double v) {
    return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

} // namespace

VideoScopes computeScopes(const uint8_t* bgra, int width, int height,
                          int linesize, int waveformColumns,
                          int vectorscopeSize, int step) {
    VideoScopes out;
    if (bgra == nullptr || width <= 0 || height <= 0 || linesize <= 0 ||
        step < 1) {
        return out;
    }

    const int wfCols = std::max(1, waveformColumns);
    const int vsSize = std::max(1, vectorscopeSize);
    out.waveformColumns = wfCols;
    out.waveform.assign(static_cast<size_t>(wfCols) * 256, 0);
    out.vectorscopeSize = vsSize;
    out.vectorscope.assign(static_cast<size_t>(vsSize) * vsSize, 0);

    for (int y = 0; y < height; y += step) {
        const uint8_t* row = bgra + static_cast<size_t>(y) * linesize;
        for (int x = 0; x < width; x += step) {
            const uint8_t* p = row + static_cast<size_t>(x) * 4;
            const double b = p[0];
            const double g = p[1];
            const double r = p[2];

            // Rec.709 luma.
            const double yf = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const uint8_t yi = to8(yf);
            ++out.luma[yi];
            ++out.red[p[2]];
            ++out.green[p[1]];
            ++out.blue[p[0]];

            const int col = static_cast<int>(
                static_cast<int64_t>(x) * wfCols / width);
            ++out.waveform[static_cast<size_t>(col) * 256 + yi];

            // Rec.709 chroma; 128 is neutral.
            const double cb = (b - yf) / 1.8556 + 128.0;
            const double cr = (r - yf) / 1.5748 + 128.0;
            const int cbi = std::clamp(static_cast<int>(std::lround(cb)), 0, 255);
            const int cri = std::clamp(static_cast<int>(std::lround(cr)), 0, 255);
            const int gx = vsSize <= 1 ? 0 : cbi * (vsSize - 1) / 255;
            const int gy = vsSize <= 1 ? 0 : cri * (vsSize - 1) / 255;
            ++out.vectorscope[static_cast<size_t>(gy) * vsSize + gx];
            ++out.sampleCount;
        }
    }

    for (const std::array<uint32_t, 256>* channel :
         {&out.luma, &out.red, &out.green, &out.blue}) {
        for (uint32_t count : *channel) {
            out.histogramMax = std::max(out.histogramMax, count);
        }
    }
    if (!out.waveform.empty()) {
        out.waveformMax = *std::max_element(out.waveform.begin(),
                                            out.waveform.end());
    }
    if (!out.vectorscope.empty()) {
        out.vectorscopeMax = *std::max_element(out.vectorscope.begin(),
                                               out.vectorscope.end());
    }
    return out;
}

} // namespace bl
