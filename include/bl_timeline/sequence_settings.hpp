#pragma once

#include <bl_core/time.hpp>

#include <cstdint>
#include <string>

namespace bl {

struct SequenceSettings {
    int32_t width{1920};
    int32_t height{1080};
    Rational fps{24000, 1001};
    int32_t sampleRate{48000};
    int32_t channelLayout{2};
};

inline bool operator==(const SequenceSettings& a, const SequenceSettings& b) {
    return a.width == b.width && a.height == b.height &&
           a.fps == b.fps && a.sampleRate == b.sampleRate &&
           a.channelLayout == b.channelLayout;
}

inline bool operator!=(const SequenceSettings& a, const SequenceSettings& b) {
    return !(a == b);
}

} // namespace bl
