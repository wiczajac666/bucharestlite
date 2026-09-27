#pragma once

#include <bl_core/result.hpp>
#include <bl_core/time.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

struct MediaLocator {
    std::string path;
};

struct VideoStreamInfo {
    int index{-1};
    uint32_t width{0};
    uint32_t height{0};
    Rational fps{0, 1};
    Rational pixelAspect{0, 1};
    std::string codecName;
    std::vector<uint8_t> extradata;
};

struct AudioStreamInfo {
    int index{-1};
    uint32_t sampleRate{0};
    uint32_t channels{0};
    std::string codecName;
    std::vector<uint8_t> extradata;
};

struct SubtitleStreamInfo {
    int index{-1};
    std::string codecName;
    std::vector<uint8_t> extradata;
};

struct StreamInfo {
    std::string containerName;
    Duration duration{};
    std::vector<VideoStreamInfo> videoStreams;
    std::vector<AudioStreamInfo> audioStreams;
    std::vector<SubtitleStreamInfo> subtitleStreams;
};

class MediaSource {
public:
    static Result<StreamInfo> probe(const MediaLocator& locator);
};

} // namespace bl
