#pragma once

#include "export/export_settings.hpp"

#include <string>
#include <vector>

namespace bl::ui {

// Lifecycle of a single queued export job.
enum class ExportJobStatus {
    Pending,   // not started yet
    Running,   // being rendered right now
    Done,      // output file written successfully
    Failed,    // the job errored (kept in the queue for a retry)
    Cancelled, // user interrupted this job
};

// One batch export entry. Everything needed to render an output file is kept
// here so the queue can be persisted and resumed on the next launch; the
// timeline/media-bin snapshot is captured at run time.
//
// Qt-free on purpose: the model is exercised from unit tests without a
// QApplication.
struct ExportJob {
    std::string id;
    ExportSettings settings;
    ExportJobStatus status = ExportJobStatus::Pending;
    std::string message; // done destination or failure text

    // Whether the job still needs rendering (pending or previously failed).
    bool needsRender() const noexcept {
        return status == ExportJobStatus::Pending ||
               status == ExportJobStatus::Failed;
    }
};

// Where the batch queue lives on this machine (AppData/cache directory shared
// by all bl_lite instances). Caller still needs to create the parent dir.
std::string defaultExportQueuePath();

// Round-trip the queue to/from JSON. Load happily ignores malformed entries
// (best effort restore) and never throws.
std::vector<ExportJob> loadExportQueue(const std::string& path);
bool saveExportQueue(const std::vector<ExportJob>& jobs,
                     const std::string& path);

} // namespace bl::ui