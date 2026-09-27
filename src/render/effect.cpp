#include <bl_render/effect.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace bl {

// --- EffectRegistry ---

void EffectRegistry::registerEffect(std::unique_ptr<IEffect> effect) {
    effects_.push_back(std::move(effect));
}

IEffect* EffectRegistry::find(std::string_view name) const noexcept {
    for (const auto& e : effects_) {
        if (e->name() == name) return e.get();
    }
    return nullptr;
}

std::vector<std::string> EffectRegistry::catalog() const {
    std::vector<std::string> names;
    names.reserve(effects_.size());
    for (const auto& e : effects_) {
        names.emplace_back(e->name());
    }
    std::sort(names.begin(), names.end());
    return names;
}

size_t EffectRegistry::count() const noexcept { return effects_.size(); }

void EffectRegistry::clear() noexcept { effects_.clear(); }

// --- IEffect defaults ---

std::string_view IEffect::displayName() const { return name(); }

std::vector<ParamSpec> IEffect::paramSpecs() const { return {}; }

nlohmann::json makeDefaultParams(const std::vector<ParamSpec>& specs) {
    nlohmann::json params = nlohmann::json::object();
    for (const auto& spec : specs) {
        params[spec.key] = spec.def;
    }
    return params;
}

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
    std::string_view displayName() const override { return "Box Blur"; }
    std::vector<ParamSpec> paramSpecs() const override {
        return {{"radius", "Radius", 0.0, 20.0, 1.0}};
    }
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
    std::string_view displayName() const override { return "Color Correction"; }
    std::vector<ParamSpec> paramSpecs() const override {
        return {
            {"brightness", "Brightness", -1.0, 1.0, 0.0},
            {"contrast", "Contrast", 0.0, 3.0, 1.0},
            {"gamma", "Gamma", 0.1, 5.0, 1.0},
        };
    }
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
    std::string_view displayName() const override { return "Greyscale"; }
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
    std::string_view displayName() const override { return "Transform 2D"; }
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

// --- Chroma Key ---

class ChromaKeyEffect : public IEffect {
public:
    std::string_view name() const override { return "chroma_key"; }
    std::string_view displayName() const override { return "Chroma Key"; }
    std::vector<ParamSpec> paramSpecs() const override {
        return {
            {"keyR", "Key Red", 0.0, 255.0, 0.0},
            {"keyG", "Key Green", 0.0, 255.0, 255.0},
            {"keyB", "Key Blue", 0.0, 255.0, 0.0},
            {"similarity", "Similarity", 0.0, 1.0, 0.2},
            {"smoothness", "Smoothness", 0.0, 1.0, 0.1},
        };
    }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json& params) override {
        const bool has = params.is_object();
        const double keyR = has ? params.value("keyR", 0.0) : 0.0;
        const double keyG = has ? params.value("keyG", 255.0) : 255.0;
        const double keyB = has ? params.value("keyB", 0.0) : 0.0;
        double thresh = std::clamp(has ? params.value("similarity", 0.2) : 0.2,
                                   0.0, 1.0);
        double soft = std::clamp(has ? params.value("smoothness", 0.1) : 0.1,
                                 0.0, 1.0);

        const double invSqrt3 = 1.0 / std::sqrt(3.0);

        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = data.data() + y * linesize;
            for (uint32_t x = 0; x < width; ++x) {
                uint8_t* px = row + x * 4;
                const double dr = (px[2] - keyR) / 255.0;
                const double dg = (px[1] - keyG) / 255.0;
                const double db = (px[0] - keyB) / 255.0;
                const double dist =
                    std::sqrt(dr * dr + dg * dg + db * db) * invSqrt3;

                uint8_t a;
                if (dist <= thresh) {
                    a = 0;
                } else if (soft <= 0.0 || dist >= thresh + soft) {
                    a = 255;
                } else {
                    a = static_cast<uint8_t>((dist - thresh) / soft * 255.0);
                }
                px[3] = a;
            }
        }
    }
};

// --- Sharpen (unsharp mask) ---

class SharpenEffect : public IEffect {
public:
    std::string_view name() const override { return "sharpen"; }
    std::string_view displayName() const override { return "Sharpen"; }
    std::vector<ParamSpec> paramSpecs() const override {
        return {
            {"amount", "Amount", 0.0, 4.0, 1.0},
            {"radius", "Radius", 1.0, 3.0, 1.0},
        };
    }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json& params) override {
        const bool has = params.is_object();
        double amount = std::clamp(has ? params.value("amount", 1.0) : 1.0,
                                   0.0, 4.0);
        int radius = std::clamp(has ? params.value("radius", 1) : 1, 1, 3);
        if (amount <= 0.0) return;

        std::vector<uint8_t> orig = data;
        boxBlur(data, width, height, linesize, radius);

        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* oRow = orig.data() + y * linesize;
            const uint8_t* bRow = data.data() + y * linesize;
            uint8_t* out = data.data() + y * linesize;
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t i = x * 4;
                for (int c = 0; c < 3; ++c) {
                    const double v = oRow[i + c] +
                        amount * (oRow[i + c] - static_cast<double>(bRow[i + c]));
                    out[i + c] =
                        static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
                }
                out[i + 3] = oRow[i + 3];
            }
        }
    }
};

