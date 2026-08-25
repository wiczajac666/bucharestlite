#include <bl_core/builtin_plugins.hpp>
#include <bl_core/codec_registry.hpp>

#include <bl_plugins/codec_plugin.h>
#include <plugins/passthrough/passthrough_plugin.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

using bl::CodecRegistry;
using bl::CodecRole;
using bl::Err;

void* testAlloc(size_t size, void*) { return std::malloc(size); }

void testFree(void* ptr, void*) { std::free(ptr); }

BlHostApi makeHostApi() {
    BlHostApi api;
    api.host_abi_version = BL_PLUGIN_ABI_VERSION;
    api.alloc = &testAlloc;
    api.free = &testFree;
    api.userdata = nullptr;
    return api;
}

TEST(BuiltinsTest, RegistersBothVariants) {
    CodecRegistry registry;
    ASSERT_TRUE(bl::registerBuiltins(registry).ok());
    EXPECT_EQ(registry.count(), 2u);

    const BlCodecPlugin* video = registry.find("passthrough.video");
    ASSERT_NE(video, nullptr);
    EXPECT_EQ(video->type, BL_CODEC_VIDEO);
    EXPECT_NE(video->caps.flags & BL_FLAG_PASSTHROUGH, 0u);
    EXPECT_EQ(video->abi_version, BL_PLUGIN_ABI_VERSION);

    const BlCodecPlugin* audio = registry.find("passthrough.audio");
    ASSERT_NE(audio, nullptr);
    EXPECT_EQ(audio->type, BL_CODEC_AUDIO);
    EXPECT_NE(audio->caps.flags & BL_FLAG_PASSTHROUGH, 0u);

    EXPECT_EQ(registry.byType(BL_CODEC_VIDEO).size(), 1u);
    EXPECT_EQ(registry.byType(BL_CODEC_AUDIO).size(), 1u);
}

TEST(BuiltinsTest, DuplicateRegistrationRejected) {
    CodecRegistry registry;
    ASSERT_TRUE(bl::registerBuiltins(registry).ok());
    auto second = bl::registerBuiltins(registry);
    ASSERT_FALSE(second.ok());
    EXPECT_EQ(second.code(), Err::RegistryDuplicate);
}

TEST(BuiltinsTest, ExcludedFromDefaultFor) {
    CodecRegistry registry;
    ASSERT_TRUE(bl::registerBuiltins(registry).ok());

    EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Preview), nullptr);
    EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Export), nullptr);
    EXPECT_EQ(registry.defaultFor(BL_CODEC_AUDIO, CodecRole::Preview), nullptr);
    EXPECT_EQ(registry.defaultFor(BL_CODEC_AUDIO, CodecRole::Export), nullptr);

    static BlCodecPlugin normal = [] {
        BlCodecPlugin p{};
        p.abi_version = BL_PLUGIN_ABI_VERSION;
        p.name = "normalcodec";
        p.description = "regular codec";
        p.type = BL_CODEC_VIDEO;
        p.caps.roles = BL_ROLE_DECODE | BL_ROLE_ENCODE;
        return p;
    }();
    normal.init = [](void** ctx, const BlCodecConfig*) -> int {
        *ctx = nullptr;
        return BL_OK;
    };
    normal.cleanup = [](void*) {};
    normal.encode = [](void*, const uint8_t*, size_t, uint8_t** out,
                       size_t* out_size, const BlFrameMeta*) -> int {
        *out = nullptr;
        *out_size = 0;
        return BL_OK;
    };
    normal.decode = [](void*, const uint8_t*, size_t, uint8_t**, size_t*,
                       BlFrameMeta*) -> int { return BL_OK; };

    ASSERT_TRUE(registry.registerPlugin(&normal).ok());
    for (CodecRole role : {CodecRole::Preview, CodecRole::Export}) {
        EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, role), &normal);
    }
}

class PassthroughRoundTrip : public ::testing::TestWithParam<const BlCodecPlugin*> {
};

