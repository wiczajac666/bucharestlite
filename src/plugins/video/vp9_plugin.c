#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>

static const BlParamDesc kParams[] = {
    {"crf", BL_PARAM_FLOAT, 0.0, 63.0, 32.0, NULL, "constant quality factor"},
    {"cpu-used", BL_PARAM_INT, 0.0, 9.0, 4.0, NULL, "speed/efficiency trade-off"},
    {"deadline", BL_PARAM_STRING, 0.0, 0.0, 0.0, NULL, "good|best|realtime"},
    {NULL, 0, 0.0, 0.0, 0.0, NULL, NULL},
};

static const BlOptionMap kOptions[] = {
    {"crf", BL_OPT_DOUBLE, "crf", 32.0, NULL, 0.0, 63.0},
    {"cpu-used", BL_OPT_INT, "cpu-used", 4.0, NULL, 0.0, 9.0},
    {"deadline", BL_OPT_STRING, "deadline", 0.0, "good", 0.0, 0.0},
    {NULL, BL_OPT_INT, NULL, 0.0, NULL, 0.0, 0.0},
};

static const char* kExtensions[] = {"webm", "mkv", NULL};

static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_VP9,
    /*decode_name*/ "vp9",
    /*encode_id*/ AV_CODEC_ID_VP9,
    /*encode_name*/ "libvpx-vp9",
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
    "ffmpeg.vp9",
    "VP9 encode and decode via libvpx-vp9 and the native FFmpeg decoder (PLG-2)",
    BL_CODEC_VIDEO,
    {BL_ROLE_ENCODE | BL_ROLE_DECODE,
     0u,
     kExtensions,
     "libvpx-vp9",
     "vp9",
     kParams},
    init,
    decode,
    encode,
    flush,
    cleanup,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }