#include <bl_core/image_sequence.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace bl {
namespace {

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("bl_imgseq_" + std::to_string(++counter_) + "_" +
                 std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    inline static int counter_ = 0;
    std::filesystem::path path_;
};

std::string frameName(const std::string& stem, int width, int index,
                      const std::string& ext) {
    std::string digits = std::to_string(index);
    if (digits.size() < static_cast<size_t>(width)) {
        digits.insert(0, static_cast<size_t>(width) - digits.size(), '0');
    }
    return stem + digits + "." + ext;
}

void makeFrame(const std::filesystem::path& dir, const std::string& stem,
               int width, int index, const std::string& ext) {
    std::ofstream(dir / frameName(stem, width, index, ext)) << "x";
}

TEST(ImageSequence, DetectsPaddedRun) {
    TempDir dir;
    for (int i = 1; i <= 5; ++i) {
        makeFrame(dir.path(), "frame_", 4, i, "png");
    }
    const auto seq = detectImageSequence(
        (dir.path() / "frame_0003.png").string());
    ASSERT_TRUE(seq.has_value());
    EXPECT_EQ(seq->frameCount, 5);
    EXPECT_EQ(seq->firstIndex, 3);
    EXPECT_EQ(seq->digits, 4);
    EXPECT_EQ(seq->stem, "frame_");
    EXPECT_EQ(seq->extension, "png");
    // The pattern always joins with '/' (the FFmpeg convention documented in
    // image_sequence.hpp), whatever separator the directory itself used.
    EXPECT_EQ(seq->patternPath,
              dir.path().string() + "/frame_%04d.png");
}

TEST(ImageSequence, SingleFileIsNotASequence) {
    TempDir dir;
    makeFrame(dir.path(), "shot_", 3, 1, "jpg");
    const auto seq = detectImageSequence((dir.path() / "shot_001.jpg").string());
    EXPECT_FALSE(seq.has_value());
}

TEST(ImageSequence, NonDigitSiblingIgnored) {
    TempDir dir;
    makeFrame(dir.path(), "frame_", 4, 1, "png");
    makeFrame(dir.path(), "frame_", 4, 2, "png");
    // A single file with a different digit width must not join the set.
    makeFrame(dir.path(), "frame_", 3, 3, "png");
    const auto seq = detectImageSequence(
        (dir.path() / "frame_0001.png").string());
    ASSERT_TRUE(seq.has_value());
    EXPECT_EQ(seq->frameCount, 2);
}

TEST(ImageSequence, MinimumCountHonoured) {
    TempDir dir;
    makeFrame(dir.path(), "a_", 3, 1, "png");
    makeFrame(dir.path(), "a_", 3, 2, "png");
    EXPECT_TRUE(detectImageSequence((dir.path() / "a_002.png").string(), 2));
    EXPECT_FALSE(detectImageSequence((dir.path() / "a_002.png").string(), 3));
}

TEST(ImageSequence, MissingOrPlainFileYieldsNothing) {
    TempDir dir;
    EXPECT_FALSE(detectImageSequence((dir.path() / "nope.png").string()));
    std::ofstream(dir.path() / "plain.txt") << "x";
    EXPECT_FALSE(detectImageSequence((dir.path() / "plain.txt").string()));
}

TEST(ImageSequence, StopsAtShortDigitRuns) {
    TempDir dir;
    makeFrame(dir.path(), "clip", 1, 1, "png");
    makeFrame(dir.path(), "clip", 1, 2, "png");
    // "clip1.png"/"clip2.png" look like ordinary numbered files, not a
    // sequence, because the digit run is one character.
    EXPECT_FALSE(detectImageSequence((dir.path() / "clip1.png").string()));
}

TEST(ImageSequence, CaseSensitiveStem) {
    TempDir dir;
    makeFrame(dir.path(), "Frame_", 4, 1, "png");
    const auto seq = detectImageSequence(
        (dir.path() / "Frame_0001.png").string());
    EXPECT_FALSE(seq.has_value());
}

} // namespace
} // namespace bl