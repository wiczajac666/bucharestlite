#include "export/export_runner.hpp"

#include "export/export_settings.hpp"

#include <bl_core/builtin_plugins.hpp>
#include <bl_core/logger.hpp>
#include <bl_export/export_types.h>
#include <bl_export/mixdown_audio_renderer.hpp>
#include <bl_export/muxer.h>
#include <bl_plugins/codec_plugin.h>
#include <bl_render/preview_engine.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace bl::ui {

namespace {

constexpr size_t kExportFrameCacheBytes = 32u * 1024u * 1024u;

struct SubtitleEvent {
    double start;
    double end;
    std::string text;
};

// Soft-subtitle events: every clip that carries non-empty subtitle text,
// clamped to the exported range. Overlapping events across tracks are kept
// (the container carries them all).
std::vector<SubtitleEvent> subtitleEvents(const bl::TimelineSnapshot& snap,
                                          double startSeconds,
                                          double endSeconds) {
    std::vector<SubtitleEvent> out;
    const bl::Sequence& seq = snap.sequence();
    const auto collect = [&](const auto& tracks) {
        for (const auto& track : tracks) {
            for (const auto& clip : track.clips()) {
                if (!clip.subtitleText || clip.subtitleText->empty()) continue;
                const double s = clip.timelineStart.toSeconds();
                const double e = s + clip.timelineDuration.toSeconds();
                const double cs = std::max(s, startSeconds);
                const double ce = std::min(e, endSeconds);
                if (ce <= cs) continue;
                out.push_back({cs, ce, *clip.subtitleText});
            }
        }
    };
    collect(seq.videoTracks);
    collect(seq.audioTracks);
    return out;
}

BlHostApi makeExportHost() {
    BlHostApi api{};
    api.host_abi_version = BL_PLUGIN_ABI_VERSION;
    api.alloc = [](size_t size, void*) { return std::malloc(size); };
    api.free = [](void* ptr, void*) { std::free(ptr); };
    api.userdata = nullptr;
    return api;
}

} // namespace

Result<void> ExportPluginSet::load(const MediaDecodeSource::Spec& spec) {
    Result<void> builtins = registerBuiltins(registry);
    if (!builtins.ok()) {
        return Result<void>::err(builtins.code(), builtins.message());
    }

    PluginLoader loader;

    // Platform default locations first (installed builds ship codecs next to
    // the app), then any explicit dirs the caller appended.
    auto defaults = loader.scanDefaultLocations();
    if (defaults.ok()) {
        for (auto& handle : defaults.value().plugins) {
            const BlCodecPlugin* plugin = handle->plugin();
            if (!plugin) continue;
            Result<void> reg = registry.registerPlugin(
                const_cast<BlCodecPlugin*>(plugin));
            if (!reg.ok()) {
                BL_LOG_DEBUG("export", "plugin registration skipped: " +
                                          reg.message());
                continue;
            }
            handles.push_back(std::move(handle));
        }
    }

    for (const auto& [dir, origin] : spec.pluginDirs) {
        auto report = loader.scanDirectory(dir, origin);
        if (!report.ok()) {
            BL_LOG_DEBUG("export", "plugin scan of '" + dir + "' skipped: " +
                                       report.message());
            continue;
        }
        for (auto& handle : report.value().plugins) {
            const BlCodecPlugin* plugin = handle->plugin();
            if (!plugin) continue;
            Result<void> reg = registry.registerPlugin(
                const_cast<BlCodecPlugin*>(plugin));
            if (!reg.ok()) {
                BL_LOG_DEBUG("export", "plugin registration skipped: " +
                                          reg.message());
                continue;
            }
            handles.push_back(std::move(handle));
        }
    }
    return Result<void>();
}

Result<void> ExportRunner::run(Input input, Progress onProgress) {
    const ExportPlan& plan = input.plan;

    // Load codec plugins (decode + encode roles registered together).
    ExportPluginSet plugins;
    Result<void> loadResult = plugins.load(input.plugins);
    if (!loadResult.ok()) {
        return loadResult;
    }

    // Decoders may be absent from scan roots; the decode path needs at least
    // the registry with what we loaded.
    if (plugins.registry.count() == 0) {
        return Result<void>::err(Err::PluginAbiMismatch,
                                 "no codec plugins available");
    }

    // Render pipeline (created per-run; PreviewEngine is single-threaded).
    PreviewEngine::Config engineCfg;
    engineCfg.outputWidth = plan.width;
    engineCfg.outputHeight = plan.height;
    engineCfg.cacheBytes = kExportFrameCacheBytes;
    engineCfg.plugins = input.plugins;
    auto engineResult = PreviewEngine::create(engineCfg, input.mediaBin);
    if (!engineResult.ok()) {
        return Result<void>::err(engineResult.code(), engineResult.message());
    }
    auto preview = std::make_unique<PreviewEngine>(
        std::move(engineResult.value()));

    // Encoding pipeline. v1 renders video plus a full timeline audio mixdown;
    // the preset's audio_enabled flag carries the user's include-audio choice.
    export_::BlFormatPreset preset = *plan.preset();

    BlHostApi host = makeExportHost();
    export_::ExportEngine encoder(plugins.registry, host);
    Result<void> initResult = encoder.initialize(&preset);
    if (!initResult.ok()) {
        return Result<void>::err(
            initResult.code(),
            "failed to initialize encoder: " + initResult.message());
    }

    // Video-only fallback when the audio encoder is unavailable.
    export_::Muxer muxer;
    encoder.configureMuxer(&muxer);
    const bl::Rational fps{plan.fps.num, static_cast<int64_t>(plan.fps.den)};
    const double exportStart =
        static_cast<double>(plan.range.firstFrame) / fps.toDouble();
    const double exportEnd = exportStart +
                             static_cast<double>(plan.range.frameCount) /
                                 fps.toDouble();
    const std::vector<SubtitleEvent> events =
        subtitleEvents(input.snapshot, exportStart, exportEnd);
    muxer.setSubtitlesEnabled(!events.empty());
    Result<void> openResult =
        muxer.open(&preset, plan.settings.outputPath.c_str());
    if (!openResult.ok()) {
        encoder.cleanup();
        return Result<void>::err(openResult.code(),
                                 "failed to open output: " +
                                     openResult.message());
    }
    encoder.setMuxer(&muxer);

    export_::BlExportJob job{};
    job.name = plan.settings.outputPath.c_str();
    job.output_path = plan.settings.outputPath.c_str();
    job.preset = &preset;
    job.frames_total = static_cast<size_t>(plan.range.frameCount);
    encoder.setJob(&job);

    const int64_t first = plan.range.firstFrame;
    const int64_t count = plan.range.frameCount;

    const auto start = std::chrono::steady_clock::now();
    bool cancelledFlag = false;
    for (int64_t i = 0; i < count && !cancelledFlag; ++i) {
        if (input.abort && input.abort->load()) {
            cancelledFlag = true;
            break;
        }
        const int64_t frame = first + i;
        const bl::Time t = bl::Time::fromFrame(frame, fps);
        auto rendered = preview->runAt(input.snapshot, t);
        if (!rendered.ok()) {
            muxer.close();
            encoder.cleanup();
            return Result<void>::err(
                Err::DecodeFailed,
                "failed to render frame " + std::to_string(frame) + ": " +
                    rendered.message());
        }
        const CompositorResult& frameResult = rendered.value();
        if (frameResult.data.empty() || frameResult.width == 0 ||
            frameResult.height == 0) {
            muxer.close();
            encoder.cleanup();
            return Result<void>::err(
                Err::DecodeFailed,
                "empty frame produced at " + std::to_string(frame));
        }

        BlFrameMeta meta{};
        meta.pts = static_cast<uint64_t>(frame < 0 ? 0 : frame);
        meta.width = frameResult.width;
        meta.height = frameResult.height;
        meta.linesize = frameResult.linesize ? frameResult.linesize
                                             : frameResult.width * 4;

        Result<export_::BlExportResult> enc =
            encoder.encodeFrameVideo(frameResult.data.data(), &meta);
        if (!enc.ok()) {
            muxer.close();
            encoder.cleanup();
            return Result<void>::err(enc.code(),
                                     "encode failed: " + enc.message());
        }

        const auto now = std::chrono::steady_clock::now();
        const double elapsed =
            std::chrono::duration<double>(now - start).count();
        if (onProgress) {
            ExportProgressInfo info;
            info.frame = static_cast<size_t>(frame);
            info.totalFrames = static_cast<size_t>(count);
            info.percent = static_cast<int>((i + 1) * 100 / count);
            info.framesPerSecond =
                elapsed > 0.0 ? static_cast<double>(i + 1) / elapsed : 0.0;
            info.etaSeconds = info.framesPerSecond > 0.0
                                  ? static_cast<double>(count - i - 1) /
                                        info.framesPerSecond
                                  : 0.0;
            onProgress(info);
        }
    }

    if (cancelledFlag) {
        muxer.close();
        encoder.cleanup();
        // Remove the partial file so the user isn't left with a truncated
        // container pretending to be a finished export.
        std::remove(plan.settings.outputPath.c_str());
        return Result<void>::err(Err::Cancelled, "export cancelled");
    }

    // Audio pass: mix every audible audio track down and feed the PCM into the
    // audio encoder. The mixdown window mirrors the rendered video range so
    // the container's audio length matches the film. Missing or undecodable
    // clips contribute silence (same policy as the metering path).
    if (preset.audio_enabled) {
        const bl::Rational fps{plan.fps.num,
                               static_cast<int64_t>(plan.fps.den)};
        const double startSeconds =
            static_cast<double>(plan.range.firstFrame) / fps.toDouble();
        const double lengthSeconds =
            static_cast<double>(plan.range.frameCount) / fps.toDouble();

        export_::TimelineMixdownOptions mix;
        mix.sampleRate = plan.sampleRate;
        mix.channels = plan.channels > 0 ? plan.channels : 2;
        mix.blockSamples = 4096;
        mix.startSample = static_cast<uint64_t>(std::llround(
            startSeconds * plan.sampleRate));
        mix.maxSamples = static_cast<uint64_t>(std::llround(
            lengthSeconds * plan.sampleRate));
        mix.host = &host;
        mix.abort = input.abort;

        auto mediaResolver =
            [mediaBin = &input.mediaBin](
                const std::string& mediaItemId) -> Result<std::string> {
            for (const auto& item : *mediaBin) {
                if (item.id == mediaItemId && !item.path.empty()) {
                    return Result<std::string>::ok(item.path);
                }
            }
            return Result<std::string>::err(
                Err::FileNotFound,
                "media item '" + mediaItemId + "' is not in the project");
        };

        std::vector<float> pcm;  // contiguous planar: plane c at offset c*samples
        Result<void> mixResult = export_::TimelineMixdown::render(
            input.snapshot.sequence(),
            mix, plugins.registry, mediaResolver,
            [&](const float* const* planes, uint32_t samples) {
                if (input.abort && input.abort->load()) {
                    return false;
                }
                pcm.resize(static_cast<size_t>(samples) * mix.channels);
                for (uint32_t c = 0; c < mix.channels; ++c) {
                    std::copy_n(planes[c], samples,
                                pcm.data() + c * samples);
                }
                BlFrameMeta meta{};
                meta.pts = 0;
                meta.sample_count = samples;
                meta.channels = mix.channels;
                meta.linesize = samples;
                Result<export_::BlExportResult> enc =
                    encoder.encodeFrameAudio(
                        reinterpret_cast<const uint8_t*>(pcm.data()), &meta);
                if (!enc.ok()) {
                    BL_LOG_WARN("export", "audio encode failed: " +
                                              enc.message());
                    return false;
                }
                return true;
            });
        if (!mixResult.ok()) {
            muxer.close();
            encoder.cleanup();
            if (mixResult.code() == Err::Cancelled) {
                std::remove(plan.settings.outputPath.c_str());
            }
            return Result<void>::err(mixResult.code(), mixResult.message());
        }
    }

    // Soft subtitles: each subtitle event becomes a MOV_TEXT sample spanning its
    // on-screen interval. Packets are written straight to the muxer (the
    // encoder has no subtitle pipeline).
    for (const SubtitleEvent& ev : events) {
        Result<void> sub = muxer.writeSubtitlePacket(
            reinterpret_cast<const uint8_t*>(ev.text.data()), ev.text.size(),
            ev.start, ev.end - ev.start);
        if (!sub.ok()) {
            BL_LOG_WARN("export", "subtitle packet skipped: " + sub.message());
        }
    }

    Result<void> fin = encoder.finalize();
    if (!fin.ok()) {
        return Result<void>::err(fin.code(), fin.message());
    }

    return Result<void>();
}

} // namespace bl::ui