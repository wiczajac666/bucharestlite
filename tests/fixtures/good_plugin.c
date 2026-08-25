#include <bl_plugins/codec_plugin.h>

#include <stdlib.h>
#include <string.h>

typedef struct FixtureCtx {
    const BlHostApi* host;
    uint64_t decode_count;
    uint64_t encode_count;
} FixtureCtx;

static int fixture_init(void** ctx_out, const BlCodecConfig* cfg) {
    if (!ctx_out || !cfg || !cfg->host || !cfg->host->alloc) {
        return BL_ERR_INVALID_ARGUMENT;
    }
    if (cfg->abi_version != BL_PLUGIN_ABI_VERSION) {
        return BL_ERR_PLUGIN_ABI_MISMATCH;
    }
    FixtureCtx* ctx = (FixtureCtx*)cfg->host->alloc(sizeof(FixtureCtx),
                                                    cfg->host->userdata);
    if (!ctx) return BL_ERR_OUT_OF_MEMORY;
    ctx->host = cfg->host;
    ctx->decode_count = 0;
    ctx->encode_count = 0;
    *ctx_out = ctx;
    return BL_OK;
}

static int fixture_decode(void* ctx_in, const uint8_t* pkt, size_t pkt_size,
                          uint8_t** out, size_t* out_size, BlFrameMeta* meta) {
    FixtureCtx* ctx = (FixtureCtx*)ctx_in;
    if (!ctx || !out || !out_size || !meta) return BL_ERR_INVALID_ARGUMENT;
    if (pkt_size == 0) return BL_DECODE_NEED_MORE_INPUT;
    uint8_t* buf =
        (uint8_t*)ctx->host->alloc(pkt_size, ctx->host->userdata);
    if (!buf) return BL_ERR_OUT_OF_MEMORY;
    memcpy(buf, pkt, pkt_size);
    *out = buf;
    *out_size = pkt_size;
    meta->pts += 1;
    meta->keyframe = 1;
    ctx->decode_count++;
    return BL_OK;
}

static int fixture_encode(void* ctx_in, const uint8_t* in, size_t in_size,
                          uint8_t** out, size_t* out_size,
                          const BlFrameMeta* meta) {
    FixtureCtx* ctx = (FixtureCtx*)ctx_in;
    if (!ctx || !out || !out_size) return BL_ERR_INVALID_ARGUMENT;
    if (!in && in_size != 0) return BL_ERR_INVALID_ARGUMENT;
    uint8_t* buf =
        in_size ? (uint8_t*)ctx->host->alloc(in_size, ctx->host->userdata)
                : NULL;
    if (in_size && !buf) return BL_ERR_OUT_OF_MEMORY;
    if (in_size) memcpy(buf, in, in_size);
    *out = buf;
    *out_size = in_size;
    if (meta) ctx->encode_count++;
    return BL_OK;
}

static int fixture_flush(void* ctx_in, uint8_t** out, size_t* out_size) {
    FixtureCtx* ctx = (FixtureCtx*)ctx_in;
    if (!ctx || !out || !out_size) return BL_ERR_INVALID_ARGUMENT;
    *out = NULL;
    *out_size = 0;
    return BL_OK;
}

static void fixture_cleanup(void* ctx_in) {
    FixtureCtx* ctx = (FixtureCtx*)ctx_in;
    if (ctx && ctx->host && ctx->host->free) {
        ctx->host->free(ctx, ctx->host->userdata);
    }
}

static const char* kExtensions[] = {"fx", "fixture", NULL};

static BlCodecPlugin g_plugin;

BlCodecPlugin* bl_get_codec_plugin(void) {
    g_plugin.abi_version = BL_PLUGIN_ABI_VERSION;
    g_plugin.name = "fixturegood";
    g_plugin.description = "valid fixture plugin used by unit tests";
    g_plugin.type = BL_CODEC_VIDEO;
    g_plugin.caps.roles = BL_ROLE_DECODE | BL_ROLE_ENCODE;
    g_plugin.caps.flags = 0;
    g_plugin.caps.file_extensions = kExtensions;
    g_plugin.caps.ff_encoder = "libfixtureenc";
    g_plugin.caps.ff_decoder = "libfixturedec";
    g_plugin.caps.params = NULL;
    g_plugin.init = fixture_init;
    g_plugin.decode = fixture_decode;
    g_plugin.encode = fixture_encode;
    g_plugin.flush = fixture_flush;
    g_plugin.cleanup = fixture_cleanup;
    return &g_plugin;
}
