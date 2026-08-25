#include <bl_core/result.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

using bl::Err;

TEST(ErrTest, ToStringCoversCodes) {
    EXPECT_STREQ(bl::toString(Err::Ok), "Ok");
    EXPECT_STREQ(bl::toString(Err::FileNotFound), "FileNotFound");
    EXPECT_STREQ(bl::toString(Err::PluginAbiMismatch), "PluginAbiMismatch");
    EXPECT_STREQ(bl::toString(Err::Internal), "Internal");
}

TEST(ResultTest, OkValueAccess) {
    bl::Result<int> r = 42;
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.code(), Err::Ok);
    EXPECT_TRUE(r.message().empty());
    EXPECT_EQ(r.value(), 42);
    r.value() += 1;
    EXPECT_EQ(r.value(), 43);
}

TEST(ResultTest, ErrCarriesCodeAndMessage) {
    auto r = bl::Result<int>::err(Err::FileNotFound, "media.bin missing");
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.code(), Err::FileNotFound);
    EXPECT_EQ(r.message(), "media.bin missing");
}

TEST(ResultTest, ValueOrFallsBack) {
    auto ok = bl::Result<std::string>::ok(std::string("present"));
    auto bad = bl::Result<std::string>::err(Err::DecodeFailed);
    EXPECT_EQ(ok.valueOr(std::string("fallback")), "present");
    EXPECT_EQ(bad.valueOr(std::string("fallback")), "fallback");
}

TEST(ResultTest, ImplicitConstructionFromValueAndErr) {
    bl::Result<double> good = 3.5;
    bl::Result<double> bad = {Err::InvalidArgument, "bad"};
    EXPECT_TRUE(good.ok());
    EXPECT_DOUBLE_EQ(good.value(), 3.5);
    EXPECT_EQ(bad.code(), Err::InvalidArgument);
}

TEST(ResultTest, MoveOutValue) {
    auto r = bl::Result<std::string>::ok(std::string("payload"));
    std::string taken = std::move(r).value();
    EXPECT_EQ(taken, "payload");
}

TEST(ResultTest, VoidResultSemantics) {
    bl::Result<void> good{};
    auto bad = bl::Result<void>(Err::IoError, "disk full");
    EXPECT_TRUE(good.ok());
    EXPECT_EQ(good.code(), Err::Ok);
    EXPECT_FALSE(bad.ok());
    EXPECT_EQ(bad.code(), Err::IoError);
    EXPECT_EQ(bad.message(), "disk full");
}

} // namespace
