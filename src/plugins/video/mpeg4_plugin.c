#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>

static const BlParamDesc kParams[] = {
    {"qscale", BL_PARAM_INT, 1.0, 31.0, 3.0, NULL, "constant/quantizer scale (lower = better)"},
    {"gop", BL_PARAM_INT, 1.0, 1000.0, 24.0, NULL, "keyframe interval in frames"},
    {NULL, 0, 0.0, 0.0, 0.0, NULL, NULL},
};

static const BlOptionMap kOptions[] = {
    {"qscale", BL_FIELD_QSCALE, NULL, 3.0, NULL, 1.0, 31.0},
    {"gop", BL_FIELD_GOP, NULL, 24.0, NULL, 1.0, 1000.0},
    {NULL, BL_OPT_INT, NULL, 0.0, NULL, 0.0, 0.0},
};

static const char* kExtensions[] = {"mp4", "mkv", "avi", NULL};

static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_MPEG4,
    /*decode_name*/ "mpeg4",
    /*encode_id*/ AV_CODEC_ID_MPEG4,
    /*encode_name*/ "mpeg4",
    /*encode_fallback_name*/ NULL,
    /*enc_pix_fmt*/ AV_PIX_FMT_YUV420P,
    /*enc_sample_fmt*/ -1,
    /*encode_options*/ kOptions,
    /*caps_flags*/ 0u,
    /*file_extensions*/ kExtensions,
    /*is_video*/ 1,
};

static int init(void** ctx, const BlCodecConfig* cfg) {
    return ffmpeg_plugin_init(ctx, cfg, &kProfile);
}

static int decode(void* ctx, const uint8_t* pkt, size_t pkt_size,
                  uint8_t** out, size_t* out_size, BlFrameMeta* meta) {
    return ffmpeg_plugin_decode(ctx, pkt, pkt_size, out, out_size, meta);
}

static int encode(void* ctx, const uint8_t* in, size_t in_size,
                  uint8_t** out, size_t* out_size, const BlFrameMeta* meta) {
    return ffmpeg_plugin_encode(ctx, in, in_size, out, out_size, meta);
}

static int flush(void* ctx, uint8_t** out, size_t* out_size) {
    return ffmpeg_plugin_flush(ctx, out, out_size);
}

static void cleanup(void* ctx) { ffmpeg_plugin_cleanup(ctx); }

static BlCodecPlugin g_plugin = {
    BL_PLUGIN_ABI_VERSION,
    "ffmpeg.mpeg4",
    "MPEG-4 Part 2 (SP/ASP) encode and decode via the native FFmpeg codec (PLG-5)",
    BL_CODEC_VIDEO,
    {BL_ROLE_ENCODE | BL_ROLE_DECODE,
     0u,
     kExtensions,
     "mpeg4",
     "mpeg4",
     kParams},
    init,
    decode,
    encode,
    flush,
    cleanup,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }