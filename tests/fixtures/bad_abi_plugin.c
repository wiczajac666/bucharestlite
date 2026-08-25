#include <bl_plugins/codec_plugin.h>

#include <stdlib.h>
#include <string.h>

typedef struct BadAbiCtx {
    const BlHostApi* host;
} BadAbiCtx;

static int bad_abi_init(void** ctx_out, const BlCodecConfig* cfg) {
    if (!ctx_out || !cfg || !cfg->host || !cfg->host->alloc) {
        return BL_ERR_INVALID_ARGUMENT;
    }
    BadAbiCtx* ctx = (BadAbiCtx*)cfg->host->alloc(sizeof(BadAbiCtx),
                                                  cfg->host->userdata);
    if (!ctx) return BL_ERR_OUT_OF_MEMORY;
    ctx->host = cfg->host;
    *ctx_out = ctx;
    return BL_OK;
}

static void bad_abi_cleanup(void* ctx_in) {
    BadAbiCtx* ctx = (BadAbiCtx*)ctx_in;
    if (ctx && ctx->host && ctx->host->free) {
        ctx->host->free(ctx, ctx->host->userdata);
    }
}

static const char* kExtensions[] = {"bad", NULL};

static BlCodecPlugin g_plugin = {
    999u,
    "fixturebadabi",
    "fixture plugin declaring a wrong ABI version",
    BL_CODEC_VIDEO,
    {BL_ROLE_DECODE | BL_ROLE_ENCODE, 0u, kExtensions, NULL, NULL, NULL},
    bad_abi_init,
    NULL,
    NULL,
    NULL,
    bad_abi_cleanup,
};

BlCodecPlugin* bl_get_codec_plugin(void) { return &g_plugin; }
