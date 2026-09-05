#include <bl_core/decoder_bridge.hpp>

#include <bl_core/logger.hpp>

#include <bl_plugins/codec_plugin.h>

#include <string>
#include <utility>

namespace bl {

// --- Frame ---

Frame::~Frame() {
    if (data && hostApi && hostApi->free) {
        hostApi->free(data, hostApi->userdata);
    }
}

Frame::Frame(Frame&& other) noexcept
    : type(other.type),
      streamIndex(other.streamIndex),
      width(other.width),
      height(other.height),
      linesize(other.linesize),
      data(other.data),
      dataSize(other.dataSize),
      sampleCount(other.sampleCount),
      channels(other.channels),
      pts(other.pts),
      keyframe(other.keyframe),
      hostApi(other.hostApi) {
    other.data = nullptr;
    other.dataSize = 0;
    other.hostApi = nullptr;
}

Frame& Frame::operator=(Frame&& other) noexcept {
    if (this != &other) {
        if (data && hostApi && hostApi->free) {
            hostApi->free(data, hostApi->userdata);
        }
        type = other.type;
        streamIndex = other.streamIndex;
        width = other.width;
        height = other.height;
        linesize = other.linesize;
        data = other.data;
        dataSize = other.dataSize;
        sampleCount = other.sampleCount;
        channels = other.channels;
        pts = other.pts;
        keyframe = other.keyframe;
        hostApi = other.hostApi;
        other.data = nullptr;
        other.dataSize = 0;
        other.hostApi = nullptr;
    }
    return *this;
}

// --- DecoderBridge::Impl ---

struct DecoderBridge::Impl {
    BlCodecPlugin* plugin{nullptr};
    void* ctx{nullptr};
    std::string name;
    const BlHostApi* hostApi{nullptr};

    ~Impl() {
        if (plugin && ctx) {
            plugin->cleanup(ctx);
            ctx = nullptr;
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl() = default;

    Impl(Impl&& other) noexcept
        : plugin(other.plugin), ctx(other.ctx), name(std::move(other.name)),
          hostApi(other.hostApi) {
        other.plugin = nullptr;
        other.ctx = nullptr;
        other.hostApi = nullptr;
    }

    Impl& operator=(Impl&& other) noexcept {
        if (this != &other) {
            if (plugin && ctx) {
                plugin->cleanup(ctx);
            }
            plugin = other.plugin;
            ctx = other.ctx;
            name = std::move(other.name);
            hostApi = other.hostApi;
            other.plugin = nullptr;
            other.ctx = nullptr;
            other.hostApi = nullptr;
        }
        return *this;
    }
};

// --- DecoderBridge ---

Result<DecoderBridge> DecoderBridge::create(const CodecRegistry& registry,
                                           std::string_view codecName,
                                           const BlCodecConfig& config) {
    BlCodecPlugin* plugin = registry.find(codecName);
    if (!plugin) {
        return Result<DecoderBridge>::err(
            Err::InvalidArgument,
            std::string("codec '") + std::string(codecName) + "' not found in registry");
    }

    if (!(plugin->caps.roles & BL_ROLE_DECODE)) {
        return Result<DecoderBridge>::err(
            Err::InvalidArgument,
            std::string("codec '") + std::string(codecName) + "' does not support decoding");
    }

    void* ctx = nullptr;
    int rc = plugin->init(&ctx, &config);
    if (rc != BL_OK) {
        return Result<DecoderBridge>::err(
            Err::DecodeFailed,
            std::string("plugin init failed for '") + std::string(codecName) +
                "': error " + std::to_string(rc));
    }

    DecoderBridge bridge;
    bridge.impl_ = std::make_unique<Impl>();
    bridge.impl_->plugin = plugin;
    bridge.impl_->ctx = ctx;
    bridge.impl_->name = std::string(codecName);
    bridge.impl_->hostApi = config.host;

    BL_LOG_INFO("decoder", "created bridge for codec '" + bridge.impl_->name + "'");
    return Result<DecoderBridge>::ok(std::move(bridge));
}

DecoderBridge::~DecoderBridge() = default;

DecoderBridge::DecoderBridge(DecoderBridge&& other) noexcept = default;
DecoderBridge& DecoderBridge::operator=(DecoderBridge&& other) noexcept = default;

Result<Frame> DecoderBridge::decode(const Packet& packet) {
    if (!impl_ || !impl_->plugin || !impl_->ctx) {
        return Result<Frame>::err(Err::InvalidArgument, "decoder bridge is not initialized");
    }

    uint8_t* out = nullptr;
    size_t outSize = 0;
    BlFrameMeta meta{};

    int rc = impl_->plugin->decode(impl_->ctx, packet.data.data(),
                                   packet.data.size(), &out, &outSize, &meta);
    if (rc == BL_DECODE_NEED_MORE_INPUT) {
        return Result<Frame>::err(Err::DecodeFailed, "need more input");
    }
    if (rc != BL_OK) {
        return Result<Frame>::err(
            Err::DecodeFailed,
            std::string("decode failed: error " + std::to_string(rc)));
    }

    Frame frame;
    frame.streamIndex = packet.streamIndex;
    frame.pts = meta.pts;
    frame.keyframe = meta.keyframe != 0;
    frame.data = out;
    frame.dataSize = outSize;
    frame.hostApi = impl_->hostApi;

    if (impl_->plugin->type == BL_CODEC_VIDEO) {
        frame.type = Frame::Type::Video;
        frame.width = meta.width;
        frame.height = meta.height;
        frame.linesize = meta.linesize;
    } else {
        frame.type = Frame::Type::Audio;
        frame.sampleCount = meta.sample_count;
        frame.channels = meta.channels;
    }

    return Result<Frame>::ok(std::move(frame));
}

Result<std::optional<Frame>> DecoderBridge::flush() {
    if (!impl_ || !impl_->plugin || !impl_->ctx) {
        return Result<std::optional<Frame>>::err(
            Err::InvalidArgument, "decoder bridge is not initialized");
    }

    uint8_t* out = nullptr;
    size_t outSize = 0;

    int rc = impl_->plugin->flush(impl_->ctx, &out, &outSize);
    if (rc != BL_OK) {
        return Result<std::optional<Frame>>::err(
            Err::DecodeFailed,
            std::string("flush failed: error " + std::to_string(rc)));
    }

    if (!out || outSize == 0) {
        return Result<std::optional<Frame>>::ok(std::nullopt);
    }

    Frame frame;
    frame.streamIndex = -1;
    frame.data = out;
    frame.dataSize = outSize;
    frame.hostApi = impl_->hostApi;

    if (impl_->plugin->type == BL_CODEC_VIDEO) {
        frame.type = Frame::Type::Video;
    } else {
        frame.type = Frame::Type::Audio;
    }

    return Result<std::optional<Frame>>::ok(std::move(frame));
}

std::string_view DecoderBridge::codecName() const noexcept {
    if (!impl_) return {};
    return impl_->name;
}

} // namespace bl
