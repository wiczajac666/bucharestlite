#include <bl_render/media_bin_thumbnail_engine.hpp>
#include <bl_render/thumbnail.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>

#include <bl_plugins/codec_plugin.h>

#include "test_media_utils.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

namespace {

using bl::CodecRegistry;
using bl::MediaBinThumbnailEngine;
using bl::MediaThumbnail;
using bl::PluginHandle;
using bl::PluginLoader;
using bl::PluginOrigin;
using bl::StreamInfo;

// Builds a registry with passthrough builtins plus the staged codec plugins.
// The handles must outlive every use of the registry, so they are kept alive
// for the process lifetime here.
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

std::vector<std::pair<std::string, PluginOrigin>> pluginDirs() {
    return {{std::string(BL_TEST_PLUGIN_DIR) + "/plugins", PluginOrigin::User}};
}

// Spins until the engine has no in-flight decode for `id` (or the deadline
// elapses, which fails the assertion in the caller).
bool waitForIdle(const MediaBinThumbnailEngine& engine, const std::string& id) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (engine.isLoading(id)) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

} // namespace

TEST(MediaThumbnailTest, ReadsFirstFrameOfVideoFixture) {
    auto thumb =
        bl::readThumbnail(bltest::mediaPath("test_video.mp4"), registry(), 64);
    ASSERT_TRUE(thumb.ok()) << thumb.message();
    const MediaThumbnail& t = thumb.value();

    EXPECT_TRUE(t.hasVideo);
    ASSERT_TRUE(t.hasPixels());
    EXPECT_LE(t.width, 64u);
    EXPECT_GT(t.height, 0u);
    EXPECT_EQ(t.linesize, t.width * 4u);
    EXPECT_EQ(t.pixels.size(), static_cast<size_t>(t.linesize) * t.height);
    EXPECT_FALSE(t.info.videoStreams.empty());
    EXPECT_GT(t.info.duration.toSeconds(), 0.0);
}

TEST(MediaThumbnailTest, ScaledThumbnailKeepsAspectRatio) {
    // The fixture is 320x240 (4:3); a 64px limit must yield 64x48.
    auto thumb =
        bl::readThumbnail(bltest::mediaPath("test_video.mp4"), registry(), 64);
    ASSERT_TRUE(thumb.ok()) << thumb.message();
    EXPECT_EQ(thumb.value().width, 64u);
    EXPECT_EQ(thumb.value().height, 48u);
}

TEST(MediaThumbnailTest, AudioOnlyHasMetadataButNoPixels) {
    auto thumb =
        bl::readThumbnail(bltest::mediaPath("test_audio.wav"), registry(), 96);
    ASSERT_TRUE(thumb.ok()) << thumb.message();
    const MediaThumbnail& t = thumb.value();

    EXPECT_FALSE(t.hasVideo);
    EXPECT_FALSE(t.hasPixels());
    ASSERT_FALSE(t.info.audioStreams.empty());
    EXPECT_GT(t.info.audioStreams.front().sampleRate, 0u);
}

TEST(MediaThumbnailTest, MissingFileFails) {
    auto thumb = bl::readThumbnail(bltest::mediaPath("nope.mp4"), registry(), 96);
    EXPECT_FALSE(thumb.ok());
}

TEST(MediaThumbnailTest, RejectsEmptyPathAndZeroWidth) {
    EXPECT_FALSE(bl::readThumbnail("", registry(), 96).ok());
    EXPECT_FALSE(
        bl::readThumbnail(bltest::mediaPath("test_video.mp4"), registry(), 0)
            .ok());
}

TEST(MediaThumbnailTest, DescribeMediaFormatsVideoAndAudio) {
    StreamInfo video;
    video.duration = bl::Duration::fromSeconds(83, {1'000'000, 1});
    bl::VideoStreamInfo v;
    v.width = 1920;
    v.height = 1080;
    v.codecName = "h264";
    video.videoStreams.push_back(v);
    EXPECT_EQ(bl::describeMedia(video), "00:01:23 · 1920×1080 · h264");

    StreamInfo audio;
    audio.duration = bl::Duration::fromSeconds(1, {1'000'000, 1});
    bl::AudioStreamInfo a;
    a.sampleRate = 48000;
    a.channels = 2;
    a.codecName = "flac";
    audio.audioStreams.push_back(a);
    EXPECT_EQ(bl::describeMedia(audio), "00:00:01 · 48 kHz · stereo · flac");

    EXPECT_EQ(bl::describeMedia(StreamInfo{}), "unknown media");
}

TEST(MediaBinThumbnailEngineTest, DecodesAndCachesVideoThumbnail) {
    MediaBinThumbnailEngine engine(1);
    engine.configure(pluginDirs());

    const std::string id = "engine-video";
    engine.request(id, bltest::mediaPath("test_video.mp4"));
    ASSERT_TRUE(waitForIdle(engine, id)) << "decode did not settle in time";

    auto thumb = engine.thumbnailFor(id);
    ASSERT_NE(thumb, nullptr);
    EXPECT_TRUE(thumb->hasPixels());

    const auto ids = engine.mediaIds();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids.front(), id);

    // Idempotent: a second request neither re-enqueues nor loses the cache.
    engine.request(id, bltest::mediaPath("test_video.mp4"));
    EXPECT_FALSE(engine.isLoading(id));
    EXPECT_NE(engine.thumbnailFor(id), nullptr);
}

TEST(MediaBinThumbnailEngineTest, CachesAudioOnlyMetadata) {
    MediaBinThumbnailEngine engine(1);
    engine.configure(pluginDirs());

    const std::string id = "engine-audio";
    engine.request(id, bltest::mediaPath("test_audio.wav"));
    ASSERT_TRUE(waitForIdle(engine, id)) << "probe did not settle in time";

    auto thumb = engine.thumbnailFor(id);
    ASSERT_NE(thumb, nullptr);
    EXPECT_FALSE(thumb->hasPixels());
    EXPECT_FALSE(thumb->info.audioStreams.empty());
}

TEST(MediaBinThumbnailEngineTest, MissingFileSettlesWithoutCaching) {
    MediaBinThumbnailEngine engine(1);
    engine.configure(pluginDirs());

    const std::string id = "engine-missing";
    engine.request(id, bltest::mediaPath("nope.mp4"));
    ASSERT_TRUE(waitForIdle(engine, id)) << "failure did not settle in time";

    EXPECT_EQ(engine.thumbnailFor(id), nullptr);
    EXPECT_TRUE(engine.mediaIds().empty());
}

TEST(MediaBinThumbnailEngineTest, RemoveAndClearDropCachedEntries) {
    MediaBinThumbnailEngine engine(1);
    engine.configure(pluginDirs());

    const std::string id = "engine-remove";
    engine.request(id, bltest::mediaPath("test_video.mp4"));
    ASSERT_TRUE(waitForIdle(engine, id));
    ASSERT_NE(engine.thumbnailFor(id), nullptr);

    engine.remove(id);
    EXPECT_EQ(engine.thumbnailFor(id), nullptr);
    EXPECT_TRUE(engine.mediaIds().empty());

    // Re-request after remove re-enqueues (the cache entry was dropped).
    engine.request(id, bltest::mediaPath("test_video.mp4"));
    engine.clear();
    EXPECT_EQ(engine.thumbnailFor(id), nullptr);
    EXPECT_TRUE(engine.mediaIds().empty());
}
