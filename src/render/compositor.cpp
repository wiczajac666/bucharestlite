#include <bl_render/compositor.hpp>

#include <bl_render/effect.hpp>
#include <bl_core/logger.hpp>
#include <bl_timeline/keyframes.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace bl {

// --- Pixel helpers (BGRA32) ---

static inline uint8_t* pixelAt(std::vector<uint8_t>& data, uint32_t linesize,
                                uint32_t x, uint32_t y) {
    return data.data() + y * linesize + x * 4;
}

static inline const uint8_t* pixelAt(const std::vector<uint8_t>& data,
                                      uint32_t linesize, uint32_t x, uint32_t y) {
    return data.data() + y * linesize + x * 4;
}

static void alphaBlend(std::vector<uint8_t>& dst, uint32_t dstW, uint32_t dstH,
                        uint32_t dstLinesize, const std::vector<uint8_t>& src,
                        uint32_t srcW, uint32_t srcH, uint32_t srcLinesize,
                        double opacity) {
    if (opacity <= 0.0) return;
    if (opacity > 1.0) opacity = 1.0;

    const uint32_t copyW = std::min(dstW, srcW);
    const uint32_t copyH = std::min(dstH, srcH);

    for (uint32_t y = 0; y < copyH; ++y) {
        uint8_t* dstRow = pixelAt(dst, dstLinesize, 0, y);
        const uint8_t* srcRow = pixelAt(src, srcLinesize, 0, y);
        for (uint32_t x = 0; x < copyW; ++x) {
            uint32_t i = x * 4;
            uint8_t* dstP = dstRow + i;
            const uint8_t* srcP = srcRow + i;
            // Premultiplied accumulation on the output canvas (in-app output is
            // alpha-less RGB, so coverage scales RGB directly). Per-pixel alpha
            // from the source frame participates: for opaque frames this is
            // byte-identical to the previous opacity-only blend, and keyed-out
            // (alpha 0) pixels fall through to whatever is already on the canvas.
            const double op = static_cast<double>(srcP[3]) / 255.0 * opacity;
            if (op <= 0.0) continue;
            const double invOp = 1.0 - op;
            for (int c = 0; c < 3; ++c) {
                dstP[c] = static_cast<uint8_t>(
                    static_cast<double>(srcP[c]) * op +
                    static_cast<double>(dstP[c]) * invOp);
            }
            dstP[3] = static_cast<uint8_t>(
                static_cast<double>(srcP[3]) * op +
                static_cast<double>(dstP[3]) * invOp);
        }
    }
}

// --- Transition parameter helpers ---
//
// TransitionSpec::params is free-form JSON, so read it defensively: a missing,
// mistyped or non-string/number value falls back to the documented default
// rather than throwing. This keeps every transition deterministic and total.