// --- Hue / Saturation ---

static void rgbToHsl(double r, double g, double b, double& h, double& s, double& l) {
    r /= 255.0; g /= 255.0; b /= 255.0;
    const double maxc = std::max({r, g, b});
    const double minc = std::min({r, g, b});
    l = (maxc + minc) / 2.0;

    const double d = maxc - minc;
    if (d == 0.0) {
        h = 0.0;
        s = 0.0;
        return;
    }
    s = (l <= 0.5) ? d / (maxc + minc) : d / (2.0 - maxc - minc);

    if (maxc == r) {
        h = 60.0 * std::fmod((g - b) / d, 6.0);
    } else if (maxc == g) {
        h = 60.0 * ((b - r) / d + 2.0);
    } else {
        h = 60.0 * ((r - g) / d + 4.0);
    }
    if (h < 0.0) h += 360.0;
}

static double hue2rgb(double p, double q, double t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

static void hslToRgb(double h, double s, double l, double& r, double& g, double& b) {
    if (s == 0.0) {
        r = g = b = l;
        return;
    }
    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;
    r = hue2rgb(p, q, h / 360.0 + 1.0 / 3.0);
    g = hue2rgb(p, q, h / 360.0);
    b = hue2rgb(p, q, h / 360.0 - 1.0 / 3.0);
}

class HueSaturationEffect : public IEffect {
public:
    std::string_view name() const override { return "hue_saturation"; }
    std::string_view displayName() const override { return "Hue / Saturation"; }
    std::vector<ParamSpec> paramSpecs() const override {
        return {
            {"hueShift", "Hue", -180.0, 180.0, 0.0},
            {"saturation", "Saturation", 0.0, 3.0, 1.0},
            {"lightness", "Lightness", -1.0, 1.0, 0.0},
        };
    }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json& params) override {
        const bool has = params.is_object();
        double hueShift = std::clamp(has ? params.value("hueShift", 0.0) : 0.0,
                                     -180.0, 180.0);
        double satMul = std::clamp(has ? params.value("saturation", 1.0) : 1.0,
                                   0.0, 3.0);
        double lightAdd = std::clamp(has ? params.value("lightness", 0.0) : 0.0,
                                     -1.0, 1.0);
        if (hueShift == 0.0 && satMul == 1.0 && lightAdd == 0.0) return;

        for (uint32_t y = 0; y < height; ++y) {
            uint8_t* row = data.data() + y * linesize;
            for (uint32_t x = 0; x < width; ++x) {
                uint8_t* px = row + x * 4;
                double h, s, l;
                rgbToHsl(px[2], px[1], px[0], h, s, l);
                h += hueShift;
                if (h < 0.0) h += 360.0;
                if (h >= 360.0) h -= 360.0;
                s = std::clamp(s * satMul, 0.0, 1.0);
                l = std::clamp(l + lightAdd, 0.0, 1.0);
                double nr, ng, nb;
                hslToRgb(h, s, l, nr, ng, nb);
                px[0] =
                    static_cast<uint8_t>(std::clamp(nb * 255.0, 0.0, 255.0));
                px[1] =
                    static_cast<uint8_t>(std::clamp(ng * 255.0, 0.0, 255.0));
                px[2] =
                    static_cast<uint8_t>(std::clamp(nr * 255.0, 0.0, 255.0));
            }
        }
    }
};

// --- Levels / Curves ---

