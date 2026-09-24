#include "panels/mixer_controller.hpp"

#include <bl_core/command.hpp>

#include <memory>

namespace bl::ui {

using TrackT = Track<Clip>;

const TrackT& MixerController::trackAt(const Sequence& seq, int flat) const {
    if (isVideoTrack(seq, flat)) {
        return seq.videoTracks[static_cast<size_t>(flat)];
    }
    return seq.audioTracks[static_cast<size_t>(kindIndex(seq, flat))];
}

TrackT& MixerController::trackAt(Sequence& seq, int flat) const {
    if (isVideoTrack(seq, flat)) {
        return seq.videoTracks[static_cast<size_t>(flat)];
    }
    return seq.audioTracks[static_cast<size_t>(kindIndex(seq, flat))];
}

bool MixerController::setTrackGain(int flatTrack, double gain) {
    if (!valid()) return false;
    Sequence& seq = timeline_->sequence();
    if (!inBounds(seq, flatTrack)) return false;
    const double old = trackAt(seq, flatTrack).gain();
    if (old == gain) return false;

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set track gain",
        [this, flatTrack, gain] { trackAt(timeline_->sequence(), flatTrack).setGain(gain); },
        [this, flatTrack, old] { trackAt(timeline_->sequence(), flatTrack).setGain(old); }));
    return true;
}

bool MixerController::setTrackPan(int flatTrack, double pan) {
    if (!valid()) return false;
    Sequence& seq = timeline_->sequence();
    if (!inBounds(seq, flatTrack)) return false;
    const double old = trackAt(seq, flatTrack).pan();
    if (old == pan) return false;

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set track pan",
        [this, flatTrack, pan] { trackAt(timeline_->sequence(), flatTrack).setPan(pan); },
        [this, flatTrack, old] { trackAt(timeline_->sequence(), flatTrack).setPan(old); }));
    return true;
}

bool MixerController::setTrackMuted(int flatTrack, bool muted) {
    if (!valid()) return false;
    Sequence& seq = timeline_->sequence();
    if (!inBounds(seq, flatTrack)) return false;
    const bool old = trackAt(seq, flatTrack).muted();
    if (old == muted) return false;

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set track muted",
        [this, flatTrack, muted] { trackAt(timeline_->sequence(), flatTrack).setMuted(muted); },
        [this, flatTrack, old] { trackAt(timeline_->sequence(), flatTrack).setMuted(old); }));
    return true;
}

bool MixerController::setTrackSoloed(int flatTrack, bool soloed) {
    if (!valid()) return false;
    Sequence& seq = timeline_->sequence();
    if (!inBounds(seq, flatTrack)) return false;
    const bool old = trackAt(seq, flatTrack).soloed();
    if (old == soloed) return false;

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set track soloed",
        [this, flatTrack, soloed] { trackAt(timeline_->sequence(), flatTrack).setSoloed(soloed); },
        [this, flatTrack, old] { trackAt(timeline_->sequence(), flatTrack).setSoloed(old); }));
    return true;
}

bool MixerController::setTrackLocked(int flatTrack, bool locked) {
    if (!valid()) return false;
    Sequence& seq = timeline_->sequence();
    if (!inBounds(seq, flatTrack)) return false;
    const bool old = trackAt(seq, flatTrack).locked();
    if (old == locked) return false;

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set track locked",
        [this, flatTrack, locked] { trackAt(timeline_->sequence(), flatTrack).setLocked(locked); },
        [this, flatTrack, old] { trackAt(timeline_->sequence(), flatTrack).setLocked(old); }));
    return true;
}

bool MixerController::setMasterGain(double gain) {
    if (!valid()) return false;
    Sequence& seq = timeline_->sequence();
    if (seq.settings.masterGain == gain) return false;
    const double old = seq.settings.masterGain;

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set master gain",
        [this, gain] { timeline_->sequence().settings.masterGain = gain; },
        [this, old] { timeline_->sequence().settings.masterGain = old; }));
    return true;
}

bool MixerController::setMasterPan(double pan) {
    if (!valid()) return false;
    Sequence& seq = timeline_->sequence();
    if (seq.settings.masterPan == pan) return false;
    const double old = seq.settings.masterPan;

    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set master pan",
        [this, pan] { timeline_->sequence().settings.masterPan = pan; },
        [this, old] { timeline_->sequence().settings.masterPan = old; }));
    return true;
}

} // namespace bl::ui