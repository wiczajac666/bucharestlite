#pragma once

#include <bl_core/media_source.hpp>
#include <bl_core/result.hpp>
#include <bl_core/time.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct AVFormatContext;

namespace bl {

struct Packet {
    int streamIndex{-1};
    std::vector<uint8_t> data;
    bool hasPts{false};
    bool hasDts{false};
    Time pts{};
    Time dts{};
    Duration duration{};
    bool keyframe{false};
};

class Demuxer {
public:
    static Result<Demuxer> open(const std::string& path);

    Demuxer(Demuxer&& other) noexcept;
    Demuxer& operator=(Demuxer&& other) noexcept;
    ~Demuxer();

    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    Result<std::optional<Packet>> nextPacket();
    Result<void> seek(Time target);

    const StreamInfo& info() const noexcept { return info_; }
    int defaultSeekStream() const noexcept;

private:
    Demuxer() = default;

    AVFormatContext* fmt_{nullptr};
    StreamInfo info_;
};

} // namespace bl
