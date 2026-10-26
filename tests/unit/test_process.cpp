#include <bl_core/process.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace {

using bl::ProcessResult;
using bl::runProcess;

#ifdef _WIN32
const std::vector<std::string> kEcho{"cmd", "/c", "echo", "hello"};
const std::vector<std::string> kExitSeven{"cmd", "/c", "exit", "7"};
const std::vector<std::string> kSleep{"cmd", "/c", "ping", "-n", "11",
                                      "127.0.0.1"};
#else
const std::vector<std::string> kEcho{"/bin/echo", "hello"};
const std::vector<std::string> kExitSeven{"/bin/sh", "-c", "exit 7"};
const std::vector<std::string> kSleep{"/bin/sh", "-c", "sleep 10"};
#endif

TEST(ProcessTest, CapturesOutputAndExitCode) {
    const ProcessResult result = runProcess(kEcho);
    EXPECT_TRUE(result.started);
    EXPECT_FALSE(result.cancelled);
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.output, "hello\n");
}

TEST(ProcessTest, ReportsNonZeroExitCode) {
    const ProcessResult result = runProcess(kExitSeven);
    EXPECT_TRUE(result.started);
    EXPECT_EQ(result.exitCode, 7);
}

TEST(ProcessTest, MissingExecutableIsReportedAsNotStarted) {
    const ProcessResult result =
        runProcess({"bl-definitely-missing-executable-9f3a"});
    EXPECT_FALSE(result.started);
    EXPECT_FALSE(result.cancelled);
    EXPECT_NE(result.exitCode, 0);
}

TEST(ProcessTest, ArgumentsAreNotShellInterpreted) {
#ifndef _WIN32
    const ProcessResult result =
        runProcess({"/bin/echo", "a b", "$HOME; echo injected"});
    ASSERT_TRUE(result.started);
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.output, "a b $HOME; echo injected\n");
#endif
}

TEST(ProcessTest, CancelPollStopsTheChild) {
    const auto start = std::chrono::steady_clock::now();
    const ProcessResult result = runProcess(kSleep, [] { return true; });
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(result.started);
    EXPECT_TRUE(result.cancelled);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

} // namespace
