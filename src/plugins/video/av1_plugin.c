#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>

static const BlParamDesc kParams[] = {
    {"crf", BL_PARAM_INT, 0.0, 63.0, 35.0, NULL, "constant quality factor"},
    {"preset", BL_PARAM_INT, 0.0, 13.0, 10.0, NULL, "SVT-AV1 preset (0 slowest..13 fastest)"},
    {NULL, 0, 0.0, 0.0, 0.0, NULL, NULL},
};

static const BlOptionMap kOptions[] = {
    {"crf", BL_OPT_INT, "crf", 35.0, NULL, 0.0, 63.0},
    {"preset", BL_OPT_INT, "preset", 10.0, NULL, 0.0, 13.0},
    {NULL, BL_OPT_INT, NULL, 0.0, NULL, 0.0, 0.0},
};

static const char* kExtensions[] = {"mp4", "mkv", "av1", "obu", NULL};

static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_AV1,
    /*decode_name*/ "libdav1d",
    /*encode_id*/ AV_CODEC_ID_AV1,
    /*encode_name*/ "libsvtav1",
    /*encode_fallback_name*/ "libaom-av1",
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

static const uint8_t* get_extradata(void* ctx, size_t* out_size) {
    return ffmpeg_plugin_get_extradata(ctx, out_size);
}

static BlCodecPlugin g_plugin = {
    BL_PLUGIN_ABI_VERSION,
    "ffmpeg.av1",
    "AV1 encode and decode via libsvtav1 (libaom-av1 fallback) and dav1d (PLG-3)",
    BL_CODEC_VIDEO,
    {BL_ROLE_ENCODE | BL_ROLE_DECODE,
     0u,
     kExtensions,
     "libsvtav1",
     "libdav1d",
     kParams},
    init,
    decode,
    encode,
    flush,
    cleanup,
    get_extradata,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }