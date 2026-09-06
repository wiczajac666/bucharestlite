#include <bl_render/media_decode_source.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/logger.hpp>

#include <bl_plugins/codec_plugin.h>

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace bl {

namespace {

// Decoded frame buffers are heap-allocated (host-side) and handed to the
// caller as plain Frame objects; Frame's destructor releases them through
// this allocator pair, matching how DecoderBridge frames are owned.
const BlHostApi kPreviewAllocHost = {
    /*host_abi_version*/ BL_PLUGIN_ABI_VERSION,
    /*alloc*/ [](size_t size, void*) { return std::malloc(size); },
    /*free*/ [](void* ptr, void*) { std::free(ptr); },
    /*userdata*/ nullptr,
};

} // namespace

struct MediaDecodeSource::Impl {
    CodecRegistry registry;
    std::vector<std::unique_ptr<PluginHandle>> handles;
    std::unordered_map<std::string, std::string> paths;
    FrameCache& cache;

    struct SourceState {
        std::optional<Demuxer> demuxer;
        std::unique_ptr<DecoderBridge> decoder;
        bool opened{false};
        int videoStreamIndex{-1};
        Rational fps{0, 1};
        uint32_t bridgeWidth{0};
        uint32_t bridgeHeight{0};
        std::optional<std::pair<Time, std::shared_ptr<Frame>>> lastFrame;
        bool atEos{false};
    };

    std::unordered_map<std::string, SourceState> sources;

    explicit Impl(std::vector<MediaBinItem> mediaBin, FrameCache& inCache, const Spec& spec)
        : cache(inCache) {
        for (const auto& item : mediaBin) {
            if (!item.path.empty()) {
                paths.emplace(item.id, item.path);
            }
        }

        (void)registerBuiltins(registry);

        PluginLoader loader;
        for (const auto& [dir, origin] : spec.pluginDirs) {
            auto report = loader.scanDirectory(dir, origin);
            if (!report.ok()) {
                BL_LOG_WARN("media",
                            "plugin scan of '" + dir + "' failed: " + report.message());
                continue;
            }
            for (auto& handle : report.value().plugins) {
                auto reg = registry.registerPlugin(
                    const_cast<BlCodecPlugin*>(handle->plugin()));
                if (!reg.ok()) {
                    BL_LOG_WARN("media", "plugin registration skipped: " + reg.message());
                    continue;
                }
                BL_LOG_DEBUG("media",
                             std::string("registered plugin '") +
                                 (handle->plugin()->name ? handle->plugin()->name
                                                         : "") +
                                 "' from " + handle->path());
                handles.push_back(std::move(handle));
            }
        }
    }

    static Frame ownedCopy(const Frame& frame) {
        Frame out;
        out.type = frame.type;
        out.streamIndex = frame.streamIndex;
        out.width = frame.width;
        out.height = frame.height;
        out.linesize = frame.linesize;
        out.dataSize = frame.dataSize;
        out.sampleCount = frame.sampleCount;
        out.channels = frame.channels;
        out.pts = frame.pts;
        out.keyframe = frame.keyframe;
        out.hostApi = &kPreviewAllocHost;
        if (frame.dataSize > 0) {
            out.data = static_cast<uint8_t*>(kPreviewAllocHost.alloc(frame.dataSize,
                                                                      kPreviewAllocHost.userdata));
            if (out.data) {
                std::memcpy(out.data, frame.data, frame.dataSize);
            }
        }
        return out;
    }

    static std::shared_ptr<Frame> toCached(const Frame& frame) {
        return std::make_shared<Frame>(ownedCopy(frame));
    }
};

MediaDecodeSource::MediaDecodeSource(std::vector<MediaBinItem> mediaBin, FrameCache& cache,
                                     Spec spec)
    : impl_(std::make_unique<Impl>(std::move(mediaBin), cache, spec)) {}

MediaDecodeSource::~MediaDecodeSource() = default;

MediaDecodeSource::MediaDecodeSource(MediaDecodeSource&&) noexcept = default;
MediaDecodeSource& MediaDecodeSource::operator=(MediaDecodeSource&&) noexcept = default;

void MediaDecodeSource::reset() {
    impl_->sources.clear();
}

Result<Frame> MediaDecodeSource::getFrame(const std::string& mediaItemId, Time sourceTime,
                                          uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return Result<Frame>::err(Err::InvalidArgument, "decode size must be non-zero");
    }

    auto pathIt = impl_->paths.find(mediaItemId);
    if (pathIt == impl_->paths.end()) {
        return Result<Frame>::err(Err::InvalidArgument,
                                  "media item '" + mediaItemId + "' not in media bin");
    }
    const std::string& path = pathIt->second;

    auto& state = impl_->sources[mediaItemId];

    if (!state.opened) {
        auto opened = Demuxer::open(path);
        if (!opened.ok()) {
            impl_->sources.erase(mediaItemId);
            return Result<Frame>::err(opened.code(), opened.message());
        }
        state.demuxer = std::move(opened.value());
        state.opened = true;

        const StreamInfo& info = state.demuxer->info();
        if (info.videoStreams.empty()) {
            impl_->sources.erase(mediaItemId);
            return Result<Frame>::err(Err::DecodeFailed,
                                      "media '" + path + "' has no video stream");
        }
        const VideoStreamInfo& v = info.videoStreams.front();
        state.videoStreamIndex = v.index;
        state.fps = (v.fps.valid() && !v.fps.isZero()) ? v.fps : Rational{24, 1};
    }

    if (!state.decoder || state.bridgeWidth != width || state.bridgeHeight != height) {
        const StreamInfo& info = state.demuxer->info();
        const VideoStreamInfo& v = info.videoStreams.front();

        BlCodecConfig cfg{};
        cfg.abi_version = BL_PLUGIN_ABI_VERSION;
        cfg.codec_name = v.codecName.c_str();
        cfg.extradata = v.extradata.empty() ? nullptr : v.extradata.data();
        cfg.extradata_size = v.extradata.size();
        cfg.video.width = width;
        cfg.video.height = height;
        cfg.video.fps = {static_cast<int32_t>(state.fps.num),
                         static_cast<uint32_t>(state.fps.den)};
        cfg.video.pixel_aspect = {1, 1};
        cfg.video.pix_fmt = BL_PIXFMT_BGRA32;
        cfg.host = &kPreviewAllocHost;

        auto bridge = DecoderBridge::create(impl_->registry, v.codecName, cfg);
        if (!bridge.ok()) {
            return Result<Frame>::err(Err::DecodeFailed,
                                      "cannot decode '" + path + "' (" + v.codecName +
                                          "): " + bridge.message());
        }
        state.decoder = std::make_unique<DecoderBridge>(std::move(bridge.value()));
        state.bridgeWidth = width;
        state.bridgeHeight = height;
        state.lastFrame.reset();
        state.atEos = false;
    }

    // Cache hit: the exact source frame was parked by an earlier request.
    const Time frameKeyTime = Time::fromFrame(sourceTime.floorFrameAt(state.fps), state.fps);
    const FrameKey key{mediaItemId, frameKeyTime, width, height};
    if (const auto hit = impl_->cache.get(key)) {
        return Result<Frame>::ok(Impl::ownedCopy(*hit));
    }

    if (!state.atEos) {
        if (auto seek = state.demuxer->seek(sourceTime); !seek.ok()) {
            BL_LOG_WARN("media", "seek failed for '" + path + "': " + seek.message());
        }
    }

    std::optional<std::pair<Time, std::shared_ptr<Frame>>> best;
    const Duration frameDur = Duration::fromFrames(1, state.fps);
    for (;;) {
        auto packetResult = state.demuxer->nextPacket();
        if (!packetResult.ok()) {
            return Result<Frame>::err(Err::DecodeFailed, packetResult.message());
        }
        const std::optional<Packet> packet = std::move(packetResult.value());
        if (!packet.has_value()) {
            // End of stream. Prefer the decoded frame covering sourceTime; if
            // the request fell past the last *decoded* frame, drain the
            // decoder's buffered tail so the final frame still displays.
            if (best) {
                state.atEos = true;
                break;
            }
            if (const auto flushed = state.decoder->flush();
                flushed.ok() && flushed.value().has_value()) {
                const Frame& tail = **flushed;
                if (!tail.empty() && tail.type == Frame::Type::Video) {
                    const Time tailTime = state.lastFrame
                                              ? state.lastFrame->first + frameDur
                                              : sourceTime;
                    const auto cached = Impl::toCached(tail);
                    impl_->cache.put(
                        {mediaItemId,
                         Time::fromFrame(tailTime.floorFrameAt(state.fps), state.fps),
                         width, height},
                        cached);
                    return Result<Frame>::ok(Impl::ownedCopy(*cached));
                }
            }
            state.atEos = true;
            break;
        }

        if (packet->streamIndex != state.videoStreamIndex) {
            continue;
        }

        auto decoded = state.decoder->decode(*packet);
        if (!decoded.ok()) {
            if (decoded.message() == "need more input") {
                continue;
            }
            BL_LOG_WARN("media", "decode error for '" + path + "': " + decoded.message());
            continue;
        }

        Frame& frame = *decoded;
        if (frame.empty() || frame.type != Frame::Type::Video) {
            continue;
        }

        const Time frameTime = packet->hasPts ? packet->pts
                                              : (best ? best->first + frameDur : sourceTime);
        const auto cached = Impl::toCached(frame);
        impl_->cache.put(
            {mediaItemId, Time::fromFrame(frameTime.floorFrameAt(state.fps), state.fps),
             width, height},
            cached);

        if (frameTime > sourceTime && best) {
            // First frame past the request; the one immediately before it covers
            // sourceTime, which is what a viewer displays at the playhead.
            return Result<Frame>::ok(Impl::ownedCopy(*best->second));
        }
        if (frameTime > sourceTime) {
            return Result<Frame>::ok(Impl::ownedCopy(*cached));
        }
        best = std::make_pair(frameTime, cached);
        state.lastFrame = best;
    }

    if (best) {
        return Result<Frame>::ok(Impl::ownedCopy(*best->second));
    }
    if (state.lastFrame) {
        return Result<Frame>::ok(Impl::ownedCopy(*state.lastFrame->second));
    }
    return Result<Frame>::err(Err::DecodeFailed,
                              "no video frame decoded for '" + path + "'");
}

} // namespace bl