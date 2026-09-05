#include <bl_render/effect.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace bl {

// --- EffectRegistry ---

EffectRegistry& EffectRegistry::instance() {
    static EffectRegistry reg;
    return reg;
}

void EffectRegistry::registerEffect(std::unique_ptr<IEffect> effect) {
    effects_.push_back(std::move(effect));
}

IEffect* EffectRegistry::find(std::string_view name) const noexcept {
    for (const auto& e : effects_) {
        if (e->name() == name) return e.get();
    }
    return nullptr;
}

size_t EffectRegistry::count() const noexcept { return effects_.size(); }

void EffectRegistry::clear() noexcept { effects_.clear(); }

// --- Box Blur ---

static void boxBlur(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
                     uint32_t linesize, int radius) {
    if (radius <= 0 || width == 0 || height == 0) return;
    const int diameter = radius * 2 + 1;
    std::vector<uint8_t> tmp(data.size());

    // Horizontal pass
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* row = data.data() + y * linesize;
            uint8_t* out = tmp.data() + y * linesize;
            int sumB = 0, sumG = 0, sumR = 0, sumA = 0;

            for (int x = -radius; x <= radius; ++x) {
                int sx = std::clamp(x, 0, static_cast<int>(width) - 1);
                const uint8_t* px = row + sx * 4;
                sumB += px[0]; sumG += px[1]; sumR += px[2]; sumA += px[3];
            }

            for (uint32_t x = 0; x < width; ++x) {
                out[x * 4 + 0] = static_cast<uint8_t>(sumB / diameter);
                out[x * 4 + 1] = static_cast<uint8_t>(sumG / diameter);
                out[x * 4 + 2] = static_cast<uint8_t>(sumR / diameter);
                out[x * 4 + 3] = static_cast<uint8_t>(sumA / diameter);

                int removeX = std::clamp(static_cast<int>(x) - radius, 0, static_cast<int>(width) - 1);
                int addX = std::clamp(static_cast<int>(x) + radius + 1, 0,
                                      static_cast<int>(width) - 1);
                const uint8_t* remPx = row + removeX * 4;
                const uint8_t* addPx = row + addX * 4;
                sumB += addPx[0] - remPx[0];
                sumG += addPx[1] - remPx[1];
                sumR += addPx[2] - remPx[2];
                sumA += addPx[3] - remPx[3];
            }
        }

        // Vertical pass
        for (uint32_t x = 0; x < width; ++x) {
            int sumB = 0, sumG = 0, sumR = 0, sumA = 0;

            for (int y = -radius; y <= radius; ++y) {
                int sy = std::clamp(y, 0, static_cast<int>(height) - 1);
                const uint8_t* px = tmp.data() + sy * linesize + x * 4;
                sumB += px[0]; sumG += px[1]; sumR += px[2]; sumA += px[3];
            }

            for (uint32_t y = 0; y < height; ++y) {
                uint8_t* out = data.data() + y * linesize + x * 4;
                out[0] = static_cast<uint8_t>(sumB / diameter);
                out[1] = static_cast<uint8_t>(sumG / diameter);
                out[2] = static_cast<uint8_t>(sumR / diameter);
                out[3] = static_cast<uint8_t>(sumA / diameter);

                int removeY = std::clamp(static_cast<int>(y) - radius, 0, static_cast<int>(height) - 1);
                int addY = std::clamp(static_cast<int>(y) + radius + 1, 0,
                                      static_cast<int>(height) - 1);
                const uint8_t* remPx = tmp.data() + removeY * linesize + x * 4;
                const uint8_t* addPx = tmp.data() + addY * linesize + x * 4;
            sumB += addPx[0] - remPx[0];
            sumG += addPx[1] - remPx[1];
            sumR += addPx[2] - remPx[2];
            sumA += addPx[3] - remPx[3];
        }
    }
}

class BoxBlurEffect : public IEffect {
public:
    std::string_view name() const override { return "blur.box"; }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json& params) override {
        int radius = (params.is_object()) ? params.value("radius", 1) : 1;
        radius = std::clamp(radius, 0, 20);
        boxBlur(data, width, height, linesize, radius);
    }
};

// --- Brightness / Contrast / Gamma ---

