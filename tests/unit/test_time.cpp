#include <bl_core/time.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

using bl::Duration;
using bl::Rational;
using bl::Time;
using bl::TimeRange;

constexpr Rational kNtscFps{24000, 1001};
constexpr Rational kFps24{24, 1};
constexpr Rational kMicros{1'000'000, 1};
constexpr Rational kFps48{48, 1};
constexpr Rational kHalfSec{2, 1};
constexpr Rational kQuarterSec{4, 1};

TEST(RationalTest, MakeReducesAndNormalizes) {
    EXPECT_EQ(Rational::make(4, -8), (Rational{-1, 2}));
    EXPECT_EQ(Rational::make(-4, -8), (Rational{1, 2}));
    EXPECT_EQ(Rational::make(0, 5), (Rational{0, 1}));
    EXPECT_EQ(Rational::make(192000, 8017), (Rational{192000, 8017}));
    EXPECT_EQ(Rational::make(24000, 1001), kNtscFps);
}

TEST(RationalTest, ExactEqualityAcrossForms) {
    EXPECT_TRUE(bl::exactEqual(Rational::make(2, 4), Rational::make(1, 2)));
    EXPECT_FALSE(bl::exactEqual(Rational::make(2, 5), Rational::make(1, 2)));
    EXPECT_EQ(Rational::make(2, 4).toString(), "1/2");
}

TEST(TimeTest, FromFrameStoresTicksAsFrameIndex) {
    Time t = Time::fromFrame(10, kNtscFps);
    EXPECT_EQ(t.ticks, 10);
    EXPECT_EQ(t.rate, kNtscFps);
    EXPECT_DOUBLE_EQ(t.toSeconds(), 10.0 * 1001.0 / 24000.0);
}

TEST(TimeTest, FromSecondsRoundTripWithinEpsilon) {
    Time t = Time::fromSeconds(41.708333333333336, kFps24);
    EXPECT_NEAR(t.toSeconds(), 41.708333333333336, 1e-9);
}

TEST(TimeTest, SameRateArithmeticIsExact) {
    Time a = Time::fromTicks(5, kFps24);
    Time b = Time::fromTicks(7, kFps24);
    Time sum = a + b;
    EXPECT_EQ(sum.ticks, 12);
    EXPECT_EQ(sum.rate, kFps24);
    Duration d = b - a;
    EXPECT_EQ(d.ticks, 2);
    EXPECT_EQ(d.rate, kFps24);
}

TEST(TimeTest, MixedRateAdditionRoundsHalfToEven) {
    Time a = Time::fromTicks(1, kHalfSec);
    Time b = Time::fromTicks(3, kQuarterSec);
    Time sum = a + b;
    EXPECT_EQ(sum.ticks, 2);

    Time c = Time::fromTicks(2, kHalfSec);
    Time d = Time::fromTicks(2, kQuarterSec);
    Time sum2 = c + d;
    EXPECT_EQ(sum2.ticks, 3);
}

TEST(TimeTest, MixedRateSubtraction) {
    Time a = Time::fromFrameAt(48, kFps48, kNtscFps);
    Time b = Time::fromFrame(1, kNtscFps);
    Duration diff = a - b;
    EXPECT_EQ(diff.ticks, 23);
    EXPECT_EQ(diff.rate, kNtscFps);
}

TEST(TimeTest, FromFrameAtConvertsExactlyWhenRepresentable) {
    Time t = Time::fromFrameAt(2, kFps24, kFps48);
    EXPECT_EQ(t.ticks, 4);
    EXPECT_EQ(t.rate, kFps48);

    Time back = Time::fromFrameAt(4, kFps48, kFps24);
    EXPECT_EQ(back.ticks, 2);
}

