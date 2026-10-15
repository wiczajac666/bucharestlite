#pragma once

#include <bl_core/result.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace bl {

// A parsed Adobe `.cube` colour lookup table (1D or 3D). `table` holds three
// floats (r, g, b) per entry; for 3D tables the red component varies fastest,
// matching the `.cube` file order.
struct CubeLut {
    std::string title;
    int size{0};
    bool is3d{true};
    std::array<float, 3> domainMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> domainMax{1.0f, 1.0f, 1.0f};
    std::vector<float> table;

    std::size_t entryCount() const noexcept {
        if (size <= 0) return 0;
        const std::size_t n = static_cast<std::size_t>(size);
        return is3d ? n * n * n : n;
    }
    std::size_t expectedFloats() const noexcept { return entryCount() * 3; }
    bool valid() const noexcept {
        return size > 0 && table.size() == expectedFloats();
    }
};

// Parses `.cube` text. Returns InvalidArgument (with a message) for a missing
// size header, bad numbers, or a table whose length does not match the
// declared size.
Result<CubeLut> parseCubeLut(const std::string& text);

// Reads and parses a `.cube` file from disk.
Result<CubeLut> loadCubeLutFile(const std::string& path);

// Samples the LUT at normalized (r, g, b) in [0, 1] using trilinear (3D) or
// linear (1D) interpolation. Values outside the table clamp to the edge. An
// invalid LUT returns the clamped input unchanged.
std::array<float, 3> sampleCubeLut(const CubeLut& lut, float r, float g,
                                   float b);

} // namespace bl