TEST_P(PassthroughRoundTrip, CopiesPacketsIdentically) {
    const BlCodecPlugin* plugin = GetParam();
    ASSERT_NE(plugin, nullptr);

    BlHostApi api = makeHostApi();

    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.codec_name = plugin->type == BL_CODEC_VIDEO ? "h264" : "aac";
    static const uint8_t kExtra[] = {0xAA, 0xBB, 0xCC};
    cfg.extradata = kExtra;
    cfg.extradata_size = sizeof(kExtra);
    cfg.host = &api;

    void* ctx = nullptr;
    ASSERT_EQ(plugin->init(&ctx, &cfg), BL_OK);
    ASSERT_NE(ctx, nullptr);

    const uint8_t pktA[] = {1, 2, 3, 4, 5, 6, 7};
    const uint8_t pktB[] = {0xDE, 0xAD};

    struct PacketCase {
        const uint8_t* data;
        size_t size;
        uint64_t pts;
    };
    const std::vector<PacketCase> cases{
        {pktA, sizeof(pktA), 42}, {pktB, sizeof(pktB), 43}};

    for (const auto& c : cases) {
        uint8_t* out = nullptr;
        size_t outSize = 0;
        BlFrameMeta meta{};
        meta.pts = c.pts;
        meta.keyframe = c.pts == 42;
        int rc = plugin->encode(ctx, c.data, c.size, &out, &outSize, &meta);
        ASSERT_EQ(rc, BL_OK);
        ASSERT_EQ(outSize, c.size);
        EXPECT_EQ(std::memcmp(out, c.data, c.size), 0);
        if (outSize) testFree(out, nullptr);
    }

    uint8_t* flushed = nullptr;
    size_t flushedSize = 1;
    EXPECT_EQ(plugin->flush(ctx, &flushed, &flushedSize), BL_OK);
    EXPECT_EQ(flushed, nullptr);
    EXPECT_EQ(flushedSize, 0u);

    plugin->cleanup(ctx);
}

INSTANTIATE_TEST_SUITE_P(
    BothVariants, PassthroughRoundTrip,
    ::testing::Values(bl_passthrough_video_plugin(),
                      bl_passthrough_audio_plugin()));

TEST(PassthroughEdgeTest, InitRejectsMissingHostAndBadAbi) {
    const BlCodecPlugin* video = bl_passthrough_video_plugin();

    BlCodecConfig noHost{};
    noHost.abi_version = BL_PLUGIN_ABI_VERSION;
    noHost.host = nullptr;
    void* ctx = nullptr;
    EXPECT_EQ(video->init(&ctx, &noHost), BL_ERR_INVALID_ARGUMENT);

    BlHostApi api = makeHostApi();
    BlCodecConfig badAbi{};
    badAbi.abi_version = BL_PLUGIN_ABI_VERSION + 100u;
    badAbi.host = &api;
    EXPECT_EQ(video->init(&ctx, &badAbi), BL_ERR_PLUGIN_ABI_MISMATCH);
}

TEST(PassthroughEdgeTest, EncodeRejectsNullOutputsAndBadInput) {
    BlHostApi api = makeHostApi();
    BlCodecConfig cfg{};
    cfg.abi_version = BL_PLUGIN_ABI_VERSION;
    cfg.host = &api;

    const BlCodecPlugin* audio = bl_passthrough_audio_plugin();
    void* ctx = nullptr;
    ASSERT_EQ(audio->init(&ctx, &cfg), BL_OK);

    const uint8_t data[] = {9, 8, 7};
    uint8_t* out = nullptr;
    size_t outSize = 0;
    BlFrameMeta meta{};

    EXPECT_EQ(audio->encode(nullptr, data, sizeof(data), &out, &outSize, &meta),
              BL_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(audio->encode(ctx, data, sizeof(data), nullptr, &outSize, &meta),
              BL_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(audio->encode(ctx, nullptr, sizeof(data), &out, &outSize, &meta),
              BL_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(audio->encode(ctx, data, sizeof(data), &out, nullptr, &meta),
              BL_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(audio->flush(nullptr, &out, &outSize), BL_ERR_INVALID_ARGUMENT);

    audio->cleanup(ctx);
}

} // namespace
