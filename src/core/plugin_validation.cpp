#include "plugin_validation.hpp"

#include <bl_plugins/codec_plugin.h>

namespace bl::detail {

PluginValidation validatePluginShape(const BlCodecPlugin* p) noexcept {
    if (!p) return PluginValidation::NullPlugin;
    if (!p->name || !p->name[0]) return PluginValidation::BadName;
    if (p->type != BL_CODEC_VIDEO && p->type != BL_CODEC_AUDIO) {
        return PluginValidation::BadType;
    }
    if (p->caps.roles == 0 ||
        (p->caps.roles & ~(BL_ROLE_DECODE | BL_ROLE_ENCODE)) != 0) {
        return PluginValidation::BadRoles;
    }
    if (!p->init || !p->cleanup) return PluginValidation::MissingInit;
    if ((p->caps.roles & BL_ROLE_DECODE) && !p->decode) {
        return PluginValidation::MissingDecode;
    }
    if ((p->caps.roles & BL_ROLE_ENCODE) && !p->encode) {
        return PluginValidation::MissingEncode;
    }
    return PluginValidation::Valid;
}

const char* validationMessage(PluginValidation v) noexcept {
    switch (v) {
        case PluginValidation::Valid: return "ok";
        case PluginValidation::NullPlugin: return "entry point returned null";
        case PluginValidation::BadName: return "missing or empty plugin name";
        case PluginValidation::BadType: return "invalid codec type";
        case PluginValidation::BadRoles: return "invalid roles mask";
        case PluginValidation::MissingInit: return "missing init or cleanup function";
        case PluginValidation::MissingCleanup: return "missing cleanup function";
        case PluginValidation::MissingDecode: return "decode role declared but decode is null";
        case PluginValidation::MissingEncode: return "encode role declared but encode is null";
    }
    return "unknown validation failure";
}

} // namespace bl::detail
