#include <bl_core/plugin_loader.hpp>

#include <bl_plugins/codec_plugin.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

#ifndef BL_DYNLIB_SUFFIX
#define BL_DYNLIB_SUFFIX ".so"
#endif

namespace {

namespace fs = std::filesystem;

using bl::Err;
using bl::PluginHandle;
using bl::PluginLoader;
using bl::PluginOrigin;

fs::path pluginDir() {
    return fs::path(BL_TEST_PLUGIN_DIR) / "plugins";
}

std::string goodPluginPath() {
    return (pluginDir() / "libfixturegood").string() + BL_DYNLIB_SUFFIX;
}

void* hostAlloc(size_t size, void*) { return std::malloc(size); }

void hostFree(void* ptr, void*) { std::free(ptr); }

BlHostApi makeHostApi() {
    BlHostApi api;
    api.host_abi_version = BL_PLUGIN_ABI_VERSION;
    api.alloc = &hostAlloc;
    api.free = &hostFree;
    api.userdata = nullptr;
    return api;
}

TEST(PluginLoaderTest, LoadGoodPluginFromDisk) {
    PluginLoader loader;
    auto result = loader.load(goodPluginPath(), PluginOrigin::User);
    ASSERT_TRUE(result.ok()) << result.message();

    const BlCodecPlugin* plugin = (*result)->plugin();
    EXPECT_STREQ(plugin->name, "fixturegood");
    EXPECT_EQ(plugin->type, BL_CODEC_VIDEO);
    EXPECT_NE(plugin->caps.roles & BL_ROLE_DECODE, 0u);
    EXPECT_NE(plugin->caps.roles & BL_ROLE_ENCODE, 0u);
}

TEST(PluginLoaderTest, InitDecodeEncodeFlushCleanupRoundTrip) {
    PluginLoader loader;
    auto handleResult = loader.load(goodPluginPath(), PluginOrigin::User);
    ASSERT_TRUE(handleResult.ok()) << handleResult.message();
    const BlCodecPlugin* plugin = (*handleResult)->plugin();

    BlHostApi api = makeHostApi();

    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.video.width = 64;
    cfg.video.height = 32;
    cfg.video.fps.num = 24000;
    cfg.video.fps.den = 1001;
    cfg.video.pix_fmt = BL_PIXFMT_BGRA32;
    cfg.host = &api;

    void* ctx = nullptr;
    ASSERT_EQ(plugin->init(&ctx, &cfg), BL_OK);
    ASSERT_NE(ctx, nullptr);

    const uint8_t packet[] = {1, 2, 3, 4, 5};
    uint8_t* out = nullptr;
    size_t outSize = 0;
    BlFrameMeta meta{};
    int rc = plugin->decode(ctx, packet, sizeof(packet), &out, &outSize, &meta);
    ASSERT_EQ(rc, BL_OK);
    ASSERT_EQ(outSize, sizeof(packet));
    EXPECT_EQ(std::memcmp(out, packet, sizeof(packet)), 0);
    EXPECT_EQ(meta.pts, 1u);
    api.free(out, nullptr);

    uint8_t* encoded = nullptr;
    size_t encodedSize = 0;
    rc = plugin->encode(ctx, packet, sizeof(packet), &encoded, &encodedSize,
                        &meta);
    ASSERT_EQ(rc, BL_OK);
    EXPECT_EQ(encodedSize, sizeof(packet));
    api.free(encoded, nullptr);

    uint8_t* flushed = nullptr;
    size_t flushedSize = 0;
    EXPECT_EQ(plugin->flush(ctx, &flushed, &flushedSize), BL_OK);
    EXPECT_EQ(flushed, nullptr);
    EXPECT_EQ(flushedSize, 0u);

    plugin->cleanup(ctx);
}

TEST(PluginLoaderTest, InitRejectsMissingHostApi) {
    PluginLoader loader;
    auto handleResult = loader.load(goodPluginPath(), PluginOrigin::User);
    ASSERT_TRUE(handleResult.ok());

    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.host = nullptr;

    void* ctx = nullptr;
    EXPECT_EQ((*handleResult)->plugin()->init(&ctx, &cfg),
              BL_ERR_INVALID_ARGUMENT);
}

TEST(PluginLoaderTest, RejectsWrongAbiVersion) {
    fs::path badPath =
        pluginDir() / ("libfixturebadabi" BL_DYNLIB_SUFFIX);
    PluginLoader loader;
    auto result = loader.load(badPath.string(), PluginOrigin::System);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::PluginAbiMismatch);
    EXPECT_NE(result.message().find("999"), std::string::npos);
}

