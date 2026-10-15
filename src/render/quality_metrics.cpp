#include <bl_render/quality_metrics.hpp>

#include <bl_core/codec_registry.hpp>
#include <bl_core/decoder_bridge.hpp>
#include <bl_core/demuxer.hpp>

#include <bl_plugins/codec_plugin.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace bl {

namespace {

// Decoded frame buffers are host-allocated (malloc/free), matching the
// ownership contract of Frame's destructor.
const BlHostApi kQualityHost = {
    /*host_abi_version*/ BL_PLUGIN_ABI_VERSION,
    /*alloc*/ [](size_t size, void*) { return std::malloc(size); },
    /*free*/ [](void* ptr, void*) { std::free(ptr); },
    /*userdata*/ nullptr,
};

// Builds a decoder for one probed video stream that outputs tightly packed
// BGRA at the stream's native size (the same configuration the thumbnail
// decoder uses).
Result<DecoderBridge> makeBridge(const CodecRegistry& registry,
                                 const VideoStreamInfo& video) {
    const Rational fps =
        (video.fps.valid() && !video.fps.isZero()) ? video.fps : Rational{24, 1};

    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.codec_name = video.codecName.c_str();
    cfg.extradata = video.extradata.empty() ? nullptr : video.extradata.data();
    cfg.extradata_size = video.extradata.size();
    cfg.video.width = video.width;
    cfg.video.height = video.height;
    cfg.video.fps = {static_cast<int32_t>(fps.num),
                     static_cast<uint32_t>(fps.den)};
    cfg.video.pixel_aspect = {1, 1};
    cfg.video.pix_fmt = BL_PIXFMT_BGRA32;
    cfg.host = &kQualityHost;
    return DecoderBridge::create(registry, video.codecName, cfg);
}

// State carried across nextVideoFrame() calls for one input.
struct DecodeCursor {
    bool flushing{false};
    bool eos{false};
};

// Pulls the next decoded video frame from `demuxer` (skipping non-video
// packets), driving the decoder through its reorder buffer and flushing once at
// end of stream. Returns nullopt when the stream is exhausted.
Result<std::optional<Frame>> nextVideoFrame(Demuxer& demuxer,
                                            DecoderBridge& bridge, int streamIndex,
                                            DecodeCursor& cursor) {
    if (cursor.eos) {
        return Result<std::optional<Frame>>::ok(std::nullopt);
    }

    for (;;) {
        if (cursor.flushing) {
            auto flushed = bridge.flush();
            if (!flushed.ok()) {
                return Result<std::optional<Frame>>::err(flushed.code(),
                                                         flushed.message());
            }
            auto frame = std::move(flushed.value());
            if (frame.has_value() && !frame->empty()) {
                return Result<std::optional<Frame>>::ok(std::move(frame));
            }
            cursor.flushing = false;
            cursor.eos = true;
            return Result<std::optional<Frame>>::ok(std::nullopt);
        }

        auto packetResult = demuxer.nextPacket();
        if (!packetResult.ok()) {
            return Result<std::optional<Frame>>::err(packetResult.code(),
                                                     packetResult.message());
        }
        auto packet = std::move(packetResult.value());
        if (!packet.has_value()) {
            cursor.flushing = true;
            continue;
        }
        if (packet->streamIndex != streamIndex) {
            continue;
        }

        auto decoded = bridge.decode(*packet);
        if (!decoded.ok()) {
            if (decoded.message() == "need more input") {
                continue;
            }
            continue;  // skip undecodable packets rather than abort the run
        }
        if (decoded.value().empty()) {
            continue;
        }
        return Result<std::optional<Frame>>::ok(std::move(decoded.value()));
    }
}

// Row-major summed-area table over a W x H scalar image, used to evaluate the
// mean/variance/covariance of every sliding SSIM window in O(1).
class Integral {
public:
    Integral(uint32_t width, uint32_t height)
        : w_(static_cast<int>(width)),
          sum_(static_cast<size_t>(w_ + 1) * (height + 1), 0.0) {}

    void build(const std::vector<double>& img) {
        buildImpl([&img](size_t i) { return img[i]; });
    }
    void buildSquared(const std::vector<double>& img) {
        buildImpl([&img](size_t i) { return img[i] * img[i]; });
    }
    void buildProduct(const std::vector<double>& a,
                      const std::vector<double>& b) {
        buildImpl([&a, &b](size_t i) { return a[i] * b[i]; });
    }

