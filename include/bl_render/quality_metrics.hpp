#pragma once

#include <bl_core/result.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

class CodecRegistry;

// PSNR is reported as this value when the two frames are bit-identical, since
// the true figure is infinite. 100 dB is far above any plausible codec result,
// so it reads unambiguously as "lossless" without needing an infinity sentinel.
inline constexpr double kPsnrIdentical = 100.0;

// Quality of one decoded frame of `test` measured against the same-index frame
// of `reference`.
struct FrameQuality {
    int64_t index{0};  // presentation order, 0-based
    double psnr{0.0};  // dB, capped at kPsnrIdentical
    double ssim{0.0};  // structural similarity, in [-1, 1] (typically [0, 1])
};

// Aggregate result of comparing two video files frame by frame.
struct QualityReport {
    uint32_t width{0};
    uint32_t height{0};
    int64_t framesCompared{0};
    bool frameCountMismatch{false};  // the inputs ended at different lengths

    double averagePsnr{0.0};
    double minPsnr{0.0};
    double maxPsnr{0.0};
    double averageSsim{0.0};
    double minSsim{0.0};
    double maxSsim{0.0};

    std::vector<FrameQuality> frames;
};

// Compares the video streams of two files frame by frame, in presentation
// order, and reports PSNR + SSIM per frame and in aggregate. Both files must
// expose a video stream of the same pixel dimensions (a mismatch is an error);
// a differing frame count stops the comparison at the shorter input and sets
// `frameCountMismatch`. `maxFrames` > 0 bounds the work (0 = compare all).
//
// Qt-free and synchronous: decode work happens on the calling thread. The
// registry must already hold the codec plugins that can decode both files
// (see ExportPluginSet / MediaBinThumbnailEngine for how the app loads them).
Result<QualityReport> compareVideo(const std::string& referencePath,
                                   const std::string& testPath,
                                   const CodecRegistry& registry,
                                   int maxFrames = 0);

// --- Pure, FFmpeg-free helpers (exposed for unit tests) --------------------

// Mean-squared error over the B, G and R channels of two BGRA buffers (the
// alpha channel is ignored). `stride` is bytes between row starts. Returns 0
// for identical inputs.
double bgraMse(const uint8_t* a, const uint8_t* b, uint32_t width,
               uint32_t height, uint32_t strideA, uint32_t strideB);

// PSNR in dB for a given MSE, capped at kPsnrIdentical when mse <= 0.
double psnrFromMse(double mse);

// Windowed structural similarity between the luma channels derived from two
// BGRA buffers of identical dimensions. Uses an 8x8 sliding window (clamped to
// the image size), the standard C1/C2 constants and a unit stride; returns the
// mean SSIM over all windows.
double bgraSsim(const uint8_t* a, const uint8_t* b, uint32_t width,
                uint32_t height, uint32_t strideA, uint32_t strideB);

}  // namespace bl
