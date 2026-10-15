#include <bl_timeline/project_template.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace bl {

namespace {

// One template's worth of common 1080p/30 sequence defaults; callers tweak the
// resolution or sample rate they care about.
SequenceSettings settings1080p30() {
    SequenceSettings settings;
    settings.width = 1920;
    settings.height = 1080;
    settings.fps = Rational{30000, 1001};
    settings.sampleRate = 48000;
    return settings;
}

// A left-anchored lower third: a small sans-serif line sitting low in the
// frame over a translucent bar, the usual news/interview style.
TitleSpec lowerThirdSpec() {
    TitleSpec spec;
    spec.text = "Lower Third";
    spec.fontSizePx = 72.0;
    spec.align = TitleAlign::Left;
    spec.anchorX = 0.08;
    spec.anchorY = 0.82;
    spec.backgroundEnabled = true;
    spec.backgroundColor = TitleColor{0, 0, 0, 180};
    spec.paddingPx = 18.0;
    spec.outlinePx = 3.0;
    return clampTitleSpec(std::move(spec));
}

// Identity colour correction, matching brightness_contrast_gamma's parameter
// defaults (brightness 0, contrast 1, gamma 1). A seeded track effect must be
// a no-op until the user dials it in.
EffectInstance identityColorCorrection() {
    EffectInstance effect;
    effect.effectId = "brightness_contrast_gamma";
    effect.enabled = true;
    effect.params = nlohmann::json{{"brightness", 0.0},
                                   {"contrast", 1.0},
                                   {"gamma", 1.0}};
    return effect;
}

const std::vector<ProjectTemplate>& builtinStorage() {
    static const std::vector<ProjectTemplate> templates = [] {
        std::vector<ProjectTemplate> list;

        ProjectTemplate standard;
        standard.id = "1080p30-standard";
        standard.name = "1080p 30fps Standard";
        standard.description =
            "Full-HD 1920x1080 at 29.97fps with one video and one audio "
            "track — the everyday starting point.";
        standard.settings = settings1080p30();
        standard.tracks = {TrackKind::Video, TrackKind::Audio};
        list.push_back(std::move(standard));

        ProjectTemplate uhd;
        uhd.id = "4k-uhd";
        uhd.name = "4K UHD";
        uhd.description =
            "Ultra-HD 3840x2160 at 29.97fps with one video and one audio "
            "track for high-resolution footage.";
        uhd.settings = settings1080p30();
        uhd.settings.width = 3840;
        uhd.settings.height = 2160;
        uhd.tracks = {TrackKind::Video, TrackKind::Audio};
        list.push_back(std::move(uhd));

        ProjectTemplate vertical;
        vertical.id = "vertical-9x16";
        vertical.name = "Vertical 9:16";
        vertical.description =
            "Portrait 1080x1920 at 29.97fps with a seed Lower Third title — "
            "ready for social/reels/stories.";
        vertical.settings = settings1080p30();
        vertical.settings.width = 1080;
        vertical.settings.height = 1920;
        vertical.tracks = {TrackKind::Video, TrackKind::Audio};
        vertical.titles.push_back(
            TemplateTitle{"Lower Third", lowerThirdSpec(),
                          Duration::fromSeconds(5.0, Rational{1'000'000, 1})});
        list.push_back(std::move(vertical));

        ProjectTemplate podcast;
        podcast.id = "audio-podcast";
        podcast.name = "Audio Podcast";
        podcast.description =
            "A cover-art video track and two 48kHz audio tracks (host and "
            "guest), with an identity colour correction seeded on the cover.";
        podcast.settings = settings1080p30();
        podcast.settings.sampleRate = 48000;
        podcast.tracks = {TrackKind::Video, TrackKind::Audio,
                          TrackKind::Audio};
        podcast.trackEffects.push_back(identityColorCorrection());
        list.push_back(std::move(podcast));

        return list;
    }();
    return templates;
}

// User templates loaded from disk, plus a dirty flag so the combined list is
// rebuilt only when the registry changes.
std::vector<ProjectTemplate>& userTemplates() {
    static std::vector<ProjectTemplate> storage;
    return storage;
}

bool& userTemplatesDirty() {
    static bool dirty = true;
    return dirty;
}

std::string trackName(TrackKind kind, size_t index) {
    const char prefix = kind == TrackKind::Video ? 'V' : 'A';
    return std::string(1, prefix) + std::to_string(index + 1);
}

} // namespace

const std::vector<ProjectTemplate>& builtinTemplates() {
    return builtinStorage();
}

const std::vector<ProjectTemplate>& allTemplates() {
    static std::vector<ProjectTemplate> combined;
    if (userTemplatesDirty()) {
        combined = builtinStorage();
        for (const ProjectTemplate& user : userTemplates()) {
            combined.push_back(user);
        }
        userTemplatesDirty() = false;
    }
    return combined;
}

const ProjectTemplate* findTemplate(const std::string& id) {
    const auto& templates = allTemplates();
    const auto it = std::find_if(
        templates.begin(), templates.end(),
        [&id](const ProjectTemplate& t) { return t.id == id; });
    return it == templates.end() ? nullptr : &*it;
}

void registerUserTemplate(ProjectTemplate tmpl) {
    auto& storage = userTemplates();
    const auto it = std::find_if(
        storage.begin(), storage.end(),
        [&tmpl](const ProjectTemplate& t) { return t.id == tmpl.id; });
    if (it != storage.end()) {
        *it = std::move(tmpl);
    } else {
        storage.push_back(std::move(tmpl));
    }
    userTemplatesDirty() = true;
}

void clearUserTemplates() {
    userTemplates().clear();
    userTemplatesDirty() = true;
}

Timeline instantiateTemplate(const ProjectTemplate& tmpl) {
    Sequence sequence;
    sequence.name = tmpl.name;
    sequence.settings = tmpl.settings;

    size_t videoCount = 0;
    size_t audioCount = 0;
    for (TrackKind kind : tmpl.tracks) {
        if (kind == TrackKind::Video) {
            sequence.addVideoTrack(trackName(kind, videoCount++));
        } else {
            sequence.addAudioTrack(trackName(kind, audioCount++));
        }
    }

    Timeline timeline(std::move(sequence));

    if (!tmpl.trackEffects.empty()) {
        VideoTrack* target = nullptr;
        if (!timeline.sequence().videoTracks.empty()) {
            target = &timeline.sequence().videoTracks.front();
        } else if (!timeline.sequence().audioTracks.empty()) {
            target = &timeline.sequence().audioTracks.front();
        }
        if (target) {
            for (const EffectInstance& effect : tmpl.trackEffects) {
                target->addEffect(effect);
            }
        }
    }

    return timeline;
}

} // namespace bl
