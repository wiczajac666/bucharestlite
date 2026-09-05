#pragma once

#include <bl_core/codec_registry.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/result.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

struct BlCodecPlugin;
struct BlCodecConfig;
struct BlHostApi;

namespace bl {

struct Frame {
    enum class Type : uint8_t { Video, Audio };
    Type type{Type::Video};
    int streamIndex{-1};

    uint32_t width{0};
    uint32_t height{0};
    uint32_t linesize{0};
    uint8_t* data{nullptr};
    size_t dataSize{0};

    uint32_t sampleCount{0};
    uint32_t channels{0};

    uint64_t pts{0};
    bool keyframe{false};

    const BlHostApi* hostApi{nullptr};

    Frame() = default;
    ~Frame();

    Frame(Frame&& other) noexcept;
    Frame& operator=(Frame&& other) noexcept;

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    bool empty() const noexcept { return data == nullptr || dataSize == 0; }
};

class DecoderBridge {
public:
    static Result<DecoderBridge> create(const CodecRegistry& registry,
                                        std::string_view codecName,
                                        const BlCodecConfig& config);

    ~DecoderBridge();

    DecoderBridge(DecoderBridge&& other) noexcept;
    DecoderBridge& operator=(DecoderBridge&& other) noexcept;

    DecoderBridge(const DecoderBridge&) = delete;
    DecoderBridge& operator=(const DecoderBridge&) = delete;

    Result<Frame> decode(const Packet& packet);
    Result<std::optional<Frame>> flush();

    std::string_view codecName() const noexcept;

private:
    DecoderBridge() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bl