static std::string paramString(const nlohmann::json& params, const char* key,
                               const std::string& fallback) {
    if (!params.is_object()) return fallback;
    auto it = params.find(key);
    if (it == params.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

static int paramInt(const nlohmann::json& params, const char* key,
                    int fallback) {
    if (!params.is_object()) return fallback;
    auto it = params.find(key);
    if (it == params.end()) return fallback;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number_unsigned())
        return static_cast<int>(it->get<uint32_t>());
    if (it->is_number_float()) return static_cast<int>(it->get<double>());
    return fallback;
}

static bool paramBool(const nlohmann::json& params, const char* key) {
    if (!params.is_object()) return false;
    auto it = params.find(key);
    return it != params.end() && it->is_boolean() && it->get<bool>();
}

// Blend two full-size BGRA32 frames (`A` outgoing, `B` incoming) into `dst`
// according to `spec`, with `p` in [0,1] the transition progress. The output is
// opaque (alpha 255) and, like the rest of the compositor, truncates the
// floating-point blend to uint8. Total by construction.
static void blendTransition(std::vector<uint8_t>& dst, uint32_t W, uint32_t H,
                            const std::vector<uint8_t>& A,
                            const std::vector<uint8_t>& B, double p,
                            const TransitionSpec& spec) {
    const double prog = std::clamp(p, 0.0, 1.0);
    const uint32_t linesize = W * 4;

    // Fade through black: the model has no distinct "Fade" kind, so a fade is
    // a crossfade flagged via params. It dims the outgoing frame to black over
    // the first half, then lifts the incoming frame from black over the second.
    const bool throughBlack =
        paramBool(spec.params, "throughBlack") ||
        paramString(spec.params, "fade", std::string()) == "through_black";
    if (throughBlack) {
        const std::vector<uint8_t>* src = &A;
        double scale = 1.0 - 2.0 * prog;
        if (prog >= 0.5) {
            src = &B;
            scale = 2.0 * prog - 1.0;
        }
        scale = std::clamp(scale, 0.0, 1.0);
        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* dstRow = pixelAt(dst, linesize, 0, y);
            const uint8_t* srcRow = pixelAt(*src, linesize, 0, y);
            for (uint32_t x = 0; x < W; ++x) {
                uint32_t i = x * 4;
                dstRow[i + 0] = static_cast<uint8_t>(srcRow[i + 0] * scale);
                dstRow[i + 1] = static_cast<uint8_t>(srcRow[i + 1] * scale);
                dstRow[i + 2] = static_cast<uint8_t>(srcRow[i + 2] * scale);
                dstRow[i + 3] = 255;
            }
        }
        return;
    }

    if (spec.kind == TransitionKind::Wipe) {
        // The incoming frame is revealed from the named edge. `direction` is
        // "left" (default), "right", "up" or "down"; `softness` is the width in
        // pixels of a linear ramp straddling the moving edge (0 = hard edge).
        const std::string direction =
            paramString(spec.params, "direction", "left");
        int softness = paramInt(spec.params, "softness", 0);
        const uint32_t maxSoft = std::min(W, H);
        if (softness < 0) softness = 0;
        if (static_cast<uint32_t>(softness) > maxSoft)
            softness = static_cast<int>(maxSoft);

        const bool vertical = (direction == "up" || direction == "down");
        const bool highSide = (direction == "right" || direction == "down");
        const double extent =
            vertical ? static_cast<double>(H) : static_cast<double>(W);
        const double edge = highSide ? extent - prog * extent : prog * extent;
        const double half = static_cast<double>(softness) / 2.0;

        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* dstRow = pixelAt(dst, linesize, 0, y);
            const uint8_t* aRow = pixelAt(A, linesize, 0, y);
            const uint8_t* bRow = pixelAt(B, linesize, 0, y);
            for (uint32_t x = 0; x < W; ++x) {
                const double pos =
                    vertical ? static_cast<double>(y) : static_cast<double>(x);
                double cov;  // amount of B in [0,1]
                if (softness == 0) {
                    cov = highSide ? (pos >= edge ? 1.0 : 0.0)
                                   : (pos < edge ? 1.0 : 0.0);
                } else {
                    double ramp = (pos - (edge - half)) /
                                  static_cast<double>(softness);
                    ramp = std::clamp(ramp, 0.0, 1.0);
                    cov = highSide ? ramp : 1.0 - ramp;
                }
                uint32_t i = x * 4;
                const double inv = 1.0 - cov;
                dstRow[i + 0] =
                    static_cast<uint8_t>(aRow[i + 0] * inv + bRow[i + 0] * cov);
                dstRow[i + 1] =
                    static_cast<uint8_t>(aRow[i + 1] * inv + bRow[i + 1] * cov);
                dstRow[i + 2] =
                    static_cast<uint8_t>(aRow[i + 2] * inv + bRow[i + 2] * cov);
                dstRow[i + 3] = 255;
            }
        }
        return;
    }

    if (spec.kind == TransitionKind::Slide) {
        // The incoming frame slides in from the named edge while the outgoing
        // frame slides out by the same amount. Pixels that draw from outside
        // their source frame are black.
        const std::string direction =
            paramString(spec.params, "direction", "left");
        const bool vertical = (direction == "up" || direction == "down");
        const bool highSide = (direction == "right" || direction == "down");
        const double extent =
            vertical ? static_cast<double>(H) : static_cast<double>(W);
        const double inShift = (1.0 - prog) * extent;  // B's offset into frame
        const double outShift = prog * extent;         // A's offset into frame

        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* dstRow = pixelAt(dst, linesize, 0, y);
            for (uint32_t x = 0; x < W; ++x) {
                const double pos =
                    vertical ? static_cast<double>(y) : static_cast<double>(x);
                bool useB;
                double sample;
                if (!highSide) {
                    // "left"/"up": the leading low side shows B.
                    if (pos < prog * extent) {
                        useB = true;
                        sample = pos + inShift;
                    } else {
                        useB = false;
                        sample = pos - outShift;
                    }
                } else {
                    // "right"/"down": the leading high side shows B.
                    if (pos >= extent - prog * extent) {
                        useB = true;
                        sample = pos - inShift;
                    } else {
                        useB = false;
                        sample = pos + outShift;
                    }
                }
                const int64_t s = static_cast<int64_t>(sample);
                const int64_t limit =
                    vertical ? static_cast<int64_t>(H)
                             : static_cast<int64_t>(W);
                uint32_t i = x * 4;
                if (s < 0 || s >= limit) {
                    dstRow[i + 0] = 0;
                    dstRow[i + 1] = 0;
                    dstRow[i + 2] = 0;
                    dstRow[i + 3] = 255;
                    continue;
                }
                const std::vector<uint8_t>& src = useB ? B : A;
                const uint32_t srcX = vertical ? x : static_cast<uint32_t>(s);
                const uint32_t srcY = vertical ? static_cast<uint32_t>(s) : y;
                const uint8_t* srcP = pixelAt(src, linesize, srcX, srcY);
                dstRow[i + 0] = srcP[0];
                dstRow[i + 1] = srcP[1];
                dstRow[i + 2] = srcP[2];
                dstRow[i + 3] = 255;
            }
        }
        return;
    }

    // Crossfade and Dissolve (and any unrecognised kind): a linear per-channel
    // blend. Dissolve is the standard alpha crossfade — deterministic, with no
    // dithering — so it shares this path with Crossfade.
    const double inv = 1.0 - prog;
    for (uint32_t y = 0; y < H; ++y) {
        uint8_t* dstRow = pixelAt(dst, linesize, 0, y);
        const uint8_t* aRow = pixelAt(A, linesize, 0, y);
        const uint8_t* bRow = pixelAt(B, linesize, 0, y);
        for (uint32_t x = 0; x < W; ++x) {
            uint32_t i = x * 4;
            dstRow[i + 0] =
                static_cast<uint8_t>(aRow[i + 0] * inv + bRow[i + 0] * prog);
            dstRow[i + 1] =
                static_cast<uint8_t>(aRow[i + 1] * inv + bRow[i + 1] * prog);
            dstRow[i + 2] =
                static_cast<uint8_t>(aRow[i + 2] * inv + bRow[i + 2] * prog);
            dstRow[i + 3] = 255;
        }
    }
}

// --- Compositor::Impl ---

struct Compositor::Impl {
    CompositorConfig config;

    static bool isClipActive(const Clip& clip, Time t) {
        Time end = clip.timelineStart + clip.effectiveDuration();
        return t >= clip.timelineStart && t < end;
    }

    static double getOpacity(const Clip& clip, Duration clipRelative) {
        if (!clip.keyframes) return 1.0;
        Time t = Time::fromTicks(clipRelative.ticks, clipRelative.rate);
        auto val = evaluateChannel(*clip.keyframes, KeyChannel::Opacity, t);
        return val.value_or(1.0);
    }

    static void applyEffectChain(std::vector<uint8_t>& data, uint32_t width,
                                  uint32_t height, uint32_t linesize,
                                  const std::vector<EffectInstance>& effects) {
        auto& reg = EffectRegistry::instance();
        for (const auto& fx : effects) {
            if (!fx.enabled) continue;
            IEffect* effect = reg.find(fx.effectId);
            if (effect) {
                effect->apply(data, width, height, linesize, fx.params);
            }
        }
    }

    static void fillTransparent(std::vector<uint8_t>& data) {
        std::fill(data.begin(), data.end(), 0);
    }

    Result<CompositorResult> render(const TimelineSnapshot& snapshot, Time t,
                                    IDecodeProvider& provider) {
        const auto& seq = snapshot.sequence();
        const uint32_t outW = config.outputWidth;
        const uint32_t outH = config.outputHeight;
        const uint32_t outLinesize = outW * 4;

        CompositorResult result;
        result.width = outW;
        result.height = outH;
        result.linesize = outLinesize;
        result.presentationTime = t;
        result.data.resize(outLinesize * outH, 0);

        for (size_t ti = 0; ti < seq.videoTracks.size(); ++ti) {
            const auto& track = seq.videoTracks[ti];
            if (track.muted()) continue;
            if (track.hidden()) continue;

            bool anySolo = false;
            for (const auto& tr : seq.videoTracks) {
                if (tr.soloed()) { anySolo = true; break; }
            }
            if (anySolo && !track.soloed()) continue;

            // A transition is a first-class operation evaluated before the
            // per-clip pass. When it owns the whole track at this instant we
            // composite it and move on; otherwise the normal per-clip loop
            // renders the track exactly as it did before.
            if (renderTransitionForTrack(track, t, provider, outW, outH,
                                         outLinesize, result.data)) {
                continue;
            }

            for (const auto& clip : track.clips()) {
                // Disabled clips keep their timeline slot but render nothing;
                // audio-only halves never carry picture.
                if (!clip.enabled || !clip.hasVideo) continue;
                if (!isClipActive(clip, t)) continue;

                Duration clipRelative = t - clip.timelineStart;
                double opacity = getOpacity(clip, clipRelative);

                Time sourceTime = clip.sourceTimeAt(t - clip.timelineStart);

                auto frameResult = provider.getFrame(
                    clip.source.mediaItemId, sourceTime, outW, outH);
                if (!frameResult.ok()) {
                    BL_LOG_WARN("render",
                                "failed to get frame for clip '" + clip.name +
                                    "': " + frameResult.message());
                    continue;
                }

                Frame frame = std::move(frameResult.value());
                if (frame.empty()) continue;

                std::vector<uint8_t> pixelData(
                    frame.data, frame.data + frame.dataSize);

                applyEffectChain(pixelData, frame.width, frame.height,
                                 frame.width * 4, clip.effects);

                alphaBlend(result.data, outW, outH, outLinesize, pixelData,
                           frame.width, frame.height, frame.width * 4, opacity);
            }
        }

        return Result<CompositorResult>::ok(std::move(result));
    }

