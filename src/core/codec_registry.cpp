#include <bl_core/codec_registry.hpp>

#include <bl_plugins/codec_plugin.h>
#include "plugin_validation.hpp"

#include <algorithm>
#include <iterator>
#include <shared_mutex>

namespace bl {

namespace {

bool isExperimental(const BlCodecPlugin* p) noexcept {
    return (p->caps.flags & BL_FLAG_EXPERIMENTAL) != 0;
}

} // namespace

Result<void> CodecRegistry::registerPlugin(BlCodecPlugin* plugin) {
    auto shape = detail::validatePluginShape(plugin);
    if (shape != detail::PluginValidation::Valid) {
        return Result<void>::err(Err::InvalidArgument,
                                 std::string("cannot register plugin: ") +
                                     detail::validationMessage(shape));
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (BlCodecPlugin* existing : plugins_) {
        if (std::string_view(existing->name) == plugin->name) {
            return Result<void>::err(Err::RegistryDuplicate,
                                     std::string("plugin '") + plugin->name +
                                         "' is already registered");
        }
    }
    plugins_.push_back(plugin);
    return Result<void>();
}

BlCodecPlugin* CodecRegistry::find(std::string_view name) const noexcept {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (BlCodecPlugin* p : plugins_) {
        if (name == p->name) return p;
    }
    // Codecs are also addressable by their FFmpeg decoder name (e.g. plugin
    // "ffmpeg.theora" -> "theora") so container probes can bind a bridge
    // straight from avcodec_get_name().
    for (BlCodecPlugin* p : plugins_) {
        if ((p->caps.roles & BL_ROLE_DECODE) != 0 && p->caps.ff_decoder &&
            name == p->caps.ff_decoder) {
            return p;
        }
    }
    return nullptr;
}

std::vector<BlCodecPlugin*> CodecRegistry::byType(
    unsigned char codecType) const noexcept {
    std::shared_lock<std::shared_mutex> lock(mutex_);
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
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (BlCodecPlugin* p : plugins_) {
        if (p->type != codecType) continue;
        if ((p->caps.flags & BL_FLAG_PASSTHROUGH) != 0) continue;
        if ((p->caps.roles & needed) == 0) continue;
        if (!isExperimental(p)) return p;
        if (!experimentalFallback) experimentalFallback = p;
    }
    return experimentalFallback;
}

size_t CodecRegistry::count() const noexcept {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return plugins_.size();
}

void CodecRegistry::clear() noexcept {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    plugins_.clear();
}

} // namespace bl
