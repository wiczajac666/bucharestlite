#include "bl_export/muxer.h"

#include <bl_plugins/codec_plugin.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace bl {
namespace export_ {

struct Muxer::Impl {
    AVFormatContext* fmt_ctx = nullptr;
    AVStream* video_stream = nullptr;
    AVStream* audio_stream = nullptr;
    AVStream* subtitle_stream = nullptr;

    const BlFormatPreset* preset = nullptr;
    std::string output_path;
    size_t bytes_written = 0;
    bool is_open = false;

    uint64_t video_next_pts = 0;
    uint64_t audio_next_pts = 0;
    uint64_t subtitle_next_pts = 0;

    int video_timescale = 30;
    int audio_timescale = 48000;
    int subtitle_timescale = 30;
    int audio_rate = 48000;

    int video_codec_id = AV_CODEC_ID_H264;
    int audio_codec_id = AV_CODEC_ID_AAC;
    bool audio_enabled = true;
    bool subtitles_enabled = false;

    // Nominal fps and the stream time base the container actually adopted
    // (captured after avformat_write_header, since muxers may resample the
    // per-track time scale on their own).
    AVRational video_fps{24, 1};
    AVRational video_tb{1, 24};
    AVRational audio_tb{1, 48000};
    AVRational subtitle_tb{1, 24};
};

Muxer::Muxer() : impl_(new Impl()) {}

Muxer::~Muxer() { close(); delete impl_; }

void Muxer::setVideoCodecId(int codec_id) {
    if (codec_id > 0) impl_->video_codec_id = codec_id;
}

void Muxer::setAudioCodecId(int codec_id) {
    if (codec_id > 0) impl_->audio_codec_id = codec_id;
}

void Muxer::setAudioEnabled(bool enabled) { impl_->audio_enabled = enabled; }

void Muxer::setSubtitlesEnabled(bool enabled) {
    impl_->subtitles_enabled = enabled;
}

Result<void> Muxer::open(const BlFormatPreset* preset, const char* output_path) {
    if (!preset || !output_path) {
        return Result<void>(Err::InvalidArgument, "null preset or path");
    }

    // Allocate output context — format auto-detected from extension
    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_alloc_output_context2(
        &fmt_ctx, nullptr, nullptr, output_path);
    if (!fmt_ctx) {
        return Result<void>(Err::Internal, "failed to allocate output context");
    }

    // Create video stream
    AVStream* video_st = avformat_new_stream(fmt_ctx, nullptr);
    if (!video_st) {
        avformat_free_context(fmt_ctx);
        return Result<void>(Err::Internal, "failed to create video stream");
    }

    // Configure video codec parameters
    AVCodecParameters* video_par = video_st->codecpar;
    video_par->codec_type = AVMEDIA_TYPE_VIDEO;
    video_par->codec_id = static_cast<AVCodecID>(impl_->video_codec_id);
    video_par->width = static_cast<int>(preset->video.width);
    video_par->height = static_cast<int>(preset->video.height);
    video_par->format = AV_PIX_FMT_YUV420P;
    video_par->bits_per_raw_sample = 8;

    // Set video time base from fps
    AVRational video_fps = {static_cast<int>(preset->video.fps.num),
                                  static_cast<int>(preset->video.fps.den)};
    AVRational video_tb = {static_cast<int>(preset->video.fps.den),
                                  static_cast<int>(preset->video.fps.num)};
    video_st->time_base = video_tb;
    video_st->avg_frame_rate = video_fps;
    video_st->r_frame_rate = video_fps;

    // Create audio stream (skipped for video-only exports).
    AVStream* audio_st = nullptr;
    if (impl_->audio_enabled) {
        audio_st = avformat_new_stream(fmt_ctx, nullptr);
        if (!audio_st) {
            avformat_free_context(fmt_ctx);
            return Result<void>(Err::Internal, "failed to create audio stream");
        }

        AVCodecParameters* audio_par = audio_st->codecpar;
        audio_par->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_par->codec_id = static_cast<AVCodecID>(impl_->audio_codec_id);
        audio_par->sample_rate = static_cast<int>(preset->audio.sample_rate);
        audio_par->ch_layout.nb_channels =
            static_cast<int>(preset->audio.channels);
        av_channel_layout_default(&audio_par->ch_layout,
                                  static_cast<int>(preset->audio.channels));
        audio_par->format = AV_SAMPLE_FMT_FLTP;
        audio_par->bits_per_raw_sample = 16;

        // Set audio time base from sample rate
        audio_st->time_base = {1, static_cast<int>(preset->audio.sample_rate)};
    }

    // Create subtitle stream (MP4 only: MOV_TEXT is not valid in other
    // containers and no subtitle packets are written yet). Independent of the
    // audio stream choice so text-only projects can carry soft subtitles too.
    AVStream* subtitle_st = nullptr;
    {
        const std::string path(output_path ? output_path : "");
        const bool is_mp4 =
            path.size() >= 4 && path.compare(path.size() - 4, 4, ".mp4") == 0;
        if (is_mp4 && impl_->subtitles_enabled) {
            subtitle_st = avformat_new_stream(fmt_ctx, nullptr);
            if (subtitle_st) {
                AVCodecParameters* subtitle_par = subtitle_st->codecpar;
                subtitle_par->codec_type = AVMEDIA_TYPE_SUBTITLE;
                subtitle_par->codec_id =
                    AV_CODEC_ID_MOV_TEXT;  // MP4 text subtitle codec
                subtitle_st->time_base = {1,
                                          static_cast<int>(preset->video.fps.num)};
            }
        }
    }

    // Open output file
    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx->pb, output_path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            avformat_free_context(fmt_ctx);
            return Result<void>(Err::IoError, "failed to open output file");
        }
    }

    // Write container header
    ret = avformat_write_header(fmt_ctx, nullptr);
    if (ret < 0) {
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmt_ctx->pb);
        }
        avformat_free_context(fmt_ctx);
        return Result<void>(Err::Internal, "failed to write header");
    }

    // The muxer may have resampled the track time bases; use whatever it
    // settled on so packet timestamps land in the right scale.
    impl_->video_fps = {static_cast<int>(preset->video.fps.num),
                        static_cast<int>(preset->video.fps.den)};
    impl_->video_tb = video_st->time_base;
    if (audio_st) {
        impl_->audio_tb = audio_st->time_base;
    }
    if (subtitle_st) {
        impl_->subtitle_tb = subtitle_st->time_base;
    }

    // Store state
    impl_->fmt_ctx = fmt_ctx;
    impl_->video_stream = video_st;
    impl_->audio_stream = impl_->audio_enabled ? audio_st : nullptr;
    impl_->subtitle_stream = subtitle_st;
    impl_->preset = preset;
    impl_->output_path = output_path;
    impl_->is_open = true;
    impl_->bytes_written = 0;
    impl_->video_next_pts = 0;
    impl_->audio_next_pts = 0;
    impl_->subtitle_next_pts = 0;
    impl_->audio_rate = static_cast<int>(preset->audio.sample_rate);

    return Result<void>();
}

