#include "transport/transport_controller.hpp"

#include <algorithm>

namespace bl::ui {

namespace {
constexpr bl::Rational kMicros{1'000'000, 1};
} // namespace

TransportController::TransportController(bl::Rational fps) : fps_(fps) {}

void TransportController::setFps(bl::Rational fps) {
    if (fps.valid() && !fps.isZero()) {
        fps_ = fps;
    }
}

void TransportController::setDuration(bl::Time duration) {
    if (duration.ticks < 0) return;
    duration_ = duration;
    if (playhead_ > duration_) {
        setPlayhead(duration_);
    }
}

void TransportController::setRate(double rate) {
    rate_ = rate > 0.0 ? rate : 1.0;
}

void TransportController::play() {
    if (playing_) return;
    if (duration_.ticks == 0) return;
    if (playhead_ >= duration_) {
        setPlayhead(bl::Time::fromTicks(0, kMicros));
    }
    playing_ = true;
    notifyState();
}

void TransportController::pause() {
    if (!playing_) return;
    playing_ = false;
    notifyState();
}

void TransportController::togglePlay() {
    if (playing_) {
        pause();
    } else {
        play();
    }
}

void TransportController::stop() {
    playing_ = false;
    setPlayhead(bl::Time::fromTicks(0, kMicros));
    notifyState();
}

void TransportController::stepForward() {
    const bl::Duration step = bl::Duration::fromFrames(1, fps_);
    setPlayhead(playhead_ + step);
    if (playhead_ > duration_) {
        setPlayhead(duration_);
    }
}

void TransportController::stepBackward() {
    const bl::Duration step = bl::Duration::fromFrames(1, fps_);
    bl::Time t = playhead_ - step;
    if (t.ticks < 0) {
        setPlayhead(bl::Time::fromTicks(0, kMicros));
    } else {
        setPlayhead(t);
    }
}

void TransportController::setPlayhead(bl::Time t) {
    if (t.ticks < 0) {
        t = bl::Time::fromTicks(0, kMicros);
    }
    if (t > duration_) {
        t = duration_;
    }
    if (t == playhead_) return;
    playhead_ = t;
    notifyPlayhead();
}

void TransportController::tick(bl::Duration dt) {
    if (!playing_ || dt.ticks == 0) return;
    const double seconds = dt.toSeconds() * rate_;
    bl::Time next = playhead_ + bl::Duration::fromSeconds(seconds, kMicros);
    if (next >= duration_) {
        next = duration_;
        playhead_ = next;
        playing_ = false;
        notifyPlayhead();
        notifyState();
        return;
    }
    if (next == playhead_) return;
    playhead_ = next;
    notifyPlayhead();
}

void TransportController::setPlayheadHandler(Handler handler) {
    playheadHandler_ = std::move(handler);
}

void TransportController::setStateHandler(Handler handler) {
    stateHandler_ = std::move(handler);
}

void TransportController::notifyPlayhead() {
    if (playheadHandler_) playheadHandler_();
}

void TransportController::notifyState() {
    if (stateHandler_) stateHandler_();
}

} // namespace bl::ui