TEST(TimeTest, ComparisonsAreExactAcrossRates) {
    Time twoThirdsA = Time::fromTicks(2, Rational{3, 1});
    Time twoThirdsB = Time::fromTicks(1'000'000, Rational{1'500'000, 1});
    EXPECT_EQ(twoThirdsA, twoThirdsB);
    EXPECT_FALSE(twoThirdsA != twoThirdsB);

    Time third = Time::fromTicks(1, Rational{3, 1});
    Time micros = Time::fromTicks(333'333, kMicros);
    EXPECT_GT(third, micros);
    EXPECT_LT(micros, third);
    EXPECT_GE(third, micros);
    EXPECT_LE(micros, third);
}

TEST(TimeTest, FrameConversionsFloorCeilRound) {
    Time quarterTick = Time::fromTicks(1, kQuarterSec);

    EXPECT_EQ(quarterTick.floorFrameAt(kHalfSec), 0);
    EXPECT_EQ(quarterTick.ceilFrameAt(kHalfSec), 1);
    EXPECT_EQ(quarterTick.toFrameAt(kHalfSec), 0);

    Time threeQuarters = Time::fromTicks(3, kQuarterSec);
    EXPECT_EQ(threeQuarters.floorFrameAt(kHalfSec), 1);
    EXPECT_EQ(threeQuarters.ceilFrameAt(kHalfSec), 2);
    EXPECT_EQ(threeQuarters.toFrameAt(kHalfSec), 2);

    Time negativeQuarter = Time::fromTicks(-1, kQuarterSec);
    EXPECT_EQ(negativeQuarter.floorFrameAt(kHalfSec), -1);
    EXPECT_EQ(negativeQuarter.ceilFrameAt(kHalfSec), 0);
    EXPECT_EQ(negativeQuarter.toFrameAt(kHalfSec), 0);

    Time exactFrames = Time::fromFrameAt(6, kFps48, kFps24);
    EXPECT_EQ(exactFrames.ticks, 3);
    EXPECT_EQ(exactFrames.toFrameAt(kFps48), 6);
    EXPECT_EQ(exactFrames.floorFrame(), 3);
}

TEST(DurationTest, ArithmeticAndScaling) {
    Duration a = Duration::fromFrames(10, kFps24);
    Duration b = Duration::fromFrames(5, kFps24);
    EXPECT_EQ((a + b).ticks, 15);
    EXPECT_EQ((a - b).ticks, 5);
    EXPECT_EQ((b * 3).ticks, 15);
    EXPECT_EQ((-b).ticks, -5);
    EXPECT_DOUBLE_EQ(a.toSeconds(), 10.0 / 24.0);
}

TEST(DurationTest, FramesAtForeignRate) {
    Duration halfSecond = Duration::fromFrames(12, kFps24);
    EXPECT_EQ(halfSecond.toFramesAt(kFps48), 24);
    EXPECT_EQ(halfSecond.toFramesAt(kNtscFps), 12);
}

TEST(TimeRangeTest, EndIsEmpty) {
    TimeRange r{Time::fromFrame(10, kFps24), Duration::fromFrames(5, kFps24)};
    EXPECT_EQ(r.end(), Time::fromFrame(15, kFps24));
    EXPECT_FALSE(r.isEmpty());

    TimeRange empty{Time::fromFrame(0, kFps24), Duration{}};
    EXPECT_TRUE(empty.isEmpty());
}

TEST(TimeRangeTest, OverlapsIsHalfOpen) {
    TimeRange a{Time::fromFrame(0, kFps24), Duration::fromFrames(10, kFps24)};
    TimeRange b{Time::fromFrame(5, kFps24), Duration::fromFrames(10, kFps24)};
    TimeRange adjacent{Time::fromFrame(10, kFps24), Duration::fromFrames(10, kFps24)};
    TimeRange before{Time::fromTicks(-5, kFps24), Duration::fromFrames(10, kFps24)};
    TimeRange fullyBefore{Time::fromTicks(-20, kFps24), Duration::fromFrames(10, kFps24)};

    EXPECT_TRUE(a.overlaps(b));
    EXPECT_TRUE(b.overlaps(a));
    EXPECT_FALSE(a.overlaps(adjacent));
    EXPECT_FALSE(adjacent.overlaps(a));
    EXPECT_TRUE(a.overlaps(before));
    EXPECT_TRUE(before.overlaps(a));
    EXPECT_FALSE(a.overlaps(fullyBefore));
    EXPECT_LT(fullyBefore.end(), a.start);
}

TEST(TimeRangeTest, ContainsBoundarySemantics) {
    TimeRange r{Time::fromFrame(0, kFps24), Duration::fromFrames(10, kFps24)};
    EXPECT_TRUE(r.contains(Time::fromFrame(0, kFps24)));
    EXPECT_TRUE(r.contains(Time::fromFrame(9, kFps24)));
    EXPECT_FALSE(r.contains(Time::fromFrame(10, kFps24)));
    EXPECT_FALSE(r.contains(Time::fromTicks(-1, kFps24)));

    TimeRange inner{Time::fromFrame(2, kFps24), Duration::fromFrames(5, kFps24)};
    TimeRange touchingEnd{Time::fromFrame(5, kFps24), Duration::fromFrames(5, kFps24)};
    EXPECT_TRUE(r.contains(inner));
    EXPECT_TRUE(r.contains(touchingEnd));

    TimeRange spillover{Time::fromFrame(8, kFps24), Duration::fromFrames(5, kFps24)};
    EXPECT_FALSE(r.contains(spillover));
}

TEST(TimeTest, CompoundAssignmentsPreserveRate) {
    Time t = Time::fromTicks(10, kFps24);
    t += Duration::fromFrames(5, kFps24);
    EXPECT_EQ(t.ticks, 15);
    t -= Duration::fromFrames(3, kFps24);
    EXPECT_EQ(t.ticks, 12);
    t += Time::fromTicks(1, kFps48);
    EXPECT_EQ(t.rate, kFps24);

    Duration span = Time::fromFrame(20, kFps24) - Time::fromFrame(5, kFps24);
    EXPECT_EQ(span.ticks, 15);
    EXPECT_EQ(span.rate, kFps24);
}

} // namespace
