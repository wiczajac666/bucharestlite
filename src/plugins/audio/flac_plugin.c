#include "ffmpeg_plugin_common.h"

#include <libavcodec/avcodec.h>

static const BlParamDesc kParams[] = {
    {"compression_level", BL_PARAM_INT, 0.0, 12.0, 5.0, NULL, "FLAC compression level 0..12"},
    {"frame_size", BL_PARAM_INT, 16.0, 65535.0, 1024.0, NULL, "encoder frame/block size"},
    {NULL, 0, 0.0, 0.0, 0.0, NULL, NULL},
};

static const BlOptionMap kOptions[] = {
    {"compression_level", BL_FIELD_COMPRESSION, NULL, 5.0, NULL, 0.0, 12.0},
    {"frame_size", BL_OPT_INT, "frame_size", 1024.0, NULL, 16.0, 65535.0},
    {NULL, BL_OPT_INT, NULL, 0.0, NULL, 0.0, 0.0},
};

static const char* kExtensions[] = {"flac", NULL};

static const BlFfmpegProfile kProfile = {
    /*decode_id*/ AV_CODEC_ID_FLAC,
    /*decode_name*/ "flac",
    /*encode_id*/ AV_CODEC_ID_FLAC,
    /*encode_name*/ "flac",
    /*encode_fallback_name*/ NULL,
    /*enc_pix_fmt*/ -1,
    /*enc_sample_fmt*/ AV_SAMPLE_FMT_S16,
    /*encode_options*/ kOptions,
    /*caps_flags*/ BL_FLAG_LOSSLESS,
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

static const uint8_t* get_extradata(void* ctx, size_t* out_size) {
    return ffmpeg_plugin_get_extradata(ctx, out_size);
}

static BlCodecPlugin g_plugin = {
    BL_PLUGIN_ABI_VERSION,
    "ffmpeg.flac",
    "FLAC lossless encode and decode via the native FFmpeg codec (PLG-7)",
    BL_CODEC_AUDIO,
    {BL_ROLE_ENCODE | BL_ROLE_DECODE,
     BL_FLAG_LOSSLESS,
     kExtensions,
     "flac",
     "flac",
     kParams},
    init,
    decode,
    encode,
    flush,
    cleanup,
    get_extradata,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }