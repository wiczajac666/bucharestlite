#include <bl_render/thumbnail.hpp>
#include <bl_render/timeline_clip_thumbnail_engine.hpp>

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>

#include <bl_plugins/codec_plugin.h>

#include "test_media_utils.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

namespace {

using bl::ClipThumbnailKey;
using bl::CodecRegistry;
using bl::MediaThumbnail;
using bl::PluginHandle;
using bl::PluginLoader;
using bl::PluginOrigin;
using bl::Time;
using bl::TimelineClipThumbnailEngine;
using bl::clipThumbnailCellCount;
using bl::clipThumbnailSampleTimes;
using bl::readThumbnailAt;

const bl::Rational kRate{1'000'000, 1};

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

Time seconds(double value) { return Time::fromSeconds(value, kRate); }

bool waitForIdle(const TimelineClipThumbnailEngine& engine,
                 const std::string& id, Time time, uint32_t width = 96) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (engine.isLoading(id, time, width)) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

bool pixelsDiffer(const MediaThumbnail& a, const MediaThumbnail& b) {
    if (!a.hasPixels() || !b.hasPixels()) return false;
    if (a.pixels.size() != b.pixels.size()) return true;
    for (size_t i = 0; i < a.pixels.size(); ++i) {
        if (a.pixels[i] != b.pixels[i]) return true;
    }
    return false;
}

} // namespace

TEST(ClipThumbnailHelpers, CellCountClampsToZoomRange) {
    EXPECT_EQ(clipThumbnailCellCount(0.0), 1u);
    EXPECT_EQ(clipThumbnailCellCount(10.0), 1u);
    EXPECT_EQ(clipThumbnailCellCount(64.0), 1u);
    EXPECT_EQ(clipThumbnailCellCount(128.0), 2u);
    EXPECT_EQ(clipThumbnailCellCount(320.0), 5u);
    EXPECT_EQ(clipThumbnailCellCount(10000.0), bl::kMaxStripCells);
}

TEST(ClipThumbnailHelpers, SampleTimesSpanClippedSourceWindow) {
    const Time in = seconds(2.0);
    const Time out = seconds(4.0);
    const auto times = clipThumbnailSampleTimes(in, out, 4);
    ASSERT_EQ(times.size(), 4u);
    EXPECT_EQ(times.front(), in);
    EXPECT_LT(times.back(), out);
    for (size_t i = 1; i < times.size(); ++i) {
        EXPECT_LT(times[i - 1], times[i]);
    }

    // A degenerate (zero-length) window still yields one sample per cell.
    const auto flat = clipThumbnailSampleTimes(in, in, 3);
    ASSERT_EQ(flat.size(), 3u);
    EXPECT_EQ(flat.front(), in);
    EXPECT_EQ(flat.back(), in);
}

TEST(ReadThumbnailAt, DecodesFrameAtRequestedSourceTime) {
    const std::string path = bltest::mediaPath("test_video.mp4");

    auto first = readThumbnailAt(path, seconds(0.0), registry(), 64);
    ASSERT_TRUE(first.ok()) << first.message();
    ASSERT_TRUE(first.value().hasPixels());
    EXPECT_LE(first.value().width, 64u);
    EXPECT_GT(first.value().height, 0u);

    auto later = readThumbnailAt(path, seconds(0.8), registry(), 64);
    ASSERT_TRUE(later.ok()) << later.message();
    ASSERT_TRUE(later.value().hasPixels());
    EXPECT_LE(later.value().width, 64u);

    // testsrc changes over time — a real seek produces different pixels than
    // always decoding the opening frame.
    EXPECT_TRUE(pixelsDiffer(first.value(), later.value()));
}

TEST(ReadThumbnailAt, RejectsNegativeSourceTime) {
    auto result = readThumbnailAt(bltest::mediaPath("test_video.mp4"),
                                  seconds(-0.1), registry(), 64);
    EXPECT_FALSE(result.ok());
}

TEST(TimelineClipThumbnailEngine, DecodesCachesAndNotifies) {
    TimelineClipThumbnailEngine engine(4);
    engine.configure(pluginDirs());

    std::mutex notifyMutex;
    std::vector<std::pair<std::string, Time>> settled;
    engine.onReady = [&](const std::string& id, Time time, uint32_t width) {
        EXPECT_EQ(width, 96u);
        std::lock_guard<std::mutex> lock(notifyMutex);
        settled.emplace_back(id, time);
    };

    const std::string path = bltest::mediaPath("test_video.mp4");
    const Time t0 = seconds(0.0);
    const Time t1 = seconds(0.5);
    engine.request("v", path, t0);
    engine.request("v", path, t1);
    ASSERT_TRUE(waitForIdle(engine, "v", t0)) << "t0 did not settle";
    ASSERT_TRUE(waitForIdle(engine, "v", t1)) << "t1 did not settle";

    auto a = engine.thumbnailFor("v", t0);
    auto b = engine.thumbnailFor("v", t1);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(a->hasPixels());
    EXPECT_TRUE(b->hasPixels());
    EXPECT_EQ(engine.cachedCount(), 2u);
    EXPECT_EQ(engine.pendingCount(), 0u);

    // Stable shared pointer and distinct keys.
    EXPECT_EQ(engine.thumbnailFor("v", t0), a);
    EXPECT_NE(a, b);

    std::lock_guard<std::mutex> lock(notifyMutex);
    EXPECT_GE(settled.size(), 2u);
}

