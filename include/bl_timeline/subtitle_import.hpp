#pragma once

#include <bl_core/time.hpp>
#include <bl_timeline/subtitle.hpp>

#include <string>
#include <vector>

namespace bl {

// Subtitle file flavours the importer understands. SSA is treated as ASS
// (the two share the [Events]/Dialogue grammar).
enum class SubtitleFormat { Srt, WebVtt, Ass, Unknown };

// One timed subtitle line. `start`/`end` are absolute timeline times at the
// microsecond tick rate; `style` carries the parsed position (ASS alignment
// tags) and defaults otherwise.
struct SubtitleCue {
    Time start{};
    Time end{};
    std::string text;
    SubtitleStyle style{};
};

inline bool operator==(const SubtitleCue& a, const SubtitleCue& b) {
    return a.start == b.start && a.end == b.end && a.text == b.text &&
           a.style == b.style;
}

// Maps a path's extension to a format: .srt, .vtt/.webvtt, .ass/.ssa.
// Case-insensitive; Unknown for anything else.
SubtitleFormat subtitleFormatForPath(const std::string& path);

// Parses `content` in `format`. Malformed cues are skipped rather than
// aborting the whole file, so a single bad timestamp does not lose the rest.
// Unknown format yields an empty list. The returned cues are sorted by start
// and clamped so end > start.
std::vector<SubtitleCue> parseSubtitles(const std::string& content,
                                        SubtitleFormat format);

// Convenience wrapper: pick the format from `path`'s extension.
std::vector<SubtitleCue> parseSubtitleFile(const std::string& path,
                                           const std::string& content);

} // namespace bl
