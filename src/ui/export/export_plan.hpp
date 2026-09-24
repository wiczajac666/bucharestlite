#pragma once

#include "export/export_settings.hpp"

#include <bl_core/result.hpp>

#include <bl_export/export_types.h>

#include <bl_plugins/codec_plugin.h>

#include <bl_timeline/sequence.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace bl::ui {

// Total length of the longest video/audio clip chain in the sequence.
bl::Duration sequenceDuration(const bl::Sequence& sequence);

// Fully-formed, self-owning export configuration. Keeps every C-string that
// the BlFormatPreset points at alive for the lifetime of the plan, so callers
// can hand `preset()` to the export engine without worrying about dangling
// pointers.
struct ExportPlan {
    ExportSettings settings;
    uint32_t width{0};
    uint32_t height{0};
    ExportFrameRange range{};
    BlRational fps{30, 1};
    uint32_t sampleRate{48000};
    uint32_t channels{2};

    Result<void> build(const bl::Sequence& sequence, bl::Duration totalDuration);

    const export_::BlFormatPreset* preset() const noexcept { return &preset_; }

private:
    std::string outputPath_;
    std::string container_;
    std::string videoCodec_;
    std::string audioCodec_;
    export_::BlFormatPreset preset_{};
    export_::BlQualityPreset quality_{};
};

} // namespace bl::ui