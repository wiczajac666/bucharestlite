#pragma once

#include <bl_core/time.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/sequence_settings.hpp>
#include <bl_timeline/timeline.hpp>
#include <bl_timeline/title.hpp>
#include <bl_timeline/track.hpp>

#include <string>
#include <vector>

namespace bl {

// A title that ships with a template. The still is rendered by the UI layer
// when the template is instantiated (the Qt-free model cannot rasterise); the
// duration is the on-timeline length of the resulting title clip.
struct TemplateTitle {
    std::string name;
    TitleSpec spec;
    Duration duration{};
};

// A reusable starting point for a new project: the sequence settings, a track
// skeleton (ordered video tracks then audio tracks), optional seed effects for
// the first track and optional titles. Deliberately Qt-free so the built-ins
// can be unit-tested and user templates parsed without pulling in the UI.
struct ProjectTemplate {
    std::string id;
    std::string name;
    std::string description;
    SequenceSettings settings;
    // Track kinds in lane order: every Video entry precedes the Audio ones,
    // matching how Sequence stores the two vectors.
    std::vector<TrackKind> tracks;
    // Seed effects for the first track (the first video track when the
    // template has one, otherwise the first audio track). Identity parameters
    // are used so an unconsumed effect is a no-op.
    std::vector<EffectInstance> trackEffects;
    // Titles rendered to stills by the UI on instantiation.
    std::vector<TemplateTitle> titles;
};

// The templates compiled into the application. Ids are stable and unique.
const std::vector<ProjectTemplate>& builtinTemplates();

// Built-ins plus any user templates registered with registerUserTemplate(),
// in that order.
const std::vector<ProjectTemplate>& allTemplates();

// Finds a template by id across all templates, or nullptr when absent.
const ProjectTemplate* findTemplate(const std::string& id);

// Registers (or replaces, by id) a user template loaded from disk so
// findTemplate() can resolve it. Kept out of builtinTemplates().
void registerUserTemplate(ProjectTemplate tmpl);

// Drops every registered user template. Built-ins are unaffected.
void clearUserTemplates();

// Builds a fresh Timeline from a template: settings, the track skeleton and
// the seed track effects. Titles are not added here — the UI renders them
// after instantiation and appends them as clips.
Timeline instantiateTemplate(const ProjectTemplate& tmpl);

} // namespace bl
