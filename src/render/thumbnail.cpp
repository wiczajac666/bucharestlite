#include <bl_render/thumbnail.hpp>

#include <bl_core/decoder_bridge.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/logger.hpp>

#include <bl_plugins/codec_plugin.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bl {

namespace {

// Decoded frame buffers are host-allocated (malloc/free), matching the
// ownership contract of Frame's destructor.
const BlHostApi kThumbnailHost = {
    /*host_abi_version*/ BL_PLUGIN_ABI_VERSION,
    /*alloc*/ [](size_t size, void*) { return std::malloc(size); },
    /*free*/ [](void* ptr, void*) { std::free(ptr); },
    /*userdata*/ nullptr,
};

// Pack a decoded BGRA frame into a tightly packed buffer unchanged.
bool packFrame(const Frame& frame, MediaThumbnail& out) {
    const uint32_t width = frame.width;
    const uint32_t height = frame.height;
    const uint32_t rowBytes = width * 4u;
    out.pixels.resize(static_cast<size_t>(rowBytes) * height);
    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(out.pixels.data() + static_cast<size_t>(y) * rowBytes,
                    frame.data + static_cast<size_t>(y) * frame.linesize,
                    rowBytes);
    }
    out.width = width;
    out.height = height;
    out.linesize = rowBytes;
    return true;
}

// Area-average (box filter) downscale of a packed BGRA frame. The codec
// plugins convert to BGRA at the *native* frame size, so the thumbnail size is
// realised here.
bool downscaleFrame(const Frame& frame, uint32_t maxWidth, MediaThumbnail& out) {
    const uint32_t srcW = frame.width;
    const uint32_t srcH = frame.height;
    const uint32_t dstW = std::min(srcW, maxWidth);
    uint32_t dstH = static_cast<uint32_t>(
        (static_cast<uint64_t>(srcH) * dstW + srcW / 2) / srcW);
    if (dstH == 0) {
        dstH = 1;
    }
    const uint32_t dstRowBytes = dstW * 4u;
    out.pixels.assign(static_cast<size_t>(dstRowBytes) * dstH, 0xFFu);

    for (uint32_t y = 0; y < dstH; ++y) {
        uint32_t sy0 = static_cast<uint32_t>(static_cast<uint64_t>(y) * srcH / dstH);
        uint32_t sy1 =
            static_cast<uint32_t>(static_cast<uint64_t>(y + 1) * srcH / dstH);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > srcH) sy1 = srcH;

        uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * dstRowBytes;
        for (uint32_t x = 0; x < dstW; ++x) {
            uint32_t sx0 =
                static_cast<uint32_t>(static_cast<uint64_t>(x) * srcW / dstW);
            uint32_t sx1 =
                static_cast<uint32_t>(static_cast<uint64_t>(x + 1) * srcW / dstW);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > srcW) sx1 = srcW;

            uint64_t sumB = 0;
            uint64_t sumG = 0;
            uint64_t sumR = 0;
            uint32_t count = 0;
            for (uint32_t sy = sy0; sy < sy1; ++sy) {
                const uint8_t* row =
                    frame.data + static_cast<size_t>(sy) * frame.linesize;
                for (uint32_t sx = sx0; sx < sx1; ++sx) {
                    const uint8_t* pixel = row + static_cast<size_t>(sx) * 4u;
                    sumB += pixel[0];
                    sumG += pixel[1];
                    sumR += pixel[2];
                    ++count;
                }
            }
            dst[x * 4u + 0] = static_cast<uint8_t>(sumB / count);
            dst[x * 4u + 1] = static_cast<uint8_t>(sumG / count);
            dst[x * 4u + 2] = static_cast<uint8_t>(sumR / count);
            dst[x * 4u + 3] = 0xFFu;
        }
    }
    out.width = dstW;
    out.height = dstH;
    out.linesize = dstRowBytes;
    return true;
}

// Store a decoded video frame, downscaling it when wider than `maxWidth`.
bool storeFrame(const Frame& frame, uint32_t maxWidth, MediaThumbnail& out) {
    if (frame.empty() || frame.type != Frame::Type::Video) {
        return false;
    }
    if (frame.width == 0 || frame.height == 0 || frame.data == nullptr) {
        return false;
    }
    if (frame.linesize < frame.width * 4u) {
        return false;
    }
    if (frame.width <= maxWidth) {
        return packFrame(frame, out);
    }
    return downscaleFrame(frame, maxWidth, out);
}

std::string formatDuration(const Duration& duration) {
    const double seconds = duration.toSeconds();
    if (!(seconds > 0.0) || !std::isfinite(seconds)) {
        return std::string("--:--:--");
    }
    const auto total = static_cast<long long>(seconds + 0.5);
    const long long hours = total / 3600;
    const long long minutes = (total % 3600) / 60;
    const long long secs = total % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", hours, minutes, secs);
    return std::string(buf);
}

