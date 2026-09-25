#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_audio/audio_meter_engine.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_timeline/sequence.hpp>

#include "test_media_utils.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

namespace {

using bl::AudioMeterEnvelope;
using bl::AudioMeterEngine;
using bl::Clip;
using bl::CodecRegistry;
using bl::Duration;
using bl::Err;
using bl::MeterLevels;
using bl::MediaBinItem;
using bl::PluginHandle;
using bl::PluginLoader;
using bl::PluginOrigin;
using bl::Result;
using bl::Sequence;
using bl::SourceMeterResolver;
using bl::SpeedRemap;
using bl::Time;
using bl::TrackMeterLevels;

constexpr double kSqrtHalf = 0.7071067811865476;

static Time sec(double s) { return Time::fromSeconds(s, {1'000'000, 1}); }
static Duration dur(double s) {
    return Duration::fromSeconds(s, {1'000'000, 1});
}

// Builds a registry with passthrough builtins plus the staged codec plugins.
// The loaded handles must outlive every use of the registry, so they are kept
// alive for the process lifetime here.
static const CodecRegistry& registry() {
    static CodecRegistry instance;
    static std::vector<std::unique_ptr<PluginHandle>> handles = [] {
        std::vector<std::unique_ptr<PluginHandle>> out;
        (void)bl::registerBuiltins(instance);
        PluginLoader loader;
        auto report = loader.scanDirectory(
            std::string(BL_TEST_PLUGIN_DIR) + "/plugins", PluginOrigin::User);
        if (report.ok()) {
            for (auto& handle : report.value().plugins) {
                auto loaded = instance.registerPlugin(
                    const_cast<BlCodecPlugin*>(handle->plugin()));
                if (loaded.ok()) {
                    out.push_back(std::move(handle));
                }
            }
        }
        return out;
    }();
    (void)handles;
    return instance;
}

// Deterministic stereo envelope: every non-window-selected window peaks at
// `peak`. Set `dipWindow`/`dipValue` to shape a single window differently.
static AudioMeterEnvelope synthStereo(float peak, size_t windowCount = 60,
                                      size_t dipWindow = 999,
                                      float dipValue = 0.0f) {
    AudioMeterEnvelope env;
    env.sampleRate = 48000;
    env.channels = 2;
    env.windowHz = 60.0;
    env.peak.assign(windowCount, std::vector<float>(2, peak));
    env.rms.assign(windowCount, std::vector<float>(2, peak * 0.7071f));
    if (dipWindow < windowCount) {
        env.peak[dipWindow][0] = dipValue;
        env.peak[dipWindow][1] = dipValue;
        env.rms[dipWindow][0] = dipValue * 0.7071f;
        env.rms[dipWindow][1] = dipValue * 0.7071f;
    }
    return env;
}

static AudioMeterEnvelope synthMono(float peak, size_t windowCount = 60) {
    AudioMeterEnvelope env;
    env.sampleRate = 48000;
    env.channels = 1;
    env.windowHz = 60.0;
    env.peak.assign(windowCount, std::vector<float>(1, peak));
    env.rms.assign(windowCount, std::vector<float>(1, peak * 0.7071f));
    return env;
}

// Resolver for a set of synthesized envelopes.
static SourceMeterResolver resolverWith(
    const std::map<std::string, std::shared_ptr<const AudioMeterEnvelope>>&
        items) {
    return [items](const std::string& id)
        -> std::shared_ptr<const AudioMeterEnvelope> {
        auto it = items.find(id);
        if (it == items.end()) return nullptr;
        return it->second;
    };
}

// A simple 1-second audio clip on a fresh single-audio-track sequence.
static Clip makeClip(const std::string& media, Time timelineStart,
                     Duration timelineDuration, Time sourceIn, Time sourceOut,
                     double gain = 1.0, double pan = 0.0,
                     SpeedRemap speed = {}) {
    Clip clip;
    clip.name = "clip";
    clip.source.mediaItemId = media;
    clip.source.sourceIn = sourceIn;
    clip.source.sourceOut = sourceOut;
    clip.timelineStart = timelineStart;
    clip.timelineDuration = timelineDuration;
    clip.speed = speed;
    clip.audio.gain = gain;
    clip.audio.pan = pan;
    return clip;
}

static TrackMeterLevels trackAt(const MeterLevels& levels, size_t index = 0) {
    return levels.tracks.at(index);
}

} // namespace

