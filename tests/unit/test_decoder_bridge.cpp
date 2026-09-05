#include <bl_core/decoder_bridge.hpp>

#include <bl_core/codec_registry.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_plugins/codec_plugin.h>
#include "test_plugin_utils.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {

using bl::CodecRegistry;
using bl::DecoderBridge;
using bl::Err;
using bl::Frame;
using bl::Packet;

#ifndef BL_TEST_PLUGIN_DIR
#define BL_TEST_PLUGIN_DIR "."
#endif

#ifndef BL_DYNLIB_SUFFIX
#define BL_DYNLIB_SUFFIX ".so"
#endif

void* hostAlloc(size_t size, void*) { return std::malloc(size); }
void hostFree(void* ptr, void*) { std::free(ptr); }

BlHostApi makeHostApi() {
    BlHostApi api{};
    api.host_abi_version = BL_PLUGIN_ABI_VERSION;
    api.alloc = &hostAlloc;
    api.free = &hostFree;
    api.userdata = nullptr;
    return api;
}

BlCodecConfig makeConfig(const BlHostApi* host) {
    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.video.width = 320;
    cfg.video.height = 240;
    cfg.video.fps.num = 24000;
    cfg.video.fps.den = 1001;
    cfg.video.pix_fmt = BL_PIXFMT_BGRA32;
    cfg.host = host;
    return cfg;
}

struct DecoderBridgeTest : ::testing::Test {
    CodecRegistry registry;
    std::unique_ptr<bl::PluginHandle> handle;
    BlHostApi hostApi = makeHostApi();
    BlCodecConfig config = makeConfig(&hostApi);

    void SetUp() override {
        bl::PluginLoader loader;
        std::string path = std::string(BL_TEST_PLUGIN_DIR) + "/plugins/libfixturegood" + BL_DYNLIB_SUFFIX;
        auto result = loader.load(path, bl::PluginOrigin::User);
        ASSERT_TRUE(result.ok()) << result.message();
        handle = std::move(result.value());

        auto regResult = registry.registerPlugin(
            const_cast<BlCodecPlugin*>(handle->plugin()));
        ASSERT_TRUE(regResult.ok()) << regResult.message();
    }
};

TEST_F(DecoderBridgeTest, CreateWithValidCodec) {
    auto result = DecoderBridge::create(registry, "fixturegood", config);
    ASSERT_TRUE(result.ok()) << result.message();
    EXPECT_EQ(result.value().codecName(), "fixturegood");
}

TEST_F(DecoderBridgeTest, CreateWithMissingCodec) {
    auto result = DecoderBridge::create(registry, "nonexistent", config);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::InvalidArgument);
}

TEST_F(DecoderBridgeTest, CreateWithEncodeOnlyPlugin) {
    CodecRegistry encodeOnlyRegistry;

    BlCodecPlugin encodeOnly = bltest::makePlugin("enconly", BL_CODEC_VIDEO,
                                                   BL_ROLE_ENCODE);
    ASSERT_TRUE(encodeOnlyRegistry.registerPlugin(&encodeOnly).ok());

    auto result = DecoderBridge::create(encodeOnlyRegistry, "enconly", config);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::InvalidArgument);
}

TEST_F(DecoderBridgeTest, DecodeProducesFrame) {
    auto bridgeResult = DecoderBridge::create(registry, "fixturegood", config);
    ASSERT_TRUE(bridgeResult.ok()) << bridgeResult.message();
    DecoderBridge bridge = std::move(bridgeResult.value());

    Packet pkt;
    pkt.streamIndex = 0;
    pkt.data = {0xDE, 0xAD, 0xBE, 0xEF};
    pkt.hasPts = true;
    pkt.keyframe = true;

    auto frameResult = bridge.decode(pkt);
    ASSERT_TRUE(frameResult.ok()) << frameResult.message();

    Frame frame = std::move(frameResult.value());
    EXPECT_EQ(frame.streamIndex, 0);
    EXPECT_FALSE(frame.empty());
    EXPECT_EQ(frame.dataSize, 4u);
    EXPECT_EQ(std::memcmp(frame.data, pkt.data.data(), 4), 0);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_NE(frame.hostApi, nullptr);
}

TEST_F(DecoderBridgeTest, DecodeEmptyPacketReturnsError) {
    auto bridgeResult = DecoderBridge::create(registry, "fixturegood", config);
    ASSERT_TRUE(bridgeResult.ok());
    DecoderBridge bridge = std::move(bridgeResult.value());

    Packet pkt;
    pkt.streamIndex = 0;

    auto frameResult = bridge.decode(pkt);
    ASSERT_FALSE(frameResult.ok());
    EXPECT_EQ(frameResult.code(), Err::DecodeFailed);
}

TEST_F(DecoderBridgeTest, FlushReturnsNulloptWhenEmpty) {
    auto bridgeResult = DecoderBridge::create(registry, "fixturegood", config);
    ASSERT_TRUE(bridgeResult.ok());
    DecoderBridge bridge = std::move(bridgeResult.value());

    auto flushResult = bridge.flush();
    ASSERT_TRUE(flushResult.ok()) << flushResult.message();
    EXPECT_FALSE(flushResult.value().has_value());
}

TEST_F(DecoderBridgeTest, FrameDestructorFreesBuffer) {
    struct Counters {
        size_t allocs{0};
        size_t frees{0};
    } counters;

    BlHostApi countingHost{};
    countingHost.host_abi_version = BL_PLUGIN_ABI_VERSION;
    countingHost.userdata = &counters;
    countingHost.alloc = [](size_t size, void* ud) -> void* {
        auto* c = static_cast<Counters*>(ud);
        c->allocs++;
        return std::malloc(size);
    };
    countingHost.free = [](void* ptr, void* ud) {
        auto* c = static_cast<Counters*>(ud);
        c->frees++;
        std::free(ptr);
    };

    BlCodecConfig countingConfig = makeConfig(&countingHost);

    auto bridgeResult = DecoderBridge::create(registry, "fixturegood", countingConfig);
    ASSERT_TRUE(bridgeResult.ok());
    DecoderBridge bridge = std::move(bridgeResult.value());

    Packet pkt;
    pkt.streamIndex = 0;
    pkt.data = {1, 2, 3};

    {
        auto frameResult = bridge.decode(pkt);
        ASSERT_TRUE(frameResult.ok());
        Frame frame = std::move(frameResult.value());
        // allocs=2: plugin ctx (init) + decode output buffer
        EXPECT_EQ(counters.allocs, 2u);
        EXPECT_EQ(counters.frees, 0u);
    }
    // Frame destroyed: decode output freed, plugin ctx still alive
    EXPECT_EQ(counters.frees, 1u);
}

TEST_F(DecoderBridgeTest, MoveSemanticsTransfersOwnership) {
    auto bridgeResult = DecoderBridge::create(registry, "fixturegood", config);
    ASSERT_TRUE(bridgeResult.ok());
    DecoderBridge bridge = std::move(bridgeResult.value());

    Packet pkt;
    pkt.streamIndex = 0;
    pkt.data = {0xAA, 0xBB};

    auto frameResult = bridge.decode(pkt);
    ASSERT_TRUE(frameResult.ok());

    Frame original = std::move(frameResult.value());
    EXPECT_FALSE(original.empty());
    uint8_t* originalData = original.data;
    size_t originalSize = original.dataSize;

    Frame moved = std::move(original);
    EXPECT_TRUE(original.empty());
    EXPECT_FALSE(moved.empty());
    EXPECT_EQ(moved.data, originalData);
    EXPECT_EQ(moved.dataSize, originalSize);
}

} // namespace
