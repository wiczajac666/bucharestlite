#include <bl_audio/audio_engine.hpp>
#include <bl_audio/mixer.hpp>
#include <bl_audio/track_strip.hpp>
#include <bl_audio/types.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace {

using bl::AudioBuffer;
using bl::AudioEngine;
using bl::AudioMixConfig;
using bl::AudioSpan;
using bl::GainDb;
using bl::Mixer;
using bl::NullDeviceOutput;
using bl::PanLaw;
using bl::TrackStrip;

static AudioMixConfig stereo() { return {48000, 2}; }

static AudioBuffer makeSine(uint32_t frames, uint32_t channels, float amplitude = 1.0f) {
    AudioBuffer buf;
    buf.resize(frames, channels);
    for (uint32_t i = 0; i < frames; ++i) {
        float sample = amplitude * std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
        for (uint32_t c = 0; c < channels; ++c) {
            buf.channelData(c)[i] = sample;
        }
    }
    return buf;
}

static AudioBuffer makeSilence(uint32_t frames, uint32_t channels) {
    AudioBuffer buf;
    buf.resize(frames, channels);
    buf.clear();
    return buf;
}

TEST(AudioTypesTest, GainDbLinearConversion) {
    EXPECT_NEAR(bl::dbToLinear(0.0), 1.0, 1e-6);
    EXPECT_NEAR(bl::dbToLinear(-6.0), 0.5, 0.01);
    EXPECT_NEAR(bl::dbToLinear(6.0), 2.0, 0.01);
    EXPECT_NEAR(bl::linearToDb(1.0), 0.0, 1e-6);
    EXPECT_NEAR(bl::linearToDb(0.5), -6.0, 0.1);
}

TEST(AudioTypesTest, PanLawConstantPower) {
    auto [l0, r0] = PanLaw::compute(0.0);
    EXPECT_NEAR(l0, r0, 1e-6);
    EXPECT_NEAR(l0 * l0 + r0 * r0, 1.0, 1e-6);

    auto [lL, rL] = PanLaw::compute(-1.0);
    EXPECT_NEAR(lL, 1.0, 1e-6);
    EXPECT_NEAR(rL, 0.0, 1e-6);

    auto [lR, rR] = PanLaw::compute(1.0);
    EXPECT_NEAR(lR, 0.0, 1e-6);
    EXPECT_NEAR(rR, 1.0, 1e-6);
}

TEST(TrackStripTest, GainApplied) {
    TrackStrip strip(stereo());
    auto input = makeSine(100, 2, 1.0f);
    AudioBuffer output;
    output.resize(100, 2);
    output.clear();

    // Use full-left pan so right channel is silent and left gets full signal
    strip.process(output.span(), input.span(), 0.5, -1.0);

    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_NEAR(output.channelData(0)[i], input.channelData(0)[i] * 0.5f, 1e-5f);
    }
}

TEST(TrackStripTest, PanLeftRight) {
    TrackStrip strip(stereo());
    auto input = makeSine(100, 2, 1.0f);
    AudioBuffer output;
    output.resize(100, 2);
    output.clear();

    strip.process(output.span(), input.span(), 1.0, -1.0);

    float leftEnergy = 0, rightEnergy = 0;
    for (uint32_t i = 0; i < 100; ++i) {
        leftEnergy += output.channelData(0)[i] * output.channelData(0)[i];
        rightEnergy += output.channelData(1)[i] * output.channelData(1)[i];
    }
    EXPECT_GT(leftEnergy, rightEnergy);
}

TEST(TrackStripTest, VolumeKeyframe) {
    TrackStrip strip(stereo());
    bl::KeyframeTrack volTrack;
    volTrack.set(bl::Time::fromSeconds(0.0, bl::Rational{1, 1}), 0.5,
                 bl::Interpolation::Linear);

    auto input = makeSine(48000, 2, 1.0f);
    AudioBuffer output;
    output.resize(48000, 2);
    output.clear();

    // Use full-left pan to avoid pan law attenuation on left channel
    strip.process(output.span(), input.span(), 1.0, -1.0, &volTrack);

    for (uint32_t i = 0; i < 48000; ++i) {
        EXPECT_NEAR(output.channelData(0)[i], input.channelData(0)[i] * 0.5f, 1e-5f);
    }
}