TEST(AudioMeterAnalysisTest, DecodesVorbisFixtureToWindows) {
    auto analyzed = bl::analyzeAudio(
        bltest::mediaPath("test_audio_vorbis.ogg"), registry());
    ASSERT_TRUE(analyzed.ok()) << analyzed.message();
    const AudioMeterEnvelope& env = analyzed.value();
    EXPECT_EQ(env.sampleRate, 48000u);
    EXPECT_EQ(env.channels, 2u);
    EXPECT_NEAR(env.windowHz, 60.0, 1e-9);
    // A one-second lavender sine yields ~60 windows, allow encoder padding.
    EXPECT_GE(env.windowCount(), 50u);
    EXPECT_LE(env.windowCount(), 70u);
    // The generated fixture is quiet (-20.8 dBFS); the decode must reproduce
    // audible content somewhere without clipping.
    float best = 0.0f;
    for (const auto& window : env.peak) {
        best = std::max(best, window[0]);
    }
    EXPECT_GT(best, 0.01f);
    EXPECT_LE(best, 1.0f);
    // RMS of a full-scale sine is ~-3 dB of its peak.
    for (size_t w = 0; w < env.windowCount(); ++w) {
        EXPECT_LE(env.rms[w][0], env.peak[w][0] + 1e-3f);
    }
}

TEST(AudioMeterAnalysisTest, DecodesMp4AndOpusFixtures) {
    auto av = bl::analyzeAudio(bltest::mediaPath("test_av.mp4"), registry());
    ASSERT_TRUE(av.ok()) << av.message();
    // The video fixture's sine stream is generated without -ac 2: mono.
    EXPECT_EQ(av.value().channels, 1u);
    EXPECT_FALSE(av.value().empty());

    auto opus = bl::analyzeAudio(bltest::mediaPath("test_audio_opus.opus"),
                                 registry());
    ASSERT_TRUE(opus.ok()) << opus.message();
    EXPECT_EQ(opus.value().channels, 2u);
    EXPECT_EQ(opus.value().sampleRate, 48000u);
}

TEST(AudioMeterAnalysisTest, RejectsMediaWithoutAudio) {
    auto video = bl::analyzeAudio(bltest::mediaPath("test_video_theora.ogv"),
                                  registry());
    ASSERT_FALSE(video.ok());
    EXPECT_NE(video.code(), Err::Ok);

    auto missing = bl::analyzeAudio(bltest::mediaPath("does-not-exist.mp4"),
                                    registry());
    ASSERT_FALSE(missing.ok());
}

TEST(AudioMeterQueryTest, CenterPannedStereoDefaults) {
    Sequence seq;
    seq.addAudioTrack("A");
    auto clip = makeClip("c", sec(0.0), dur(1.0), sec(0.0), sec(1.0));
    ASSERT_TRUE(seq.appendClipToAudioTrack(0, clip));

    auto env = synthStereo(1.0f);
    MeterLevels m = bl::meterLevelsAt(seq, sec(0.5),
                                      resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env))}));
    ASSERT_EQ(m.tracks.size(), 1u);
    // Stereo clip-shaped by clip pan (0) then track pan (0): two constant
    // power stages of 1/sqrt(2) each.
    EXPECT_NEAR(trackAt(m).left, kSqrtHalf * kSqrtHalf, 1e-3);
    EXPECT_NEAR(trackAt(m).right, kSqrtHalf * kSqrtHalf, 1e-3);
    // Master adds the track's bus (0.5) then applies master pan (0).
    EXPECT_NEAR(m.master.left, kSqrtHalf * kSqrtHalf * kSqrtHalf, 1e-3);
    EXPECT_NEAR(m.master.right, kSqrtHalf * kSqrtHalf * kSqrtHalf, 1e-3);
}

