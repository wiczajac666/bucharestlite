#include "edl_exporter.h"
#include <fstream>
#include <iostream>

namespace bl {
namespace export_ {

EdlExporter::EdlExporter(Timeline& timeline, const BlFormatPreset* preset)
    : timeline_(timeline), preset_(preset) {}

EdlExporter::~EdlExporter() {}

Result<void> EdlExporter::exportEdl(const char* output_path, BlExportJob* job) {
    std::ofstream out(output_path);
    if (!out.is_open()) {
        return Result<void>::err(Err::IoError);
    }

    Result<void> hr = writeHeader(out);
    if (!hr.ok()) return hr;

    hr = writeTimeline(out);
    if (!hr.ok()) return hr;

    out.close();
    return Result<void>();
}

Result<void> EdlExporter::writeHeader(std::ofstream& out) {
    out << "<?xml version=\"1.0\"?>\n";
    out << "<!DOCTYPE edl PUBLIC \"-//EDL//DTD 1.0//EN\"\n";
    out << "  \"http://www edl-smpte-org/dtd/edl.dtd\">\n";
    out << "<edl>\n";
    return Result<void>();
}

Result<void> EdlExporter::writeTimeline(std::ofstream& out) {
    const Timeline& timeline = timeline_;
    const Sequence& seq = timeline.sequence();

    out << "  <timeline>\n";

    // Write video tracks
    for (int v = 0; v < (int)seq.videoTracks.size(); v++) {
        out << "    <videoTrack number=\"" << (v + 1) << "\">\n";
        const auto& videoTrack = seq.videoTracks[v];
        for (int c = 0; c < (int)videoTrack.clips().size(); c++) {
            const Clip& clip = videoTrack.clips()[c];
            out << "      <clip in=\"0\" out=\"0\" number=\""
                << (c + 1) << "\">\n";
            out << "        <name>" << clip.name.c_str() << "</name>\n";
            out << "        <property name=\"transition_in\" value=\"0\"/>\n";
            out << "        <property name=\"transition_out\" value=\"0\"/>\n";
            out << "      </clip>\n";
        }
        out << "    </videoTrack>\n";
    }

    // Write audio tracks
    for (int a = 0; a < (int)seq.audioTracks.size(); a++) {
        out << "    <audioTrack number=\"" << (a + 1) << "\">\n";
        const auto& audioTrack = seq.audioTracks[a];
        for (int c = 0; c < (int)audioTrack.clips().size(); c++) {
            const Clip& clip = audioTrack.clips()[c];
            out << "      <clip in=\"0\" out=\"0\" number=\""
                << (c + 1) << "\">\n";
            out << "        <name>" << clip.name.c_str() << "</name>\n";
            out << "        <property name=\"transition_in\" value=\"0\"/>\n";
            out << "        <property name=\"transition_out\" value=\"0\"/>\n";
            out << "      </clip>\n";
        }
        out << "    </audioTrack>\n";
    }

    out << "  </timeline>\n";
    return Result<void>();
}

Result<void> EdlExporter::writeClipEntry(std::ofstream& out, int /*track_idx*/, int /*clip_idx*/) {
    return Result<void>();
}

BlRational EdlExporter::computeInTime(int /*clip_idx*/) const {
    return BlRational{0, 1};
}

BlRational EdlExporter::computeOutTime(int /*clip_idx*/) const {
    return BlRational{0, 1};
}

} // namespace export_
} // namespace bl
