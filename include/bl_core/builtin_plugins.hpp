#pragma once

#include <bl_core/result.hpp>

namespace bl {

class CodecRegistry;

Result<void> registerBuiltins(CodecRegistry& registry);

} // namespace bl
