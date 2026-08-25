#include "passthrough_plugin.h"

#include <stdlib.h>
#include <string.h>

typedef struct PassthroughCtx {
    const BlHostApi* host;
    uint64_t packets_copied;
} PassthroughCtx;

static int passthrough_init(void** ctx_out, const BlCodecConfig* cfg) {
    if (!ctx_out || !cfg || !cfg->host || !cfg->host->alloc) {
        return BL_ERR_INVALID_ARGUMENT;
    }
    if (cfg->abi_version != BL_PLUGIN_ABI_VERSION) {
        return BL_ERR_PLUGIN_ABI_MISMATCH;
    }
    PassthroughCtx* ctx = (PassthroughCtx*)cfg->host->alloc(
        sizeof(PassthroughCtx), cfg->host->userdata);
    if (!ctx) return BL_ERR_OUT_OF_MEMORY;
    ctx->host = cfg->host;
    ctx->packets_copied = 0;
    *ctx_out = ctx;
    return BL_OK;
}

static int passthrough_encode(void* ctx_in, const uint8_t* in, size_t in_size,
                              uint8_t** out, size_t* out_size,
                              const BlFrameMeta* meta) {
    PassthroughCtx* ctx = (PassthroughCtx*)ctx_in;
    if (!ctx || !out || !out_size) return BL_ERR_INVALID_ARGUMENT;
    if (!in && in_size != 0) return BL_ERR_INVALID_ARGUMENT;
    uint8_t* buf =
        in_size ? (uint8_t*)ctx->host->alloc(in_size, ctx->host->userdata)
                : NULL;
    if (in_size && !buf) return BL_ERR_OUT_OF_MEMORY;
    if (in_size) memcpy(buf, in, in_size);
    *out = buf;
    *out_size = in_size;
    if (meta) ctx->packets_copied++;
    return BL_OK;
}

static int passthrough_flush(void* ctx_in, uint8_t** out, size_t* out_size) {
    PassthroughCtx* ctx = (PassthroughCtx*)ctx_in;
    if (!ctx || !out || !out_size) return BL_ERR_INVALID_ARGUMENT;
    *out = NULL;
    *out_size = 0;
    return BL_OK;
}

static void passthrough_cleanup(void* ctx_in) {
    PassthroughCtx* ctx = (PassthroughCtx*)ctx_in;
    if (ctx && ctx->host && ctx->host->free) {
        ctx->host->free(ctx, ctx->host->userdata);
    }
}

static const char* kVideoExtensions[] = {"mkv", "mp4", NULL};
static const char* kAudioExtensions[] = {"mkv", "m4a", NULL};

static BlParamDesc kNoParams[] = {{NULL, BL_PARAM_INT, 0.0, 0.0, 0.0, NULL, NULL}};

static BlCodecPlugin g_video_plugin = {
    BL_PLUGIN_ABI_VERSION,
    "passthrough.video",
    "built-in stream-copy pseudo-plugin for video tracks",
    BL_CODEC_VIDEO,
    {BL_ROLE_ENCODE,
     BL_FLAG_PASSTHROUGH,
     kVideoExtensions,
     NULL,
     NULL,
     kNoParams},
    passthrough_init,
    NULL,
    passthrough_encode,
    passthrough_flush,
    passthrough_cleanup,
};

static BlCodecPlugin g_audio_plugin = {
    BL_PLUGIN_ABI_VERSION,
    "passthrough.audio",
    "built-in stream-copy pseudo-plugin for audio tracks",
    BL_CODEC_AUDIO,
    {BL_ROLE_ENCODE,
     BL_FLAG_PASSTHROUGH,
     kAudioExtensions,
     NULL,
     NULL,
     kNoParams},
    passthrough_init,
    NULL,
    passthrough_encode,
    passthrough_flush,
    passthrough_cleanup,
};

BlCodecPlugin* bl_passthrough_video_plugin(void) { return &g_video_plugin; }

BlCodecPlugin* bl_passthrough_audio_plugin(void) { return &g_audio_plugin; }
