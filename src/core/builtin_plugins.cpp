#include <bl_core/builtin_plugins.hpp>

#include <bl_core/codec_registry.hpp>
#include "../plugins/passthrough/passthrough_plugin.h"

namespace bl {

Result<void> registerBuiltins(CodecRegistry& registry) {
    if (auto r = registry.registerPlugin(bl_passthrough_video_plugin());
        !r.ok()) {
        return r;
    }
    return registry.registerPlugin(bl_passthrough_audio_plugin());
}

} // namespace bl
