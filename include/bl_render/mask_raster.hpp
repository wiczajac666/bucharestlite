#pragma once

#include <bl_timeline/mask.hpp>

#include <cstdint>
#include <vector>

namespace bl {

// Rasterises one mask into an 8-bit coverage buffer (row-major, width * height).
// Coordinates are normalised to the frame; `feather` is a fraction of the
// shorter side. A mask with fewer than three points, or a disabled mask,
// covers everything so an in-progress mask never blanks its clip.
std::vector<uint8_t> rasteriseMask(const Mask& mask, uint32_t width,
                                   uint32_t height);

// Rasterises several masks and unions their coverage (each honouring invert and
// opacity). Returns an empty buffer when no enabled mask contributes, which
// callers treat as "no masking".
std::vector<uint8_t> rasteriseMasks(const std::vector<Mask>& masks,
                                    uint32_t width, uint32_t height);

// Multiplies the alpha channel of a BGRA32 frame by `coverage` (same
// width/height as the frame). A coverage buffer of the wrong size is ignored.
void applyMaskCoverage(std::vector<uint8_t>& data, uint32_t width,
                       uint32_t height, uint32_t linesize,
                       const std::vector<uint8_t>& coverage);

} // namespace bl