std::string sampleRateLabel(uint32_t rate) {
    if (rate == 0) {
        return "?";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.5g kHz", static_cast<double>(rate) / 1000.0);
    return std::string(buf);
}

std::string channelLabel(uint32_t channels) {
    if (channels == 0) return "?";
    if (channels == 1) return "mono";
    if (channels == 2) return "stereo";
    return std::to_string(channels) + "ch";
}

} // namespace

Result<MediaThumbnail> readThumbnail(const std::string& path,
                                     const CodecRegistry& registry,
                                     uint32_t maxWidth) {
    if (path.empty()) {
        return Result<MediaThumbnail>::err(Err::InvalidArgument,
                                           "empty media path");
    }
    if (maxWidth == 0) {
        return Result<MediaThumbnail>::err(Err::InvalidArgument,
                                           "maxWidth must be non-zero");
    }

    auto opened = Demuxer::open(path);
    if (!opened.ok()) {
        return Result<MediaThumbnail>::err(opened.code(), opened.message());
    }
    Demuxer demuxer = std::move(opened.value());

    MediaThumbnail thumb;
    const StreamInfo& info = demuxer.info();
    thumb.info = info;

    if (info.videoStreams.empty()) {
        return Result<MediaThumbnail>::ok(std::move(thumb));
    }
    thumb.hasVideo = true;

    const VideoStreamInfo& video = info.videoStreams.front();
    if (video.width == 0 || video.height == 0) {
        return Result<MediaThumbnail>::ok(std::move(thumb));
    }

    const Rational fps =
        (video.fps.valid() && !video.fps.isZero()) ? video.fps : Rational{24, 1};

    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.codec_name = video.codecName.c_str();
    cfg.extradata = video.extradata.empty() ? nullptr : video.extradata.data();
    cfg.extradata_size = video.extradata.size();
    // The ffmpeg plugins decode at the native frame size; the thumbnail is
    // scaled down afterwards in downscaleFrame().
    cfg.video.width = video.width;
    cfg.video.height = video.height;
    cfg.video.fps = {static_cast<int32_t>(fps.num),
                     static_cast<uint32_t>(fps.den)};
    cfg.video.pixel_aspect = {1, 1};
    cfg.video.pix_fmt = BL_PIXFMT_BGRA32;
    cfg.host = &kThumbnailHost;

    auto bridgeResult = DecoderBridge::create(registry, video.codecName, cfg);
    if (!bridgeResult.ok()) {
        BL_LOG_WARN("thumb", "cannot decode '" + path + "' (" + video.codecName +
                                 "): " + bridgeResult.message());
        return Result<MediaThumbnail>::ok(std::move(thumb));
    }
    DecoderBridge bridge = std::move(bridgeResult.value());

    // The opening keyframe is normally a handful of packets in; the cap only
    // guards against malformed streams.
    constexpr int kMaxPackets = 1024;
    for (int i = 0; i < kMaxPackets; ++i) {
        auto packetResult = demuxer.nextPacket();
        if (!packetResult.ok()) {
            BL_LOG_WARN("thumb", "demux error for '" + path +
                                     "': " + packetResult.message());
            break;
        }
        auto packet = std::move(packetResult.value());
        if (!packet.has_value()) {
            break;  // end of stream
        }
        if (packet->streamIndex != video.index) {
            continue;
        }

        auto decoded = bridge.decode(*packet);
        if (!decoded.ok()) {
            if (decoded.message() == "need more input") {
                continue;
            }
            BL_LOG_WARN("thumb", "decode error for '" + path +
                                     "': " + decoded.message());
            continue;
        }
        if (storeFrame(*decoded, maxWidth, thumb)) {
            return Result<MediaThumbnail>::ok(std::move(thumb));
        }
    }

    // The first frame may still sit in the decoder's reorder buffer.
    if (auto flushed = bridge.flush();
        flushed.ok() && flushed.value().has_value()) {
        storeFrame(**flushed, maxWidth, thumb);
    }
    return Result<MediaThumbnail>::ok(std::move(thumb));
}

std::string describeMedia(const StreamInfo& info) {
    const std::string duration = formatDuration(info.duration);

    if (!info.videoStreams.empty()) {
        const VideoStreamInfo& v = info.videoStreams.front();
        std::string out = duration + " · " + std::to_string(v.width) + "×" +
                          std::to_string(v.height);
        if (!v.codecName.empty()) {
            out += " · " + v.codecName;
        }
        return out;
    }
    if (!info.audioStreams.empty()) {
        const AudioStreamInfo& a = info.audioStreams.front();
        std::string out = duration + " · " + sampleRateLabel(a.sampleRate) +
                          " · " + channelLabel(a.channels);
        if (!a.codecName.empty()) {
            out += " · " + a.codecName;
        }
        return out;
    }
    if (!info.subtitleStreams.empty()) {
        const SubtitleStreamInfo& s = info.subtitleStreams.front();
        std::string out = duration + " · subtitle";
        if (!s.codecName.empty()) {
            out += " · " + s.codecName;
        }
        return out;
    }
    return "unknown media";
}

} // namespace bl
