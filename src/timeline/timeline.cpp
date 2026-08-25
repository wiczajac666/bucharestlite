#include <bl_timeline/timeline.hpp>

namespace bl {

void to_json(nlohmann::json& j, const Timeline& t) {
    j = {{"sequence", t.sequence()}};
}

void from_json(const nlohmann::json& j, Timeline& t) {
    Sequence seq;
    from_json(j.at("sequence"), seq);
    t = Timeline(std::move(seq));
}

} // namespace bl