TEST(TimelineClipThumbnailEngine, BoundedCacheEvictsOldest) {
    TimelineClipThumbnailEngine engine(2, 1);
    engine.configure(pluginDirs());
    const std::string path = bltest::mediaPath("test_video.mp4");

    const Time t0 = seconds(0.0);
    const Time t1 = seconds(0.2);
    const Time t2 = seconds(0.4);
    for (Time t : {t0, t1, t2}) {
        engine.request("v", path, t);
        ASSERT_TRUE(waitForIdle(engine, "v", t));
    }

    EXPECT_EQ(engine.maxEntries(), 2u);
    EXPECT_EQ(engine.cachedCount(), 2u);
    EXPECT_EQ(engine.thumbnailFor("v", t0), nullptr);
    EXPECT_NE(engine.thumbnailFor("v", t2), nullptr);
}

TEST(TimelineClipThumbnailEngine, AccessPromotesEntryBeforeEviction) {
    TimelineClipThumbnailEngine engine(2, 1);
    engine.configure(pluginDirs());
    const std::string path = bltest::mediaPath("test_video.mp4");

    const Time t0 = seconds(0.0);
    const Time t1 = seconds(0.2);
    const Time t2 = seconds(0.4);
    engine.request("v", path, t0);
    ASSERT_TRUE(waitForIdle(engine, "v", t0));
    engine.request("v", path, t1);
    ASSERT_TRUE(waitForIdle(engine, "v", t1));

    // Touch t0 so it becomes the most recently used entry.
    ASSERT_NE(engine.thumbnailFor("v", t0), nullptr);

    engine.request("v", path, t2);
    ASSERT_TRUE(waitForIdle(engine, "v", t2));
    EXPECT_NE(engine.thumbnailFor("v", t0), nullptr);
    EXPECT_EQ(engine.thumbnailFor("v", t1), nullptr);
    EXPECT_EQ(engine.cachedCount(), 2u);
}

TEST(TimelineClipThumbnailEngine, RemoveAndRetainPruneEntries) {
    TimelineClipThumbnailEngine engine(4);
    engine.configure(pluginDirs());
    const std::string path = bltest::mediaPath("test_video.mp4");
    const Time t = seconds(0.1);

    engine.request("a", path, t);
    engine.request("b", path, t);
    ASSERT_TRUE(waitForIdle(engine, "a", t));
    ASSERT_TRUE(waitForIdle(engine, "b", t));
    ASSERT_EQ(engine.cachedCount(), 2u);

    engine.remove("a");
    EXPECT_EQ(engine.thumbnailFor("a", t), nullptr);
    EXPECT_NE(engine.thumbnailFor("b", t), nullptr);
    EXPECT_EQ(engine.cachedCount(), 1u);

    engine.retain({"b"});
    EXPECT_EQ(engine.cachedCount(), 1u);
    engine.retain({});
    EXPECT_EQ(engine.cachedCount(), 0u);
}

TEST(TimelineClipThumbnailEngine, RequestsAreNonBlockingAndOffCallerThread) {
    TimelineClipThumbnailEngine engine(1);
    engine.configure(pluginDirs());
    const std::string path = bltest::mediaPath("test_video.mp4");
    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> ranOnCaller{false};
    engine.onReady = [&](const std::string&, Time, uint32_t) {
        if (std::this_thread::get_id() == caller) {
            ranOnCaller.store(true);
        }
    };

    std::vector<Time> times;
    for (int i = 0; i < 12; ++i) {
        times.push_back(seconds(static_cast<double>(i) * 0.05));
    }
    const auto started = std::chrono::steady_clock::now();
    for (Time t : times) {
        engine.request("v", path, t);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;

    // enqueue() must not decode on the calling (UI) thread.
    EXPECT_LT(elapsed, std::chrono::seconds(2));
    EXPECT_GE(engine.pendingCount(), 1u);

    for (Time t : times) {
        ASSERT_TRUE(waitForIdle(engine, "v", t));
    }
    EXPECT_FALSE(ranOnCaller.load());
}

TEST(TimelineClipThumbnailEngine, MissingPathSettlesWithoutCaching) {
    TimelineClipThumbnailEngine engine(1);
    engine.configure(pluginDirs());
    const Time t = seconds(0.0);
    engine.request("missing", bltest::mediaPath("nope.mp4"), t);
    ASSERT_TRUE(waitForIdle(engine, "missing", t));
    EXPECT_EQ(engine.thumbnailFor("missing", t), nullptr);
    EXPECT_EQ(engine.cachedCount(), 0u);
    EXPECT_EQ(engine.pendingCount(), 0u);
}
