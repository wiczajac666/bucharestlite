#include <bl_export/mixdown_audio_renderer.hpp>

#include <bl_export/audio_pcm_source.hpp>

#include <bl_audio/types.hpp>
#include <bl_core/logger.hpp>
#include <bl_plugins/codec_plugin.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace bl {
namespace export_ {

namespace {

struct ActiveClip {
    const bl::Clip* clip{nullptr};
    const bl::Track<bl::Clip>* track{nullptr};

    uint64_t ts0{0};   // timeline window start (target sample grid)
    uint64_t ts1{0};   // timeline window end (exclusive)
    int64_t src0{0};   // source window start (target sample grid)
    int64_t srcOut{0}; // source window end (target sample grid)
    bool reversed{false};
    double invSpeed{1.0};

    uint64_t nextTimeline{0};  // next timeline sample to emit
    double nextSrc{0.0};       // source position (fractional) at nextTimeline

    uint32_t srcChannels{1};             // decoded channel count (1 or 2)
    int64_t lastSeek{-1};                // last source sample sought to
    std::unique_ptr<PcmAudioSource> src;  // lazily opened
};

// Round a seconds value onto the target sample grid.
uint64_t secToSample(double seconds, uint32_t rate) {
    return static_cast<uint64_t>(std::llround(seconds * rate));
}

bool isValidSpeed(const bl::SpeedRemap& speed) {
    return speed.rateNum > 0 && speed.rateDen > 0;
}

// Volume keyframes are stamped in clip-local time (matching the render
// engine's transform/opacity channels), so the keyframe is evaluated at the
// sample position measured from the clip's start.
double volumeModAt(const bl::Clip& clip, uint64_t local, uint32_t rate) {
    if (!clip.keyframes) return 1.0;
    const bl::KeyframeTrack* volume = clip.keyframes->track(KeyChannel::Volume);
    if (!volume || volume->empty()) return 1.0;
    const bl::Rational grid{1, static_cast<int64_t>(rate)};
    const bl::Time t =
        bl::Time::fromTicks(static_cast<int64_t>(local), grid);
    return volume->evaluate(t);
}

}  // namespace

uint64_t timelineMixdownSampleCount(const bl::Sequence& sequence,
                                    uint32_t sampleRate) {
    double endSeconds = 0.0;
    const auto scan = [&endSeconds](const std::vector<bl::Clip>& clips) {
        for (const auto& clip : clips) {
            const double end = (clip.timelineStart + clip.timelineDuration)
                                   .toSeconds();
            if (end > endSeconds) endSeconds = end;
        }
    };
    for (const auto& track : sequence.videoTracks) {
        scan(track.clips());
    }
    for (const auto& track : sequence.audioTracks) {
        scan(track.clips());
    }
    return secToSample(endSeconds, sampleRate);
}

Result<void> TimelineMixdown::render(const Sequence& sequence,
                                     const TimelineMixdownOptions& opts,
                                     const CodecRegistry& registry,
                                     MediaResolver mediaResolver,
                                     BlockCallback onBlock) {
    if (opts.sampleRate == 0 || opts.host == nullptr ||
        opts.channels == 0 || opts.blockSamples == 0 ||
        (opts.channels > 1 && opts.channels > 2)) {
        return Result<void>::err(
            Err::InvalidArgument,
            "mixdown requires a positive rate/channels/block and a host");
    }
    if (!mediaResolver || !onBlock) {
        return Result<void>::err(Err::InvalidArgument,
                                 "mixdown requires a media resolver and "
                                 "a block callback");
    }
    const uint32_t outChannels = static_cast<uint32_t>(opts.channels);
    if (outChannels > 2) {
        return Result<void>::err(Err::InvalidArgument, "max 2 mixdown channels");
    }

    // Collect the clips each audio track contributes (skip muted tracks and
    // tracks hidden by solo), mirroring meterLevelsAt().
    bool anySolo{false};
    for (const auto& track : sequence.audioTracks) {
        anySolo = anySolo || track.soloed();
    }
    std::vector<ActiveClip> clips;
    for (const auto& track : sequence.audioTracks) {
        if (track.muted() || (anySolo && !track.soloed())) continue;
        for (const bl::Clip& clip : track.clips()) {
            if (clip.source.mediaItemId.empty()) continue;
            const double startSec = clip.timelineStart.toSeconds();
            const double endSec =
                (clip.timelineStart + clip.timelineDuration).toSeconds();
            if (endSec <= startSec) continue;
            const double srcIn = clip.source.sourceIn.toSeconds();
            const double srcOut = clip.source.sourceOut.toSeconds();
            if (srcOut <= srcIn || srcOut <= 0.0) continue;

            ActiveClip active;
            active.clip = &clip;
            active.track = &track;
            active.ts0 = secToSample(startSec, opts.sampleRate);
            active.ts1 = secToSample(endSec, opts.sampleRate);
            active.src0 = static_cast<int64_t>(secToSample(
                srcIn, opts.sampleRate));
            const int64_t srcOutSample = static_cast<int64_t>(
                secToSample(srcOut, opts.sampleRate));
            const double speedK =
                isValidSpeed(clip.speed)
                    ? static_cast<double>(clip.speed.rateNum) /
                          static_cast<double>(clip.speed.rateDen)
                    : 1.0;
            active.invSpeed = speedK;
            active.reversed = clip.speed.reversed;
            active.srcOut = srcOutSample;
            active.nextTimeline = active.ts0;
            active.nextSrc = active.reversed
                                 ? static_cast<double>(srcOutSample)
                                 : static_cast<double>(active.src0);
            if (srcOutSample > active.src0) {
                clips.push_back(std::move(active));
            }
        }
    }

    if (clips.empty()) {
        BL_LOG_TRACE("export", "mixdown: no audible audio clips");
    }

    const uint64_t timelineEnd = [&]() {
        uint64_t end = 0;
        for (const auto& c : clips) {
            end = std::max(end, static_cast<uint64_t>(c.ts1));
        }
        return end;
    }();
    const uint64_t extractStart = opts.startSample;
    uint64_t extractEnd = timelineEnd;
    if (opts.maxSamples > 0) {
        extractEnd = std::min<uint64_t>(extractEnd,
                                        extractStart + opts.maxSamples);
    }
    if (extractEnd <= extractStart) {
        return Result<void>();
    }

    // Per-block output buffer: outChannels interleaved planar block.
    std::vector<float> blockData(static_cast<size_t>(opts.blockSamples) *
                                 outChannels, 0.0f);
    std::vector<float*> planes(outChannels, nullptr);
    for (uint32_t c = 0; c < outChannels; ++c) {
        planes[c] = blockData.data() + c * opts.blockSamples;
    }

    // Per-block temporary pulled source buffer (largest possible window).
    std::vector<float> pull0(opts.blockSamples, 0.0f);
    std::vector<float> pull1(opts.blockSamples, 0.0f);

    const float masterGain = static_cast<float>(sequence.settings.masterGain);
    const auto [masterLeft, masterRight] =
        PanLaw::compute(sequence.settings.masterPan);

    uint64_t blockStart = extractStart;
    while (blockStart < extractEnd) {
        const uint32_t samples =
            static_cast<uint32_t>(std::min<uint64_t>(
                opts.blockSamples, extractEnd - blockStart));
        const uint64_t blockEnd = blockStart + samples;

        for (uint32_t c = 0; c < outChannels; ++c) {
            std::fill(planes[c], planes[c] + samples, 0.0f);
        }

        for (ActiveClip& active : clips) {
            if (active.ts1 <= blockStart || active.ts0 >= blockEnd) continue;
            if (active.nextTimeline < active.ts0) {
                active.nextTimeline = active.ts0;
                active.nextSrc = active.reversed
                                     ? static_cast<double>(active.srcOut)
                                     : static_cast<double>(active.src0);
            }
            if (active.nextTimeline < blockStart) {
                // Align the running phase to this block; recompute exactly so
                // slow clips (speed < 1) stay sample-accurate across blocks.
                const uint64_t offset = blockStart - active.ts0;
                if (active.reversed) {
                    active.nextSrc =
                        static_cast<double>(active.srcOut) -
                        static_cast<double>(offset) * active.invSpeed;
                } else {
                    active.nextSrc =
                        static_cast<double>(active.src0) +
                        static_cast<double>(offset) * active.invSpeed;
                }
                active.nextTimeline = blockStart;
            }
            if (active.nextTimeline >= blockEnd) continue;

            const uint64_t segStart = active.nextTimeline;
            const uint64_t segEnd = std::min(active.ts1, blockEnd);
            const uint64_t segLen = segEnd - segStart;

            // Source window for this segment, always expressed as an ascending
            // [lo, hi) pull range even when the clip plays in reverse.
            const double loFrac = active.reversed
                                      ? active.nextSrc -
                                            static_cast<double>(segLen) *
                                                active.invSpeed
                                      : active.nextSrc;
            const double hiFrac = active.reversed
                                      ? active.nextSrc
                                      : active.nextSrc +
                                            static_cast<double>(segLen) *
                                                active.invSpeed;
            const int64_t srcStartAbs = std::max<int64_t>(
                static_cast<int64_t>(std::floor(loFrac)), 0);
            const int64_t srcEndAbs =
                static_cast<int64_t>(std::ceil(hiFrac));
            const uint32_t need =
                static_cast<uint32_t>(
                    std::max<int64_t>(srcEndAbs - srcStartAbs, 1));
            if (need > opts.blockSamples) {
                // Should not happen (block covers whole timeline window); skip
                // the clip rather than fail the whole export.
                active.nextTimeline = active.ts1;
                continue;
            }

            // Lazily open the clip's source and position it. Media that cannot
            // be decoded (no audio stream, unsupported codec, missing file)
            // contributes silence, exactly as it does on the metering path.
            if (!active.src) {
                auto path = mediaResolver(active.clip->source.mediaItemId);
                if (!path.ok()) {
                    BL_LOG_WARN("export", "mixdown: skipping clip '" +
                                              active.clip->id + "': " +
                                              path.message());
                    active.nextTimeline = active.ts1;
                    continue;
                }
                PcmAudioSource::OpenRequest req;
                req.path = *path;
                req.targetSampleRate = opts.sampleRate;
                req.host = opts.host;
                auto src = PcmAudioSource::open(req, registry);
                if (!src.ok()) {
                    BL_LOG_WARN("export", "mixdown: skipping clip '" +
                                              active.clip->id + "': " +
                                              src.message());
                    active.nextTimeline = active.ts1;
                    continue;
                }
                active.src = std::make_unique<PcmAudioSource>(
                    std::move(src.value()));
                active.srcChannels = active.src->channels();
                active.lastSeek = -1;
            }

            if (active.lastSeek != srcStartAbs) {
                auto seek = active.src->seekToSeconds(
                    static_cast<double>(srcStartAbs) / opts.sampleRate);
                if (!seek.ok()) {
                    BL_LOG_WARN("export", "mixdown: skipping clip '" +
                                              active.clip->id + "': " +
                                              seek.message());
                    active.nextTimeline = active.ts1;
                    continue;
                }
                active.lastSeek = srcStartAbs;
            }

            const uint32_t got = active.src->pull(
                pull0.data(),
                active.srcChannels >= 2 ? pull1.data() : nullptr, need);
            if (got > need) {
                BL_LOG_WARN("export", "mixdown: skipping clip '" +
                                          active.clip->id + "': over-pull");
                active.nextTimeline = active.ts1;
                continue;
            }

            const bool stereo =
                active.srcChannels >= 2 && outChannels >= 2;
            const auto [clipLeft, clipRight] =
                stereo ? PanLaw::compute(active.clip->audio.pan)
                       : std::pair<double, double>{1.0, 1.0};
            const auto [trackLeft, trackRight] =
                stereo ? PanLaw::compute(active.track->pan())
                       : std::pair<double, double>{1.0, 1.0};
            const double g = active.clip->audio.gain * active.track->gain();

            // First source sample a reversed window emits (the last sample of the
            // window, prior to the exclusive end).
            const double readStart =
                active.reversed ? hiFrac - 1.0 : loFrac;

            for (uint64_t i = 0; i < segLen; ++i) {
                const int64_t srcIdx = active.reversed
                                           ? static_cast<int64_t>(std::floor(
                                                 readStart -
                                                 static_cast<double>(i) *
                                                     active.invSpeed))
                                           : static_cast<int64_t>(std::floor(
                                                 readStart +
                                                 static_cast<double>(i) *
                                                     active.invSpeed));
                const int64_t k = srcIdx - srcStartAbs;
                const float in0 = (k >= 0 && k < static_cast<int64_t>(got))
                                      ? pull0[k]
                                      : 0.0f;
                const float in1 =
                    (stereo && k >= 0 && k < static_cast<int64_t>(got))
                        ? pull1[k]
                        : 0.0f;

                const uint64_t outIdx = segStart + i - blockStart;
                const double volMod = volumeModAt(
                    *active.clip, segStart + i - active.ts0, opts.sampleRate);
                const double gain = g * volMod;

                if (stereo) {
                    planes[0][outIdx] +=
                        static_cast<float>(in0 * gain * clipLeft * trackLeft);
                    planes[1][outIdx] +=
                        static_cast<float>(in1 * gain * clipRight * trackRight);
                } else {
                    // Mono input feeds the left bus only (mirrors TrackStrip
                    // and the metering path).
                    planes[0][outIdx] +=
                        static_cast<float>(in0 * gain);
                }
            }

            active.nextSrc = active.reversed ? loFrac : hiFrac;
            active.nextTimeline = segEnd;
        }

        // Master gain/pan stage.
        const float masterL = masterGain * static_cast<float>(masterLeft);
        const float masterR = masterGain * static_cast<float>(masterRight);
        for (uint64_t i = 0; i < samples; ++i) {
            if (outChannels >= 2) {
                planes[0][i] *= masterL;
                planes[1][i] *= masterR;
            } else {
                planes[0][i] *= static_cast<float>(masterGain);
            }
        }

        if (opts.abort && opts.abort->load()) {
            return Result<void>::err(Err::Cancelled, "mixdown cancelled");
        }
        if (!onBlock(planes.data(), samples)) {
            return Result<void>::err(Err::Cancelled, "mixdown aborted");
        }

        blockStart += samples;
    }

    return Result<void>();
}

}  // namespace export_
}  // namespace bl