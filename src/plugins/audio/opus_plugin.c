#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>

static const BlParamDesc kParams[] = {
    {"bitrate", BL_PARAM_INT, 500.0, 512000.0, 96000.0, NULL, "average bitrate (bps)"},
    {"application", BL_PARAM_STRING, 0.0, 0.0, 0.0, NULL, "voip|audio|lowdelay"},
    {"frame_size", BL_PARAM_INT, 40.0, 2880.0, 960.0, NULL, "encoder frame size (samples)"},
    {NULL, 0, 0.0, 0.0, 0.0, NULL, NULL},
};

static const BlOptionMap kOptions[] = {
    {"bitrate", BL_OPT_INT, "b", 96000.0, NULL, 500.0, 512000.0},
    {"application", BL_OPT_STRING, "application", 0.0, "audio", 0.0, 0.0},
    {"frame_size", BL_OPT_INT, "frame_size", 960.0, NULL, 40.0, 2880.0},
    {NULL, BL_OPT_INT, NULL, 0.0, NULL, 0.0, 0.0},
};

static const char* kExtensions[] = {"opus", "ogg", "mka", NULL};

static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_OPUS,
    /*decode_name*/ "opus",
    /*encode_id*/ AV_CODEC_ID_OPUS,
    /*encode_name*/ "libopus",
    /*encode_fallback_name*/ NULL,
    /*enc_pix_fmt*/ -1,
    /*enc_sample_fmt*/ AV_SAMPLE_FMT_S16,
    /*encode_options*/ kOptions,
    /*caps_flags*/ 0u,
    /*file_extensions*/ kExtensions,
    /*is_video*/ 0,
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
    "ffmpeg.opus",
    "Opus encode and decode via libopus (encode) and the native FFmpeg decoder (PLG-9)",
    BL_CODEC_AUDIO,
    {BL_ROLE_ENCODE | BL_ROLE_DECODE,
     0u,
     kExtensions,
     "libopus",
     "opus",
     kParams},
    init,
    decode,
    encode,
    flush,
    cleanup,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }