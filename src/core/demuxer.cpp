#include <bl_core/demuxer.hpp>

#include <bl_core/logger.hpp>

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
}

#include <string>
#include <utility>

namespace bl {

namespace {

bool isValidRational(const AVRational& r) { return r.num > 0 && r.den > 0; }

} // namespace

Result<Demuxer> Demuxer::open(const std::string& path) {
    if (path.empty()) {
        return Result<Demuxer>::err(Err::InvalidArgument, "empty media path");
    }

    AVFormatContext* fmt = nullptr;
    int ret = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        bool notFound = ret == AVERROR(ENOENT);
        return Result<Demuxer>::err(
            notFound ? Err::FileNotFound : Err::IoError,
            std::string("failed to open '") + path + "': " + errBuf);
    }

    ret = avformat_find_stream_info(fmt, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        avformat_close_input(&fmt);
        return Result<Demuxer>::err(Err::DecodeFailed,
                                    std::string("failed to read streams: ") +
                                        errBuf);
    }

    auto probeResult =
        MediaSource::probe(MediaLocator{path});
    if (!probeResult.ok()) {
        avformat_close_input(&fmt);
        return Result<Demuxer>::err(probeResult.code(),
                                    probeResult.message());
    }

    Demuxer demuxer;
    demuxer.fmt_ = fmt;
    demuxer.info_ = std::move(probeResult).value();
    return Result<Demuxer>::ok(std::move(demuxer));
}

Demuxer::Demuxer(Demuxer&& other) noexcept
    : fmt_(other.fmt_), info_(std::move(other.info_)) {
    other.fmt_ = nullptr;
}

Demuxer& Demuxer::operator=(Demuxer&& other) noexcept {
    if (this != &other) {
        if (fmt_) avformat_close_input(&fmt_);
        fmt_ = other.fmt_;
        info_ = std::move(other.info_);
        other.fmt_ = nullptr;
    }
    return *this;
}

Demuxer::~Demuxer() {
    if (fmt_) avformat_close_input(&fmt_);
}

int Demuxer::defaultSeekStream() const noexcept {
    if (!info_.videoStreams.empty()) return info_.videoStreams.front().index;
    if (!info_.audioStreams.empty()) return info_.audioStreams.front().index;
    return 0;
}

Result<std::optional<Packet>> Demuxer::nextPacket() {
    if (!fmt_) {
        return Result<std::optional<Packet>>::err(
            Err::InvalidArgument, "demuxer is not open");
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return Result<std::optional<Packet>>::err(Err::OutOfMemory,
                                                  "av_packet_alloc failed");
    }

    int ret = av_read_frame(fmt_, pkt);
    if (ret < 0) {
        av_packet_free(&pkt);
        if (ret == AVERROR_EOF || avio_feof(fmt_->pb)) {
            return Result<std::optional<Packet>>::ok(std::nullopt);
        }
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        return Result<std::optional<Packet>>::err(
            Err::DecodeFailed, std::string("av_read_frame failed: ") + errBuf);
    }

    Packet out;
    out.streamIndex = pkt->stream_index;
    out.keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

    const AVRational timeBase = fmt_->streams[pkt->stream_index]->time_base;
    const Rational rate = Rational::make(timeBase.num, timeBase.den);

    if (pkt->pts != AV_NOPTS_VALUE) {
        out.hasPts = true;
        out.pts = Time::fromTicks(pkt->pts, rate);
    }
    if (pkt->dts != AV_NOPTS_VALUE) {
        out.hasDts = true;
        out.dts = Time::fromTicks(pkt->dts, rate);
    }
    if (pkt->duration > 0) {
        out.duration = Duration::fromTicks(pkt->duration, rate);
    }

    out.data.assign(pkt->data, pkt->data + pkt->size);

    av_packet_free(&pkt);
    return Result<std::optional<Packet>>::ok(std::move(out));
}

Result<void> Demuxer::seek(Time target) {
    if (!fmt_) {
        return Result<void>::err(Err::InvalidArgument, "demuxer is not open");
    }

    const int streamIndex = defaultSeekStream();
    const AVRational timeBase = fmt_->streams[streamIndex]->time_base;
    const Rational streamRate = Rational::make(timeBase.num, timeBase.den);

    Time inStreamUnits = Time::fromTicks(0, streamRate) + target;

    int64_t timestamp = inStreamUnits.ticks;
    if (timestamp < 0) timestamp = 0;

    int ret = av_seek_frame(fmt_, streamIndex, timestamp,
                            AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        ret = av_seek_frame(fmt_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
    }
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        return Result<void>::err(Err::IoError,
                                 std::string("seek failed: ") + errBuf);
    }

    BL_LOG_TRACE("media", "seek to " +
                              std::to_string(inStreamUnits.toSeconds()) + "s");
    return Result<void>();
}

} // namespace bl