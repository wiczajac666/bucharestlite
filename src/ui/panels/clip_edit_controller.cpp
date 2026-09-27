#include "panels/clip_edit_controller.hpp"

#include <bl_core/command.hpp>

#include <memory>
#include <utility>

namespace bl::ui {

using TrackT = Track<Clip>;

const Clip* ClipEditController::findClip(const Sequence& seq, int flat,
                                         const ClipId& id) const {
    if (flat < 0 ||
        static_cast<size_t>(flat) >=
            seq.videoTracks.size() + seq.audioTracks.size()) {
        return nullptr;
    }
    for (const auto& c : trackAt(seq, flat).clips()) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const TrackT& ClipEditController::trackAt(const Sequence& seq, int flat) const {
    if (isVideoTrack(seq, flat)) {
        return seq.videoTracks[static_cast<size_t>(flat)];
    }
    return seq.audioTracks[static_cast<size_t>(kindIndex(seq, flat))];
}

bool ClipEditController::setClipName(int flatTrack, const ClipId& id,
                                     const std::string& name) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (clip->name == name) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Rename clip",
        [this, video, idx, id, name] {
            if (video) {
                timeline_->setClipNameInVideoTrack(idx, id, name);
            } else {
                timeline_->setClipNameInAudioTrack(idx, id, name);
            }
        },
        [this, video, idx, id, old = clip->name] {
            if (video) {
                timeline_->setClipNameInVideoTrack(idx, id, old);
            } else {
                timeline_->setClipNameInAudioTrack(idx, id, old);
            }
        }));
    return true;
}

bool ClipEditController::setColorLabel(int flatTrack, const ClipId& id,
                                       uint32_t label) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (clip->colorLabel == label) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set clip color",
        [this, video, idx, id, label] {
            if (video) {
                timeline_->setClipColorLabelInVideoTrack(idx, id, label);
            } else {
                timeline_->setClipColorLabelInAudioTrack(idx, id, label);
            }
        },
        [this, video, idx, id, old = clip->colorLabel] {
            if (video) {
                timeline_->setClipColorLabelInVideoTrack(idx, id, old);
            } else {
                timeline_->setClipColorLabelInAudioTrack(idx, id, old);
            }
        }));
    return true;
}

bool ClipEditController::setSubtitleText(int flatTrack, const ClipId& id,
                                         const std::string& text) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (clip->subtitleText == text) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set clip subtitle",
        [this, video, idx, id, text] {
            if (video) {
                timeline_->setSubtitleTextInVideoTrack(idx, id, text);
            } else {
                timeline_->setSubtitleTextInAudioTrack(idx, id, text);
            }
        },
        [this, video, idx, id, old = clip->subtitleText.value_or("")] {
            if (video) {
                timeline_->setSubtitleTextInVideoTrack(idx, id, old);
            } else {
                timeline_->setSubtitleTextInAudioTrack(idx, id, old);
            }
        }));
    return true;
}

bool ClipEditController::setSourceRange(int flatTrack, const ClipId& id,
                                        Time sourceIn, Time sourceOut) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (sourceIn > sourceOut) return false;
    if (clip->source.sourceIn == sourceIn &&
        clip->source.sourceOut == sourceOut) {
        return false;
    }

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set source range",
        [this, video, idx, id, sourceIn, sourceOut] {
            if (video) {
                timeline_->setClipSourceRangeInVideoTrack(idx, id, sourceIn,
                                                          sourceOut);
            } else {
                timeline_->setClipSourceRangeInAudioTrack(idx, id, sourceIn,
                                                          sourceOut);
            }
        },
        [this, video, idx, id, oldIn = clip->source.sourceIn,
         oldOut = clip->source.sourceOut] {
            if (video) {
                timeline_->setClipSourceRangeInVideoTrack(idx, id, oldIn,
                                                          oldOut);
            } else {
                timeline_->setClipSourceRangeInAudioTrack(idx, id, oldIn,
                                                          oldOut);
            }
        }));
    return true;
}

bool ClipEditController::setGain(int flatTrack, const ClipId& id,
                                 double gain) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (clip->audio.gain == gain) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set clip gain",
        [this, video, idx, id, gain] {
            if (video) {
                timeline_->setClipGainInVideoTrack(idx, id, gain);
            } else {
                timeline_->setClipGainInAudioTrack(idx, id, gain);
            }
        },
        [this, video, idx, id, old = clip->audio.gain] {
            if (video) {
                timeline_->setClipGainInVideoTrack(idx, id, old);
            } else {
                timeline_->setClipGainInAudioTrack(idx, id, old);
            }
        }));
    return true;
}

