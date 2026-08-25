#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>

#include <bl_plugins/codec_plugin.h>
#include <plugins/passthrough/passthrough_plugin.h>
#include "test_plugin_utils.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

#ifndef BL_DYNLIB_SUFFIX
#define BL_DYNLIB_SUFFIX ".so"
#endif

namespace {

namespace fs = std::filesystem;

using bl::CodecRegistry;
using bl::CodecRole;
using bl::PluginLoader;
using bl::PluginOrigin;
using bltest::makePlugin;

TEST(ThreadSafetyTest, RegistryConcurrentWritersAndReaders) {
    CodecRegistry registry;

    constexpr int kWriters = 8;
    constexpr int kPerWriter = 40;
    constexpr int kReaderIterations = 4000;

    std::atomic<bool> go{false};
    std::atomic<int> registerFailures{0};
    std::atomic<int> registeredCount{0};

    std::vector<std::unique_ptr<BlCodecPlugin>> owned;
    owned.reserve(kWriters * kPerWriter);
    std::vector<std::string> names;
    names.reserve(kWriters * kPerWriter);
    for (int w = 0; w < kWriters; ++w) {
        for (int i = 0; i < kPerWriter; ++i) {
            names.emplace_back("plugin_" + std::to_string(w) + "_" +
                               std::to_string(i));
            owned.emplace_back(new BlCodecPlugin(makePlugin(
                names.back().c_str(), BL_CODEC_VIDEO,
                BL_ROLE_DECODE | BL_ROLE_ENCODE)));
        }
    }

    std::vector<std::thread> writers;
    for (int w = 0; w < kWriters; ++w) {
        writers.emplace_back([&, w] {
            for (int i = 0; i < kPerWriter; ++i) {
                BlCodecPlugin* plugin = owned[w * kPerWriter + i].get();
                while (!go.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                if (registry.registerPlugin(plugin).ok()) {
                    registeredCount.fetch_add(1, std::memory_order_relaxed);
                } else {
                    registerFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    std::atomic<bool> readersDone{false};
    std::atomic<int> readerFailures{0};
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&] {
            while (!readersDone.load(std::memory_order_acquire)) {
                size_t n = registry.count();
                if (n > kWriters * kPerWriter) {
                    readerFailures.fetch_add(1, std::memory_order_relaxed);
                }
                BlCodecPlugin* probe = registry.find("plugin_0_0");
                if (probe && std::string(probe->name) != "plugin_0_0") {
                    readerFailures.fetch_add(1, std::memory_order_relaxed);
                }
                registry.byType(BL_CODEC_AUDIO);
                registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Export);
            }
        });
    }

    go.store(true, std::memory_order_release);
    for (auto& t : writers) t.join();
    readersDone.store(true, std::memory_order_release);
    for (auto& t : readers) t.join();

    EXPECT_EQ(registerFailures.load(), 0);
    EXPECT_EQ(registeredCount.load(), kWriters * kPerWriter);
    EXPECT_EQ(readerFailures.load(), 0);
    EXPECT_EQ(registry.count(), static_cast<size_t>(kWriters * kPerWriter));

    for (int w = 0; w < kWriters; ++w) {
        for (int i = 0; i < kPerWriter; ++i) {
            std::string name =
                "plugin_" + std::to_string(w) + "_" + std::to_string(i);
            EXPECT_NE(registry.find(name), nullptr) << name;
        }
    }
}

TEST(ThreadSafetyTest, DuplicateNameRaceAllowsExactlyOneWinner) {
    CodecRegistry registry;
    constexpr int kThreads = 8;

    std::vector<std::unique_ptr<BlCodecPlugin>> owned;
    for (int i = 0; i < kThreads; ++i) {
        owned.emplace_back(new BlCodecPlugin(makePlugin(
            "contested", BL_CODEC_AUDIO, BL_ROLE_DECODE | BL_ROLE_ENCODE)));
    }

    std::atomic<int> successes{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&registry, &owned, &successes, i] {
            if (registry.registerPlugin(owned[i].get()).ok()) {
                successes.fetch_add(1);
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(successes.load(), 1);
    EXPECT_EQ(registry.count(), 1u);
}

TEST(ThreadSafetyTest, PassthroughAccessorsRaceFree) {
    constexpr int kThreads = 16;
    constexpr int kCalls = 2000;

    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&mismatches] {
            for (int i = 0; i < kCalls; ++i) {
                const BlCodecPlugin* v = bl_passthrough_video_plugin();
                const BlCodecPlugin* a = bl_passthrough_audio_plugin();
                if (std::string(v->name) != "passthrough.video" ||
                    v->abi_version != BL_PLUGIN_ABI_VERSION ||
                    std::string(a->name) != "passthrough.audio") {
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(mismatches.load(), 0);
}

TEST(ThreadSafetyTest, ParallelIndependentPluginLoads) {
    fs::path source =
        fs::path(BL_TEST_PLUGIN_DIR) / "plugins" /
        ("libfixturegood" BL_DYNLIB_SUFFIX);
    ASSERT_TRUE(fs::exists(source));

    constexpr int kCopies = 4;
    std::vector<fs::path> copies;
    fs::path dir = fs::temp_directory_path() /
                   ("bl_parload_" +
                    std::to_string(std::chrono::steady_clock::now()
                                       .time_since_epoch()
                                       .count()));
    fs::create_directories(dir);
    for (int i = 0; i < kCopies; ++i) {
        fs::path dst = dir / ("copy_" + std::to_string(i) + BL_DYNLIB_SUFFIX);
        fs::copy_file(source, dst, fs::copy_options::overwrite_existing);
        copies.push_back(dst);
    }

    PluginLoader loader;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kCopies);
    for (int i = 0; i < kCopies; ++i) {
        threads.emplace_back([&loader, &copies, &failures, i] {
            auto result = loader.load(copies[i].string(), PluginOrigin::User);
            if (!result.ok()) {
                failures.fetch_add(1);
                return;
            }
            if (std::string((*result)->plugin()->name) != "fixturegood") {
                failures.fetch_add(1);
            }
        });
    }
    for (auto& t : threads) t.join();

    std::error_code ec;
    fs::remove_all(dir, ec);
    EXPECT_EQ(failures.load(), 0);
}

} // namespace
