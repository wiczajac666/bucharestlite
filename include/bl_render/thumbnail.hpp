#pragma once

#include <bl_core/media_source.hpp>
#include <bl_core/result.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

class CodecRegistry;

// A best-effort still frame plus the probed stream metadata for one media
// file. Audio-only files carry no pixels but still expose the probed `info`.
struct MediaThumbnail {
    StreamInfo info;
    uint32_t width{0};
    uint32_t height{0};
    uint32_t linesize{0};
    std::vector<uint8_t> pixels;  // BGRA32, tightly packed
    bool hasVideo{false};

    bool hasPixels() const noexcept {
        return hasVideo && width > 0 && height > 0 && !pixels.empty();
    }
};

// Decodes the first video frame of `path` through `registry`, scaled so its
// width is at most `maxWidth` (aspect ratio preserved). Metadata is returned
// even when no frame can be decoded; the call only fails when the file cannot
// be opened or probed at all. Qt-free and unit-testable.
Result<MediaThumbnail> readThumbnail(const std::string& path,
                                     const CodecRegistry& registry,
                                     uint32_t maxWidth = 96);

// One-line human summary for a media-bin row:
//   video: "00:00:02 · 320×240 · h264"
//   audio: "00:00:01 · 48 kHz · stereo · vorbis"
//   none:  "unknown media"
std::string describeMedia(const StreamInfo& info);

} // namespace bl
