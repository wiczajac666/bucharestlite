#include <bl_timeline/subtitle_import.hpp>

#include <gtest/gtest.h>

namespace bl {
namespace {

const Rational kRate{1'000'000, 1};

int64_t ms(const Time& t) {
    return static_cast<int64_t>(t.toSeconds() * 1000.0 + 0.5);
}

TEST(SubtitleFormat, ExtensionDetection) {
    EXPECT_EQ(subtitleFormatForPath("a.srt"), SubtitleFormat::Srt);
    EXPECT_EQ(subtitleFormatForPath("A.SRT"), SubtitleFormat::Srt);
    EXPECT_EQ(subtitleFormatForPath("dir/x.vtt"), SubtitleFormat::WebVtt);
    EXPECT_EQ(subtitleFormatForPath("x.webvtt"), SubtitleFormat::WebVtt);
    EXPECT_EQ(subtitleFormatForPath("x.ass"), SubtitleFormat::Ass);
    EXPECT_EQ(subtitleFormatForPath("x.ssa"), SubtitleFormat::Ass);
    EXPECT_EQ(subtitleFormatForPath("x.txt"), SubtitleFormat::Unknown);
    EXPECT_EQ(subtitleFormatForPath("noext"), SubtitleFormat::Unknown);
}

TEST(SubtitleImport, ParsesSrt) {
    const std::string srt =
        "1\n"
        "00:00:01,000 --> 00:00:04,000\n"
        "Hello world\n"
        "second line\n"
        "\n"
        "2\n"
        "00:00:05,500 --> 00:00:07,250\n"
        "Bye\n";
    const auto cues = parseSubtitles(srt, SubtitleFormat::Srt);
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(ms(cues[0].start), 1000);
    EXPECT_EQ(ms(cues[0].end), 4000);
    EXPECT_EQ(cues[0].text, "Hello world\nsecond line");
    EXPECT_EQ(ms(cues[1].start), 5500);
    EXPECT_EQ(ms(cues[1].end), 7250);
    EXPECT_EQ(cues[1].text, "Bye");
}

TEST(SubtitleImport, ParsesWebVttAndSkipsHeaderAndNotes) {
    const std::string vtt =
        "WEBVTT\n"
        "\n"
        "NOTE this is a comment\n"
        "spanning two lines\n"
        "\n"
        "intro\n"
        "00:00:02.000 --> 00:00:03.000 align:start\n"
        "Hi <b>there</b>\n"
        "\n"
        "00:04.000 --> 00:05.000\n"
        "Short form\n";
    const auto cues = parseSubtitles(vtt, SubtitleFormat::WebVtt);
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(ms(cues[0].start), 2000);
    EXPECT_EQ(cues[0].text, "Hi there"); // tags stripped
    EXPECT_EQ(ms(cues[1].start), 4000);  // MM:SS.mmm accepted
    EXPECT_EQ(ms(cues[1].end), 5000);
}

TEST(SubtitleImport, ParsesAssDialogueWithFormatAndTags) {
    const std::string ass =
        "[Script Info]\n"
        "Title: demo\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize\n"
        "Style: Default,Arial,24\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
        "Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:04.00,Default,,0,0,0,,{\\an8}Top line\\N"
        "second, with comma\n"
        "Comment: 0,0:00:09.00,0:00:10.00,Default,,0,0,0,,ignored\n"
        "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,Bottom\n";
    const auto cues = parseSubtitles(ass, SubtitleFormat::Ass);
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(ms(cues[0].start), 1000);
    EXPECT_EQ(ms(cues[0].end), 4000);
    EXPECT_EQ(cues[0].text, "Top line\nsecond, with comma");
    EXPECT_EQ(cues[0].style.position, SubtitleStyle::Position::Top);
    EXPECT_EQ(cues[1].text, "Bottom");
    EXPECT_EQ(cues[1].style.position, SubtitleStyle::Position::Bottom);
}

TEST(SubtitleImport, AssWithoutFormatLineUsesDefaultColumns) {
    const std::string ass =
        "[Events]\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello\n";
    const auto cues = parseSubtitles(ass, SubtitleFormat::Ass);
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].text, "Hello");
    EXPECT_EQ(ms(cues[0].start), 1000);
    EXPECT_EQ(ms(cues[0].end), 2000);
}

TEST(SubtitleImport, SortsCuesByStart) {
    const std::string srt =
        "1\n00:00:05,000 --> 00:00:06,000\nlater\n\n"
        "2\n00:00:01,000 --> 00:00:02,000\nearlier\n";
    const auto cues = parseSubtitles(srt, SubtitleFormat::Srt);
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(cues[0].text, "earlier");
    EXPECT_EQ(cues[1].text, "later");
}

TEST(SubtitleImport, SkipsMalformedAndInvertedCues) {
    const std::string srt =
        "1\nnot a timestamp\nignored\n\n"
        "2\n00:00:04,000 --> 00:00:02,000\ninverted\n\n"
        "3\n00:00:08,000 --> 00:00:09,000\nkept\n";
    const auto cues = parseSubtitles(srt, SubtitleFormat::Srt);
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].text, "kept");
}

TEST(SubtitleImport, UnknownFormatYieldsNothing) {
    EXPECT_TRUE(parseSubtitles("whatever", SubtitleFormat::Unknown).empty());
}

TEST(SubtitleImport, ParseSubtitleFilePicksFormatFromExtension) {
    const auto cues =
        parseSubtitleFile("movie.srt",
                          "1\n00:00:01,000 --> 00:00:02,000\nHi\n");
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].text, "Hi");
}

} // namespace
} // namespace bl