void Muxer::close() {
    if (!impl_->is_open) return;

    // Write trailer (finalizes container)
    av_write_trailer(impl_->fmt_ctx);

    // Close file
    if (!(impl_->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&impl_->fmt_ctx->pb);
    }

    // Free context
    avformat_free_context(impl_->fmt_ctx);

    impl_->video_stream = nullptr;
    impl_->audio_stream = nullptr;
    impl_->subtitle_stream = nullptr;
    impl_->is_open = false;
    impl_->bytes_written = 0;
}

Result<void> Muxer::writeVideoPacket(const uint8_t* data, size_t size,
                                                  uint64_t pts, bool keyframe) {
    if (!impl_->is_open || !impl_->video_stream) {
        return Result<void>(Err::InvalidArgument, "muxer not open");
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return Result<void>(Err::OutOfMemory, "failed to alloc packet");
    }

    pkt->stream_index = impl_->video_stream->index;
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = static_cast<int>(size);
    // The encoder hands back opaque packet buffers without their timestamps,
    // and B-frame codecs emit packets out of display order. Assign monotonic
    // container timestamps in the stream's adopted time base so the container
    // stays valid (mov/mp4 require non-decreasing dts); reordering happens
    // inside the player from the encoded bitstream.
    const AVRational fps_tb = {impl_->video_fps.den, impl_->video_fps.num};
    const int64_t pts_tb =
        av_rescale_q(static_cast<int64_t>(impl_->video_next_pts), fps_tb,
                     impl_->video_tb);
    pkt->pts = pts_tb;
    pkt->dts = pts_tb;
    pkt->duration = av_rescale_q(1, fps_tb, impl_->video_tb);
    impl_->video_next_pts++;
    if (keyframe) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    int ret = av_interleaved_write_frame(impl_->fmt_ctx, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        return Result<void>(Err::IoError, "failed to write video packet");
    }

    impl_->bytes_written += size;
    return Result<void>();
}

