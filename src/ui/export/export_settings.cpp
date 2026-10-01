#include "export/export_settings.hpp"

#include <cstdint>

namespace bl::ui {

namespace {

int64_t clampI64(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

uint32_t scaledDimension(uint32_t base, ResolutionScale scale,
                         uint32_t custom) noexcept {
    switch (scale) {
    case ResolutionScale::Half:
        return base / 2;
    case ResolutionScale::Quarter:
        return base / 4;
    case ResolutionScale::Custom:
        return custom;
    case ResolutionScale::Full:
    default:
        return base;
    }
}

} // namespace

uint32_t outputWidth(const ExportSettings& s, const SequenceSettings& seq) noexcept {
    return scaledDimension(static_cast<uint32_t>(seq.width), s.scale,
                           s.customWidth);
}

uint32_t outputHeight(const ExportSettings& s, const SequenceSettings& seq) noexcept {
    return scaledDimension(static_cast<uint32_t>(seq.height), s.scale,
                           s.customHeight);
}

ExportFrameRange frameRange(const ExportSettings& s,
                            const SequenceSettings& seq,
                            Duration totalDuration) noexcept {
    const int64_t totalFrames = totalDuration.toFramesAt(seq.fps);

    int64_t firstFrame = 0;
    int64_t frameCount = totalFrames;

    if (s.range == ExportRange::InOut) {
        const Duration inOutDuration = s.outPoint - s.inPoint;
        firstFrame = s.inPoint.toFrameAt(seq.fps);
        frameCount = inOutDuration.toFramesAt(seq.fps);
        if (firstFrame < 0) {
            frameCount += firstFrame;  // shift range right by the negative offset
            firstFrame = 0;
        }
        frameCount = clampI64(frameCount, 0, totalFrames - firstFrame);
    }

    return ExportFrameRange{firstFrame, frameCount < 0 ? 0 : frameCount};
}

std::string defaultContainerForCodec(const std::string& codecName) {
    if (codecName == "vp9" || codecName == "av1" || codecName == "theora" ||
        codecName == "opus" || codecName == "vorbis") {
        return "webm";
    }
    return "mp4";
}

bool isSupportedContainer(const std::string& container) noexcept {
    // v1 muxing supports MP4 (h264/mpeg4), WebM (vp9/av1/theora) and Matroska
    // (all of them -- matroskaenc accepts every codec the app offers). The
    // Matroska header write needs SPS/PPS extradata up front, which the export
    // engine now supplies from the encoder plugins.
    return container == "mp4" || container == "webm" || container == "mkv";
}

bool isSupportedCodec(const std::string& codecName) noexcept {
    return codecName == "h264" || codecName == "vp9" || codecName == "av1" ||
           codecName == "theora" || codecName == "mpeg4" ||
           codecName == "aac" || codecName == "flac" ||
           codecName == "vorbis" || codecName == "opus";
}

} // namespace bl::ui