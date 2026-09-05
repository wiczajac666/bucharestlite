#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace bl {

class IEffect {
public:
    virtual ~IEffect() = default;
    virtual std::string_view name() const = 0;
    virtual void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
                       uint32_t linesize, const nlohmann::json& params) = 0;
};

class EffectRegistry {
public:
    static EffectRegistry& instance();

    void registerEffect(std::unique_ptr<IEffect> effect);
    IEffect* find(std::string_view name) const noexcept;
    size_t count() const noexcept;
    void clear() noexcept;

private:
    std::vector<std::unique_ptr<IEffect>> effects_;
};

void registerBuiltinEffects();

} // namespace bl