    // Sum over [x0, x1) x [y0, y1).
    double rect(int x0, int y0, int x1, int y1) const {
        const size_t row = static_cast<size_t>(w_ + 1);
        return sum_[static_cast<size_t>(y1) * row + x1] -
               sum_[static_cast<size_t>(y0) * row + x1] -
               sum_[static_cast<size_t>(y1) * row + x0] +
               sum_[static_cast<size_t>(y0) * row + x0];
    }

private:
    template <typename ValueAt>
    void buildImpl(ValueAt valueAt) {
        const size_t row = static_cast<size_t>(w_ + 1);
        const size_t rows = sum_.size() / row;
        for (size_t y = 0; y + 1 < rows; ++y) {
            double rowSum = 0.0;
            for (int x = 0; x < w_; ++x) {
                rowSum += valueAt(y * static_cast<size_t>(w_) +
                                  static_cast<size_t>(x));
                sum_[(y + 1) * row + (x + 1)] = sum_[y * row + (x + 1)] + rowSum;
            }
        }
    }

    int w_;
    std::vector<double> sum_;
};

std::vector<double> lumaPlane(const uint8_t* bgra, uint32_t width,
                              uint32_t height, uint32_t stride) {
    std::vector<double> luma(static_cast<size_t>(width) * height, 0.0);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src = bgra + static_cast<size_t>(y) * stride;
        double* dst = luma.data() + static_cast<size_t>(y) * width;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = src + static_cast<size_t>(x) * 4u;
            // Rec.709 luma from BGRA.
            dst[x] = 0.2126 * p[2] + 0.7152 * p[1] + 0.0722 * p[0];
        }
    }
    return luma;
}

}  // namespace

double bgraMse(const uint8_t* a, const uint8_t* b, uint32_t width,
               uint32_t height, uint32_t strideA, uint32_t strideB) {
    if (a == nullptr || b == nullptr || width == 0 || height == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* ra = a + static_cast<size_t>(y) * strideA;
        const uint8_t* rb = b + static_cast<size_t>(y) * strideB;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* pa = ra + static_cast<size_t>(x) * 4u;
            const uint8_t* pb = rb + static_cast<size_t>(x) * 4u;
            for (int c = 0; c < 3; ++c) {
                const double d =
                    static_cast<double>(pa[c]) - static_cast<double>(pb[c]);
                sum += d * d;
            }
        }
    }
    return sum / (static_cast<double>(width) * height * 3.0);
}

double psnrFromMse(double mse) {
    if (!(mse > 0.0)) {
        return kPsnrIdentical;
    }
    const double psnr = 10.0 * std::log10((255.0 * 255.0) / mse);
    return psnr > kPsnrIdentical ? kPsnrIdentical : psnr;
}

double bgraSsim(const uint8_t* a, const uint8_t* b, uint32_t width,
                uint32_t height, uint32_t strideA, uint32_t strideB) {
    if (a == nullptr || b == nullptr || width == 0 || height == 0) {
        return 1.0;
    }

    const std::vector<double> lx = lumaPlane(a, width, height, strideA);
    const std::vector<double> ly = lumaPlane(b, width, height, strideB);

    const int win = static_cast<int>(std::min<uint32_t>(8, std::min(width, height)));
    const int stepsX = static_cast<int>(width) - win + 1;
    const int stepsY = static_cast<int>(height) - win + 1;
    if (stepsX <= 0 || stepsY <= 0) {
        return 1.0;
    }

    Integral ix(width, height);
    Integral iy(width, height);
    Integral ixx(width, height);
    Integral iyy(width, height);
    Integral ixy(width, height);
    ix.build(lx);
    iy.build(ly);
    ixx.buildSquared(lx);
    iyy.buildSquared(ly);
    ixy.buildProduct(lx, ly);

    // Standard SSIM stabilisers for 8-bit data (K1 = 0.01, K2 = 0.03).
    constexpr double c1 = 6.5025;
    constexpr double c2 = 58.5225;
    const double n = static_cast<double>(win) * win;

    double total = 0.0;
    long long count = 0;
    for (int y = 0; y < stepsY; ++y) {
        for (int x = 0; x < stepsX; ++x) {
            const double mux = ix.rect(x, y, x + win, y + win) / n;
            const double muy = iy.rect(x, y, x + win, y + win) / n;
            double varx = ixx.rect(x, y, x + win, y + win) / n - mux * mux;
            double vary = iyy.rect(x, y, x + win, y + win) / n - muy * muy;
            const double cov = ixy.rect(x, y, x + win, y + win) / n - mux * muy;
            if (varx < 0.0) varx = 0.0;
            if (vary < 0.0) vary = 0.0;

            const double numerator = (2.0 * mux * muy + c1) * (2.0 * cov + c2);
            const double denominator =
                (mux * mux + muy * muy + c1) * (varx + vary + c2);
            total += numerator / denominator;
            ++count;
        }
    }
    return count > 0 ? total / static_cast<double>(count) : 1.0;
}

