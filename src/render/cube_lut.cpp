#include <bl_render/cube_lut.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace bl {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

Result<CubeLut> parseCubeLut(const std::string& text) {
    CubeLut lut;
    std::istringstream stream(text);
    std::string line;
    bool sawSize = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        std::istringstream ls(t);
        std::string keyword;
        ls >> keyword;
        const std::string key = upper(keyword);

        if (key == "TITLE") {
            // The title is everything after the keyword, quotes stripped.
            std::string rest = trim(t.substr(keyword.size()));
            if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
                rest = rest.substr(1, rest.size() - 2);
            }
            lut.title = rest;
            continue;
        }
        if (key == "LUT_3D_SIZE" || key == "LUT_1D_SIZE") {
            int n = 0;
            if (!(ls >> n) || n < 2) {
                return Result<CubeLut>::err(Err::InvalidArgument,
                                            "Invalid LUT size");
            }
            lut.size = n;
            lut.is3d = (key == "LUT_3D_SIZE");
            sawSize = true;
            continue;
        }
        if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (!(ls >> r >> g >> b)) {
                return Result<CubeLut>::err(Err::InvalidArgument,
                                            "Invalid DOMAIN line");
            }
            if (key == "DOMAIN_MIN") {
                lut.domainMin = {r, g, b};
            } else {
                lut.domainMax = {r, g, b};
            }
            continue;
        }

        // Otherwise the line must be a data row: three floats. `ls` already
        // consumed the first token as a keyword, so parse the row afresh.
        float r = 0.0f, g = 0.0f, b = 0.0f;
        std::istringstream row(t);
        if (!(row >> r >> g >> b)) {
            return Result<CubeLut>::err(Err::InvalidArgument,
                                        "Malformed LUT data row: " + t);
        }
        lut.table.push_back(r);
        lut.table.push_back(g);
        lut.table.push_back(b);
    }

    if (!sawSize) {
        return Result<CubeLut>::err(Err::InvalidArgument,
                                    "Missing LUT_3D_SIZE/LUT_1D_SIZE");
    }
    if (!lut.valid()) {
        return Result<CubeLut>::err(
            Err::InvalidArgument, "LUT table length does not match its size");
    }
    return lut;
}

Result<CubeLut> loadCubeLutFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Result<CubeLut>::err(Err::FileNotFound,
                                    "Cannot open LUT: " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseCubeLut(buffer.str());
}

namespace {

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

// Maps a channel through the LUT's domain into [0, 1].
float toNormalized(float v, float lo, float hi) {
    const float span = hi - lo;
    if (std::abs(span) < 1e-6f) return 0.0f;
    return clamp01((v - lo) / span);
}

} // namespace

std::array<float, 3> sampleCubeLut(const CubeLut& lut, float r, float g,
                                   float b) {
    std::array<float, 3> input{
        clamp01(toNormalized(r, lut.domainMin[0], lut.domainMax[0])),
        clamp01(toNormalized(g, lut.domainMin[1], lut.domainMax[1])),
        clamp01(toNormalized(b, lut.domainMin[2], lut.domainMax[2]))};

    if (!lut.valid()) return input;

    const int n = lut.size;
    const auto entry = [&](int ri, int gi, int bi) -> const float* {
        const std::size_t index =
            static_cast<std::size_t>(ri) +
            static_cast<std::size_t>(gi) * static_cast<std::size_t>(n) +
            static_cast<std::size_t>(bi) * static_cast<std::size_t>(n) *
                static_cast<std::size_t>(n);
        return lut.table.data() + index * 3;
    };

    if (!lut.is3d) {
        // 1D LUT: the same ramp applies to every channel independently.
        std::array<float, 3> out{};
        for (int c = 0; c < 3; ++c) {
            const float scaled = input[c] * (n - 1);
            const int i0 = std::min(static_cast<int>(std::floor(scaled)), n - 1);
            const int i1 = std::min(i0 + 1, n - 1);
            const float fx = scaled - i0;
            const float v0 = lut.table[static_cast<std::size_t>(i0) * 3 + c];
            const float v1 = lut.table[static_cast<std::size_t>(i1) * 3 + c];
            out[c] = v0 + (v1 - v0) * fx;
        }
        return out;
    }

    const float sr = input[0] * (n - 1);
    const float sg = input[1] * (n - 1);
    const float sb = input[2] * (n - 1);

    const int r0 = std::min(static_cast<int>(std::floor(sr)), n - 1);
    const int g0 = std::min(static_cast<int>(std::floor(sg)), n - 1);
    const int b0 = std::min(static_cast<int>(std::floor(sb)), n - 1);
    const int r1 = std::min(r0 + 1, n - 1);
    const int g1 = std::min(g0 + 1, n - 1);
    const int b1 = std::min(b0 + 1, n - 1);

    const float fr = sr - r0;
    const float fg = sg - g0;
    const float fb = sb - b0;

    const float* c000 = entry(r0, g0, b0);
    const float* c100 = entry(r1, g0, b0);
    const float* c010 = entry(r0, g1, b0);
    const float* c110 = entry(r1, g1, b0);
    const float* c001 = entry(r0, g0, b1);
    const float* c101 = entry(r1, g0, b1);
    const float* c011 = entry(r0, g1, b1);
    const float* c111 = entry(r1, g1, b1);

    std::array<float, 3> out{};
    for (int c = 0; c < 3; ++c) {
        const float c00 = c000[c] * (1.0f - fr) + c100[c] * fr;
        const float c10 = c010[c] * (1.0f - fr) + c110[c] * fr;
        const float c01 = c001[c] * (1.0f - fr) + c101[c] * fr;
        const float c11 = c011[c] * (1.0f - fr) + c111[c] * fr;
        const float c0 = c00 * (1.0f - fg) + c10 * fg;
        const float c1 = c01 * (1.0f - fg) + c11 * fg;
        out[c] = c0 * (1.0f - fb) + c1 * fb;
    }
    return out;
}

} // namespace bl