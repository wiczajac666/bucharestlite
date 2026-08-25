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

static BlCodecPlugin g_plugin;

BlCodecPlugin* bl_get_codec_plugin(void) {
    g_plugin.abi_version = 999u;
    g_plugin.name = "fixturebadabi";
    g_plugin.description = "fixture plugin declaring a wrong ABI version";
    g_plugin.type = BL_CODEC_VIDEO;
    g_plugin.caps.roles = BL_ROLE_DECODE | BL_ROLE_ENCODE;
    g_plugin.caps.flags = 0;
    g_plugin.caps.file_extensions = kExtensions;
    g_plugin.init = bad_abi_init;
    g_plugin.decode = NULL;
    g_plugin.encode = NULL;
    g_plugin.flush = NULL;
    g_plugin.cleanup = bad_abi_cleanup;
    return &g_plugin;
}