class BrightnessContrastGammaEffect : public IEffect {
public:
    std::string_view name() const override { return "brightness_contrast_gamma"; }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json& params) override {
        const bool hasParams = params.is_object();
        double brightness = hasParams ? params.value("brightness", 0.0) : 0.0;
        double contrast = hasParams ? params.value("contrast", 1.0) : 1.0;
        double gamma = hasParams ? params.value("gamma", 1.0) : 1.0;

        brightness = std::clamp(brightness, -1.0, 1.0);
        contrast = std::clamp(contrast, 0.0, 3.0);
        gamma = std::clamp(gamma, 0.1, 5.0);

        const double invGamma = 1.0 / gamma;
        const double contrastFactor = (contrast - 1.0);

        uint8_t lut[256];
        for (int i = 0; i < 256; ++i) {
            double v = i / 255.0;
            v += brightness;
            v = (v - 0.5) * (1.0 + contrastFactor) + 0.5;
            v = std::clamp(v, 0.0, 1.0);
            v = std::pow(v, invGamma);
            lut[i] = static_cast<uint8_t>(std::clamp(v * 255.0, 0.0, 255.0));
        }

        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = data.data() + y * linesize;
            for (uint32_t x = 0; x < width; ++x) {
                row[x * 4 + 0] = lut[row[x * 4 + 0]];
                row[x * 4 + 1] = lut[row[x * 4 + 1]];
                row[x * 4 + 2] = lut[row[x * 4 + 2]];
            }
        }
    }
};

// --- Greyscale ---

class GreyscaleEffect : public IEffect {
public:
    std::string_view name() const override { return "greyscale"; }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json&) override {
        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = data.data() + y * linesize;
            for (uint32_t x = 0; x < width; ++x) {
                uint8_t b = row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t r = row[x * 4 + 2];
                uint8_t grey = static_cast<uint8_t>(0.299 * r + 0.587 * g + 0.114 * b);
                row[x * 4 + 0] = grey;
                row[x * 4 + 1] = grey;
                row[x * 4 + 2] = grey;
            }
        }
    }
};

// --- Transform 2D (nearest-neighbor) ---

class Transform2DEffect : public IEffect {
public:
    std::string_view name() const override { return "transform_2d"; }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json& params) override {
        const bool hasParams = params.is_object();
        double scaleX = hasParams ? params.value("scaleX", 1.0) : 1.0;
        double scaleY = hasParams ? params.value("scaleY", 1.0) : 1.0;
        double translateX = hasParams ? params.value("translateX", 0.0) : 0.0;
        double translateY = hasParams ? params.value("translateY", 0.0) : 0.0;

        if (scaleX == 1.0 && scaleY == 1.0 && translateX == 0.0 && translateY == 0.0) {
            return;
        }

        std::vector<uint8_t> output(data.size());
        std::fill(output.begin(), output.end(), 0);

        const double cx = width / 2.0;
        const double cy = height / 2.0;
        const double invScaleX = 1.0 / scaleX;
        const double invScaleY = 1.0 / scaleY;

        for (uint32_t dy = 0; dy < height; ++dy) {
            uint8_t* outRow = output.data() + dy * linesize;
            for (uint32_t dx = 0; dx < width; ++dx) {
                double srcX = (dx - cx - translateX) * invScaleX + cx;
                double srcY = (dy - cy - translateY) * invScaleY + cy;
                int sx = static_cast<int>(std::round(srcX));
                int sy = static_cast<int>(std::round(srcY));

                if (sx >= 0 && sx < static_cast<int>(width) && sy >= 0 &&
                    sy < static_cast<int>(height)) {
                    const uint8_t* srcRow = data.data() + sy * linesize;
                    outRow[dx * 4 + 0] = srcRow[sx * 4 + 0];
                    outRow[dx * 4 + 1] = srcRow[sx * 4 + 1];
                    outRow[dx * 4 + 2] = srcRow[sx * 4 + 2];
                    outRow[dx * 4 + 3] = srcRow[sx * 4 + 3];
                }
            }
        }

        data = std::move(output);
    }
};

// --- Register builtins ---

void registerBuiltinEffects() {
    auto& reg = EffectRegistry::instance();
    reg.registerEffect(std::make_unique<BoxBlurEffect>());
    reg.registerEffect(std::make_unique<BrightnessContrastGammaEffect>());
    reg.registerEffect(std::make_unique<GreyscaleEffect>());
    reg.registerEffect(std::make_unique<Transform2DEffect>());
}

} // namespace bl
