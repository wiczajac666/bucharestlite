#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct BlFfmpegCtx {
    const BlHostApi* host;
    const BlFfmpegProfile* profile;

    AVCodecContext* decoder;
    AVCodecContext* encoder;

    AVFrame* dec_frame;
    AVPacket* dec_pkt;
    AVFrame* enc_in;
    AVFrame* conv_frame;

    int is_video;
    int encode_available;

    /* decode */
    int decoder_sent_eof;
    int decoder_eof;
    int decode_used;

    int64_t dec_pts; /* monotonic fallback pts */

    /* decode conversion scratch */
    SwsContext* sws_dec;
    SwrContext* swr_dec;

    /* encode */
    uint32_t enc_width;
    uint32_t enc_height;
    uint32_t enc_linesize;
    uint32_t enc_channels;
    int enc_geometry_set;
    int64_t enc_pts; /* video: frames; audio: samples */
    int encoder_sent_eof;
    int encoder_eof;
    int encode_used;
    SwsContext* sws_enc;
    SwrContext* swr_enc;

    /* Encoder codec private data (SPS/PPS, AudioSpecificConfig, ...), captured
     * for get_extradata(). Owned by the plugin; freed in cleanup. */
    uint8_t* enc_extradata;
    size_t enc_extradata_size;

    /* audio encode: planar f32 slices not yet accepted by the encoder
     * (audio encoders may EAGAIN while their internal buffers fill). */
    uint8_t* pend_buf;
    size_t pend_cap;
    size_t pend_samples;
} BlFfmpegCtx;

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static uint8_t* host_alloc(BlFfmpegCtx* c, size_t n) {
    if (!c || !c->host || !c->host->alloc) return NULL;
    return (uint8_t*)c->host->alloc(n, c->host->userdata);
}

static void host_free(BlFfmpegCtx* c, void* p) {
    if (c->host && c->host->free) c->host->free(p, c->host->userdata);
}

/* Convert a decoded AVFrame into the universal interchange format and hand
 * it to the host. Returns BL_OK or a negative BL_ERR_*. */
static int deliver_frame(BlFfmpegCtx* c, AVFrame* f, uint8_t** out,
                         size_t* out_size, BlFrameMeta* meta) {
    if (c->is_video) {
        int w = f->width, h = f->height;
        if (w <= 0 || h <= 0 || (int)f->format < 0)
            return BL_ERR_DECODE_FAILED;
        size_t bytes = (size_t)w * (size_t)h * 4u;
        uint8_t* buf = host_alloc(c, bytes);
        if (!buf) return BL_ERR_OUT_OF_MEMORY;

        c->sws_dec = sws_getCachedContext(
            c->sws_dec, w, h, f->format, w, h, AV_PIX_FMT_BGRA, SWS_BILINEAR,
            NULL, NULL, NULL);
        if (!c->sws_dec) {
            host_free(c, buf);
            return BL_ERR_INTERNAL;
        }

        AVFrame* cv = c->conv_frame;
        av_frame_unref(cv);
        cv->format = AV_PIX_FMT_BGRA;
        cv->width = w;
        cv->height = h;
        if (av_frame_get_buffer(cv, 32) < 0) {
            host_free(c, buf);
            return BL_ERR_INTERNAL;
        }
        sws_scale(c->sws_dec, (const uint8_t* const*)f->data, f->linesize,
                  0, h, cv->data, cv->linesize);

        for (int y = 0; y < h; ++y) {
            memcpy(buf + (size_t)y * (size_t)w * 4u,
                   cv->data[0] + (size_t)y * (size_t)cv->linesize[0],
                   (size_t)w * 4u);
        }
        av_frame_unref(cv);

        *out = buf;
        *out_size = bytes;
        meta->width = (uint32_t)w;
        meta->height = (uint32_t)h;
        meta->linesize = (uint32_t)w * 4u;
        meta->sample_count = 0;
        meta->channels = 0;
        meta->keyframe = (f->flags & AV_FRAME_FLAG_KEY) ? 1 : 0;
        meta->pts = (f->pts != AV_NOPTS_VALUE)
                        ? (uint64_t)f->pts
                        : (uint64_t)(c->dec_pts++);
        return BL_OK;
    }

    /* audio -> f32 planar, one float-plane per channel */
    int nb = f->nb_samples;
    int ch = f->ch_layout.nb_channels > 0 ? f->ch_layout.nb_channels : 2;
    if (nb <= 0 || (int)f->format < 0) return BL_ERR_DECODE_FAILED;
    int rate = f->sample_rate > 0 ? f->sample_rate : 48000;

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, ch);

    c->swr_dec = NULL;
    if (swr_alloc_set_opts2(&c->swr_dec, &out_layout, AV_SAMPLE_FMT_FLTP,
                            rate, &f->ch_layout, f->format, rate, 0, NULL) < 0)
        return BL_ERR_INTERNAL;
    if (!c->swr_dec) return BL_ERR_INTERNAL;
    if (swr_init(c->swr_dec) < 0) return BL_ERR_INTERNAL;

    int max_out = (int)swr_get_out_samples(c->swr_dec, nb);
    if (max_out <= 0) max_out = nb;

    AVFrame* cv = c->conv_frame;
    av_frame_unref(cv);
    cv->format = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_copy(&cv->ch_layout, &out_layout);
    cv->sample_rate = rate;
    cv->nb_samples = max_out;
    if (av_frame_get_buffer(cv, 32) < 0) return BL_ERR_INTERNAL;

    int conv = swr_convert(c->swr_dec, cv->data, cv->nb_samples,
                           (const uint8_t**)f->data, nb);
    if (conv < 0) {
        av_frame_unref(cv);
        return BL_ERR_INTERNAL;
    }
    cv->nb_samples = conv;

    size_t bytes = (size_t)ch * (size_t)conv * 4u;
    uint8_t* buf = host_alloc(c, bytes);
    if (!buf) {
        av_frame_unref(cv);
        return BL_ERR_OUT_OF_MEMORY;
    }
    for (int k = 0; k < ch; ++k)
        memcpy(buf + (size_t)k * (size_t)conv * 4u, cv->data[k],
               (size_t)conv * 4u);
    av_frame_unref(cv);

    *out = buf;
    *out_size = bytes;
    meta->sample_count = (uint32_t)conv;
    meta->channels = (uint32_t)ch;
    meta->linesize = (uint32_t)conv; /* f32 planar line length in samples */
    meta->width = 0;
    meta->height = 0;
    meta->keyframe = 1;
    meta->pts = (f->pts != AV_NOPTS_VALUE)
                    ? (uint64_t)f->pts
                    : (uint64_t)(c->dec_pts++);
    return BL_OK;
}

