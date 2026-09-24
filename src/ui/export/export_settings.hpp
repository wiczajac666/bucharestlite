#pragma once

#include <bl_core/time.hpp>
#include <bl_timeline/sequence_settings.hpp>

#include <cstdint>
#include <string>

namespace bl::ui {

// Range of the timeline to render.
enum class ExportRange { EntireProject, InOut };

// Output resolution scaling relative to the project sequence.
enum class ResolutionScale { Full, Half, Quarter, Custom };

// Qt-free export configuration captured by the export dialog and consumed by
// the render worker. Kept dependency-free so the semantics are unit-testable
// without a QApplication.
struct ExportSettings {
    ExportRange range = ExportRange::EntireProject;
    Time inPoint{};
    Time outPoint{};

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

} // namespace bl::ui