TEST(PluginLoaderTest, RejectsMissingEntryPoint) {
    PluginLoader loader;
    auto result = loader.load(
        (pluginDir() / ("libfixturenoentry" BL_DYNLIB_SUFFIX)).string(),
        PluginOrigin::System);
    if (!result.ok()) {
        EXPECT_TRUE(result.code() == Err::InvalidArgument ||
                    result.code() == Err::IoError)
            << result.message();
        return;
    }
    FAIL() << "library without entry point must not load";
}

TEST(PluginLoaderTest, MissingFileReportsFileNotFound) {
    PluginLoader loader;
    auto result = loader.load("/nonexistent/path/libnothing.so",
                              PluginOrigin::System);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::FileNotFound);
}

TEST(PluginLoaderTest, ScanDiscoversGoodAndSkipsBad) {
    PluginLoader loader;
    auto result = loader.scanDirectory(pluginDir().string(),
                                       PluginOrigin::User);
    ASSERT_TRUE(result.ok());

    bool foundGood = false;
    for (const auto& handle : result.value().plugins) {
        if (std::string(handle->plugin()->name) == "fixturegood") {
            foundGood = true;
            EXPECT_EQ(handle->origin(), PluginOrigin::User);
        }
    }
    EXPECT_TRUE(foundGood);

    bool reportedBadAbi = false;
    for (const auto& skipped : result.value().skipped) {
        if (skipped.reason.find("ABI version") != std::string::npos ||
            skipped.path.find("badabi") != std::string::npos) {
            reportedBadAbi = true;
        }
    }
    EXPECT_TRUE(reportedBadAbi);
}

TEST(PluginLoaderTest, UserLocalOverridesSystemOnDuplicateName) {
    fs::path sysDir = fs::temp_directory_path() /
                      ("bl_sys_" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    fs::path userDir = fs::temp_directory_path() /
                       ("bl_usr_" +
                        std::to_string(std::chrono::steady_clock::now()
                                           .time_since_epoch()
                                           .count()) +
                        "_x");
    fs::create_directories(sysDir / "video");
    fs::create_directories(userDir / "video");

    fs::path source =
        pluginDir() / ("libfixturegood" BL_DYNLIB_SUFFIX);
    ASSERT_TRUE(fs::exists(source)) << source;

    fs::copy_file(source,
                  sysDir / "video" / ("liba" BL_DYNLIB_SUFFIX),
                  fs::copy_options::overwrite_existing);
    fs::copy_file(source,
                  userDir / "video" / ("libb" BL_DYNLIB_SUFFIX),
                  fs::copy_options::overwrite_existing);

    PluginLoader loader;
    auto result =
        loader.scanDirectories({{sysDir.string(), PluginOrigin::System},
                                {userDir.string(), PluginOrigin::User}});
    ASSERT_TRUE(result.ok());
    const auto& report = result.value();

    ASSERT_EQ(report.plugins.size(), 1u);
    EXPECT_STREQ(report.plugins[0]->plugin()->name, "fixturegood");
    EXPECT_EQ(report.plugins[0]->origin(), PluginOrigin::User);

    bool overrideRecorded = false;
    for (const auto& skipped : report.skipped) {
        if (skipped.reason.find("overridden") != std::string::npos) {
            overrideRecorded = true;
        }
    }
    EXPECT_TRUE(overrideRecorded);

    std::error_code ec;
    fs::remove_all(sysDir, ec);
    fs::remove_all(userDir, ec);
}

TEST(PluginLoaderTest, ScanDefaultLocationsSucceeds) {
    PluginLoader loader;
    auto result = loader.scanDefaultLocations();
    EXPECT_TRUE(result.ok());
}

} // namespace
