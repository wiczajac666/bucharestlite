#include "bl_core/result.hpp"

namespace bl {

const char* toString(Err code) noexcept {
    switch (code) {
        case Err::Ok: return "Ok";
        case Err::FileNotFound: return "FileNotFound";
        case Err::DecodeFailed: return "DecodeFailed";
        case Err::EncodeFailed: return "EncodeFailed";
        case Err::PluginAbiMismatch: return "PluginAbiMismatch";
        case Err::RegistryDuplicate: return "RegistryDuplicate";
        case Err::InvalidArgument: return "InvalidArgument";
        case Err::OutOfMemory: return "OutOfMemory";
        case Err::Cancelled: return "Cancelled";
        case Err::IoError: return "IoError";
        case Err::JsonError: return "JsonError";
        case Err::Internal: return "Internal";
    }
    return "Unknown";
}

} // namespace bl
