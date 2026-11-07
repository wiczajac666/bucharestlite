#pragma once

#include <cstdint>
#include <vector>

namespace bl {

// One decoded, effect- and mask-processed clip layer waiting to be composited.
// The compositor prepares these in timeline order; a backend blends them
// bottom-to-top into the output frame.
struct CompositeLayer {
    std::vector<uint8_t> data;  // BGRA32
    uint32_t width{0};
    uint32_t height{0};
    uint32_t linesize{0};
    double opacity{1.0};
};

} // namespace bl