TEST(MixerTest, SilentTracks) {
    Mixer mixer(stereo());
    auto silence = makeSilence(100, 2);
    AudioBuffer output;
    output.resize(100, 2);

    std::vector<AudioSpan> inputs = {silence.span()};
    std::vector<double> gains = {1.0};
    std::vector<double> pans = {0.0};

    mixer.mix(output.span(), inputs, gains, pans);

    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_NEAR(output.channelData(0)[i], 0.0f, 1e-6f);
    }
}

TEST(MixerTest, SingleTrackPassthrough) {
    Mixer mixer(stereo());
    auto input = makeSine(100, 2, 0.5f);
    AudioBuffer output;
    output.resize(100, 2);

    std::vector<AudioSpan> inputs = {input.span()};
    std::vector<double> gains = {1.0};
    std::vector<double> pans = {-1.0};  // full-left → unity on left channel

    mixer.mix(output.span(), inputs, gains, pans);

    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_NEAR(output.channelData(0)[i], input.channelData(0)[i], 1e-5f);
    }
}

TEST(MixerTest, TwoTrackSum) {
    Mixer mixer(stereo());
    AudioBuffer a;
    a.resize(10, 2);
    AudioBuffer b;
    b.resize(10, 2);
    for (uint32_t i = 0; i < 10; ++i) {
        a.channelData(0)[i] = 0.3f;
        a.channelData(1)[i] = 0.3f;
        b.channelData(0)[i] = 0.2f;
        b.channelData(1)[i] = 0.2f;
    }

    AudioBuffer output;
    output.resize(10, 2);

    // Full-left pan → unity on left channel
    std::vector<AudioSpan> inputs = {a.span(), b.span()};
    std::vector<double> gains = {1.0, 1.0};
    std::vector<double> pans = {-1.0, -1.0};

    mixer.mix(output.span(), inputs, gains, pans);

    for (uint32_t i = 0; i < 10; ++i) {
        EXPECT_NEAR(output.channelData(0)[i], 0.5f, 1e-5f);
    }
}

TEST(MixerTest, GainInteraction) {
    Mixer mixer(stereo());
    AudioBuffer a;
    a.resize(10, 2);
    for (uint32_t i = 0; i < 10; ++i) {
        a.channelData(0)[i] = 1.0f;
        a.channelData(1)[i] = 1.0f;
    }

    AudioBuffer output;
    output.resize(10, 2);

    // Full-left pan → unity on left channel
    std::vector<AudioSpan> inputs = {a.span()};
    std::vector<double> gains = {0.25};
    std::vector<double> pans = {-1.0};

    mixer.mix(output.span(), inputs, gains, pans);

    for (uint32_t i = 0; i < 10; ++i) {
        EXPECT_NEAR(output.channelData(0)[i], 0.25f, 1e-5f);
    }
}

TEST(AudioEngineTest, CreateDestroy) {
    auto engine = AudioEngine::create(std::make_unique<NullDeviceOutput>());
    ASSERT_TRUE(engine.ok());
}

TEST(AudioEngineTest, PullMixEmpty) {
    auto engine = AudioEngine::create(std::make_unique<NullDeviceOutput>());
    ASSERT_TRUE(engine.ok());

    ASSERT_TRUE(engine->start(stereo()).ok());

    AudioBuffer output;
    auto res = engine->pullMix(output, 100);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(output.sampleCount, 100u);

    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_NEAR(output.channelData(0)[i], 0.0f, 1e-6f);
    }
}

TEST(AudioEngineTest, PushPullRoundTrip) {
    auto engine = AudioEngine::create(std::make_unique<NullDeviceOutput>());
    ASSERT_TRUE(engine.ok());
    ASSERT_TRUE(engine->start(stereo()).ok());

    auto input = makeSine(100, 2, 0.5f);
    EXPECT_TRUE(engine->pushSamples(0, std::move(input)));

    AudioBuffer output;
    auto res = engine->pullMix(output, 100);
    ASSERT_TRUE(res.ok());

    float maxVal = 0;
    for (uint32_t i = 0; i < 100; ++i) {
        maxVal = std::max(maxVal, std::abs(output.channelData(0)[i]));
    }
    EXPECT_GT(maxVal, 0.0f);
}

} // namespace
