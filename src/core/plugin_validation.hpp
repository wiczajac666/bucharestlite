#pragma once

#include <string>

struct BlCodecPlugin;

namespace bl::detail {

enum class PluginValidation {
    Valid,
    NullPlugin,
    BadName,
    BadType,
    BadRoles,
    MissingInit,
    MissingCleanup,
    MissingDecode,
    MissingEncode,
};

PluginValidation validatePluginShape(const BlCodecPlugin* plugin) noexcept;

const char* validationMessage(PluginValidation v) noexcept;

} // namespace bl::detail
