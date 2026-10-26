#include <bl_core/proxy.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <thread>

namespace {

using bl::JobManager;
using bl::ProxyConfig;
using bl::ProxyGenerator;
using bl::ProxyJobInput;
using bl::ProxyJobResult;
using bl::ProxyProgressFn;

TEST(ProxyGeneratorTest, ConfigDefaults) {
    ProxyConfig config;
    EXPECT_EQ(config.autoProxyMinWidth, 3840u);
    EXPECT_FLOAT_EQ(config.resolutionFactor, 0.5f);
    EXPECT_EQ(config.crf, 18);
    EXPECT_EQ(config.codec, "h264");
    EXPECT_EQ(config.maxConcurrentJobs, 2u);
    EXPECT_TRUE(config.enabled);
    EXPECT_TRUE(config.proxyDir.empty());
}

TEST(ProxyGeneratorTest, CanCreateGenerator) {
    JobManager jobManager(1);
    ProxyConfig config;
    config.enabled = true;
    config.maxConcurrentJobs = 1;

    ProxyGenerator generator(jobManager, config);
    EXPECT_EQ(generator.pendingCount(), 0u);
    EXPECT_EQ(generator.activeCount(), 0u);
}

TEST(ProxyGeneratorTest, ShouldAutoProxyReturnsFalseWhenDisabled) {
    JobManager jobManager(1);
    ProxyConfig config;
    config.enabled = false;

    ProxyGenerator generator(jobManager, config);

    bl::MediaBinItem item;
    item.id = "test";
    item.path = "/fake/path.mp4";
    item.sourceWidth = 3840;
    item.sourceHeight = 2160;
    item.proxyStatus = "none";

    EXPECT_FALSE(ProxyGenerator::shouldAutoProxy(item, config));
}

TEST(ProxyGeneratorTest, ShouldAutoProxyReturnsFalseWhenProxyExists) {
    JobManager jobManager(1);
    ProxyConfig config;
    config.enabled = true;

    ProxyGenerator generator(jobManager, config);

    bl::MediaBinItem item;
    item.id = "test";
    item.path = "/fake/path.mp4";
    item.sourceWidth = 3840;
    item.sourceHeight = 2160;
    item.proxyPath = "/fake/proxy.mp4";
    item.proxyStatus = "ready";

    EXPECT_FALSE(ProxyGenerator::shouldAutoProxy(item, config));
}

TEST(ProxyGeneratorTest, ShouldAutoProxyReturnsFalseWhenBelowThreshold) {
    JobManager jobManager(1);
    ProxyConfig config;
    config.enabled = true;
    config.autoProxyMinWidth = 3840;

    ProxyGenerator generator(jobManager, config);

    bl::MediaBinItem item;
    item.id = "test";
    item.path = "/fake/path.mp4";
    item.sourceWidth = 1920;  // Below 3840
    item.sourceHeight = 1080;
    item.proxyStatus = "none";

    EXPECT_FALSE(ProxyGenerator::shouldAutoProxy(item, config));
}

TEST(ProxyGeneratorTest, ShouldAutoProxyReturnsTrueFor4K) {
    JobManager jobManager(1);
    ProxyConfig config;
    config.enabled = true;
    config.autoProxyMinWidth = 3840;

    ProxyGenerator generator(jobManager, config);

    bl::MediaBinItem item;
    item.id = "test";
    item.path = "/fake/path.mp4";
    item.sourceWidth = 3840;
    item.sourceHeight = 2160;
    item.proxyStatus = "none";

    EXPECT_TRUE(ProxyGenerator::shouldAutoProxy(item, config));
}

TEST(ProxyGeneratorTest, ShouldAutoProxyReturnsFalseWhenGenerating) {
    JobManager jobManager(1);
    ProxyConfig config;
    config.enabled = true;

    ProxyGenerator generator(jobManager, config);

    bl::MediaBinItem item;
    item.id = "test";
    item.path = "/fake/path.mp4";
    item.sourceWidth = 3840;
    item.sourceHeight = 2160;
    item.proxyStatus = "generating";

    EXPECT_FALSE(ProxyGenerator::shouldAutoProxy(item, config));
}

TEST(ProxyGeneratorTest, CancelAllClearsJobs) {
    JobManager jobManager(1);
    ProxyConfig config;
    config.enabled = true;
    config.maxConcurrentJobs = 2;

    ProxyGenerator generator(jobManager, config);
    generator.cancelAll();
    EXPECT_EQ(generator.pendingCount(), 0u);
}

TEST(ProxyGeneratorTest, ProxyPathForUsesConfiguredDirectoryAndCodecExtension) {
    const auto dir =
        std::filesystem::temp_directory_path() / "bl-proxy-test-vp9";
    std::filesystem::remove_all(dir);

    ProxyConfig config;
    config.proxyDir = dir.string();
    config.codec = "vp9";

    const std::filesystem::path path(ProxyGenerator::proxyPathFor(
        config, "/project", "media 1"));
    EXPECT_EQ(path.parent_path(), dir);
    EXPECT_EQ(path.filename().string(), "media 1_proxy.webm");
    EXPECT_TRUE(std::filesystem::exists(dir));

    std::filesystem::remove_all(dir);
}

TEST(ProxyGeneratorTest, ProxyPathForDefaultsToProjectProxyDir) {
    const auto project =
        std::filesystem::temp_directory_path() / "bl-proxy-test-project";
    std::filesystem::remove_all(project);

    ProxyConfig config;
    config.proxyDir.clear();
    config.codec = "h264";

    const std::filesystem::path path(
        ProxyGenerator::proxyPathFor(config, project.string(), "m1"));
    EXPECT_EQ(path.parent_path(), project / "proxy");
    EXPECT_EQ(path.filename().string(), "m1_proxy.mp4");

    std::filesystem::remove_all(project);
}

TEST(ProxyGeneratorTest, ProxyPathForFallsBackToTempDir) {
    ProxyConfig config;
    config.proxyDir.clear();
    config.codec = "hevc";

    const std::filesystem::path path(
        ProxyGenerator::proxyPathFor(config, "", "m2"));
    EXPECT_EQ(path.parent_path(),
              std::filesystem::temp_directory_path() /
                  "bucharest-lite-proxy");
    EXPECT_EQ(path.filename().string(), "m2_proxy.mp4");
}

} // namespace