static int aac_sampling_frequency_index(int rate) {
    static const int rates[] = {96000, 88200, 64000, 48000, 44100, 32000,
                                24000, 22050, 16000, 12000, 11025, 8000,
                                7350};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
        if (rate == rates[i]) return (int)i;
    return 3; /* 48000 */
}

/* Build the 3-byte AudioSpecificConfig (AAC-LC) an AAC decoder requires when
 * the stream carries raw access units instead of ADTS framing. */
static void synthesize_aac_extradata(const BlCodecConfig* cfg,
                                     AVCodecContext* dec) {
    int rate = cfg->audio.sample_rate > 0 ? (int)cfg->audio.sample_rate : 48000;
    int ch = cfg->audio.channels > 0 ? (int)cfg->audio.channels : 2;
    int sfi = aac_sampling_frequency_index(rate);
    uint8_t asc[3];
    asc[0] = (uint8_t)((2u << 3) | ((unsigned)sfi >> 1));
    asc[1] = (uint8_t)(((unsigned)(sfi & 1u) << 7) | ((unsigned)ch << 3));
    asc[2] = 0; /* GASpecificConfig */
    dec->extradata = (uint8_t*)av_mallocz(sizeof(asc) + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!dec->extradata) return;
    memcpy(dec->extradata, asc, sizeof(asc));
    dec->extradata_size = (int)sizeof(asc);
}

static void configure_decoder_extradata(BlFfmpegCtx* c, const BlCodecConfig* cfg) {
    if (cfg->extradata && cfg->extradata_size) {
        uint8_t* ed = (uint8_t*)av_mallocz(cfg->extradata_size +
                                           AV_INPUT_BUFFER_PADDING_SIZE);
        if (!ed) return;
        memcpy(ed, cfg->extradata, cfg->extradata_size);
        c->decoder->extradata = ed;
        c->decoder->extradata_size = (int)cfg->extradata_size;
        return;
    }
    /* No container-negotiated extradata: some codecs (e.g. raw AAC) cannot
     * decode without it. Synthesize it from the config where possible. */
    if (!c->is_video && c->profile->decode_id == AV_CODEC_ID_AAC) {
        synthesize_aac_extradata(cfg, c->decoder);
    }
}

/* Receive one decoded frame; returns BL_OK (output filled),
 * BL_DECODE_NEED_MORE_INPUT (nothing available), or a negative error. */
