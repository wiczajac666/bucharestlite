#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace bl {

struct AudioMixConfig {
    uint32_t sampleRate{48000};
    uint32_t channels{2};
};

struct AudioSpan {
    float* data{nullptr};
    uint32_t sampleCount{0};
    uint32_t channels{0};
    uint32_t linesize{0};

    float* channelData(uint32_t ch) {
        return data + ch * linesize;
    }
    const float* channelData(uint32_t ch) const {
        return data + ch * linesize;
    }
};

struct AudioBuffer {
    std::vector<float> data;
    uint32_t sampleCount{0};
    uint32_t channels{0};
    uint32_t linesize{0};

    AudioSpan span() {
        return {data.data(), sampleCount, channels, linesize};
    }

    float* channelData(uint32_t ch) {
        return data.data() + ch * linesize;
    }
    const float* channelData(uint32_t ch) const {
        return data.data() + ch * linesize;
    }

    void resize(uint32_t samples, uint32_t ch) {
        channels = ch;
        sampleCount = samples;
        linesize = samples;
        data.resize(static_cast<size_t>(samples) * ch);
    }

    void clear() {
        std::fill(data.begin(), data.end(), 0.0f);
    }
};

using GainDb = double;

inline double dbToLinear(GainDb db) {
    return std::pow(10.0, db / 20.0);
}

inline GainDb linearToDb(double linear) {
    if (linear <= 0.0) return -1000.0;
    return 20.0 * std::log10(linear);
}

struct PanLaw {
    static std::pair<double, double> compute(double pan) {
        pan = std::clamp(pan, -1.0, 1.0);
        double angle = (pan + 1.0) * (M_PI / 4.0);
        return {std::cos(angle), std::sin(angle)};
    }
};

} // namespace bl
