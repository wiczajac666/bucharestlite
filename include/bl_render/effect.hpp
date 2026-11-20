#pragma once

#include <bl_timeline/clip.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bl {

struct ParamSpec {
    std::string key;
    std::string label;
    double min{0.0};
    double max{1.0};
    double def{0.0};
};

class IEffect {
public:
    virtual ~IEffect() = default;
    virtual std::string_view name() const = 0;
    virtual std::string_view displayName() const;
    virtual std::vector<ParamSpec> paramSpecs() const;
    virtual void apply(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
                       uint32_t linesize, const nlohmann::json& params) = 0;
};

// Builds an object of parameter defaults (key -> def) from parameter specs.
nlohmann::json makeDefaultParams(const std::vector<ParamSpec>& specs);

class EffectRegistry {
public:
    static EffectRegistry& instance();

    void registerEffect(std::unique_ptr<IEffect> effect);
    IEffect* find(std::string_view name) const noexcept;
    std::vector<std::string> catalog() const;
    size_t count() const noexcept;
    void clear() noexcept;

private:
    std::vector<std::unique_ptr<IEffect>> effects_;
};

void registerBuiltinEffects();

// A clip effect resolved for display on the timeline. `name` is the registry's
// human-readable displayName(), or the raw effect id when the effect is not
// registered (e.g. a plugin referenced by an older project). Stack order and
// the enabled flag are preserved so the UI can show bypassed effects dimmed.
struct EffectDisplay {
    std::string name;
    bool enabled{true};
};

// Resolves a clip's effect stack into display labels in stack order. Kept in
// bl_render (next to EffectRegistry) so it is Qt-free and unit-testable.
std::vector<EffectDisplay> clipEffectDisplays(
    const std::vector<EffectInstance>& effects);

} // namespace bl