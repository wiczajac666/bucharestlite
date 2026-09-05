#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>

static const BlParamDesc kParams[] = {
    {"preset", BL_PARAM_STRING, 0.0, 0.0, 0.0, NULL, "libx264 preset"},
    {"crf", BL_PARAM_FLOAT, 0.0, 51.0, 23.0, NULL, "constant rate factor"},
    {NULL, 0, 0.0, 0.0, 0.0, NULL, NULL},
};

static const BlOptionMap kOptions[] = {
    {"preset", BL_OPT_STRING, "preset", 0.0, "medium", 0.0, 0.0},
    {"crf", BL_OPT_DOUBLE, "crf", 23.0, NULL, 0.0, 51.0},
    {NULL, BL_OPT_INT, NULL, 0.0, NULL, 0.0, 0.0},
};

static const char* kExtensions[] = {"mp4", "mkv", "h264", "264", NULL};

static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_H264,
    /*decode_name*/ "h264",
    /*encode_id*/ AV_CODEC_ID_H264,
    /*encode_name*/ "libx264",
    /*encode_fallback_name*/ "h264",
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
    "ffmpeg.h264",
    "H.264/AVC encode and decode via libx264 and the native FFmpeg decoder (PLG-1)",
    BL_CODEC_VIDEO,
    {BL_ROLE_ENCODE | BL_ROLE_DECODE,
     0u,
     kExtensions,
     "libx264",
     "h264",
     kParams},
    init,
    decode,
    encode,
    flush,
    cleanup,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }