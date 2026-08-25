#include <bl_core/codec_registry.hpp>

#include <bl_plugins/codec_plugin.h>
#include "test_plugin_utils.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using bl::CodecRegistry;
using bl::CodecRole;
using bl::Err;

TEST(CodecRegistryTest, RegisterFindAndCount) {
    CodecRegistry registry;
    BlCodecPlugin video = bltest::makePlugin("h264", BL_CODEC_VIDEO,
                                             BL_ROLE_DECODE | BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&video).ok());
    EXPECT_EQ(registry.count(), 1u);
    EXPECT_EQ(registry.find("h264"), &video);
    EXPECT_EQ(registry.find("missing"), nullptr);
}

TEST(CodecRegistryTest, DuplicateNameRejected) {
    CodecRegistry registry;
    BlCodecPlugin a = bltest::makePlugin("vp9", BL_CODEC_VIDEO, BL_ROLE_ENCODE);
    BlCodecPlugin b = bltest::makePlugin("vp9", BL_CODEC_VIDEO, BL_ROLE_DECODE);
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

    BlCodecPlugin badType = bltest::makePlugin("weird", 7u, BL_ROLE_DECODE);
    badType.type = 7u;
    EXPECT_EQ(registry.registerPlugin(&badType).code(), Err::InvalidArgument);
}

TEST(CodecRegistryTest, ByTypeFilters) {
    CodecRegistry registry;
    BlCodecPlugin v1 = bltest::makePlugin("h264", BL_CODEC_VIDEO, BL_ROLE_ENCODE);
    BlCodecPlugin v2 = bltest::makePlugin("av1", BL_CODEC_VIDEO, BL_ROLE_DECODE);
    BlCodecPlugin a1 = bltest::makePlugin("aac", BL_CODEC_AUDIO, BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&v1).ok());
    ASSERT_TRUE(registry.registerPlugin(&v2).ok());
    ASSERT_TRUE(registry.registerPlugin(&a1).ok());

    EXPECT_EQ(registry.byType(BL_CODEC_VIDEO).size(), 2u);
    EXPECT_EQ(registry.byType(BL_CODEC_AUDIO).size(), 1u);
}

TEST(CodecRegistryTest, DefaultForPrefersNonExperimental) {
    CodecRegistry registry;
    static BlCodecPlugin experimental =
        bltest::makePlugin("expcodec", BL_CODEC_VIDEO, BL_ROLE_DECODE | BL_ROLE_ENCODE,
                   BL_FLAG_EXPERIMENTAL);
    static BlCodecPlugin stable =
        bltest::makePlugin("stablecodec", BL_CODEC_VIDEO,
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
        bltest::makePlugin("onlyexp", BL_CODEC_VIDEO, BL_ROLE_DECODE,
                   BL_FLAG_EXPERIMENTAL);
    ASSERT_TRUE(registry.registerPlugin(&experimental).ok());
    EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Preview),
              &experimental);
}

TEST(CodecRegistryTest, DefaultForRespectsCapability) {
    CodecRegistry registry;
    static BlCodecPlugin encodeOnly =
        bltest::makePlugin("enconly", BL_CODEC_AUDIO, BL_ROLE_ENCODE);
    static BlCodecPlugin decodeOnly =
        bltest::makePlugin("deconly", BL_CODEC_AUDIO, BL_ROLE_DECODE);
    ASSERT_TRUE(registry.registerPlugin(&encodeOnly).ok());
    ASSERT_TRUE(registry.registerPlugin(&decodeOnly).ok());

    EXPECT_EQ(registry.defaultFor(BL_CODEC_AUDIO, CodecRole::Export),
              &encodeOnly);
    EXPECT_EQ(registry.defaultFor(BL_CODEC_AUDIO, CodecRole::Preview),
              &decodeOnly);

    static BlCodecPlugin encodeOnlyVideo =
        bltest::makePlugin("venconly", BL_CODEC_VIDEO, BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&encodeOnlyVideo).ok());
    EXPECT_EQ(registry.defaultFor(BL_CODEC_VIDEO, CodecRole::Preview), nullptr);
}

TEST(CodecRegistryTest, ClearEmptiesRegistry) {
    CodecRegistry registry;
    BlCodecPlugin p = bltest::makePlugin("opus", BL_CODEC_AUDIO, BL_ROLE_ENCODE);
    ASSERT_TRUE(registry.registerPlugin(&p).ok());
    registry.clear();
    EXPECT_EQ(registry.count(), 0u);
    EXPECT_EQ(registry.find("opus"), nullptr);
}

} // namespace
