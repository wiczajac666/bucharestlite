#include "bl_export/mkv_muxer.h"

#include <bl_plugins/codec_plugin.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include <cstring>
#include <string>

namespace bl {
namespace export_ {

struct MKVMuxer::Impl {
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
    AVRational audio_tb{1, 48000};
};

MKVMuxer::MKVMuxer() : impl_(new Impl()) {}

MKVMuxer::~MKVMuxer() { close(); delete impl_; }

Result<void> MKVMuxer::open(const BlFormatPreset* preset,
                                          const char* output_path) {
    if (!preset || !output_path) {
        return Result<void>(Err::InvalidArgument, "null preset or path");
    }

    // Allocate output context with matroska format
    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_alloc_output_context2(
        &fmt_ctx, nullptr, "matroska", output_path);
    if (!fmt_ctx) {
        return Result<void>(Err::Internal,
                                   "failed to allocate output context");
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
    video_par->codec_id = AV_CODEC_ID_H264;
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

    // Create audio stream
    AVStream* audio_st = avformat_new_stream(fmt_ctx, nullptr);
    if (!audio_st) {
        avformat_free_context(fmt_ctx);
        return Result<void>(Err::Internal, "failed to create audio stream");
    }

    AVCodecParameters* audio_par = audio_st->codecpar;
    audio_par->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_par->codec_id = AV_CODEC_ID_AAC;
    audio_par->sample_rate = static_cast<int>(preset->audio.sample_rate);
    audio_par->ch_layout.nb_channels = static_cast<int>(preset->audio.channels);
    av_channel_layout_default(&audio_par->ch_layout,
                                     static_cast<int>(preset->audio.channels));
    audio_par->format = AV_SAMPLE_FMT_FLTP;
    audio_par->bits_per_raw_sample = 16;

    // Set audio time base from sample rate
    audio_st->time_base = {1, static_cast<int>(preset->audio.sample_rate)};

    // Create subtitle stream
    AVStream* subtitle_st = avformat_new_stream(fmt_ctx, nullptr);
    if (!subtitle_st) {
        avformat_free_context(fmt_ctx);
        return Result<void>(Err::Internal, "failed to create subtitle stream");
    }

    AVCodecParameters* subtitle_par = subtitle_st->codecpar;
    subtitle_par->codec_type = AVMEDIA_TYPE_SUBTITLE;
    subtitle_par->codec_id = AV_CODEC_ID_TEXT;  // MKV text subtitle
    subtitle_st->time_base = {1, static_cast<int>(preset->video.fps.num)};

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

    // Store state
    impl_->fmt_ctx = fmt_ctx;
    impl_->video_stream = video_st;
    impl_->audio_stream = audio_st;
    impl_->subtitle_stream = subtitle_st;
    impl_->preset = preset;
    impl_->output_path = output_path;
    impl_->is_open = true;
    impl_->bytes_written = 0;
    impl_->video_next_pts = 0;
    impl_->audio_next_pts = 0;
    impl_->subtitle_next_pts = 0;
    impl_->audio_rate = static_cast<int>(preset->audio.sample_rate);
    impl_->audio_tb = audio_st->time_base;

    return Result<void>();
}

void MKVMuxer::close() {
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

Result<void> MKVMuxer::writeVideoPacket(const uint8_t* data, size_t size,
                                                        uint64_t pts,
                                                        bool keyframe) {
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
    pkt->pts = static_cast<int64_t>(pts);
    pkt->dts = static_cast<int64_t>(pts);
    pkt->duration = 1;  // one frame per packet
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

Result<void> MKVMuxer::writeAudioPacket(const uint8_t* data, size_t size,
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

Result<void> MKVMuxer::writeSubtitlePacket(const uint8_t* data, size_t size,
                                                           uint64_t pts) {
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
    pkt->pts = static_cast<int64_t>(pts);
    pkt->dts = static_cast<int64_t>(pts);
    pkt->duration = 1;  // one subtitle frame

    int ret = av_interleaved_write_frame(impl_->fmt_ctx, pkt);
    av_packet_free(&pkt);

    if (ret < 0) {
        return Result<void>(Err::IoError, "failed to write subtitle packet");
    }

    impl_->bytes_written += size;
    return Result<void>();
}

bool MKVMuxer::isOpen() const { return impl_->is_open; }

const char* MKVMuxer::outputPath() const { return impl_->output_path.c_str(); }

size_t MKVMuxer::bytesWritten() const { return impl_->bytes_written; }

}  // namespace export_
}  // namespace bl