Result<QualityReport> compareVideo(const std::string& referencePath,
                                   const std::string& testPath,
                                   const CodecRegistry& registry,
                                   int maxFrames) {
    if (referencePath.empty() || testPath.empty()) {
        return Result<QualityReport>::err(Err::InvalidArgument,
                                          "both media paths are required");
    }

    auto refOpen = Demuxer::open(referencePath);
    if (!refOpen.ok()) {
        return Result<QualityReport>::err(refOpen.code(), refOpen.message());
    }
    auto testOpen = Demuxer::open(testPath);
    if (!testOpen.ok()) {
        return Result<QualityReport>::err(testOpen.code(), testOpen.message());
    }
    Demuxer refDemuxer = std::move(refOpen.value());
    Demuxer testDemuxer = std::move(testOpen.value());

    if (refDemuxer.info().videoStreams.empty()) {
        return Result<QualityReport>::err(Err::InvalidArgument,
                                          "'" + referencePath +
                                              "' has no video stream");
    }
    if (testDemuxer.info().videoStreams.empty()) {
        return Result<QualityReport>::err(
            Err::InvalidArgument, "'" + testPath + "' has no video stream");
    }

    const VideoStreamInfo refVideo = refDemuxer.info().videoStreams.front();
    const VideoStreamInfo testVideo = testDemuxer.info().videoStreams.front();
    if (refVideo.width != testVideo.width ||
        refVideo.height != testVideo.height) {
        return Result<QualityReport>::err(
            Err::InvalidArgument,
            "video dimensions differ (" + std::to_string(refVideo.width) + "x" +
                std::to_string(refVideo.height) + " vs " +
                std::to_string(testVideo.width) + "x" +
                std::to_string(testVideo.height) + ")");
    }

    auto refBridge = makeBridge(registry, refVideo);
    if (!refBridge.ok()) {
        return Result<QualityReport>::err(refBridge.code(), refBridge.message());
    }
    auto testBridge = makeBridge(registry, testVideo);
    if (!testBridge.ok()) {
        return Result<QualityReport>::err(testBridge.code(),
                                           testBridge.message());
    }
    DecoderBridge refDecoder = std::move(refBridge.value());
    DecoderBridge testDecoder = std::move(testBridge.value());

    QualityReport report;
    report.width = refVideo.width;
    report.height = refVideo.height;

    DecodeCursor refCursor;
    DecodeCursor testCursor;
    double minPsnr = std::numeric_limits<double>::infinity();
    double maxPsnr = -std::numeric_limits<double>::infinity();
    double minSsim = std::numeric_limits<double>::infinity();
    double maxSsim = -std::numeric_limits<double>::infinity();
    double sumPsnr = 0.0;
    double sumSsim = 0.0;
    int64_t index = 0;

    for (;;) {
        if (maxFrames > 0 && index >= maxFrames) {
            break;
        }

        auto refFrame = nextVideoFrame(refDemuxer, refDecoder, refVideo.index,
                                       refCursor);
        if (!refFrame.ok()) {
            return Result<QualityReport>::err(refFrame.code(),
                                              refFrame.message());
        }
        auto testFrame = nextVideoFrame(testDemuxer, testDecoder, testVideo.index,
                                        testCursor);
        if (!testFrame.ok()) {
            return Result<QualityReport>::err(testFrame.code(),
                                              testFrame.message());
        }

        const bool hasRef = refFrame.value().has_value();
        const bool hasTest = testFrame.value().has_value();
        if (!hasRef && !hasTest) {
            break;
        }
        if (hasRef != hasTest) {
            report.frameCountMismatch = true;
            break;
        }

        const Frame& a = **refFrame;
        const Frame& b = **testFrame;
        if (a.width != b.width || a.height != b.height) {
            return Result<QualityReport>::err(
                Err::InvalidArgument, "decoded frame dimensions differ");
        }

        const double mse = bgraMse(a.data, b.data, a.width, a.height, a.linesize,
                                   b.linesize);
        const double psnr = psnrFromMse(mse);
        const double ssim = bgraSsim(a.data, b.data, a.width, a.height,
                                     a.linesize, b.linesize);

        report.frames.push_back(FrameQuality{index, psnr, ssim});
        sumPsnr += psnr;
        sumSsim += ssim;
        minPsnr = std::min(minPsnr, psnr);
        maxPsnr = std::max(maxPsnr, psnr);
        minSsim = std::min(minSsim, ssim);
        maxSsim = std::max(maxSsim, ssim);
        ++index;
    }

    report.framesCompared = index;
    if (index > 0) {
        report.averagePsnr = sumPsnr / static_cast<double>(index);
        report.averageSsim = sumSsim / static_cast<double>(index);
        report.minPsnr = minPsnr;
        report.maxPsnr = maxPsnr;
        report.minSsim = minSsim;
        report.maxSsim = maxSsim;
    }
    return Result<QualityReport>::ok(std::move(report));
}

}  // namespace bl
