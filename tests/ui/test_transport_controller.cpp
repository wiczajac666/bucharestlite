#include <gtest/gtest.h>

#include "transport/transport_controller.hpp"

namespace bl::ui {
namespace {

using bl::Duration;
using bl::Rational;
using bl::Time;

constexpr Rational kMicros{1'000'000, 1};

class TransportControllerTest : public ::testing::Test {};

TEST_F(TransportControllerTest, DefaultsAreStoppedAtZero) {
    TransportController transport;
    EXPECT_EQ(transport.playhead(), Time{});
    EXPECT_FALSE(transport.playing());
    EXPECT_EQ(transport.duration(), Time{});
    EXPECT_EQ(transport.fps(), (Rational{24000, 1001}));
    EXPECT_EQ(transport.rate(), 1.0);
}

TEST_F(TransportControllerTest, SetPlayheadClampsAndIsIdempotent) {
    TransportController transport;
    transport.setDuration(Time::fromSeconds(2.0, kMicros));

    int playheadNotifications = 0;
    int stateNotifications = 0;
    transport.setPlayheadHandler([&] { ++playheadNotifications; });
    transport.setStateHandler([&] { ++stateNotifications; });

    transport.setPlayhead(Time::fromSeconds(5.0, kMicros));
    EXPECT_EQ(transport.playhead(), transport.duration());
    EXPECT_EQ(playheadNotifications, 1);

    // Setting the same position must not re-notify (breaks scrub feedback
    // loops between the timeline and the preview transport).
    transport.setPlayhead(transport.duration());
    EXPECT_EQ(playheadNotifications, 1);

    transport.setPlayhead(Time::fromSeconds(-1.0, kMicros));
    EXPECT_EQ(transport.playhead(), Time::fromTicks(0, kMicros));

    EXPECT_EQ(stateNotifications, 0);
}

TEST_F(TransportControllerTest, PlayOnEmptyTimelineIsANoOp) {
    TransportController transport;
    transport.play();
    EXPECT_FALSE(transport.playing());
    EXPECT_EQ(transport.playhead(), Time{});
}

TEST_F(TransportControllerTest, PlayPauseAndToggleNotifyState) {
    TransportController transport;
    transport.setDuration(Time::fromSeconds(2.0, kMicros));

    int stateNotifications = 0;
    transport.setStateHandler([&] { ++stateNotifications; });

    transport.play();
    EXPECT_TRUE(transport.playing());
    EXPECT_EQ(stateNotifications, 1);

    transport.pause();
    EXPECT_FALSE(transport.playing());
    EXPECT_EQ(stateNotifications, 2);

    transport.togglePlay();
    EXPECT_TRUE(transport.playing());
    EXPECT_EQ(stateNotifications, 3);
}

TEST_F(TransportControllerTest, PlayAtEndRewindsToStart) {
    TransportController transport;
    transport.setDuration(Time::fromSeconds(2.0, kMicros));
    transport.setPlayhead(Time::fromSeconds(2.0, kMicros));
    EXPECT_EQ(transport.playhead(), transport.duration());

    transport.play();
    EXPECT_TRUE(transport.playing());
    EXPECT_EQ(transport.playhead(), Time::fromTicks(0, kMicros));
}

TEST_F(TransportControllerTest, TickAdvancesByElapsedWallClock) {
    TransportController transport;
    transport.setDuration(Time::fromSeconds(2.0, kMicros));
    transport.play();

    transport.tick(Duration::fromSeconds(0.5, kMicros));
    EXPECT_EQ(transport.playhead(), Time::fromSeconds(0.5, kMicros));
    EXPECT_TRUE(transport.playing());

    transport.tick(Duration::fromSeconds(0.25, kMicros));
    EXPECT_EQ(transport.playhead(), Time::fromSeconds(0.75, kMicros));
}

TEST_F(TransportControllerTest, TickAutoPausesAtDuration) {
    TransportController transport;
    transport.setDuration(Time::fromSeconds(2.0, kMicros));
    transport.play();

    int stateNotifications = 0;
    transport.setStateHandler([&] { ++stateNotifications; });

    transport.tick(Duration::fromSeconds(5.0, kMicros));
    EXPECT_EQ(transport.playhead(), transport.duration());
    EXPECT_FALSE(transport.playing());
    EXPECT_EQ(stateNotifications, 1);

    // Already stopped: a further tick must not move anything.
    transport.tick(Duration::fromSeconds(1.0, kMicros));
    EXPECT_EQ(transport.playhead(), transport.duration());
    EXPECT_EQ(stateNotifications, 1);
}

TEST_F(TransportControllerTest, TickDoesNothingWhenPaused) {
    TransportController transport;
    transport.setDuration(Time::fromSeconds(2.0, kMicros));
    transport.tick(Duration::fromSeconds(1.0, kMicros));
    EXPECT_EQ(transport.playhead(), Time{});
}

TEST_F(TransportControllerTest, StepMovesByOneFrameAtFps) {
    TransportController transport;
    transport.setFps(Rational{24, 1});
    transport.setDuration(Time::fromSeconds(3.0, kMicros));

    transport.stepForward();
    EXPECT_NEAR(transport.playhead().toSeconds(), 1.0 / 24.0, 1e-6);

    transport.stepBackward();
    EXPECT_DOUBLE_EQ(transport.playhead().toSeconds(), 0.0);

    // Step back at zero stays clamped at zero.
    transport.stepBackward();
    EXPECT_EQ(transport.playhead(), Time::fromTicks(0, kMicros));

    // Step forward past the end clamps to duration.
    for (int i = 0; i < 100; ++i) transport.stepForward();
    EXPECT_EQ(transport.playhead(), transport.duration());
}

TEST_F(TransportControllerTest, StopPausesAndRewindsAlwaysNotifying) {
    TransportController transport;
    transport.setDuration(Time::fromSeconds(2.0, kMicros));
    transport.setPlayhead(Time::fromSeconds(1.0, kMicros));
    transport.play();

    int stateNotifications = 0;
    transport.setStateHandler([&] { ++stateNotifications; });

    transport.stop();
    EXPECT_FALSE(transport.playing());
    EXPECT_EQ(transport.playhead(), Time::fromTicks(0, kMicros));
    EXPECT_EQ(stateNotifications, 1);

    // Stopping an already-stopped transport still reports its state so the UI
    // can resync button glyphs after session restore.
    transport.stop();
    EXPECT_EQ(stateNotifications, 2);
}

TEST_F(TransportControllerTest, SetFpsIgnoresInvalidRates) {
    TransportController transport;
    transport.setFps(Rational{0, 0});
    EXPECT_EQ(transport.fps(), (Rational{24000, 1001}));
    transport.setFps(Rational{30, 1});
    EXPECT_EQ(transport.fps(), (Rational{30, 1}));
}

TEST_F(TransportControllerTest, SetRateClampsPositive) {
    TransportController transport;
    transport.setRate(2.0);
    EXPECT_EQ(transport.rate(), 2.0);
    transport.setRate(0.0);
    EXPECT_EQ(transport.rate(), 1.0);

    transport.setDuration(Time::fromSeconds(4.0, kMicros));
    transport.setRate(2.0);
    transport.play();
    transport.tick(Duration::fromSeconds(0.5, kMicros));
    EXPECT_EQ(transport.playhead(), Time::fromSeconds(1.0, kMicros));
}

} // namespace
} // namespace bl::ui