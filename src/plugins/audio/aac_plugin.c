#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>

static const BlParamDesc kParams[] = {
    {"bitrate", BL_PARAM_INT, 16000.0, 512000.0, 128000.0, NULL,
     "target bitrate in bits per second"},
    {NULL, 0, 0.0, 0.0, 0.0, NULL, NULL},
};

static const BlOptionMap kOptions[] = {
    {"bitrate", BL_FIELD_BITRATE, NULL, 128000.0, NULL, 16000.0, 512000.0},
    {NULL, BL_OPT_INT, NULL, 0.0, NULL, 0.0, 0.0},
};

static const char* kExtensions[] = {"m4a", "aac", "mp4", "mkv", NULL};

static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_AAC,
    /*decode_name*/ "aac",
    /*encode_id*/ AV_CODEC_ID_AAC,
    /*encode_name*/ "aac",
    /*encode_fallback_name*/ NULL,
    /*enc_pix_fmt*/ -1,
    /*enc_sample_fmt*/ AV_SAMPLE_FMT_FLTP,
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
    "ffmpeg.aac",
    "AAC audio encode and decode via native FFmpeg codecs (PLG-6)",
    BL_CODEC_AUDIO,
    {BL_ROLE_ENCODE | BL_ROLE_DECODE,
     0u,
     kExtensions,
     "aac",
     "aac",
     kParams},
    init,
    decode,
    encode,
    flush,
    cleanup,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }