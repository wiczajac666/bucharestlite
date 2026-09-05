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
    const double invAlpha = 1.0 - opacity;

    for (uint32_t y = 0; y < copyH; ++y) {
        uint8_t* dstRow = pixelAt(dst, dstLinesize, 0, y);
        const uint8_t* srcRow = pixelAt(src, srcLinesize, 0, y);
        for (uint32_t x = 0; x < copyW; ++x) {
            uint32_t i = x * 4;
            dstRow[i + 0] =
                static_cast<uint8_t>(srcRow[i + 0] * opacity + dstRow[i + 0] * invAlpha);
            dstRow[i + 1] =
                static_cast<uint8_t>(srcRow[i + 1] * opacity + dstRow[i + 1] * invAlpha);
            dstRow[i + 2] =
                static_cast<uint8_t>(srcRow[i + 2] * opacity + dstRow[i + 2] * invAlpha);
            dstRow[i + 3] =
                static_cast<uint8_t>(srcRow[i + 3] * opacity + dstRow[i + 3] * invAlpha);
        }
    }
}

static void alphaBlendTransition(std::vector<uint8_t>& dst, uint32_t dstW,
                                  uint32_t dstH, uint32_t dstLinesize,
                                  const std::vector<uint8_t>& srcA, uint32_t aW,
                                  uint32_t aH, uint32_t aLinesize,
                                  const std::vector<uint8_t>& srcB, uint32_t bW,
                                  uint32_t bH, uint32_t bLinesize, double t) {
    const uint32_t copyW = std::min({dstW, aW, bW});
    const uint32_t copyH = std::min({dstH, aH, bH});
    const double invT = 1.0 - t;

    for (uint32_t y = 0; y < copyH; ++y) {
        uint8_t* dstRow = pixelAt(dst, dstLinesize, 0, y);
        const uint8_t* aRow = pixelAt(srcA, aLinesize, 0, y);
        const uint8_t* bRow = pixelAt(srcB, bLinesize, 0, y);
        for (uint32_t x = 0; x < copyW; ++x) {
            uint32_t i = x * 4;
            dstRow[i + 0] = static_cast<uint8_t>(aRow[i + 0] * invT + bRow[i + 0] * t);
            dstRow[i + 1] = static_cast<uint8_t>(aRow[i + 1] * invT + bRow[i + 1] * t);
            dstRow[i + 2] = static_cast<uint8_t>(aRow[i + 2] * invT + bRow[i + 2] * t);
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

            bool anySolo = false;
            for (const auto& tr : seq.videoTracks) {
                if (tr.soloed()) { anySolo = true; break; }
            }
            if (anySolo && !track.soloed()) continue;

            for (const auto& clip : track.clips()) {
                if (!isClipActive(clip, t)) continue;

                Duration clipRelative = t - clip.timelineStart;
                double opacity = getOpacity(clip, clipRelative);

                Time sourceTime = advanceSourceTime(clip.source.sourceIn,
                                                     t - clip.timelineStart, clip.speed);

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

                if (clip.transitionOut && ti < seq.videoTracks.size()) {
                    const auto& trans = *clip.transitionOut;
                    Time transStart =
                        clip.timelineStart + clip.effectiveDuration() -
                        trans.duration;
                    if (t >= transStart && t < clip.timelineStart + clip.effectiveDuration()) {
                        double progress = 0.0;
                        if (trans.duration > Duration{}) {
                            progress = static_cast<double>(
                                (t - transStart).ticks) /
                                static_cast<double>(trans.duration.ticks);
                            progress = std::clamp(progress, 0.0, 1.0);
                        }

                        auto nextClip = findNextClip(seq, clip, ti);
                        if (nextClip) {
                            Time nextSource = advanceSourceTime(
                                nextClip->source.sourceIn, t - nextClip->timelineStart,
                                nextClip->speed);
                            auto nextFrame = provider.getFrame(
                                nextClip->source.mediaItemId, nextSource,
                                outW, outH);
                            if (nextFrame.ok() && !nextFrame->empty()) {
                                std::vector<uint8_t> nextPixels(
                                    nextFrame->data,
                                    nextFrame->data + nextFrame->dataSize);
                                applyEffectChain(nextPixels, nextFrame->width,
                                                 nextFrame->height,
                                                 nextFrame->width * 4,
                                                 nextClip->effects);
                                alphaBlendTransition(
                                    result.data, outW, outH, outLinesize,
                                    pixelData, frame.width, frame.height,
                                    frame.width * 4, nextPixels,
                                    nextFrame->width, nextFrame->height,
                                    nextFrame->width * 4, progress);
                                continue;
                            }
                        }
                    }
                }

                alphaBlend(result.data, outW, outH, outLinesize, pixelData,
                           frame.width, frame.height, frame.width * 4, opacity);
            }
        }

        return Result<CompositorResult>::ok(std::move(result));
    }

    static std::optional<Clip> findNextClip(const Sequence& seq,
                                             const Clip& current, size_t trackIdx) {
        if (trackIdx >= seq.videoTracks.size()) return std::nullopt;
        const auto& clips = seq.videoTracks[trackIdx].clips();
        for (size_t i = 0; i + 1 < clips.size(); ++i) {
            if (clips[i].id == current.id) {
                return clips[i + 1];
            }
        }
        return std::nullopt;
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
