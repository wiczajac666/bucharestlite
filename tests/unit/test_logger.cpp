#include <bl_core/logger.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;

std::string uniqueLogPath(const std::string& tag) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream name;
    name << "bl_logger_test_" << tag << "_" << now << ".log";
    fs::path p = fs::temp_directory_path() / name.str();
    return p.string();
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

TEST(LoggerTest, LevelGating) {
    bl::Logger& log = bl::Logger::get();
    bl::Logger::Options opts;
    opts.minLevel = bl::LogLevel::Warn;
    opts.consoleEnabled = false;
    log.configure(opts);
    EXPECT_TRUE(log.enabled(bl::LogLevel::Warn));
    EXPECT_TRUE(log.enabled(bl::LogLevel::Error));
    EXPECT_FALSE(log.enabled(bl::LogLevel::Info));
    EXPECT_FALSE(log.enabled(bl::LogLevel::Debug));
    EXPECT_FALSE(log.enabled(bl::LogLevel::Trace));

    log.setLevel(bl::LogLevel::Trace);
    EXPECT_TRUE(log.enabled(bl::LogLevel::Trace));
}

TEST(LoggerTest, WritesFormattedLinesToFile) {
    bl::Logger& log = bl::Logger::get();
    std::string path = uniqueLogPath("basic");
    bl::Logger::Options opts;
    opts.minLevel = bl::LogLevel::Trace;
    opts.consoleEnabled = false;
    opts.filePath = path;
    log.configure(opts);

    log.log(bl::LogLevel::Info, "core", "hello from core");
    BL_LOG_WARN("render", "gpu warming up");
    log.logf(bl::LogLevel::Error, "export", "failed after %d of %zu frames", 12,
             size_t{100});
    log.flush();

    std::string contents = readFile(path);
    EXPECT_NE(contents.find("[INFO] [core] hello from core"), std::string::npos);
    EXPECT_NE(contents.find("[WARN] [render] gpu warming up"), std::string::npos);
    EXPECT_NE(contents.find("[ERROR] [export] failed after 12 of 100 frames"),
              std::string::npos);
    EXPECT_EQ(contents.find("TRACE"), std::string::npos) << contents;
    EXPECT_EQ(std::count(contents.begin(), contents.end(), '\n'), 3);

    std::remove(path.c_str());
}

TEST(LoggerTest, FiltersBelowMinLevelEverywhere) {
    bl::Logger& log = bl::Logger::get();
    std::string path = uniqueLogPath("filter");
    bl::Logger::Options opts;
    opts.minLevel = bl::LogLevel::Warn;
    opts.consoleEnabled = false;
    opts.filePath = path;
    log.configure(opts);

    log.log(bl::LogLevel::Debug, "core", "invisible");
    log.log(bl::LogLevel::Error, "core", "visible");
    log.flush();

    std::string contents = readFile(path);
    EXPECT_EQ(contents.find("invisible"), std::string::npos);
    EXPECT_NE(contents.find("visible"), std::string::npos);

    std::remove(path.c_str());
}

TEST(LoggerTest, RotatesWhenSizeExceeded) {
    bl::Logger& log = bl::Logger::get();
    std::string path = uniqueLogPath("rotate");
    bl::Logger::Options opts;
    opts.minLevel = bl::LogLevel::Trace;
    opts.consoleEnabled = false;
    opts.filePath = path;
    opts.maxFileBytes = 256;
    opts.maxRotations = 2;
    log.configure(opts);

    const std::string filler(60, 'x');
    for (int i = 0; i < 40; ++i) {
        log.logf(bl::LogLevel::Info, "bulk", "%03d %s", i, filler.c_str());
    }
    log.flush();
    log.shutdown();

    EXPECT_FALSE(readFile(path).empty());
    EXPECT_FALSE(readFile(path + ".1").empty());
    EXPECT_FALSE(readFile(path + ".2").empty());
    EXPECT_TRUE(readFile(path + ".3").empty());

    for (int k = 0; k <= 3; ++k) {
        std::string candidate = k == 0 ? path : path + "." + std::to_string(k);
        std::remove(candidate.c_str());
    }
}

} // namespace
