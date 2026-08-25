#pragma once

#include <bl_plugins/codec_plugin.h>

namespace bltest {

inline int trivialInit(void** ctx, const BlCodecConfig* cfg) {
    (void)cfg;
    *ctx = nullptr;
    return BL_OK;
}

inline void trivialCleanup(void*) {}

inline int trivialDecode(void*, const uint8_t*, size_t, uint8_t**, size_t*,
                         BlFrameMeta*) {
    return BL_OK;
}

inline int trivialEncode(void*, const uint8_t*, size_t, uint8_t**, size_t*,
                         const BlFrameMeta*) {
    return BL_OK;
}

inline BlCodecPlugin makePlugin(const char* name, unsigned char type,
                                uint32_t roles, uint32_t flags = 0) {
    BlCodecPlugin p{};
    p.abi_version = BL_PLUGIN_ABI_VERSION;
    p.name = name;
    p.description = "test plugin";
    p.type = type;
    p.caps.roles = roles;
    p.caps.flags = flags;
    p.init = &trivialInit;
    p.cleanup = &trivialCleanup;
    if (roles & BL_ROLE_DECODE) p.decode = &trivialDecode;
    if (roles & BL_ROLE_ENCODE) p.encode = &trivialEncode;
    return p;
}

} // namespace bltest
