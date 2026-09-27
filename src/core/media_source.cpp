#include <bl_core/media_source.hpp>

#include <bl_core/logger.hpp>

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/rational.h>
}

#include <string>

namespace bl {

namespace {

Rational toRational(const AVRational& r) {
    if (r.den == 0) return Rational{0, 1};
    return Rational::make(r.num, r.den);
}

} // namespace

Result<StreamInfo> MediaSource::probe(const MediaLocator& locator) {
    if (locator.path.empty()) {
        return Result<StreamInfo>::err(Err::InvalidArgument,
                                       "empty media path");
    }

    AVFormatContext* fmt = nullptr;
    int ret = avformat_open_input(&fmt, locator.path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        bool notFound = ret == AVERROR(ENOENT);
        return Result<StreamInfo>::err(
            notFound ? Err::FileNotFound : Err::IoError,
            std::string("failed to open '") + locator.path +
                "': " + errBuf);
    }

    StreamInfo info;
    info.containerName = fmt->iformat ? fmt->iformat->name : "unknown";

    ret = avformat_find_stream_info(fmt, nullptr);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errBuf, sizeof(errBuf));
        avformat_close_input(&fmt);
        return Result<StreamInfo>::err(
            Err::DecodeFailed,
            std::string("failed to probe streams: ") + errBuf);
    }

    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration >= 0) {
        constexpr Rational kAvTimeBase{1'000'000, 1};
        info.duration = Duration::fromTicks(fmt->duration, kAvTimeBase);
    }

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream* stream = fmt->streams[i];
        const AVCodecParameters* par = stream->codecpar;

        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            VideoStreamInfo v;
            v.index = static_cast<int>(i);
            v.width = static_cast<uint32_t>(par->width);
            v.height = static_cast<uint32_t>(par->height);
            v.codecName = avcodec_get_name(par->codec_id);

            AVRational fps = stream->avg_frame_rate;
            if (fps.num <= 0 || fps.den <= 0) fps = stream->r_frame_rate;
            v.fps = toRational(fps);

            AVRational aspect = stream->sample_aspect_ratio;
            if (aspect.num <= 0 || aspect.den <= 0) {
                aspect = par->sample_aspect_ratio;
            }
            v.pixelAspect = toRational(aspect);

            if (par->extradata_size > 0 && par->extradata) {
                v.extradata.assign(par->extradata,
                                   par->extradata + par->extradata_size);
            }

            info.videoStreams.push_back(std::move(v));
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            AudioStreamInfo a;
            a.index = static_cast<int>(i);
            a.sampleRate = static_cast<uint32_t>(par->sample_rate);
#if LIBAVUTIL_VERSION_MAJOR >= 58
            a.channels = static_cast<uint32_t>(par->ch_layout.nb_channels);
#else
            a.channels = static_cast<uint32_t>(par->channels);
#endif
            a.codecName = avcodec_get_name(par->codec_id);

            if (par->extradata_size > 0 && par->extradata) {
                a.extradata.assign(par->extradata,
                                   par->extradata + par->extradata_size);
            }

            info.audioStreams.push_back(std::move(a));
        } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            SubtitleStreamInfo s;
            s.index = static_cast<int>(i);
            s.codecName = avcodec_get_name(par->codec_id);

            if (par->extradata_size > 0 && par->extradata) {
                s.extradata.assign(par->extradata,
                                   par->extradata + par->extradata_size);
            }

            info.subtitleStreams.push_back(std::move(s));
        }
    }

    avformat_close_input(&fmt);

    BL_LOG_DEBUG("media", "probed '" + locator.path + "' (" +
                              info.containerName + ", " +
                              std::to_string(info.videoStreams.size()) +
                              " video, " +
                              std::to_string(info.audioStreams.size()) +
                              " audio, " +
                              std::to_string(info.subtitleStreams.size()) +
                              " subtitles)");

    return Result<StreamInfo>::ok(std::move(info));
}

} // namespace bl