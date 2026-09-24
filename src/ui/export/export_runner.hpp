#pragma once

#include "export/export_plan.hpp"

#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_core/project_data.hpp>
#include <bl_core/result.hpp>
#include <bl_render/media_decode_source.hpp>
#include <bl_timeline/timeline.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bl::ui {

struct ExportProgressInfo {
    int percent = 0;
    size_t frame = 0;
    size_t totalFrames = 0;
    double framesPerSecond = 0.0;
    double etaSeconds = 0.0;
};

// Qt-free single-pass export driver. Renders every frame of the plan's range
// through the same compositor the preview uses and muxes the encoded output
// to disk. Instances are single-threaded: call run() from one thread and
// request cancellation by setting the shared abort flag.
class ExportRunner {
public:
    struct Input {
        ExportPlan plan;
        TimelineSnapshot snapshot;  // must stay valid for the whole run
        std::vector<MediaBinItem> mediaBin;
        MediaDecodeSource::Spec plugins;
        std::atomic<bool>* abort = nullptr;
    };

    using Progress = std::function<void(const ExportProgressInfo&)>;

    static Result<void> run(Input input, Progress onProgress);

private:
    ExportRunner() = default;
};

// Codec plugins owned for the duration of an export job. Keeps the dlopen
// handles alive while the registry holds their raw plugin pointers.
struct ExportPluginSet {
    CodecRegistry registry;
    std::vector<std::unique_ptr<PluginHandle>> handles;

    // Loads builtins + every plugin under the given dirs and the platform
    // default locations (installed builds ship codecs next to the app).
    Result<void> load(const MediaDecodeSource::Spec& spec);
};

} // namespace bl::ui