bool ClipEditController::setPan(int flatTrack, const ClipId& id, double pan) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (clip->audio.pan == pan) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set clip pan",
        [this, video, idx, id, pan] {
            if (video) {
                timeline_->setClipPanInVideoTrack(idx, id, pan);
            } else {
                timeline_->setClipPanInAudioTrack(idx, id, pan);
            }
        },
        [this, video, idx, id, old = clip->audio.pan] {
            if (video) {
                timeline_->setClipPanInVideoTrack(idx, id, old);
            } else {
                timeline_->setClipPanInAudioTrack(idx, id, old);
            }
        }));
    return true;
}

bool ClipEditController::setSpeed(int flatTrack, const ClipId& id,
                                  SpeedRemap speed) {
    if (!valid()) return false;
    if (speed.rateNum <= 0 || speed.rateDen <= 0) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (clip->speed == speed) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    const SpeedRemap oldSpeed = clip->speed;
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set clip speed",
        [this, video, idx, id, speed] {
            if (video) {
                timeline_->setClipSpeedInVideoTrack(idx, id, speed);
            } else {
                timeline_->setClipSpeedInAudioTrack(idx, id, speed);
            }
        },
        [this, video, idx, id, oldSpeed] {
            if (video) {
                timeline_->setClipSpeedInVideoTrack(idx, id, oldSpeed);
            } else {
                timeline_->setClipSpeedInAudioTrack(idx, id, oldSpeed);
            }
        }));
    return true;
}

bool ClipEditController::addEffect(int flatTrack, const ClipId& id,
                                   const EffectInstance& effect) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (effect.effectId.empty()) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Add effect",
        [this, video, idx, id, effect] {
            if (video) {
                timeline_->addClipEffectInVideoTrack(idx, id, effect);
            } else {
                timeline_->addClipEffectInAudioTrack(idx, id, effect);
            }
        },
        [this, video, idx, id, count = clip->effects.size()] {
            if (video) {
                timeline_->removeClipEffectInVideoTrack(idx, id, count);
            } else {
                timeline_->removeClipEffectInAudioTrack(idx, id, count);
            }
        }));
    return true;
}

bool ClipEditController::removeEffect(int flatTrack, const ClipId& id,
                                      size_t index) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (index >= clip->effects.size()) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    const EffectInstance removed = clip->effects[index];
    const size_t originalCount = clip->effects.size();
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Remove effect",
        [this, video, idx, id, index] {
            if (video) {
                timeline_->removeClipEffectInVideoTrack(idx, id, index);
            } else {
                timeline_->removeClipEffectInAudioTrack(idx, id, index);
            }
        },
        [this, video, idx, id, index, originalCount, removed] {
            if (video) {
                timeline_->addClipEffectInVideoTrack(idx, id, removed);
                if (index < originalCount - 1) {
                    timeline_->reorderClipEffectInVideoTrack(
                        idx, id, originalCount - 1, index);
                }
            } else {
                timeline_->addClipEffectInAudioTrack(idx, id, removed);
                if (index < originalCount - 1) {
                    timeline_->reorderClipEffectInAudioTrack(
                        idx, id, originalCount - 1, index);
                }
            }
        }));
    return true;
}

bool ClipEditController::reorderEffect(int flatTrack, const ClipId& id,
                                       size_t from, size_t to) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (from == to) return false;
    if (from >= clip->effects.size() || to >= clip->effects.size()) {
        return false;
    }

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Reorder effect",
        [this, video, idx, id, from, to] {
            if (video) {
                timeline_->reorderClipEffectInVideoTrack(idx, id, from, to);
            } else {
                timeline_->reorderClipEffectInAudioTrack(idx, id, from, to);
            }
        },
        [this, video, idx, id, from, to] {
            // Undo applies the inverse move; valid because from/to were both
            // in-bounds before the move and remain so after a single swap.
            if (video) {
                timeline_->reorderClipEffectInVideoTrack(idx, id, to, from);
            } else {
                timeline_->reorderClipEffectInAudioTrack(idx, id, to, from);
            }
        }));
    return true;
}

