#include <bl_render/mask_raster.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bl {

namespace {

constexpr int kSubSamples = 4;
constexpr int kSmoothSteps = 16;

struct Point {
    double x{0.0};
    double y{0.0};
};

// Expands a smooth mask's points into a dense closed Catmull-Rom polyline.
// The spline interpolates every control point, so dragging a point moves the
// curve through it (the same affordance a bezier editor gives).
std::vector<Point> flattenSmooth(const std::vector<Point>& control) {
    const size_t n = control.size();
    if (n < 3) return control;
    std::vector<Point> out;
    out.reserve(n * kSmoothSteps);
    for (size_t i = 0; i < n; ++i) {
        const Point& p0 = control[(i + n - 1) % n];
        const Point& p1 = control[i];
        const Point& p2 = control[(i + 1) % n];
        const Point& p3 = control[(i + 2) % n];
        for (int s = 0; s < kSmoothSteps; ++s) {
            const double t = static_cast<double>(s) / kSmoothSteps;
            const double t2 = t * t;
            const double t3 = t2 * t;
            Point p;
            p.x = 0.5 * ((2.0 * p1.x) + (-p0.x + p2.x) * t +
                         (2.0 * p0.x - 5.0 * p1.x + 4.0 * p2.x - p3.x) * t2 +
                         (-p0.x + 3.0 * p1.x - 3.0 * p2.x + p3.x) * t3);
            p.y = 0.5 * ((2.0 * p1.y) + (-p0.y + p2.y) * t +
                         (2.0 * p0.y - 5.0 * p1.y + 4.0 * p2.y - p3.y) * t2 +
                         (-p0.y + 3.0 * p1.y - 3.0 * p2.y + p3.y) * t3);
            out.push_back(p);
        }
    }
    return out;
}

// Scanline even-odd fill with vertical supersampling and fractional horizontal
// coverage, written into a float coverage buffer (0..255).
void fillPolygon(const std::vector<Point>& poly, uint32_t width,
                 uint32_t height, std::vector<float>& coverage) {
    if (poly.size() < 3) return;
    const double subWeight = 1.0 / kSubSamples;
    std::vector<double> crossings;
    for (uint32_t y = 0; y < height; ++y) {
        for (int s = 0; s < kSubSamples; ++s) {
            const double sy =
                static_cast<double>(y) + (s + 0.5) / kSubSamples;
            crossings.clear();
            for (size_t i = 0; i < poly.size(); ++i) {
                const Point& a = poly[i];
                const Point& b = poly[(i + 1) % poly.size()];
                if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
                    const double t = (sy - a.y) / (b.y - a.y);
                    crossings.push_back(a.x + t * (b.x - a.x));
                }
            }
            std::sort(crossings.begin(), crossings.end());
            for (size_t k = 0; k + 1 < crossings.size(); k += 2) {
                double x0 = crossings[k];
                double x1 = crossings[k + 1];
                if (x1 <= x0) continue;
                // Pixel centres map to [px, px+1); accumulate the covered
                // fraction of each pixel in the span.
                const int px0 = std::max(0, static_cast<int>(std::floor(x0)));
                const int px1 = std::min(
                    static_cast<int>(width) - 1,
                    static_cast<int>(std::ceil(x1)) - 1);
                for (int px = px0; px <= px1; ++px) {
                    const double left = std::max(x0, static_cast<double>(px));
                    const double right =
                        std::min(x1, static_cast<double>(px) + 1.0);
                    if (right <= left) continue;
                    coverage[static_cast<size_t>(y) * width +
                             static_cast<size_t>(px)] +=
                        static_cast<float>((right - left) * subWeight * 255.0);
                }
            }
        }
    }
}

// Separable box blur on the coverage buffer, approximating a Gaussian feather.
void boxBlur(std::vector<float>& buf, uint32_t width, uint32_t height,
             int radius, bool horizontal) {
    if (radius < 1) return;
    const int len = static_cast<int>(horizontal ? width : height);
    const int lines = static_cast<int>(horizontal ? height : width);
    const int window = radius * 2 + 1;
    std::vector<float> line(static_cast<size_t>(len));
    for (int l = 0; l < lines; ++l) {
        for (int i = 0; i < len; ++i) {
            const size_t idx = horizontal
                                   ? static_cast<size_t>(l) * width +
                                         static_cast<size_t>(i)
                                   : static_cast<size_t>(i) * width +
                                         static_cast<size_t>(l);
            line[static_cast<size_t>(i)] = buf[idx];
        }
        const auto at = [&line, len](int i) {
            return line[static_cast<size_t>(std::clamp(i, 0, len - 1))];
        };
        float sum = 0.0f;
        for (int i = -radius; i <= radius; ++i) sum += at(i);
        for (int i = 0; i < len; ++i) {
            const size_t idx = horizontal
                                   ? static_cast<size_t>(l) * width +
                                         static_cast<size_t>(i)
                                   : static_cast<size_t>(i) * width +
                                         static_cast<size_t>(l);
            buf[idx] = sum / static_cast<float>(window);
            sum += at(i + radius + 1) - at(i - radius);
        }
    }
}

} // namespace

std::vector<uint8_t> rasteriseMask(const Mask& mask, uint32_t width,
                                   uint32_t height) {
    std::vector<uint8_t> full(static_cast<size_t>(width) * height, 255);
    if (width == 0 || height == 0) return {};
    if (!mask.enabled) return full;
    if (mask.points.size() < 3) return full;

    std::vector<Point> poly;
    poly.reserve(mask.points.size());
    for (const MaskPoint& p : mask.points) {
        poly.push_back({p.x * width, p.y * height});
    }
    if (mask.smooth) poly = flattenSmooth(poly);

    std::vector<float> coverage(static_cast<size_t>(width) * height, 0.0f);
    fillPolygon(poly, width, height, coverage);

    if (mask.feather > 0.0) {
        const int radius = static_cast<int>(std::lround(
            mask.feather * static_cast<double>(std::min(width, height))));
        if (radius > 0) {
            boxBlur(coverage, width, height, radius, true);
            boxBlur(coverage, width, height, radius, false);
        }
    }

    std::vector<uint8_t> out(static_cast<size_t>(width) * height);
    const double opacity = std::clamp(mask.opacity, 0.0, 1.0);
    for (size_t i = 0; i < out.size(); ++i) {
        double v = std::clamp(coverage[i], 0.0f, 255.0f);
        if (mask.invert) v = 255.0 - v;
        v *= opacity;
        out[i] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
    }
    return out;
}

std::vector<uint8_t> rasteriseMasks(const std::vector<Mask>& masks,
                                    uint32_t width, uint32_t height) {
    std::vector<uint8_t> result;
    for (const Mask& mask : masks) {
        if (!mask.enabled) continue;
        std::vector<uint8_t> coverage = rasteriseMask(mask, width, height);
        if (coverage.empty()) continue;
        if (result.empty()) {
            result = std::move(coverage);
            continue;
        }
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = std::max(result[i], coverage[i]);
        }
    }
    return result;
}

void applyMaskCoverage(std::vector<uint8_t>& data, uint32_t width,
                       uint32_t height, uint32_t linesize,
                       const std::vector<uint8_t>& coverage) {
    if (coverage.size() != static_cast<size_t>(width) * height) return;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = data.data() + static_cast<size_t>(y) * linesize;
        const uint8_t* cov = coverage.data() + static_cast<size_t>(y) * width;
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t a = row[x * 4 + 3];
            row[x * 4 + 3] = static_cast<uint8_t>(
                (a * static_cast<uint32_t>(cov[x]) + 127u) / 255u);
        }
    }
}

} // namespace bl
