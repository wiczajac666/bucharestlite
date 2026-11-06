#include <bl_audio/playback_audio_source.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

#ifndef BL_TEST_MEDIA_DIR
#define BL_TEST_MEDIA_DIR "."
#endif

namespace {

using bl::CodecRegistry;
using bl::PlaybackAudioSource;
using bl::PluginHandle;
using bl::PluginLoader;
using bl::PluginOrigin;

// Same registry the other media tests use: passthrough builtins plus the
// staged codec plugins, with the handles kept alive for the process lifetime.
const CodecRegistry& registry() {
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
                if (loaded.ok()) out.push_back(std::move(handle));
            }
        }
        return out;
    }();
    (void)handles;
    return instance;
}

std::string media(const std::string& name) {
    return std::string(BL_TEST_MEDIA_DIR) + "/" + name;
}

float peakAbs(const bl::AudioBuffer& buffer) {
    float peak = 0.0f;
    for (float sample : buffer.data) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

#define OPEN_SOURCE(var, path)                                            \
    auto var##Opened = PlaybackAudioSource::open({(path), 48000, 2}, registry()); \
    ASSERT_TRUE(var##Opened.ok()) << var##Opened.message();               \
    auto var = std::move(var##Opened.value())

TEST(PlaybackAudioSourceTest, OpensStereoMediaAtTargetRate) {
    OPEN_SOURCE(source, media("test_audio_opus.opus"));
    EXPECT_EQ(source.channels(), 2u);
    EXPECT_EQ(source.sampleRate(), 48000u);

    bl::AudioBuffer buffer;
    const uint32_t produced = source.pull(buffer, 4800);
    EXPECT_EQ(produced, 4800u);
    EXPECT_GT(peakAbs(buffer), 1e-3f);
    EXPECT_NEAR(source.positionSeconds(), 0.1, 1e-3);
}

TEST(PlaybackAudioSourceTest, SeekMovesTheNextPull) {
    OPEN_SOURCE(source, media("test_audio_opus.opus"));

    bl::AudioBuffer first;
    ASSERT_EQ(source.pull(first, 4800), 4800u);

    ASSERT_TRUE(source.seekToSeconds(0.5).ok());
    EXPECT_NEAR(source.positionSeconds(), 0.5, 1e-6);

    bl::AudioBuffer afterSeek;
    ASSERT_EQ(source.pull(afterSeek, 4800), 4800u);
    EXPECT_GT(peakAbs(afterSeek), 1e-3f);
    EXPECT_NEAR(source.positionSeconds(), 0.6, 1e-3);
}

TEST(PlaybackAudioSourceTest, Resamples44kSourceTo48k) {
    auto opened = PlaybackAudioSource::open(
        {media("test_audio_44k.flac"), 48000, 2}, registry());
    ASSERT_TRUE(opened.ok()) << opened.message();
    auto source = std::move(opened.value());
    EXPECT_EQ(source.sampleRate(), 48000u);

    bl::AudioBuffer buffer;
    ASSERT_EQ(source.pull(buffer, 48000), 48000u);
    EXPECT_GT(peakAbs(buffer), 1e-3f);
    EXPECT_NEAR(source.positionSeconds(), 1.0, 1e-3);
}

TEST(PlaybackAudioSourceTest, UpmixesMonoAndPadsPastEnd) {
    OPEN_SOURCE(source, media("test_audio_beep.opus"));
    EXPECT_EQ(source.channels(), 2u);

    bl::AudioBuffer buffer;
    ASSERT_EQ(source.pull(buffer, 96000), 96000u);

    // First second is the tone; both output planes carry the mono source.
    float left = 0.0f;
    float right = 0.0f;
    for (uint32_t i = 0; i < 24000; ++i) {
        left = std::max(left, std::abs(buffer.channelData(0)[i]));
        right = std::max(right, std::abs(buffer.channelData(1)[i]));
    }
    EXPECT_GT(left, 1e-3f);
    EXPECT_FLOAT_EQ(left, right);

    // Second second is silence in the source.
    float tail = 0.0f;
    for (uint32_t i = 72000; i < 96000; ++i) {
        tail = std::max(tail, std::abs(buffer.channelData(0)[i]));
    }
    EXPECT_LT(tail, 1e-4f);
}

TEST(PlaybackAudioSourceTest, RejectsMissingMedia) {
    auto opened = PlaybackAudioSource::open(
        {media("does-not-exist.wav"), 48000, 2}, registry());
    EXPECT_FALSE(opened.ok());
}

} // namespace
