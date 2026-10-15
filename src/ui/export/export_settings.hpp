#pragma once

#include <bl_core/time.hpp>
#include <bl_timeline/sequence_settings.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace bl::ui {

// Range of the timeline to render.
enum class ExportRange { EntireProject, InOut, Clip };

// Output resolution scaling relative to the project sequence.
enum class ResolutionScale { Full, Half, Quarter, Custom };

// Where an export goes: a file on disk, or straight to a network endpoint.
enum class ExportTarget { File, Stream };

// Qt-free export configuration captured by the export dialog and consumed by
// the render worker. Kept dependency-free so the semantics are unit-testable
// without a QApplication.
struct ExportSettings {
    ExportRange range = ExportRange::EntireProject;
    Time inPoint{};
    Time outPoint{};

    ExportTarget target = ExportTarget::File;
    std::string streamUrl;   // used when target == ExportTarget::Stream

    std::string outputPath;
    std::string container;   // "mp4", "mkv", "webm" — inferred from path suffix
    std::string videoCodec;  // plugin name, e.g. "h264", "vp9", "av1"
    std::string audioCodec;  // plugin name, e.g. "aac"; clears to no audio track
    bool includeAudio = true;

    int videoCq = 0;              // CRF / quality value; 0 = encoder default
    int audioBitrate = 192000;    // bits per second
    bool lossless = false;

    ResolutionScale scale = ResolutionScale::Full;
    uint32_t customWidth = 1920;
    uint32_t customHeight = 1080;
};

// Inferred output resolution for a project sequence.
uint32_t outputWidth(const ExportSettings& s,
                     const SequenceSettings& seq) noexcept;
uint32_t outputHeight(const ExportSettings& s,
                      const SequenceSettings& seq) noexcept;

struct ExportFrameRange {
    int64_t firstFrame = 0;  // inclusive
    int64_t frameCount = 0;  // frames to render; end frame is firstFrame+count
};

// Computes the render frame range. `totalDuration` is the full timeline
// length; InOut ranges clamp to [0, totalDuration].
ExportFrameRange frameRange(const ExportSettings& s,
                            const SequenceSettings& seq,
                            Duration totalDuration) noexcept;

// Best-effort container for a codec name ("h264" -> "mp4", "vp9" -> "webm").
std::string defaultContainerForCodec(const std::string& codecName);

bool isSupportedContainer(const std::string& container) noexcept;
bool isSupportedCodec(const std::string& codecName) noexcept;

// Streaming format for a URL scheme: rtmp/rtmps -> "flv", udp -> "mpegts",
// anything else -> "" (unsupported). Scheme = text before "://", lowercased.
std::string streamFormatForUrl(const std::string& url);
bool isStreamUrl(const std::string& url);   // scheme is rtmp/rtmps/udp

// A one-click encode configuration (Kdenlive "render profile"). Applying a
// preset only changes encode fields; the output path and the render range are
// owned by the caller and left untouched.
struct ExportPreset {
    std::string id;
    std::string name;
    ExportSettings settings;
};

// Built-in presets, ordered for display. Covers the common delivery targets
// (YouTube/Vimeo, WebM, DVD/Blu-ray-sized, mobile).
const std::vector<ExportPreset>& exportPresets();

// Preset by stable id, or nullptr.
const ExportPreset* findExportPreset(const std::string& id) noexcept;

} // namespace bl::ui