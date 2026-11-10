#pragma once

#include <bl_core/time.hpp>

#include <functional>

namespace bl::ui {

// Qt-free transport state machine: owns the canonical preview playhead and
// knows how to advance it, step it and clamp it. Kept dependency-free so the
// semantics are unit-testable without a QApplication.
class TransportController {
public:
    using Handler = std::function<void()>;

    explicit TransportController(bl::Rational fps = {24000, 1001});

    void setFps(bl::Rational fps);
    bl::Rational fps() const noexcept { return fps_; }

    void setDuration(bl::Time duration);
    bl::Time duration() const noexcept { return duration_; }

    bool playing() const noexcept { return playing_; }
    bl::Time playhead() const noexcept { return playhead_; }
    double rate() const noexcept { return rate_; }
    void setRate(double rate);

    // Loop playback over a range (defaults to the full timeline when no
    // explicit range is set or the range is invalid). When looping, reaching
    // the end of the loop range restarts playback at the range start instead
    // of pausing.
    void setLoop(bool loop);
    bool loop() const noexcept { return loop_; }
    void setLoopRange(bl::Time in, bl::Time out);
    void clearLoopRange();
    bl::Time loopIn() const noexcept { return loopIn_; }
    bl::Time loopOut() const noexcept { return loopOut_; }

    // Transport commands.
    void play();
    void pause();
    void togglePlay();
    void stop();               // pause and rewind to zero
    void stepForward();
    void stepBackward();
    void setPlayhead(bl::Time t);  // external scrub

    // Advances the playhead by dt while playing; loops or auto-pauses at end
    // of media depending on loop state.
    void tick(bl::Duration dt);

    void setPlayheadHandler(Handler handler);
    void setStateHandler(Handler handler);

private:
    void notifyPlayhead();
    void notifyState();
    bool hasLoopRange() const;
    bl::Time effectiveLoopIn() const;
    bl::Time effectiveLoopOut() const;
    void clampPlayheadToRange();

    bl::Rational fps_{24000, 1001};
    bl::Time duration_{};
    bl::Time playhead_{};
    double rate_{1.0};
    bool playing_{false};
    bool loop_{false};
    bool hasLoopRange_{false};
    bl::Time loopIn_{};
    bl::Time loopOut_{};
    Handler playheadHandler_;
    Handler stateHandler_;
};

} // namespace bl::ui