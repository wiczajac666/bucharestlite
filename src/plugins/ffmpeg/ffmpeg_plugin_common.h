#ifndef BL_FFMPEG_PLUGIN_COMMON_H
#define BL_FFMPEG_PLUGIN_COMMON_H

/*
 * Shared implementation core for the FFmpeg-backed codec plugins.
 *
 * Each plugin (h264, vp9, av1, theora, mpeg4, aac, flac, vorbis, opus)
 * is a thin descriptor over this library:
 *   - maps BlCodecConfig -> avcodec_open2 options
 *   - converts the universal interchange formats (video BGRA32 packed,
 *     audio f32 planar) to/from codec-native buffers via libswscale /
 *     libswresample
 *   - owns packet/frame send-receive buffering and drain-flush semantics
 *   - allocates every returned output buffer through the BlHostApi
 *     allocator so the host can free it (ABI rule, avoids CRT mismatch)
 */

#include <bl_plugins/codec_plugin.h>

#include <libavcodec/codec_id.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How a single encoder parameter is applied to the AVCodecContext. */
typedef enum BlOptKind {
    BL_OPT_INT,           /* av_opt_set_int on the codec context */
    BL_OPT_DOUBLE,        /* av_opt_set_double */
    BL_OPT_STRING,        /* av_opt_set (string) */
    BL_FIELD_BITRATE,     /* avctx->bit_rate = clamp(value) in bits/s */
    BL_FIELD_QSCALE,      /* avctx->global_quality = value; AV_CODEC_FLAG_QSCALE */
    BL_FIELD_GOP,         /* avctx->gop_size */
    BL_FIELD_THREADS,     /* avctx->thread_count */
    BL_FIELD_COMPRESSION, /* avctx->compression_level */
    BL_FIELD_STRICT,      /* avctx->strict_std_compliance */
} BlOptKind;

typedef struct BlOptionMap {
    const char* param;   /* BlConfigEntry name; NULL terminates the table */
    BlOptKind kind;
    const char* av_name; /* av_opt name for BL_OPT_* kinds, may be NULL */
    double def_num;      /* numeric default when the entry is absent */
    const char* def_str; /* string default (BL_OPT_STRING / BL_OPT_STRING defs) */
    double min;
    double max;
} BlOptionMap;

typedef struct BlFfmpegProfile {
    /* Decoder */
    enum AVCodecID decode_id;
    const char* decode_name; /* prefer by-name decoder (e.g. "h264"), may be NULL */

    /* Encoder; encode is unsupported when encode_id is AV_CODEC_ID_NONE */
    enum AVCodecID encode_id;
    const char* encode_name;          /* preferred encoder by name, may be NULL */
    const char* encode_fallback_name; /* secondary encoder by name, may be NULL */

    int enc_pix_fmt;    /* AV_PIX_FMT_* for video encode, -1 => codec default */
    int enc_sample_fmt; /* AV_SAMPLE_FMT_* for audio encode, -1 => codec default */

    const BlOptionMap* encode_options; /* NULL-terminated option table, may be NULL */

    uint32_t caps_flags;                /* BlCaps flags */
    const char* const* file_extensions; /* advisory extensions for the UI */

    int is_video; /* video (BGRA32) vs audio (f32 planar) interchange */
} BlFfmpegProfile;

/* Wire these into a BlCodecPlugin. cfg->params is a BlConfigEntry array
 * terminated by an entry whose name is NULL (addresses omitted size). */
int ffmpeg_plugin_init(void** ctx_out, const BlCodecConfig* cfg,
                       const BlFfmpegProfile* profile);
int ffmpeg_plugin_decode(void* ctx, const uint8_t* pkt, size_t pkt_size,
                         uint8_t** out, size_t* out_size, BlFrameMeta* meta);
int ffmpeg_plugin_encode(void* ctx, const uint8_t* in, size_t in_size,
                         uint8_t** out, size_t* out_size,
                         const BlFrameMeta* meta);
int ffmpeg_plugin_flush(void* ctx, uint8_t** out, size_t* out_size);
void ffmpeg_plugin_cleanup(void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* BL_FFMPEG_PLUGIN_COMMON_H */