    // Renders the outgoing-side transition of the first adjacent clip pair
    // whose transition span contains `t`, compositing the blended result onto
    // `out`. Returns true when a transition was rendered (the caller must then
    // skip the normal per-clip pass for this track at this instant) and false
    // to fall back to it.
    static bool renderTransitionForTrack(const VideoTrack& track, Time t,
                                         IDecodeProvider& provider,
                                         uint32_t outW, uint32_t outH,
                                         uint32_t outLinesize,
                                         std::vector<uint8_t>& out) {
        const auto& clips = track.clips();
        for (size_t i = 0; i + 1 < clips.size(); ++i) {
            const Clip& a = clips[i];
            const Clip& b = clips[i + 1];
            if (!a.transitionOut) continue;

            // Gate the outgoing side exactly like the normal clip loop.
            if (!a.enabled || !a.hasVideo) continue;
            // An incoming clip that renders nothing cannot be a transition
            // target: fall back so the outgoing clip renders normally.
            if (!b.enabled || !b.hasVideo) continue;

            const TransitionSpec& spec = *a.transitionOut;
            const Duration dur = spec.duration;
            if (dur <= Duration{}) continue;

            const Time aEnd = a.timelineStart + a.effectiveDuration();

            // The timeline span the transition occupies depends on alignment:
            // Left sits entirely on the outgoing tail, Right entirely on the
            // incoming head, Center straddles the cut by half a duration each
            // way. Half-duration is integer ticks (exact for even counts).
            const int64_t half = dur.ticks / 2;
            Time spanStart;
            Time spanEnd;
            switch (spec.alignment) {
                case TransitionAlignment::Left:
                    spanStart = aEnd - dur;
                    spanEnd = aEnd;
                    break;
                case TransitionAlignment::Right:
                    spanStart = aEnd;
                    spanEnd = aEnd + dur;
                    break;
                case TransitionAlignment::Center:
                default:
                    spanStart = aEnd - Duration::fromTicks(half, dur.rate);
                    spanEnd = aEnd + Duration::fromTicks(half, dur.rate);
                    break;
            }
            if (!(spanStart <= t && t < spanEnd)) continue;

            double progress = static_cast<double>((t - spanStart).ticks) /
                              static_cast<double>(dur.ticks);
            progress = std::clamp(progress, 0.0, 1.0);

            // Clamp each side's timeline-relative offset into its own span so a
            // Center/Right transition can hold the outgoing clip's last frame
            // (and a Center/Left transition the incoming clip's first frame).
            Duration aOffset = t - a.timelineStart;
            if (aOffset < Duration{})
                aOffset = Duration::fromTicks(0, aOffset.rate);
            if (aOffset > a.effectiveDuration()) aOffset = a.effectiveDuration();

            Duration bOffset = t - b.timelineStart;
            if (bOffset < Duration{})
                bOffset = Duration::fromTicks(0, bOffset.rate);
            if (bOffset > b.effectiveDuration()) bOffset = b.effectiveDuration();

            auto aFrameResult =
                provider.getFrame(a.source.mediaItemId, a.sourceTimeAt(aOffset),
                                  outW, outH);
            if (!aFrameResult.ok() || aFrameResult->empty()) {
                BL_LOG_WARN("render",
                            "failed to get transition frame for clip '" +
                                a.name + "': " + aFrameResult.message());
                return false;
            }
            auto bFrameResult =
                provider.getFrame(b.source.mediaItemId, b.sourceTimeAt(bOffset),
                                  outW, outH);
            if (!bFrameResult.ok() || bFrameResult->empty()) {
                BL_LOG_WARN("render",
                            "failed to get transition frame for clip '" +
                                b.name + "': " + bFrameResult.message());
                return false;
            }

            Frame aFrame = std::move(aFrameResult.value());
            Frame bFrame = std::move(bFrameResult.value());

            std::vector<uint8_t> aPixels(aFrame.data,
                                         aFrame.data + aFrame.dataSize);
            std::vector<uint8_t> bPixels(bFrame.data,
                                         bFrame.data + bFrame.dataSize);
            applyEffectChain(aPixels, aFrame.width, aFrame.height,
                             aFrame.width * 4, a.effects);
            applyEffectChain(bPixels, bFrame.width, bFrame.height,
                             bFrame.width * 4, b.effects);

            std::vector<uint8_t> blended(
                static_cast<size_t>(outW) * outH * 4, 0);
            blendTransition(blended, outW, outH, aPixels, bPixels, progress,
                            spec);

            alphaBlend(out, outW, outH, outLinesize, blended, outW, outH,
                       outW * 4, 1.0);
            return true;
        }
        return false;
    }
};

// --- Compositor public API ---

Result<Compositor> Compositor::create(const CompositorConfig& config) {
    if (config.outputWidth == 0 || config.outputHeight == 0) {
        return Result<Compositor>::err(Err::InvalidArgument,
                                       "output dimensions must be non-zero");
    }

    Compositor comp;
    comp.impl_ = std::make_unique<Impl>();
    comp.impl_->config = config;

    BL_LOG_INFO("render",
                "compositor created " + std::to_string(config.outputWidth) + "x" +
                    std::to_string(config.outputHeight));
    return Result<Compositor>::ok(std::move(comp));
}

Compositor::Compositor(Compositor&&) noexcept = default;
Compositor& Compositor::operator=(Compositor&&) noexcept = default;
Compositor::~Compositor() = default;

Result<CompositorResult> Compositor::renderFrame(
    const TimelineSnapshot& snapshot, Time t, IDecodeProvider& provider) {
    if (!impl_) {
        return Result<CompositorResult>::err(Err::InvalidArgument,
                                              "compositor not initialized");
    }
    return impl_->render(snapshot, t, provider);
}

const CompositorConfig& Compositor::config() const noexcept {
    return impl_->config;
}

} // namespace bl