static int receive_decoded(BlFfmpegCtx* c, uint8_t** out, size_t* out_size,
                           BlFrameMeta* meta) {
    av_frame_unref(c->dec_frame);
    int ret = avcodec_receive_frame(c->decoder, c->dec_frame);
    if (ret == AVERROR(EAGAIN)) return BL_DECODE_NEED_MORE_INPUT;
    if (ret == AVERROR_EOF) {
        c->decoder_eof = 1;
        return BL_DECODE_NEED_MORE_INPUT;
    }
    if (ret < 0) return BL_ERR_DECODE_FAILED;
    return deliver_frame(c, c->dec_frame, out, out_size, meta);
}

/* Copy the encoder's codec private data out of the AVCodecContext the first time
 * it is available. avcodec_open2() populates it for most codecs, but a few only
 * fill it in while producing the first packet, so this runs both after open and
 * after each successfully received packet. Idempotent: once captured the buffer
 * is kept (the parameter sets do not change mid-stream). */
static void capture_encoder_extradata(BlFfmpegCtx* c) {
    if (c->enc_extradata || !c->encoder) return;
    if (!c->encoder->extradata || c->encoder->extradata_size <= 0) return;

    uint8_t* copy = (uint8_t*)av_malloc((size_t)c->encoder->extradata_size);
    if (!copy) return;
    memcpy(copy, c->encoder->extradata, (size_t)c->encoder->extradata_size);
    c->enc_extradata = copy;
    c->enc_extradata_size = (size_t)c->encoder->extradata_size;
}

/* Receive one encoded packet; returns BL_OK (output filled or none yet),
 * BL_DECODE_NEED_MORE_INPUT (nothing available), or a negative error. */
static int receive_encoded(BlFfmpegCtx* c, uint8_t** out, size_t* out_size) {
    av_packet_unref(c->dec_pkt);
    int ret = avcodec_receive_packet(c->encoder, c->dec_pkt);
    if (ret == AVERROR(EAGAIN)) return BL_DECODE_NEED_MORE_INPUT;
    if (ret == AVERROR_EOF) {
        c->encoder_eof = 1;
        return BL_DECODE_NEED_MORE_INPUT;
    }
    if (ret < 0) return BL_ERR_ENCODE_FAILED;
    if (c->dec_pkt->size <= 0) return BL_DECODE_NEED_MORE_INPUT;

    /* Encoders that only publish their parameter sets alongside the first
     * packet land here; with BL_ENCFLAG_GLOBAL_HEADER they published at open
     * already and this is a no-op. */
    capture_encoder_extradata(c);

    uint8_t* buf = host_alloc(c, (size_t)c->dec_pkt->size);
    if (!buf) return BL_ERR_OUT_OF_MEMORY;
    memcpy(buf, c->dec_pkt->data, (size_t)c->dec_pkt->size);
    *out = buf;
    *out_size = (size_t)c->dec_pkt->size;
    return BL_OK;
}

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */

static const BlConfigEntry* find_param(const BlCodecConfig* cfg,
                                       const char* name) {
    if (!cfg->params || !name) return NULL;
    for (const BlConfigEntry* e = cfg->params; e && e->name; ++e) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

static void apply_encode_options(BlFfmpegCtx* c, AVCodecContext* avctx,
                                 const BlCodecConfig* cfg) {
    const BlOptionMap* t = c->profile->encode_options;
    if (!t) return;

    for (const BlOptionMap* it = t; it->param; ++it) {
        double val = it->def_num;
        const char* sval = it->def_str;
        const int is_string = (it->kind == BL_OPT_STRING);

        const BlConfigEntry* e = find_param(cfg, it->param);
        if (e) {
            switch (e->value.type) {
                case BL_VALUE_INT:
                    val = (double)e->value.i;
                    break;
                case BL_VALUE_FLOAT:
                    val = e->value.f;
                    break;
                case BL_VALUE_BOOL:
                    val = e->value.i ? 1.0 : 0.0;
                    break;
                case BL_VALUE_STRING:
                    sval = e->value.s;
                    break;
                default:
                    break;
            }
        }
        if (!is_string && (it->min != it->max)) {
            if (val < it->min) val = it->min;
            if (val > it->max) val = it->max;
        }

        switch (it->kind) {
            case BL_OPT_INT:
                av_opt_set_int(avctx, it->av_name, (int64_t)val, 0);
                break;
            case BL_OPT_DOUBLE:
                av_opt_set_double(avctx, it->av_name, val, 0);
                break;
            case BL_OPT_STRING:
                av_opt_set(avctx, it->av_name, sval ? sval : "", 0);
                break;
            case BL_FIELD_BITRATE:
                avctx->bit_rate = (int64_t)val;
                break;
            case BL_FIELD_QSCALE:
                avctx->global_quality = (int)val;
                avctx->flags |= AV_CODEC_FLAG_QSCALE;
                break;
            case BL_FIELD_GOP:
                avctx->gop_size = (int)val;
                break;
            case BL_FIELD_THREADS:
                avctx->thread_count = (int)val;
                break;
            case BL_FIELD_COMPRESSION:
                avctx->compression_level = (int)val;
                break;
            case BL_FIELD_STRICT:
                avctx->strict_std_compliance = (int)val;
                break;
            default:
                break;
        }
    }
}

static int open_encoder(BlFfmpegCtx* c, const BlCodecConfig* cfg) {
    const BlFfmpegProfile* pr = c->profile;
    const AVCodec* codec = NULL;

    if (pr->encode_name)
        codec = avcodec_find_encoder_by_name(pr->encode_name);
    if (!codec && pr->encode_fallback_name)
        codec = avcodec_find_encoder_by_name(pr->encode_fallback_name);
    if (!codec && pr->encode_id != AV_CODEC_ID_NONE)
        codec = avcodec_find_encoder(pr->encode_id);
    if (!codec) return BL_ERR_ENCODE_FAILED;

    AVCodecContext* av = avcodec_alloc_context3(codec);
    if (!av) return BL_ERR_OUT_OF_MEMORY;
    c->encoder = av;

    if (c->is_video) {
        if (cfg->video.width > 0 && cfg->video.height > 0) {
            av->width = (int)cfg->video.width;
            av->height = (int)cfg->video.height;
        } else {
            av->width = 640;
            av->height = 360;
        }
        const void* cfg_list = NULL;
        int ncfgs = 0;
        if (avcodec_get_supported_config(av, codec, AV_CODEC_CONFIG_PIX_FORMAT,
                                         0, &cfg_list, &ncfgs) >= 0 &&
            ncfgs > 0) {
            const enum AVPixelFormat* fmts =
                (const enum AVPixelFormat*)cfg_list;
            av->pix_fmt =
                (enum AVPixelFormat)(pr->enc_pix_fmt >= 0
                                         ? pr->enc_pix_fmt
                                         : (fmts[0] != AV_PIX_FMT_NONE
                                                ? fmts[0]
                                                : AV_PIX_FMT_YUV420P));
        } else {
            av->pix_fmt = (enum AVPixelFormat)(pr->enc_pix_fmt >= 0
                                                   ? pr->enc_pix_fmt
                                                   : AV_PIX_FMT_YUV420P);
        }
        if (cfg->video.fps.num > 0 && cfg->video.fps.den > 0) {
            av->time_base = (AVRational){cfg->video.fps.den,
                                         cfg->video.fps.num};
            av->framerate = (AVRational){cfg->video.fps.num,
                                         cfg->video.fps.den};
        } else {
            av->time_base = (AVRational){1, 24};
            av->framerate = (AVRational){24, 1};
        }
        if (cfg->video.pixel_aspect.num > 0 && cfg->video.pixel_aspect.den > 0)
            av->sample_aspect_ratio = (AVRational){
                cfg->video.pixel_aspect.num, cfg->video.pixel_aspect.den};
        if (codec->id != AV_CODEC_ID_MPEG4) av->max_b_frames = 2;
        if (av->gop_size == 0) av->gop_size = 24;
    } else {
        av->sample_rate = cfg->audio.sample_rate > 0
                              ? (int)cfg->audio.sample_rate
                              : 48000;
        uint32_t chs = cfg->audio.channels > 0 ? cfg->audio.channels : 2;
        av_channel_layout_default(&av->ch_layout, (int)chs);
        const void* cfg_list = NULL;
        int ncfgs = 0;
        if (avcodec_get_supported_config(av, codec,
                                         AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                         &cfg_list, &ncfgs) >= 0 &&
            ncfgs > 0) {
            const enum AVSampleFormat* fmts =
                (const enum AVSampleFormat*)cfg_list;
            av->sample_fmt =
                (enum AVSampleFormat)(pr->enc_sample_fmt >= 0
                                          ? pr->enc_sample_fmt
                                          : (fmts[0] != AV_SAMPLE_FMT_NONE
                                                 ? fmts[0]
                                                 : AV_SAMPLE_FMT_FLTP));
        } else {
            av->sample_fmt =
                (enum AVSampleFormat)(pr->enc_sample_fmt >= 0
                                          ? pr->enc_sample_fmt
                                          : AV_SAMPLE_FMT_FLTP);
        }
        av->time_base = (AVRational){1, av->sample_rate};
    }

    apply_encode_options(c, av, cfg);

    /* Encoders that repeat their parameter sets in-band (libx264 does unless
     * told otherwise) never populate extradata, so a muxer that has to write a
     * complete track header before any packet arrives gets nothing. Opt into
     * the global-header form when the caller asks for it. */
    if (cfg->enc_flags & BL_ENCFLAG_GLOBAL_HEADER) {
        av->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(av, codec, NULL) < 0) {
        avcodec_free_context(&c->encoder);
        return BL_ERR_ENCODE_FAILED;
    }
    /* Most encoders publish their parameter sets here; the rest are picked up
     * by capture_encoder_extradata() after the first encoded packet. */
    capture_encoder_extradata(c);
    return BL_OK;
}

int ffmpeg_plugin_init(void** ctx_out, const BlCodecConfig* cfg,
                       const BlFfmpegProfile* profile) {
    if (!ctx_out || !cfg || !profile || !cfg->host || !cfg->host->alloc ||
        !cfg->host->free)
        return BL_ERR_INVALID_ARGUMENT;
    if (cfg->abi_version != BL_PLUGIN_ABI_VERSION)
        return BL_ERR_PLUGIN_ABI_MISMATCH;

    BlFfmpegCtx* c = (BlFfmpegCtx*)cfg->host->alloc(
        sizeof(BlFfmpegCtx), cfg->host->userdata);
    if (!c) return BL_ERR_OUT_OF_MEMORY;
    memset(c, 0, sizeof(*c));
    c->host = cfg->host;
    c->profile = profile;
    c->is_video = profile->is_video;

    c->dec_frame = av_frame_alloc();
    c->dec_pkt = av_packet_alloc();
    c->enc_in = av_frame_alloc();
    c->conv_frame = av_frame_alloc();
    if (!c->dec_frame || !c->dec_pkt || !c->enc_in || !c->conv_frame)
        goto fail;

    /* decoder (mandatory; every plugin ships a native decoder) */
    const AVCodec* dec_codec =
        profile->decode_name
            ? avcodec_find_decoder_by_name(profile->decode_name)
            : avcodec_find_decoder(profile->decode_id);
    if (!dec_codec) goto fail;
    c->decoder = avcodec_alloc_context3(dec_codec);
    if (!c->decoder) goto fail;
    /* Deterministic one-packet->one-frame delivery (no frame-thread delay). */
    c->decoder->thread_count = 1;
    if (!c->is_video) {
        if (cfg->audio.sample_rate > 0)
            c->decoder->sample_rate = (int)cfg->audio.sample_rate;
        if (cfg->audio.channels > 0)
            av_channel_layout_default(&c->decoder->ch_layout,
                                      (int)cfg->audio.channels);
    }
    configure_decoder_extradata(c, cfg);
    /* The decoder is best-effort: bitstreams like theora/vorbis/opus refuse
     * to open without container-negotiated extradata, so encode-only callers
     * must still be able to initialize. Decode attempts without an open
     * decoder fail cleanly. */
    if (avcodec_open2(c->decoder, dec_codec, NULL) < 0) {
        avcodec_free_context(&c->decoder);
        c->decoder = NULL;
    }

    /* encoder (best effort; absence only disables encode()) */
    c->encode_available = (open_encoder(c, cfg) == BL_OK);

    *ctx_out = c;
    return BL_OK;

fail:
    if (c->decoder) avcodec_free_context(&c->decoder);
    if (c->encoder) avcodec_free_context(&c->encoder);
    av_frame_free(&c->dec_frame);
    av_packet_free(&c->dec_pkt);
    av_frame_free(&c->enc_in);
    av_frame_free(&c->conv_frame);
    if (c->host) c->host->free(c, c->host->userdata);
    return BL_ERR_INTERNAL;
}

/* ------------------------------------------------------------------ */
/* decode                                                              */
/* ------------------------------------------------------------------ */

int ffmpeg_plugin_decode(void* ctx_in, const uint8_t* pkt, size_t pkt_size,
                         uint8_t** out, size_t* out_size, BlFrameMeta* meta) {
    BlFfmpegCtx* c = (BlFfmpegCtx*)ctx_in;
    if (!c || !out || !out_size || !meta)
        return BL_ERR_INVALID_ARGUMENT;
    if (!c->decoder) return BL_ERR_DECODE_FAILED;
    *out = NULL;
    *out_size = 0;

    /* The caller hands one packet at a time; the packet is always consumed.
     * A codec may buffer a packet before a matching frame is available
     * (reordering, parameter sets) so a return of BL_DECODE_NEED_MORE_INPUT
     * simply means "no output for this packet yet, keep feeding". */
    if (c->decoder_eof) return BL_DECODE_NEED_MORE_INPUT;

    if (pkt && pkt_size > 0) {
        AVPacket tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.data = (uint8_t*)pkt;
        tmp.size = (int)pkt_size;
        tmp.pts = AV_NOPTS_VALUE;
        tmp.dts = AV_NOPTS_VALUE;
        int ret = avcodec_send_packet(c->decoder, &tmp);
        if (ret < 0 && ret != AVERROR_EOF) return BL_ERR_DECODE_FAILED;
        c->decode_used = 1;
    }

    int r = receive_decoded(c, out, out_size, meta);
    if (r == BL_OK) return BL_OK;
    if (r < 0) return r;
    return BL_DECODE_NEED_MORE_INPUT;
}

/* ------------------------------------------------------------------ */
/* encode                                                              */
/* ------------------------------------------------------------------ */

/* Feed the pending planar audio batch into the encoder, honoring its
 * frame_size slicing requirement and stopping when the encoder's buffers are
 * full (EAGAIN keeps the remainder pending). */
/* Build, convert and send one audio slice of n samples drawn from the front
 * of the pending batch. n must be >= 1 and <= pending count. Full-frame
 * granules guarantee encoders like libopus (which reject mixed-size frames)
 * and libvorbis (which rejects frames larger than its frame_size) stay happy.
 * Returns BL_OK, BL_DECODE_NEED_MORE_INPUT (EAGAIN: encoder buffers full),
 * or a BL error. */
static int feed_one_audio_slice(BlFfmpegCtx* c,
                                const AVChannelLayout* in_layout,
                                uint32_t n) {
    AVFrame* src = c->enc_in;
    av_frame_unref(src);
    src->format = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_copy(&src->ch_layout, in_layout);
    src->sample_rate = c->encoder->sample_rate;
    src->nb_samples = (int)n;
    src->linesize[0] = (int)c->pend_samples * 4;
    for (uint32_t k = 0; k < c->enc_channels; ++k)
        src->data[k] = c->pend_buf + (size_t)k * (size_t)c->pend_samples * 4u;

    int max_out = (int)swr_get_out_samples(c->swr_enc, (int)n);
    if (max_out <= 0) max_out = (int)n;

    AVFrame* nat = c->conv_frame;
    av_frame_unref(nat);
    nat->format = c->encoder->sample_fmt;
    av_channel_layout_copy(&nat->ch_layout, &c->encoder->ch_layout);
    nat->sample_rate = c->encoder->sample_rate;
    nat->nb_samples = max_out;
    if (av_frame_get_buffer(nat, 32) < 0) return BL_ERR_INTERNAL;

    int conv = swr_convert(c->swr_enc, nat->data, nat->nb_samples,
                           (const uint8_t**)src->data, (int)n);
    if (conv < 0) {
        av_frame_unref(nat);
        return BL_ERR_INTERNAL;
    }
    nat->nb_samples = conv;
    nat->pts = c->enc_pts;
    c->enc_pts += n;
    av_frame_unref(src);

    int ret = avcodec_send_frame(c->encoder, nat);
    av_frame_unref(nat);
    if (ret == AVERROR(EAGAIN)) return BL_DECODE_NEED_MORE_INPUT;
    if (ret < 0 && ret != AVERROR_EOF) return BL_ERR_ENCODE_FAILED;
    c->encode_used = 1;

    /* consume n samples from every plane, recompacting the batch */
    for (uint32_t k = 0; k < c->enc_channels; ++k)
        memmove(c->pend_buf + (size_t)k * (size_t)(c->pend_samples - n) * 4u,
                c->pend_buf + (size_t)k * (size_t)c->pend_samples * 4u,
                (size_t)(c->pend_samples - n) * 4u);
    c->pend_samples -= n;
    return BL_OK;
}

/* Push pending audio into the encoder in full frame_size granules only. A
 * trailing sub-frame remainder stays pending until flush() pushes it as the
 * final frame. Stops (keeping the rest) when the encoder is full. */
static int push_pending_samples(BlFfmpegCtx* c,
                                const AVChannelLayout* in_layout) {
    if (!c->swr_enc) {
        if (swr_alloc_set_opts2(&c->swr_enc, &c->encoder->ch_layout,
                                c->encoder->sample_fmt,
                                c->encoder->sample_rate, in_layout,
                                AV_SAMPLE_FMT_FLTP, c->encoder->sample_rate, 0,
                                NULL) < 0)
            return BL_ERR_INTERNAL;
        if (!c->swr_enc) return BL_ERR_INTERNAL;
        if (swr_init(c->swr_enc) < 0) return BL_ERR_INTERNAL;
    }

    uint32_t granule =
        c->encoder->frame_size > 0 ? (uint32_t)c->encoder->frame_size : 1;

    while (c->pend_samples >= granule) {
        int r = feed_one_audio_slice(c, in_layout, granule);
        if (r == BL_DECODE_NEED_MORE_INPUT) return BL_OK;
        if (r != BL_OK) return r;
    }
    return BL_OK;
}

static int encode_feed(BlFfmpegCtx* c, const uint8_t* in,
                       const BlFrameMeta* meta) {
    if (c->is_video) {
        if (!c->enc_geometry_set) {
            c->enc_width = meta && meta->width ? meta->width : 640;
            c->enc_height = meta && meta->height ? meta->height : 360;
            c->enc_linesize = meta && meta->linesize ? meta->linesize
                                                     : c->enc_width * 4u;
            c->enc_geometry_set = 1;
        }
        if (!c->encoder->width) c->encoder->width = (int)c->enc_width;
        if (!c->encoder->height) c->encoder->height = (int)c->enc_height;

        AVFrame* src = c->enc_in;
        av_frame_unref(src);
        src->format = AV_PIX_FMT_BGRA;
        src->width = (int)c->enc_width;
        src->height = (int)c->enc_height;
        src->linesize[0] = (int)c->enc_linesize;
        src->data[0] = (uint8_t*)in;

        AVFrame* nat = c->conv_frame;
        av_frame_unref(nat);
        nat->format = c->encoder->pix_fmt;
        nat->width = (int)c->enc_width;
        nat->height = (int)c->enc_height;
        nat->pts = c->enc_pts++;
        if (av_frame_get_buffer(nat, 32) < 0) return BL_ERR_INTERNAL;

        c->sws_enc = sws_getCachedContext(
            c->sws_enc, (int)c->enc_width, (int)c->enc_height,
            AV_PIX_FMT_BGRA, (int)c->enc_width, (int)c->enc_height,
            c->encoder->pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
        if (!c->sws_enc) {
            av_frame_unref(nat);
            return BL_ERR_INTERNAL;
        }
        sws_scale(c->sws_enc, (const uint8_t* const*)src->data, src->linesize,
                  0, (int)c->enc_height, nat->data, nat->linesize);
        av_frame_unref(src);

        int ret = avcodec_send_frame(c->encoder, nat);
        av_frame_unref(nat);
        if (ret < 0 && ret != AVERROR_EOF) return BL_ERR_ENCODE_FAILED;
        c->encode_used = 1;
        return BL_OK;
    }

    /* audio */
    if (!meta || meta->sample_count == 0) return BL_ERR_INVALID_ARGUMENT;
    if (!c->enc_geometry_set) {
        c->enc_channels = meta->channels ? meta->channels : 2;
        c->enc_geometry_set = 1;
    }
    uint32_t frames = meta->sample_count;

    /* Append the incoming planar frames to the pending batch. */
    size_t need =
        (c->pend_samples + frames) * (size_t)c->enc_channels * 4u;
    if (need > c->pend_cap) {
        uint8_t* nb = (uint8_t*)av_realloc(c->pend_buf, need);
        if (!nb) return BL_ERR_OUT_OF_MEMORY;
        c->pend_buf = nb;
        c->pend_cap = need;
    }
    for (uint32_t k = 0; k < c->enc_channels; ++k)
        memcpy(c->pend_buf + (size_t)k * (c->pend_samples + frames) * 4u,
               in + (size_t)k * (size_t)frames * 4u, (size_t)frames * 4u);
    c->pend_samples += frames;

    AVChannelLayout in_layout;
    av_channel_layout_default(&in_layout, (int)c->enc_channels);

    return push_pending_samples(c, &in_layout);
}

int ffmpeg_plugin_encode(void* ctx_in, const uint8_t* in, size_t in_size,
                         uint8_t** out, size_t* out_size,
                         const BlFrameMeta* meta) {
    BlFfmpegCtx* c = (BlFfmpegCtx*)ctx_in;
    if (!c || !out || !out_size) return BL_ERR_INVALID_ARGUMENT;
    if (!c->encoder || !c->encode_available) return BL_ERR_ENCODE_FAILED;
    if (!meta) return BL_ERR_INVALID_ARGUMENT;
    if (!in && in_size != 0) return BL_ERR_INVALID_ARGUMENT;
    (void)in_size;
    *out = NULL;
    *out_size = 0;

    /* drain delayed encoded packets first */
    int r = receive_encoded(c, out, out_size);
    if (r == BL_OK) return BL_OK;
    if (r < 0) return r;

    r = encode_feed(c, in, meta);
    if (r != BL_OK) return r;

    r = receive_encoded(c, out, out_size);
    if (r == BL_OK) return BL_OK;
    if (r < 0) return r;
    return BL_OK; /* nothing produced yet (codec buffering) */
}

/* ------------------------------------------------------------------ */
/* flush                                                               */
/* ------------------------------------------------------------------ */

static int flush_decoder(BlFfmpegCtx* c, uint8_t** out, size_t* out_size) {
    if (!c->decoder) return BL_ERR_INVALID_ARGUMENT;
    if (!c->decoder_sent_eof) {
        avcodec_send_packet(c->decoder, NULL);
        c->decoder_sent_eof = 1;
    }
    BlFrameMeta meta = {0};
    int r = receive_decoded(c, out, out_size, &meta);
    if (r == BL_OK || r < 0) return r;
    return BL_OK; /* nothing left */
}

static int flush_encoder(BlFfmpegCtx* c, uint8_t** out, size_t* out_size) {
    if (!c->encoder) return BL_ERR_INVALID_ARGUMENT;

    /* Any audio input the encoder could not accept yet must go in before the
     * EOF marker, or those samples would be silently dropped. Full granules
     * first; the sub-frame remainder becomes the final (small) frame. */
    if (c->pend_samples > 0) {
        AVChannelLayout il;
        av_channel_layout_default(&il, (int)c->enc_channels);
        int pr = push_pending_samples(c, &il);
        if (pr != BL_OK) return pr;

        int guard = 0;
        while (c->pend_samples > 0) {
            int r = feed_one_audio_slice(c, &il,
                                         (uint32_t)c->pend_samples);
            if (r == BL_DECODE_NEED_MORE_INPUT) {
                int rr = receive_encoded(c, out, out_size);
                if (rr == BL_OK) return BL_OK; /* packet out; retry later */
                if (rr < 0) return rr;
                if (++guard > 10000) return BL_ERR_ENCODE_FAILED;
                continue;
            }
            if (r != BL_OK) return r;
        }
    }

    if (!c->encoder_sent_eof) {
        avcodec_send_frame(c->encoder, NULL);
        c->encoder_sent_eof = 1;
    }
    int r = receive_encoded(c, out, out_size);
    if (r == BL_OK || r < 0) return r;
    return BL_OK; /* nothing left */
}

int ffmpeg_plugin_flush(void* ctx_in, uint8_t** out, size_t* out_size) {
    BlFfmpegCtx* c = (BlFfmpegCtx*)ctx_in;
    if (!c || !out || !out_size) return BL_ERR_INVALID_ARGUMENT;
    *out = NULL;
    *out_size = 0;

    /* A context is used either as a decoder or as an encoder in practice;
     * prefer the side that has actually been fed. */
    if (c->encode_used && c->encoder && !c->decode_used)
        return flush_encoder(c, out, out_size);
    return flush_decoder(c, out, out_size);
}

/* ------------------------------------------------------------------ */
/* extradata                                                           */
/* ------------------------------------------------------------------ */

const uint8_t* ffmpeg_plugin_get_extradata(void* ctx_in, size_t* out_size) {
    BlFfmpegCtx* c = (BlFfmpegCtx*)ctx_in;
    if (out_size) *out_size = 0;
    if (!c) return NULL;
    /* Retry the capture: the host may ask between open and the first packet,
     * or on a context that was never fed. */
    capture_encoder_extradata(c);
    if (!c->enc_extradata) return NULL;
    if (out_size) *out_size = c->enc_extradata_size;
    return c->enc_extradata;
}

/* ------------------------------------------------------------------ */
/* cleanup                                                             */
/* ------------------------------------------------------------------ */

void ffmpeg_plugin_cleanup(void* ctx_in) {
    BlFfmpegCtx* c = (BlFfmpegCtx*)ctx_in;
    if (!c) return;

    if (c->decoder) avcodec_free_context(&c->decoder);
    if (c->encoder) avcodec_free_context(&c->encoder);
    av_frame_free(&c->dec_frame);
    av_packet_free(&c->dec_pkt);
    av_frame_free(&c->enc_in);
    av_frame_free(&c->conv_frame);
    sws_freeContext(c->sws_dec);
    sws_freeContext(c->sws_enc);
    if (c->swr_dec) swr_free(&c->swr_dec);
    if (c->swr_enc) swr_free(&c->swr_enc);
    av_free(c->pend_buf);
    av_free(c->enc_extradata);

    if (c->host && c->host->free) c->host->free(c, c->host->userdata);
}