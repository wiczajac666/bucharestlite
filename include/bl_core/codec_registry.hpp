#pragma once

#include <bl_core/result.hpp>

#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <vector>

struct BlCodecPlugin;

namespace bl {

enum class CodecRole : unsigned char { Preview, Export };

class CodecRegistry {
public:
    Result<void> registerPlugin(BlCodecPlugin* plugin);

    BlCodecPlugin* find(std::string_view name) const noexcept;

    std::vector<BlCodecPlugin*> byType(unsigned char codecType) const noexcept;

    BlCodecPlugin* defaultFor(unsigned char codecType, CodecRole role) const noexcept;

    size_t count() const noexcept;

    void clear() noexcept;

private:
    mutable std::shared_mutex mutex_;
    std::vector<BlCodecPlugin*> plugins_;
};

} // namespace bl
