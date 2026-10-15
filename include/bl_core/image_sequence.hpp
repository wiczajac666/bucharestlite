#pragma once

#include <optional>
#include <string>

namespace bl {

// A detected image sequence: files sharing a stem and a fixed-width digit run
// in one directory (frame_0001.png, frame_0002.png, ...). `patternPath` is the
// FFmpeg image2 pattern ("/dir/frame_%04d.png") the renderer can open directly.
struct ImageSequence {
    std::string patternPath;  // absolute pattern, e.g. /mnt/frames/frame_%04d.png
    std::string directory;    // /mnt/frames
    std::string stem;         // "frame_" (including any separator)
    std::string extension;    // "png"
    int digits{0};            // width of the digit run (4 for "%04d")
    int firstIndex{0};
    int frameCount{0};
};

// Returns the sequence `filePath` belongs to, when one is detectable: the file
// name must end in a run of at least 2 digits before the extension, and the
// directory must hold at least `minCount` files sharing the same stem, digit
// width and extension. Returns nullopt for ordinary media files.
std::optional<ImageSequence> detectImageSequence(const std::string& filePath,
                                                 int minCount = 2);

} // namespace bl