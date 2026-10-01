#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>

static const BlParamDesc kParams[] = {
    {"quality", BL_PARAM_INT, 0.0, 10.0, 5.0, NULL, "target quality 0..10"},
    {NULL, 0, 0.0, 0.0, 0.0, NULL, NULL},
};

static const BlOptionMap kOptions[] = {
    {"quality", BL_OPT_INT, "quality", 5.0, NULL, 0.0, 10.0},
    {NULL, BL_OPT_INT, NULL, 0.0, NULL, 0.0, 0.0},
};

static const char* kExtensions[] = {"ogv", "ogg", NULL};

static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_THEORA,
    /*decode_name*/ "theora",
    /*encode_id*/ AV_CODEC_ID_THEORA,
    /*encode_name*/ "libtheora",
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

static const uint8_t* get_extradata(void* ctx, size_t* out_size) {
    return ffmpeg_plugin_get_extradata(ctx, out_size);
}

static BlCodecPlugin g_plugin = {
    BL_PLUGIN_ABI_VERSION,
    "ffmpeg.theora",
    "Theora encode and decode via libtheora and the native FFmpeg decoder (PLG-4)",
    BL_CODEC_VIDEO,
    {BL_ROLE_ENCODE | BL_ROLE_DECODE,
     0u,
     kExtensions,
     "libtheora",
     "theora",
     kParams},
    init,
    decode,
    encode,
    flush,
    cleanup,
    get_extradata,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }