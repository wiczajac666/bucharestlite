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

    // Both the zone range and a single-clip range are expressed as an
    // inPoint/outPoint pair; only the whole-project range ignores them.
    if (s.range == ExportRange::InOut || s.range == ExportRange::Clip) {
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

std::string streamFormatForUrl(const std::string& url) {
    // Scheme is everything before "://"; a URL without a scheme is not a
    // streaming target we can drive.
    const std::string::size_type sep = url.find("://");
    if (sep == std::string::npos || sep == 0) {
        return std::string();
    }
    std::string scheme = url.substr(0, sep);
    for (char& c : scheme) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (scheme == "rtmp" || scheme == "rtmps") {
        return "flv";
    }
    if (scheme == "udp") {
        return "mpegts";
    }
    return std::string();
}

bool isStreamUrl(const std::string& url) {
    return !streamFormatForUrl(url).empty();
}

const std::vector<ExportPreset>& exportPresets() {
    static const std::vector<ExportPreset> presets = [] {
        auto preset = [](const char* id, const char* name,
                         const char* container, const char* vcodec,
                         const char* acodec, ResolutionScale scale,
                         int cq, int audioBitrate, uint32_t w, uint32_t h) {
            ExportPreset p;
            p.id = id;
            p.name = name;
            p.settings.container = container;
            p.settings.videoCodec = vcodec;
            p.settings.audioCodec = acodec;
            p.settings.includeAudio = acodec != nullptr && *acodec != '\0';
            p.settings.scale = scale;
            p.settings.videoCq = cq;
            p.settings.audioBitrate = audioBitrate;
            p.settings.customWidth = w;
            p.settings.customHeight = h;
            return p;
        };
        return std::vector<ExportPreset>{
            preset("youtube-1080p", "YouTube 1080p", "mp4", "h264", "aac",
                   ResolutionScale::Full, 20, 192000, 1920, 1080),
            preset("youtube-4k", "YouTube 4K", "mp4", "h264", "aac",
                   ResolutionScale::Custom, 18, 320000, 3840, 2160),
            preset("vimeo-1080p", "Vimeo 1080p", "mp4", "h264", "aac",
                   ResolutionScale::Full, 18, 320000, 1920, 1080),
            preset("webm-vp9", "WebM (VP9)", "webm", "vp9", "opus",
                   ResolutionScale::Full, 30, 128000, 1920, 1080),
            preset("dvd-480p", "DVD (MPEG-4, 720x480)", "mp4", "mpeg4", "aac",
                   ResolutionScale::Custom, 5, 192000, 720, 480),
            preset("bluray-1080p", "Blu-ray (H.264 1080p)", "mp4", "h264",
                   "aac", ResolutionScale::Custom, 18, 256000, 1920, 1080),
            preset("mobile-720p", "Mobile 720p", "mp4", "h264", "aac",
                   ResolutionScale::Custom, 26, 128000, 1280, 720),
        };
    }();
    return presets;
}

const ExportPreset* findExportPreset(const std::string& id) noexcept {
    for (const ExportPreset& p : exportPresets()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

} // namespace bl::ui