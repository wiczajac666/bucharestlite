#include <bl_core/codec_registry.hpp>

#include <bl_plugins/codec_plugin.h>
#include "plugin_validation.hpp"

#include <algorithm>
#include <iterator>

namespace bl {

namespace {

bool isExperimental(const BlCodecPlugin* p) noexcept {
    return (p->caps.flags & BL_FLAG_EXPERIMENTAL) != 0;
}

bool isPassthrough(const BlCodecPlugin* p) noexcept {
    return (p->caps.flags & BL_FLAG_PASSTHROUGH) != 0;
}

} // namespace

Result<void> CodecRegistry::registerPlugin(BlCodecPlugin* plugin) {
    auto shape = detail::validatePluginShape(plugin);
    if (shape != detail::PluginValidation::Valid) {
        return Result<void>::err(Err::InvalidArgument,
                                 std::string("cannot register plugin: ") +
                                     detail::validationMessage(shape));
    }
    if (find(plugin->name)) {
        return Result<void>::err(Err::RegistryDuplicate,
                                 std::string("plugin '") + plugin->name +
                                     "' is already registered");
    }
    plugins_.push_back(plugin);
    return Result<void>();
}

BlCodecPlugin* CodecRegistry::find(std::string_view name) const noexcept {
    for (BlCodecPlugin* p : plugins_) {
        if (name == p->name) return p;
    }
    return nullptr;
}

std::vector<BlCodecPlugin*> CodecRegistry::byType(
    unsigned char codecType) const noexcept {
    std::vector<BlCodecPlugin*> out;
    for (BlCodecPlugin* p : plugins_) {
        if (p->type == codecType) out.push_back(p);
    }
    return out;
}

BlCodecPlugin* CodecRegistry::defaultFor(unsigned char codecType,
                                         CodecRole role) const noexcept {
    const uint32_t needed =
        role == CodecRole::Preview ? BL_ROLE_DECODE : BL_ROLE_ENCODE;
    BlCodecPlugin* experimentalFallback = nullptr;
    for (BlCodecPlugin* p : byType(codecType)) {
        if (isPassthrough(p)) continue;
        if ((p->caps.roles & needed) == 0) continue;
        if (!isExperimental(p)) return p;
        if (!experimentalFallback) experimentalFallback = p;
    }
    return experimentalFallback;
}

} // namespace bl
