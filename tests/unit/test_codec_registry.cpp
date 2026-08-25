#include <bl_core/codec_registry.hpp>

#include <bl_plugins/codec_plugin.h>

#include <gtest/gtest.h>

#include <string>

namespace {

using bl::CodecRegistry;
using bl::CodecRole;
using bl::Err;

int trivialInit(void** ctx, const BlCodecConfig* cfg) {
    (void)cfg;
    *ctx = nullptr;
    return BL_OK;
}

void trivialCleanup(void*) {}

int trivialDecode(void*, const uint8_t*, size_t, uint8_t**, size_t*,
                  BlFrameMeta*) {
    return BL_OK;
}

int trivialEncode(void*, const uint8_t*, size_t, uint8_t**, size_t*,
                  const BlFrameMeta*) {
    return BL_OK;
}

BlCodecPlugin makePlugin(const char* name, unsigned char type, uint32_t roles,
                         uint32_t flags = 0) {
    BlCodecPlugin p{};
    p.abi_version = BL_PLUGIN_ABI_VERSION;
    p.name = name;
    p.description = "test plugin";
    p.type = type;
    p.caps.roles = roles;
    p.caps.flags = flags;
    p.init = &trivialInit;
    p.cleanup = &trivialCleanup;
    if (roles & BL_ROLE_DECODE) p.decode = &trivialDecode;
    if (roles & BL_ROLE_ENCODE) p.encode = &trivialEncode;
    return p;
}

TEST(CodecRegistryTest, RegisterFindAndCount) {
    CodecRegistry registry;
    BlCodecPlugin video = makePlugin("h264", BL_CODEC_VIDEO,
                                     BL_ROLE_DECODE | BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&video).ok());
    EXPECT_EQ(registry.count(), 1u);
    EXPECT_EQ(registry.find("h264"), &video);
    EXPECT_EQ(registry.find("missing"), nullptr);
}

TEST(CodecRegistryTest, DuplicateNameRejected) {
    CodecRegistry registry;
    BlCodecPlugin a = makePlugin("vp9", BL_CODEC_VIDEO, BL_ROLE_ENCODE);
    BlCodecPlugin b = makePlugin("vp9", BL_CODEC_VIDEO, BL_ROLE_DECODE);
    EXPECT_TRUE(registry.registerPlugin(&a).ok());
    auto result = registry.registerPlugin(&b);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::RegistryDuplicate);
    EXPECT_EQ(registry.find("vp9"), &a);
}

TEST(CodecRegistryTest, NullAndBadShapeRejected) {
    CodecRegistry registry;
    auto nullResult = registry.registerPlugin(nullptr);
    ASSERT_FALSE(nullResult.ok());
    EXPECT_EQ(nullResult.code(), Err::InvalidArgument);

    BlCodecPlugin broken{};
    broken.abi_version = BL_PLUGIN_ABI_VERSION;
    broken.name = "broken";
    broken.type = BL_CODEC_VIDEO;
    broken.caps.roles = BL_ROLE_DECODE;
    auto shapeResult = registry.registerPlugin(&broken);
    ASSERT_FALSE(shapeResult.ok());
    EXPECT_EQ(shapeResult.code(), Err::InvalidArgument);

    BlCodecPlugin badType = makePlugin("weird", 7u, BL_ROLE_DECODE);
    badType.type = 7u;
    EXPECT_EQ(registry.registerPlugin(&badType).code(), Err::InvalidArgument);
}

TEST(CodecRegistryTest, ByTypeFilters) {
    CodecRegistry registry;
    BlCodecPlugin v1 = makePlugin("h264", BL_CODEC_VIDEO, BL_ROLE_ENCODE);
    BlCodecPlugin v2 = makePlugin("av1", BL_CODEC_VIDEO, BL_ROLE_DECODE);
    BlCodecPlugin a1 = makePlugin("aac", BL_CODEC_AUDIO, BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&v1).ok());
    ASSERT_TRUE(registry.registerPlugin(&v2).ok());
    ASSERT_TRUE(registry.registerPlugin(&a1).ok());

    EXPECT_EQ(registry.byType(BL_CODEC_VIDEO).size(), 2u);
    EXPECT_EQ(registry.byType(BL_CODEC_AUDIO).size(), 1u);
}

TEST(CodecRegistryTest, DefaultForPrefersNonExperimental) {
    CodecRegistry registry;
    static BlCodecPlugin experimental =
        makePlugin("expcodec", BL_CODEC_VIDEO, BL_ROLE_DECODE | BL_ROLE_ENCODE,
                   BL_FLAG_EXPERIMENTAL);
    static BlCodecPlugin stable =
        makePlugin("stablecodec", BL_CODEC_VIDEO,
                   BL_ROLE_DECODE | BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&experimental).ok());
    ASSERT_TRUE(registry.registerPlugin(&stable).ok());

    EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Preview),
              &stable);
    EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Export), &stable);
}

TEST(CodecRegistryTest, DefaultForFallsBackToExperimental) {
    CodecRegistry registry;
    static BlCodecPlugin experimental =
        makePlugin("onlyexp", BL_CODEC_VIDEO, BL_ROLE_DECODE,
                   BL_FLAG_EXPERIMENTAL);
    ASSERT_TRUE(registry.registerPlugin(&experimental).ok());
    EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Preview),
              &experimental);
}

TEST(CodecRegistryTest, DefaultForRespectsCapability) {
    CodecRegistry registry;
    static BlCodecPlugin encodeOnly =
        makePlugin("enconly", BL_CODEC_AUDIO, BL_ROLE_ENCODE);
    static BlCodecPlugin decodeOnly =
        makePlugin("deconly", BL_CODEC_AUDIO, BL_ROLE_DECODE);
    ASSERT_TRUE(registry.registerPlugin(&encodeOnly).ok());
    ASSERT_TRUE(registry.registerPlugin(&decodeOnly).ok());

    EXPECT_EQ(registry.defaultFor(BL_CODEC_AUDIO, CodecRole::Export),
              &encodeOnly);
    EXPECT_EQ(registry.defaultFor(BL_CODEC_AUDIO, CodecRole::Preview),
              &decodeOnly);

    static BlCodecPlugin encodeOnlyVideo =
        makePlugin("venconly", BL_CODEC_VIDEO, BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&encodeOnlyVideo).ok());
    EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Preview), nullptr);
}

TEST(CodecRegistryTest, ClearEmptiesRegistry) {
    CodecRegistry registry;
    BlCodecPlugin p = makePlugin("opus", BL_CODEC_AUDIO, BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&p).ok());
    registry.clear();
    EXPECT_EQ(registry.count(), 0u);
    EXPECT_EQ(registry.find("opus"), nullptr);
}

} // namespace
