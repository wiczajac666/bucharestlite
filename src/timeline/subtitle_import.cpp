#include <bl_timeline/subtitle_import.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace bl {

namespace {

const Rational kRate{1'000'000, 1};

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

// "HH:MM:SS,mmm" / "HH:MM:SS.mmm" / "MM:SS.mmm" / ASS "H:MM:SS.cc" → seconds.
bool parseClock(const std::string& raw, double& seconds) {
    const std::string s = trim(raw);
    if (s.empty()) return false;

    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t pos = s.find(':', start);
        if (pos == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    if (parts.size() < 2 || parts.size() > 3) return false;

    auto toNumber = [](const std::string& token, double& out) {
        if (token.empty()) return false;
        std::string normalized;
        normalized.reserve(token.size());
        for (char c : token) {
            if (c == ',') {
                normalized.push_back('.');
            } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                       c == '.') {
                normalized.push_back(c);
            } else {
                return false;
            }
        }
        try {
            out = std::stod(normalized);
        } catch (...) {
            return false;
        }
        return true;
    };

    double hours = 0.0;
    double minutes = 0.0;
    double secs = 0.0;
    if (parts.size() == 3) {
        if (!toNumber(parts[0], hours) || !toNumber(parts[1], minutes) ||
            !toNumber(parts[2], secs)) {
            return false;
        }
    } else {
        if (!toNumber(parts[0], minutes) || !toNumber(parts[1], secs)) {
            return false;
        }
    }
    seconds = hours * 3600.0 + minutes * 60.0 + secs;
    return seconds >= 0.0;
}

// Splits a subtitle body line block into trimmed text lines.
std::string joinLines(const std::vector<std::string>& lines, size_t from) {
    std::string out;
    for (size_t i = from; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (t.empty()) continue;
        if (!out.empty()) out.push_back('\n');
        out += t;
    }
    return out;
}

// Removes markup from cue text: SRT/VTT angle-bracket tags and ASS override
// blocks. ASS hard line breaks (\N, \n) become newlines.
std::string stripTags(const std::string& in, bool assOverrides) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (assOverrides) {
            if (c == '{') {
                while (i < in.size() && in[i] != '}') ++i;
                continue;
            }
            if (c == '\\' && i + 1 < in.size() &&
                (in[i + 1] == 'N' || in[i + 1] == 'n')) {
                out.push_back('\n');
                ++i;
                continue;
            }
            out.push_back(c);
            continue;
        }
        if (c == '<') {
            const size_t close = in.find('>', i);
            if (close != std::string::npos) {
                i = close;
                continue;
            }
        }
        out.push_back(c);
    }
    return trim(out);
}

// ASS alignment override (\an1..\an9) → vertical position. 7-9 are the top
// row, 4-6 the middle, 1-3 the bottom.
SubtitleStyle::Position assPosition(const std::string& rawText) {
    const size_t an = rawText.find("\\an");
    if (an == std::string::npos) return SubtitleStyle::Position::Bottom;
    int value = 0;
    for (size_t i = an + 3; i < rawText.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(rawText[i]))) {
            value = rawText[i] - '0';
            break;
        }
        break;
    }
    if (value >= 7 && value <= 9) return SubtitleStyle::Position::Top;
    if (value >= 4 && value <= 6) return SubtitleStyle::Position::Center;
    return SubtitleStyle::Position::Bottom;
}

bool addCue(std::vector<SubtitleCue>& cues, const std::string& startRaw,
            const std::string& endRaw, const std::string& text,
            SubtitleStyle::Position position) {
    double startSec = 0.0;
    double endSec = 0.0;
    if (!parseClock(startRaw, startSec) || !parseClock(endRaw, endSec)) {
        return false;
    }
    if (endSec <= startSec) return false;
    SubtitleCue cue;
    cue.start = Time::fromSeconds(startSec, kRate);
    cue.end = Time::fromSeconds(endSec, kRate);
    cue.text = text;
    cue.style.position = position;
    if (cue.text.empty()) return false;
    cues.push_back(std::move(cue));
    return true;
}

// --- SRT / WebVTT (blank-line separated blocks) ----------------------------