class LevelsCurvesEffect : public IEffect {
public:
    std::string_view name() const override { return "levels_curves"; }
    std::string_view displayName() const override { return "Levels / Curves"; }
    std::vector<ParamSpec> paramSpecs() const override {
        return {
            {"black", "Black Point", 0.0, 1.0, 0.0},
            {"white", "White Point", 0.0, 1.0, 1.0},
            {"gamma", "Gamma", 0.1, 5.0, 1.0},
            {"c0", "Curve 0", 0.0, 1.0, 0.0},
            {"c1", "Curve 1", 0.0, 1.0, 0.25},
            {"c2", "Curve 2", 0.0, 1.0, 0.5},
            {"c3", "Curve 3", 0.0, 1.0, 0.75},
            {"c4", "Curve 4", 0.0, 1.0, 1.0},
        };
    }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json& params) override {
        const bool has = params.is_object();
        double black = std::clamp(has ? params.value("black", 0.0) : 0.0,
                                  0.0, 1.0);
        double white = std::clamp(has ? params.value("white", 1.0) : 1.0,
                                  0.0, 1.0);
        white = std::max(white, black + 0.01);
        double gamma = std::clamp(has ? params.value("gamma", 1.0) : 1.0,
                                  0.1, 5.0);
        double anchors[5];
        for (int i = 0; i < 5; ++i) {
            const std::string key = "c" + std::to_string(i);
            anchors[i] = std::clamp(has ? params.value(key, i * 0.25) : i * 0.25,
                                    0.0, 1.0);
        }

        const double invGamma = 1.0 / gamma;
        uint8_t lut[256];
        for (int i = 0; i < 256; ++i) {
            double v = (i / 255.0 - black) / (white - black);
            v = std::clamp(v, 0.0, 1.0);
            v = std::pow(v, invGamma);

            const double seg = v * 4.0;
            const int idx = std::min(static_cast<int>(seg), 3);
            const double frac = seg - idx;
            v = anchors[idx] + (anchors[idx + 1] - anchors[idx]) * frac;
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

// --- Crop (region resampled back to full frame) ---

class CropEffect : public IEffect {
public:
    std::string_view name() const override { return "crop"; }
    std::string_view displayName() const override { return "Crop"; }
    std::vector<ParamSpec> paramSpecs() const override {
        return {
            {"left", "Left", 0.0, 0.95, 0.0},
            {"right", "Right", 0.0, 0.95, 0.0},
            {"top", "Top", 0.0, 0.95, 0.0},
            {"bottom", "Bottom", 0.0, 0.95, 0.0},
        };
    }
    void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
               uint32_t linesize, const nlohmann::json& params) override {
        if (width == 0 || height == 0) return;
        const bool has = params.is_object();
        auto clampFrac = [&](const char* key, double def) {
            return std::clamp(has ? params.value(key, def) : def, 0.0, 0.95);
        };
        const double left = clampFrac("left", 0.0);
        const double right = clampFrac("right", 0.0);
        const double top = clampFrac("top", 0.0);
        const double bottom = clampFrac("bottom", 0.0);
        if (left == 0.0 && right == 0.0 && top == 0.0 && bottom == 0.0) return;

        double sx0 = left * width;
        double sx1 = width - right * width;
        double sy0 = top * height;
        double sy1 = height - bottom * height;
        if (sx1 - sx0 < 1.0) { sx0 = 0.0; sx1 = width; }
        if (sy1 - sy0 < 1.0) { sy0 = 0.0; sy1 = height; }
        const double regionW = sx1 - sx0;
        const double regionH = sy1 - sy0;

        std::vector<uint8_t> src = data;
        const double invW1 = 1.0 / std::max(1.0, static_cast<double>(width - 1));
        const double invH1 = 1.0 / std::max(1.0, static_cast<double>(height - 1));
        for (uint32_t y = 0; y < height; ++y) {
            const double fv = sy0 + (static_cast<double>(y) * invH1) *
                                        (regionH - 1.0);
            const int y0 = std::clamp(static_cast<int>(std::floor(fv)), 0,
                                      static_cast<int>(sy1) - 1);
            const int y1 = std::min(y0 + 1, static_cast<int>(sy1) - 1);
            const double fy = fv - y0;
            uint8_t* out = data.data() + y * linesize;
            for (uint32_t x = 0; x < width; ++x) {
                const double fu = sx0 + (static_cast<double>(x) * invW1) *
                                            (regionW - 1.0);
                const int x0 = std::clamp(static_cast<int>(std::floor(fu)), 0,
                                          static_cast<int>(sx1) - 1);
                const int x1 = std::min(x0 + 1, static_cast<int>(sx1) - 1);
                const double fx = fu - x0;

                const uint8_t* p00 = src.data() + y0 * linesize + x0 * 4;
                const uint8_t* p01 = src.data() + y0 * linesize + x1 * 4;
                const uint8_t* p10 = src.data() + y1 * linesize + x0 * 4;
                const uint8_t* p11 = src.data() + y1 * linesize + x1 * 4;

                uint8_t* outP = out + x * 4;
                for (int c = 0; c < 3; ++c) {
                    const double topV =
                        p00[c] * (1.0 - fx) + p01[c] * fx;
                    const double botV =
                        p10[c] * (1.0 - fx) + p11[c] * fx;
                    outP[c] = static_cast<uint8_t>(topV * (1.0 - fy) +
                                                   botV * fy + 0.5);
                }
                outP[3] = p00[3];
            }
        }
    }
};

// --- Register builtins ---

namespace {

void registerBuiltinEffectsTo(EffectRegistry& reg) {
    reg.registerEffect(std::make_unique<BoxBlurEffect>());
    reg.registerEffect(std::make_unique<BrightnessContrastGammaEffect>());
    reg.registerEffect(std::make_unique<GreyscaleEffect>());
    reg.registerEffect(std::make_unique<Transform2DEffect>());
    reg.registerEffect(std::make_unique<ChromaKeyEffect>());
    reg.registerEffect(std::make_unique<SharpenEffect>());
    reg.registerEffect(std::make_unique<HueSaturationEffect>());
    reg.registerEffect(std::make_unique<LevelsCurvesEffect>());
    reg.registerEffect(std::make_unique<CropEffect>());
}

} // namespace

// Builtins are registered on first use so the singleton is useful in the app
// (and in test binaries) without an explicit startup call on every entry point.
EffectRegistry& EffectRegistry::instance() {
    static EffectRegistry* reg = [] {
        auto* r = new EffectRegistry;
        registerBuiltinEffectsTo(*r);
        return r;
    }();
    return *reg;
}

void registerBuiltinEffects() {
    registerBuiltinEffectsTo(EffectRegistry::instance());
}

} // namespace bl
