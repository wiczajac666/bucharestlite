#include <bl_timeline/project_template.hpp>

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

using bl::ProjectTemplate;
using bl::TrackKind;
using bl::builtinTemplates;
using bl::findTemplate;
using bl::instantiateTemplate;

} // namespace

TEST(ProjectTemplate, BuiltinsAreNonEmptyWithUniqueIds) {
    const std::vector<ProjectTemplate>& templates = builtinTemplates();
    ASSERT_FALSE(templates.empty());

    std::set<std::string> ids;
    for (const ProjectTemplate& tmpl : templates) {
        EXPECT_FALSE(tmpl.id.empty());
        EXPECT_FALSE(tmpl.name.empty());
        EXPECT_FALSE(tmpl.description.empty());
        EXPECT_TRUE(ids.insert(tmpl.id).second)
            << "duplicate template id: " << tmpl.id;
        EXPECT_FALSE(tmpl.tracks.empty());

        // Video tracks must precede audio tracks, matching Sequence's layout.
        bool seenAudio = false;
        for (TrackKind kind : tmpl.tracks) {
            if (kind == TrackKind::Audio) {
                seenAudio = true;
            } else {
                EXPECT_FALSE(seenAudio)
                    << "video track after an audio track in " << tmpl.id;
            }
        }
    }
}

TEST(ProjectTemplate, FindTemplateResolvesById) {
    for (const char* id : {"1080p30-standard", "4k-uhd", "vertical-9x16",
                           "audio-podcast"}) {
        const ProjectTemplate* tmpl = findTemplate(id);
        ASSERT_NE(tmpl, nullptr) << id;
        EXPECT_EQ(tmpl->id, id);
    }
    EXPECT_EQ(findTemplate("does-not-exist"), nullptr);
}

TEST(ProjectTemplate, StandardInstantiatesSettingsAndTracks) {
    const ProjectTemplate* tmpl = findTemplate("1080p30-standard");
    ASSERT_NE(tmpl, nullptr);

    const bl::Timeline timeline = instantiateTemplate(*tmpl);
    const bl::Sequence& seq = timeline.sequence();

    EXPECT_EQ(seq.settings, tmpl->settings);
    EXPECT_EQ(seq.settings.width, 1920);
    EXPECT_EQ(seq.settings.height, 1080);
    EXPECT_EQ(seq.settings.fps, (bl::Rational{30000, 1001}));
    EXPECT_EQ(seq.settings.sampleRate, 48000);

    ASSERT_EQ(seq.videoTracks.size(), 1u);
    ASSERT_EQ(seq.audioTracks.size(), 1u);
    EXPECT_EQ(seq.videoTracks[0].kind(), TrackKind::Video);
    EXPECT_EQ(seq.audioTracks[0].kind(), TrackKind::Audio);
    EXPECT_EQ(seq.videoTracks[0].name(), "V1");
    EXPECT_EQ(seq.audioTracks[0].name(), "A1");
    // A fresh skeleton carries no clips and no seeded effects.
    EXPECT_TRUE(seq.videoTracks[0].clips().empty());
    EXPECT_TRUE(seq.videoTracks[0].effects().empty());
}

TEST(ProjectTemplate, UhdAndVerticalResolutions) {
    const ProjectTemplate* uhd = findTemplate("4k-uhd");
    ASSERT_NE(uhd, nullptr);
    const bl::Sequence uhdSeq = instantiateTemplate(*uhd).sequence();
    EXPECT_EQ(uhdSeq.settings.width, 3840);
    EXPECT_EQ(uhdSeq.settings.height, 2160);

    const ProjectTemplate* vertical = findTemplate("vertical-9x16");
    ASSERT_NE(vertical, nullptr);
    const bl::Sequence verticalSeq = instantiateTemplate(*vertical).sequence();
    EXPECT_EQ(verticalSeq.settings.width, 1080);
    EXPECT_EQ(verticalSeq.settings.height, 1920);
}

TEST(ProjectTemplate, PodcastTemplateHasTwoAudioTracks) {
    const ProjectTemplate* podcast = findTemplate("audio-podcast");
    ASSERT_NE(podcast, nullptr);

    const bl::Sequence seq = instantiateTemplate(*podcast).sequence();
    EXPECT_EQ(seq.settings.sampleRate, 48000);
    ASSERT_EQ(seq.videoTracks.size(), 1u);
    ASSERT_EQ(seq.audioTracks.size(), 2u);
    EXPECT_EQ(seq.audioTracks[0].name(), "A1");
    EXPECT_EQ(seq.audioTracks[1].name(), "A2");
}

TEST(ProjectTemplate, SeedTrackEffectsApplyToFirstTrack) {
    const ProjectTemplate* podcast = findTemplate("audio-podcast");
    ASSERT_NE(podcast, nullptr);
    ASSERT_FALSE(podcast->trackEffects.empty());

    const bl::Sequence seq = instantiateTemplate(*podcast).sequence();
    ASSERT_EQ(seq.videoTracks[0].effects().size(),
              podcast->trackEffects.size());
    const bl::EffectInstance& effect = seq.videoTracks[0].effects()[0];
    EXPECT_EQ(effect.effectId, "brightness_contrast_gamma");
    EXPECT_TRUE(effect.enabled);
    // Identity defaults: applying the effect must not change the image.
    EXPECT_DOUBLE_EQ(effect.params.value("brightness", -1.0), 0.0);
    EXPECT_DOUBLE_EQ(effect.params.value("contrast", -1.0), 1.0);
    EXPECT_DOUBLE_EQ(effect.params.value("gamma", -1.0), 1.0);
}

TEST(ProjectTemplate, TitleBearingBuiltinCarriesValidSpec) {
    const ProjectTemplate* vertical = findTemplate("vertical-9x16");
    ASSERT_NE(vertical, nullptr);
    ASSERT_FALSE(vertical->titles.empty());

    for (const bl::TemplateTitle& title : vertical->titles) {
        EXPECT_FALSE(title.name.empty());
        EXPECT_FALSE(title.spec.text.empty());
        EXPECT_GT(title.duration, bl::Duration{});
        // A valid spec is already clamped, so clamping again is a fixed point.
        EXPECT_EQ(nlohmann::json(title.spec),
                  nlohmann::json(bl::clampTitleSpec(title.spec)));
    }
}

TEST(ProjectTemplate, UserTemplatesRegisterResolveAndReplace) {
    bl::clearUserTemplates();
    EXPECT_EQ(findTemplate("user:custom"), nullptr);

    ProjectTemplate custom;
    custom.id = "user:custom";
    custom.name = "Custom";
    custom.tracks = {TrackKind::Video};
    bl::registerUserTemplate(custom);
    ASSERT_NE(findTemplate("user:custom"), nullptr);
    EXPECT_EQ(findTemplate("user:custom")->name, "Custom");

    // Registering the same id replaces in place.
    custom.name = "Custom 2";
    bl::registerUserTemplate(custom);
    EXPECT_EQ(findTemplate("user:custom")->name, "Custom 2");
    // Built-ins still resolve alongside user templates.
    EXPECT_NE(findTemplate("1080p30-standard"), nullptr);

    bl::clearUserTemplates();
    EXPECT_EQ(findTemplate("user:custom"), nullptr);
}