std::vector<SubtitleCue> parseSrtLike(const std::string& content, bool webvtt) {
    std::vector<SubtitleCue> cues;
    std::istringstream stream(content);
    std::vector<std::string> block;
    auto flush = [&] {
        if (block.empty()) return;
        // Find the timing line (the one that contains "-->").
        size_t timing = block.size();
        for (size_t i = 0; i < block.size(); ++i) {
            if (block[i].find("-->") != std::string::npos) {
                timing = i;
                break;
            }
        }
        if (timing < block.size()) {
            const std::string& line = block[timing];
            const size_t arrow = line.find("-->");
            const std::string startRaw = line.substr(0, arrow);
            const std::string endPart =
                trim(line.substr(arrow + 3));
            // VTT cue settings follow the end timestamp after whitespace.
            std::string endRaw = endPart;
            const size_t space = endPart.find_first_of(" \t");
            if (space != std::string::npos) endRaw = endPart.substr(0, space);
            std::string text = joinLines(block, timing + 1);
            text = stripTags(text, /*assOverrides=*/false);
            addCue(cues, startRaw, endRaw, text,
                   SubtitleStyle::Position::Bottom);
        }
        block.clear();
    };

    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty()) {
            flush();
            continue;
        }
        if (webvtt) {
            const std::string lt = lower(t);
            if (block.empty() && (lt == "webvtt" || startsWith(lt, "webvtt "))) {
                continue; // header
            }
            if (block.empty() && startsWith(lt, "note")) {
                continue; // NOTE blocks are skipped line by line below
            }
            if (lt == "style" || startsWith(lt, "style ") || lt == "region" ||
                startsWith(lt, "region ")) {
                block.clear();
                continue;
            }
        }
        block.push_back(line);
    }
    flush();
    return cues;
}

// --- ASS / SSA -------------------------------------------------------------

std::vector<std::string> splitComma(const std::string& s, size_t maxFields) {
    std::vector<std::string> out;
    size_t start = 0;
    while (out.size() + 1 < maxFields) {
        const size_t pos = s.find(',', start);
        if (pos == std::string::npos) break;
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    out.push_back(s.substr(start)); // final field keeps embedded commas
    return out;
}

std::vector<SubtitleCue> parseAss(const std::string& content) {
    std::vector<SubtitleCue> cues;
    std::istringstream stream(content);
    std::string line;
    bool inEvents = false;
    std::vector<std::string> format;
    auto columnOf = [&](const std::string& name) -> int {
        for (size_t i = 0; i < format.size(); ++i) {
            if (lower(format[i]) == name) return static_cast<int>(i);
        }
        return -1;
    };

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            inEvents = lower(trim(t.substr(1, t.size() - 2))) == "events";
            format.clear();
            continue;
        }
        if (!inEvents) continue;

        const size_t colon = t.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = lower(trim(t.substr(0, colon)));
        const std::string value = trim(t.substr(colon + 1));

        if (key == "format") {
            format.clear();
            std::istringstream fs(value);
            std::string col;
            while (std::getline(fs, col, ',')) format.push_back(trim(col));
            continue;
        }
        if (key != "dialogue") continue;

        // Default ASS column order when no Format line was seen.
        if (format.empty()) {
            format = {"Layer", "Start", "End", "Style", "Name",
                      "MarginL", "MarginR", "MarginV", "Effect", "Text"};
        }
        const int startCol = columnOf("start");
        const int endCol = columnOf("end");
        const int textCol = columnOf("text");
        if (startCol < 0 || endCol < 0 || textCol < 0) continue;

        const size_t fieldCount = format.size();
        const std::vector<std::string> fields =
            splitComma(value, fieldCount);
        if (static_cast<int>(fields.size()) <= textCol) continue;

        const std::string rawText = fields[static_cast<size_t>(textCol)];
        const SubtitleStyle::Position pos = assPosition(rawText);
        const std::string text = stripTags(rawText, /*assOverrides=*/true);
        addCue(cues, fields[static_cast<size_t>(startCol)],
               fields[static_cast<size_t>(endCol)], text, pos);
    }
    return cues;
}

} // namespace

SubtitleFormat subtitleFormatForPath(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return SubtitleFormat::Unknown;
    const std::string ext = lower(path.substr(dot + 1));
    if (ext == "srt") return SubtitleFormat::Srt;
    if (ext == "vtt" || ext == "webvtt") return SubtitleFormat::WebVtt;
    if (ext == "ass" || ext == "ssa") return SubtitleFormat::Ass;
    return SubtitleFormat::Unknown;
}

std::vector<SubtitleCue> parseSubtitles(const std::string& content,
                                        SubtitleFormat format) {
    std::vector<SubtitleCue> cues;
    switch (format) {
    case SubtitleFormat::Srt:
        cues = parseSrtLike(content, /*webvtt=*/false);
        break;
    case SubtitleFormat::WebVtt:
        cues = parseSrtLike(content, /*webvtt=*/true);
        break;
    case SubtitleFormat::Ass:
        cues = parseAss(content);
        break;
    case SubtitleFormat::Unknown:
    default:
        return {};
    }
    std::sort(cues.begin(), cues.end(), [](const SubtitleCue& a,
                                           const SubtitleCue& b) {
        return a.start < b.start;
    });
    return cues;
}

std::vector<SubtitleCue> parseSubtitleFile(const std::string& path,
                                           const std::string& content) {
    return parseSubtitles(content, subtitleFormatForPath(path));
}

} // namespace bl