TEST(AudioMeterQueryTest, MonoFeedsLeftOnlyIgnoringPan) {
    Sequence seq;
    seq.addAudioTrack("A");
    auto clip = makeClip("c", sec(0.0), dur(1.0), sec(0.0), sec(1.0),
                         1.0, 0.5);
    ASSERT_TRUE(seq.appendClipToAudioTrack(0, clip));

    auto env = synthMono(1.0f);
    MeterLevels m = bl::meterLevelsAt(seq, sec(0.5),
                                      resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env))}));
    // Mono mirrors TrackStrip: left bus only, pan ignored.
    EXPECT_NEAR(trackAt(m).left, 1.0, 1e-4);
    EXPECT_NEAR(trackAt(m).right, 0.0, 1e-6);
}

TEST(AudioMeterQueryTest, ClipGainAndPanShapeLevels) {
    Sequence seq;
    seq.addAudioTrack("A");
    auto clip = makeClip("c", sec(0.0), dur(1.0), sec(0.0), sec(1.0),
                         0.5, -1.0);
    ASSERT_TRUE(seq.appendClipToAudioTrack(0, clip));

    auto env = synthStereo(1.0f);
    MeterLevels m = bl::meterLevelsAt(seq, sec(0.5),
                                      resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env))}));
    // pan=-1: clip left gain 1, right 0; then track pan (0) halves both.
    EXPECT_NEAR(trackAt(m).left, 0.5 * kSqrtHalf, 1e-3);
    EXPECT_NEAR(trackAt(m).right, 0.0, 1e-6);
}

TEST(AudioMeterQueryTest, MutedSoloAndMasterGain) {
    Sequence seq;
    seq.addAudioTrack("A");
    seq.addAudioTrack("B");
    auto env = synthStereo(1.0f);

    auto clip = makeClip("c", sec(0.0), dur(1.0), sec(0.0), sec(1.0), 0.5);
    auto other = makeClip("d", sec(0.0), dur(1.0), sec(0.0), sec(1.0), 0.5);
    ASSERT_TRUE(seq.appendClipToAudioTrack(0, clip));
    ASSERT_TRUE(seq.appendClipToAudioTrack(1, other));

    // Track A muted: contributes nothing; B alone at its defaults.
    seq.audioTracks[0].setMuted(true);
    {
        MeterLevels m = bl::meterLevelsAt(seq, sec(0.5),
                                          resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env)), std::make_pair("d", std::make_shared<const AudioMeterEnvelope>(env))}));
        EXPECT_NEAR(trackAt(m, 0).left, 0.0, 1e-6);
        EXPECT_NEAR(trackAt(m, 1).left, 0.5 * kSqrtHalf * kSqrtHalf, 1e-3);
        EXPECT_NEAR(m.master.left,
                    0.5 * kSqrtHalf * kSqrtHalf * kSqrtHalf, 1e-3);
    }

    // Unmute A, solo B: muted no longer matters, A is silenced by solo.
    seq.audioTracks[0].setMuted(false);
    seq.audioTracks[1].setSoloed(true);
    {
        MeterLevels m = bl::meterLevelsAt(seq, sec(0.5),
                                          resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env)), std::make_pair("d", std::make_shared<const AudioMeterEnvelope>(env))}));
        EXPECT_NEAR(trackAt(m, 0).left, 0.0, 1e-6);
        EXPECT_NEAR(trackAt(m, 1).left, 0.5 * kSqrtHalf * kSqrtHalf, 1e-3);
    }

    // Master gain halves the bus.
    seq.settings.masterGain = 0.5;
    {
        MeterLevels m = bl::meterLevelsAt(seq, sec(0.5),
                                          resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env)), std::make_pair("d", std::make_shared<const AudioMeterEnvelope>(env))}));
        EXPECT_NEAR(m.master.left,
                    0.5 * 0.5 * kSqrtHalf * kSqrtHalf * kSqrtHalf, 1e-3);
    }
}