bool ClipEditController::setEffectEnabled(int flatTrack, const ClipId& id,
                                          size_t index, bool enabled) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (index >= clip->effects.size()) return false;
    if (clip->effects[index].enabled == enabled) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Toggle effect",
        [this, video, idx, id, index, enabled] {
            if (video) {
                timeline_->setEffectEnabledInVideoTrack(idx, id, index, enabled);
            } else {
                timeline_->setEffectEnabledInAudioTrack(idx, id, index, enabled);
            }
        },
        [this, video, idx, id, index, old = clip->effects[index].enabled] {
            if (video) {
                timeline_->setEffectEnabledInVideoTrack(idx, id, index, old);
            } else {
                timeline_->setEffectEnabledInAudioTrack(idx, id, index, old);
            }
        }));
    return true;
}

bool ClipEditController::setEffectParams(int flatTrack, const ClipId& id,
                                         size_t index,
                                         const nlohmann::json& params) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;
    if (index >= clip->effects.size()) return false;
    if (!params.is_object()) return false;
    if (clip->effects[index].params == params) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set effect params",
        [this, video, idx, id, index, params] {
            if (video) {
                timeline_->setEffectParamsInVideoTrack(idx, id, index, params);
            } else {
                timeline_->setEffectParamsInAudioTrack(idx, id, index, params);
            }
        },
        [this, video, idx, id, index, old = clip->effects[index].params] {
            if (video) {
                timeline_->setEffectParamsInVideoTrack(idx, id, index, old);
            } else {
                timeline_->setEffectParamsInAudioTrack(idx, id, index, old);
            }
        }));
    return true;
}

bool ClipEditController::setKeyframe(int flatTrack, const ClipId& id,
                                     KeyChannel channel, Time t, double value,
                                     Interpolation interp) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;

    std::optional<Keyframe> previous;
    bool changed = true;
    const KeyframeTrack* track = nullptr;
    if (clip->keyframes) track = clip->keyframes->track(channel);
    if (track) {
        for (const Keyframe& k : track->samples()) {
            if (k.t == t) {
                previous = k;
                if (k.value == value && k.interpolation == interp) {
                    changed = false;
                }
                break;
            }
        }
    }
    if (!changed) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Set keyframe",
        [this, video, idx, id, channel, t, value, interp] {
            if (video) {
                timeline_->setClipKeyframeInVideoTrack(idx, id, channel, t,
                                                       value, interp);
            } else {
                timeline_->setClipKeyframeInAudioTrack(idx, id, channel, t,
                                                       value, interp);
            }
        },
        [this, video, idx, id, channel, t, previous] {
            if (previous) {
                if (video) {
                    timeline_->setClipKeyframeInVideoTrack(
                        idx, id, channel, t, previous->value,
                        previous->interpolation);
                } else {
                    timeline_->setClipKeyframeInAudioTrack(
                        idx, id, channel, t, previous->value,
                        previous->interpolation);
                }
            } else if (video) {
                timeline_->removeClipKeyframeInVideoTrack(idx, id, channel, t);
            } else {
                timeline_->removeClipKeyframeInAudioTrack(idx, id, channel, t);
            }
        }));
    return true;
}

bool ClipEditController::removeKeyframe(int flatTrack, const ClipId& id,
                                        KeyChannel channel, Time t) {
    if (!valid()) return false;
    const Sequence& seq = timeline_->sequence();
    const Clip* clip = findClip(seq, flatTrack, id);
    if (!clip) return false;

    std::optional<Keyframe> previous;
    if (clip->keyframes) {
        if (const KeyframeTrack* tr = clip->keyframes->track(channel)) {
            for (const Keyframe& k : tr->samples()) {
                if (k.t == t) {
                    previous = k;
                    break;
                }
            }
        }
    }
    if (!previous) return false;

    const bool video = isVideoTrack(seq, flatTrack);
    const size_t idx = static_cast<size_t>(kindIndex(seq, flatTrack));
    undoStack_->push(std::make_unique<FunctionCommand>(
        "Remove keyframe",
        [this, video, idx, id, channel, t] {
            if (video) {
                timeline_->removeClipKeyframeInVideoTrack(idx, id, channel, t);
            } else {
                timeline_->removeClipKeyframeInAudioTrack(idx, id, channel, t);
            }
        },
        [this, video, idx, id, channel, t, value = previous->value,
         interp = previous->interpolation] {
            if (video) {
                timeline_->setClipKeyframeInVideoTrack(idx, id, channel, t,
                                                       value, interp);
            } else {
                timeline_->setClipKeyframeInAudioTrack(idx, id, channel, t,
                                                       value, interp);
            }
        }));
    return true;
}

} // namespace bl::ui