Result<void> Muxer::writeAudioPacket(const uint8_t* data, size_t size,
                                                  uint32_t duration_samples) {
    if (!impl_->is_open || !impl_->audio_stream) {
        return Result<void>(Err::InvalidArgument, "muxer not open");
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return Result<void>(Err::OutOfMemory, "failed to alloc packet");
    }

    pkt->stream_index = impl_->audio_stream->index;
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = static_cast<int>(size);
    // Audio is fed as a contiguous PCM stream; one encoded packet covers one
    // encoder granule (frame_size) of samples. Assign monotonic timestamps by
    // advancing a running sample counter so the container duration stays exact
    // and dts never moves backwards, matching the video path.
    const AVRational rate_tb = {1, impl_->audio_rate};
    const int64_t pts_tb =
        av_rescale_q(static_cast<int64_t>(impl_->audio_next_pts), rate_tb,
                     impl_->audio_tb);
    pkt->pts = pts_tb;
    pkt->dts = pts_tb;
    pkt->duration = av_rescale_q(
        static_cast<int64_t>(duration_samples), rate_tb, impl_->audio_tb);
    impl_->audio_next_pts += duration_samples;

    int ret = av_interleaved_write_frame(impl_->fmt_ctx, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        return Result<void>(Err::IoError, "failed to write audio packet");
    }

    impl_->bytes_written += size;
    return Result<void>();
}

Result<void> Muxer::writeSubtitlePacket(const uint8_t* data, size_t size,
                                         double pts_seconds,
                                         double duration_seconds) {
    if (!impl_->is_open || !impl_->subtitle_stream) {
        return Result<void>(Err::InvalidArgument, "muxer not open");
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return Result<void>(Err::OutOfMemory, "failed to alloc packet");
    }

    pkt->stream_index = impl_->subtitle_stream->index;
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = static_cast<int>(size);
    // Subtitle packets arrive in timeline seconds; rescale into whatever time
    // base the container settled on for the subtitle track.
    const AVRational sec_tb = {1, 1'000'000};
    const double startSec = std::max(0.0, pts_seconds);
    const int64_t pts_tb = av_rescale_q(
        static_cast<int64_t>(std::llround(startSec * 1'000'000.0)), sec_tb,
        impl_->subtitle_tb);
    const int64_t dur_tb = av_rescale_q(std::max<int64_t>(
        1, static_cast<int64_t>(std::llround(
               duration_seconds * 1'000'000.0))),
        sec_tb, impl_->subtitle_tb);
    pkt->pts = pts_tb;
    pkt->dts = pts_tb;
    pkt->duration = dur_tb;
    impl_->subtitle_next_pts = static_cast<uint64_t>(pts_tb) +
                               static_cast<uint64_t>(dur_tb);

    int ret = av_interleaved_write_frame(impl_->fmt_ctx, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        return Result<void>(Err::IoError, "failed to write subtitle packet");
    }

    impl_->bytes_written += size;
    return Result<void>();
}

bool Muxer::isOpen() const { return impl_->is_open; }

const char* Muxer::outputPath() const { return impl_->output_path.c_str(); }

size_t Muxer::bytesWritten() const { return impl_->bytes_written; }

}  // namespace export_
}  // namespace bl