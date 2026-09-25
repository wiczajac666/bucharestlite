#include <QApplication>
#include <QTest>

#include <app/project_controller.hpp>
#include <panels/master_fader_panel.hpp>
#include <panels/mixer_panel.hpp>
#include <widgets/level_meter.hpp>

#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_timeline/sequence.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>

namespace {

constexpr double kSqrtHalf = 0.7071067811865476;

QApplication* ensureApp() {
    static QApplication* app = [] {
        int argc = 1;
        static char arg[] = "test";
        static char* argv[] = {arg, nullptr};
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return new QApplication(argc, argv);
    }();
    return app;
}

bl::Time sec(double s) { return bl::Time::fromSeconds(s, {1'000'000, 1}); }
bl::Duration dur(double s) {
    return bl::Duration::fromSeconds(s, {1'000'000, 1});
}

bl::Clip makeClip(const std::string& media, bl::Time start,
                  bl::Duration length) {
    bl::Clip clip;
    clip.name = "clip";
    clip.source.mediaItemId = media;
    clip.source.sourceIn = sec(0.0);
    clip.source.sourceOut = sec(1.0);
    clip.timelineStart = start;
    clip.timelineDuration = length;
    return clip;
}

// 60 windows of silence with a single unit spike at window 30 (0.5 s in).
bl::AudioMeterEnvelope spikeEnvelope(float value) {
    bl::AudioMeterEnvelope env;
    env.sampleRate = 48000;
    env.channels = 2;
    env.windowHz = 60.0;
    env.peak.assign(60, std::vector<float>(2, 0.0f));
    env.rms.assign(60, std::vector<float>(2, 0.0f));
    env.peak[30] = {value, value};
    env.rms[30] = {value * static_cast<float>(kSqrtHalf),
                   value * static_cast<float>(kSqrtHalf)};
    return env;
}

bl::SourceMeterResolver resolverWith(
    const std::map<std::string, std::shared_ptr<const bl::AudioMeterEnvelope>>&
        items) {
    return [items](const std::string& id)
        -> std::shared_ptr<const bl::AudioMeterEnvelope> {
        auto it = items.find(id);
        if (it == items.end()) return nullptr;
        return it->second;
    };
}

struct MeterFixture {
    QApplication* app{nullptr};
    bl::ui::ProjectController controller;

    MeterFixture() {
        app = ensureApp();
        controller.newProject(QStringLiteral("Meters"));
    }
};

} // namespace

TEST(MixerMeters, SilenceWithoutResolver) {
    MeterFixture f;
    bl::ui::MixerPanel panel(&f.controller);
    ASSERT_NE(panel.meter(0), nullptr);
    panel.onPlayheadChanged(sec(0.5));
    EXPECT_FLOAT_EQ(panel.meter(0)->level(0), 0.0f);
    EXPECT_FLOAT_EQ(panel.meter(0)->level(1), 0.0f);
}

TEST(MixerMeters, FollowThePlayhead) {
    MeterFixture f;
    bl::Clip clip = makeClip("m", sec(0.0), dur(1.0));
    ASSERT_TRUE(f.controller.timeline().sequence().appendClipToAudioTrack(
        0, clip));

    auto env = std::make_shared<const bl::AudioMeterEnvelope>(
        spikeEnvelope(1.0f));
    bl::ui::MixerPanel panel(&f.controller);
    panel.setMeterResolver(resolverWith({std::make_pair("m", env)}));
    ASSERT_NE(panel.meter(0), nullptr);

    // At 0.5 s the spike window (0.5 s of source) is active: with default
    // clamps/levels the stereo track bus reads 1/sqrt(2)^2 per side.
    panel.onPlayheadChanged(sec(0.5));
    EXPECT_NEAR(panel.meter(0)->level(0), kSqrtHalf * kSqrtHalf, 1e-3);
    EXPECT_NEAR(panel.meter(0)->level(1), kSqrtHalf * kSqrtHalf, 1e-3);

    // Away from the spike the same clip reads silence.
    panel.onPlayheadChanged(sec(0.02));
    EXPECT_NEAR(panel.meter(0)->level(0), 0.0f, 1e-6);
}

