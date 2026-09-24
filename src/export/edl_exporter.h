#pragma once

#include "bl_export/export_types.h"
#include <bl_timeline/timeline.hpp>

#include <string>

namespace bl {
namespace export_ {

class EdlExporter {
public:
    EdlExporter(Timeline& timeline, const BlFormatPreset* preset);
    ~EdlExporter();

    Result<void> exportEdl(const char* output_path, BlExportJob* job);

private:
    Timeline& timeline_;
    const BlFormatPreset* preset_;

    Result<void> writeHeader(std::ofstream& out);
    Result<void> writeTimeline(std::ofstream& out);
    Result<void> writeClipEntry(std::ofstream& out, int track_idx, int clip_idx);
    BlRational computeInTime(int clip_idx) const;
    BlRational computeOutTime(int clip_idx) const;
};

} // namespace export_
} // namespace bl