TEST(AudioMeterQueryTest, SourceMappingHonoursSpeedMotion) {
    Sequence seq;
    seq.addAudioTrack("A");
    SpeedRemap speed;
    speed.rateNum = 2;
    // 2x speed: 0.5 s of timeline covers 1.0 s of source.
    auto clip = makeClip("c", sec(0.0), dur(0.5), sec(0.0), sec(1.0),
                         1.0, 0.0, speed);
    ASSERT_TRUE(seq.appendClipToAudioTrack(0, clip));

    // Envelope silent at source window 30 (0.5 s) so the mapping is visible.
    auto env = synthStereo(1.0f, 60, 30, 0.1f);
    MeterLevels m = bl::meterLevelsAt(seq, sec(0.25),
                                      resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env))}));
    // timeline 0.25 -> source 0.5 (window 30): expect the dipped value.
    EXPECT_NEAR(trackAt(m).left, 0.1f * kSqrtHalf * kSqrtHalf, 1e-3);
    EXPECT_NEAR(trackAt(m).right, 0.1f * kSqrtHalf * kSqrtHalf, 1e-3);
}

TEST(AudioMeterQueryTest, SourceOffsetTrimClampsIntoRange) {
    Sequence seq;
    seq.addAudioTrack("A");
    // Clip starts at source 0.4 (trimmed head 0.4 s) over 0.6 s of material.
    auto clip = makeClip("c", sec(0.0), dur(0.6), sec(0.4), sec(1.0));
    ASSERT_TRUE(seq.appendClipToAudioTrack(0, clip));

    auto env = synthStereo(1.0f, 60, 30, 0.1f);
    // Within the clip at timeline 0.1 -> source 0.5 (window 30).
    MeterLevels inside = bl::meterLevelsAt(seq, sec(0.1),
                                           resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env))}));
    EXPECT_NEAR(trackAt(inside).left, 0.1f * kSqrtHalf * kSqrtHalf, 1e-3);

    // Just past the clip end -> nothing contributes.
    MeterLevels after = bl::meterLevelsAt(seq, sec(0.61),
                                          resolverWith({std::make_pair("c", std::make_shared<const AudioMeterEnvelope>(env))}));
    EXPECT_NEAR(trackAt(after).left, 0.0, 1e-6);
}

TEST(AudioMeterEngineTest, AnalyzesAndSettlesBackground) {
    AudioMeterEngine engine(1);
    engine.configure({{std::string(BL_TEST_PLUGIN_DIR) + "/plugins",
                       PluginOrigin::User}});

    const std::string id = "engine-media";
    engine.analyze(id, bltest::mediaPath("test_audio_vorbis.ogg"));
    EXPECT_TRUE(engine.isAnalyzing(id));

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (engine.isAnalyzing(id)) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline)
            << "analysis job did not settle in time";
        std::this_thread::yield();
    }

    auto env = engine.envelopeFor(id);
    ASSERT_NE(env, nullptr);
    EXPECT_EQ(env->channels, 2u);
}

TEST(AudioMeterEngineTest, RemoveCancelsInFlightWithoutCaching) {
    AudioMeterEngine engine(1);
    engine.configure({{std::string(BL_TEST_PLUGIN_DIR) + "/plugins",
                       PluginOrigin::User}});

    const std::string id = "engine-remove";
    engine.analyze(id, bltest::mediaPath("test_audio_opus.opus"));
    engine.remove(id);
    EXPECT_FALSE(engine.isAnalyzing(id));
    EXPECT_EQ(engine.envelopeFor(id), nullptr);

    // A settled (cached) item survives remove-free lookups.
    engine.analyze(id, bltest::mediaPath("test_audio_opus.opus"));
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (engine.isAnalyzing(id)) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::yield();
    }
    ASSERT_NE(engine.envelopeFor(id), nullptr);
    engine.clear();
    EXPECT_EQ(engine.envelopeFor(id), nullptr);
}