TEST(MixerMeters, RespectTrackGainAndMute) {
    MeterFixture f;
    bl::Clip clip = makeClip("m", sec(0.0), dur(1.0));
    ASSERT_TRUE(f.controller.timeline().sequence().appendClipToAudioTrack(
        0, clip));

    auto env = std::make_shared<const bl::AudioMeterEnvelope>(
        spikeEnvelope(1.0f));
    bl::ui::MixerPanel panel(&f.controller);
    panel.setMeterResolver(resolverWith({std::make_pair("m", env)}));
    ASSERT_NE(panel.meter(0), nullptr);

    // Half fader: track gain 0.5 halves the stereo bus.
    f.controller.timeline().sequence().audioTracks[0].setGain(0.5);
    panel.onPlayheadChanged(sec(0.5));
    EXPECT_NEAR(panel.meter(0)->level(0),
                0.5 * kSqrtHalf * kSqrtHalf, 1e-3);

    // Muting the track silences the strip meter outright.
    f.controller.timeline().sequence().audioTracks[0].setMuted(true);
    panel.onPlayheadChanged(sec(0.5));
    EXPECT_NEAR(panel.meter(0)->level(0), 0.0f, 1e-6);
    EXPECT_NEAR(panel.meter(0)->level(1), 0.0f, 1e-6);
}

TEST(MixerMeters, SilencesMissingEnvelope) {
    MeterFixture f;
    bl::Clip clip = makeClip("m", sec(0.0), dur(1.0));
    ASSERT_TRUE(f.controller.timeline().sequence().appendClipToAudioTrack(
        0, clip));

    auto env = std::make_shared<const bl::AudioMeterEnvelope>(
        spikeEnvelope(1.0f));
    // Resolver knows a different item id, so "m" resolves to nothing.
    bl::ui::MixerPanel panel(&f.controller);
    panel.setMeterResolver(resolverWith({std::make_pair("other", env)}));
    panel.onPlayheadChanged(sec(0.5));
    EXPECT_NEAR(panel.meter(0)->level(0), 0.0f, 1e-6);
}

TEST(MixerMeters, RebuildKeepsMetersInOrder) {
    MeterFixture f;
    bl::Clip clip = makeClip("m", sec(0.0), dur(1.0));
    ASSERT_TRUE(f.controller.timeline().sequence().appendClipToAudioTrack(
        0, clip));
    f.controller.timeline().sequence().addAudioTrack("A2");

    auto env = std::make_shared<const bl::AudioMeterEnvelope>(
        spikeEnvelope(1.0f));
    bl::ui::MixerPanel panel(&f.controller);
    panel.setMeterResolver(resolverWith({std::make_pair("m", env)}));

    emit f.controller.projectChanged();
    ASSERT_EQ(panel.stripCount(), 2);
    ASSERT_NE(panel.meter(0), nullptr);
    ASSERT_NE(panel.meter(1), nullptr);

    panel.onPlayheadChanged(sec(0.5));
    EXPECT_NEAR(panel.meter(0)->level(0), kSqrtHalf * kSqrtHalf, 1e-3);
    EXPECT_NEAR(panel.meter(1)->level(0), 0.0f, 1e-6);
}

TEST(MasterFaderPanelMeters, ShowTheSummedMasterBus) {
    MeterFixture f;
    bl::Clip clip = makeClip("m", sec(0.0), dur(1.0));
    ASSERT_TRUE(f.controller.timeline().sequence().appendClipToAudioTrack(
        0, clip));

    auto env = std::make_shared<const bl::AudioMeterEnvelope>(
        spikeEnvelope(1.0f));
    bl::ui::MasterFaderPanel panel(&f.controller);
    panel.setMeterResolver(resolverWith({std::make_pair("m", env)}));
    ASSERT_NE(panel.masterMeter(), nullptr);

    // Track bus = 1/sqrt(2)^2 summed into master, then the master's own
    // constant-power pan stage: 1/sqrt(2)^3.
    panel.onPlayheadChanged(sec(0.5));
    EXPECT_NEAR(panel.masterMeter()->level(0),
                kSqrtHalf * kSqrtHalf * kSqrtHalf, 1e-3);
    EXPECT_NEAR(panel.masterMeter()->level(1),
                kSqrtHalf * kSqrtHalf * kSqrtHalf, 1e-3);

    // Master gain scales the sum.
    f.controller.timeline().sequence().settings.masterGain = 0.5;
    panel.onPlayheadChanged(sec(0.5));
    EXPECT_NEAR(panel.masterMeter()->level(0),
                0.5 * kSqrtHalf * kSqrtHalf * kSqrtHalf, 1e-3);

    // Muting the only track silences the master bus.
    f.controller.timeline().sequence().audioTracks[0].setMuted(true);
    panel.onPlayheadChanged(sec(0.5));
    EXPECT_NEAR(panel.masterMeter()->level(0), 0.0f, 1e-6);
}