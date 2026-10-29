#include "panels/preview_panel.hpp"

#include "app/project_controller.hpp"

#include <bl_audio/audio_engine.hpp>
#include <bl_core/builtin_plugins.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/decoder_bridge.hpp>
#include <bl_core/logger.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_plugins/codec_plugin.h>
#include <bl_render/preview_engine.hpp>
#include <bl_timeline/timeline.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace bl::ui {

namespace {

constexpr const char* kPlayGlyph = "\u25B6";       // ▶
constexpr const char* kPauseGlyph = "\u23F8";      // ⏸
constexpr const char* kStopGlyph = "\u25A0";       // ■
constexpr const char* kStepBackGlyph = "\u2190";   // ←
constexpr const char* kStepForwardGlyph = "\u2192"; // →

// Renders the compositor output at (or below) sequence resolution while
// keeping the long edge bounded so preview stays cheap on big timelines.
PreviewEngine::Config previewConfig(const bl::SequenceSettings& settings,
                                    const MediaDecodeSource::Spec& plugins) {
    constexpr uint32_t kMaxPreviewEdge = 1024;
    const double scale =
        std::min(1.0, static_cast<double>(kMaxPreviewEdge) /
                          std::max<uint32_t>(1,
                                             std::max<uint32_t>(settings.width,
                                                                settings.height)));
    const auto size = [&](int32_t dim) -> uint32_t {
        return static_cast<uint32_t>(std::max<int64_t>(1, llround(dim * scale)));
    };
    PreviewEngine::Config cfg;
    cfg.outputWidth = size(settings.width);
    cfg.outputHeight = size(settings.height);
    cfg.plugins = plugins;
    return cfg;
}

} // namespace

// --- PreviewSurface --------------------------------------------------------

PreviewSurface::PreviewSurface(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("PreviewSurface"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumSize(120, 68);
    setFocusPolicy(Qt::NoFocus);
}

void PreviewSurface::setFrame(const CompositorResult& result) {
    if (result.width == 0 || result.height == 0 || result.data.empty()) {
        clearFrame();
        return;
    }
    const QImage raw(reinterpret_cast<const uchar*>(result.data.data()),
                     static_cast<int>(result.width), static_cast<int>(result.height),
                     static_cast<int>(result.linesize ? result.linesize
                                                      : result.width * 4),
                     QImage::Format_RGB32);
    image_ = raw.copy();
    message_.clear();
    update();
}

void PreviewSurface::clearFrame() {
    image_ = QImage();
    message_ = tr("No preview");
    update();
}

void PreviewSurface::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (!image_.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        QSize target = image_.size();
        target.scale(size(), Qt::KeepAspectRatio);
        const QPoint origin((width() - target.width()) / 2,
                            (height() - target.height()) / 2);
        painter.drawImage(QRect(origin, target), image_);
    } else if (!message_.isEmpty()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, message_);
    }
}

// --- PreviewPanel ----------------------------------------------------------

PreviewPanel::PreviewPanel(ProjectController* controller,
                           QWidget* parent,
                           std::vector<std::pair<std::string, bl::PluginOrigin>> pluginDirs)
    : QWidget(parent), controller_(controller), pluginDirs_(std::move(pluginDirs)) {
    setObjectName(QStringLiteral("PreviewPanel"));
    setWindowTitle(tr("Preview"));

    // The scopes dock receives frames through a signal; register the payload so
    // Qt can carry it (direct connections copy nothing, but this keeps queued
    // use legal too).
    qRegisterMetaType<bl::CompositorResult>("bl::CompositorResult");

    surface_ = new PreviewSurface(this);

    // Build a codec registry for audio decoding from the configured plugin dirs.
    (void)bl::registerBuiltins(audioRegistry_);
    {
        bl::PluginLoader loader;
        for (const auto& [dir, origin] : pluginDirs_) {
            auto report = loader.scanDirectory(dir, origin);
            if (!report.ok()) continue;
            for (auto& handle : report.value().plugins) {
                (void)audioRegistry_.registerPlugin(const_cast<BlCodecPlugin*>(handle->plugin()));
                audioHandles_.push_back(std::move(handle));
            }
        }
    }

    if (auto r = bl::AudioEngine::create(bl::createDefaultAudioOutput()); r.ok()) {
        audioEngine_ = std::make_unique<bl::AudioEngine>(std::move(r.value()));
    }

    playButton_ = new QToolButton(this);
    playButton_->setObjectName(QStringLiteral("previewPlayButton"));
    playButton_->setText(tr(kPlayGlyph));
    playButton_->setToolTip(tr("Play/pause"));
    connect(playButton_, &QToolButton::clicked, this, [this] {
        if (transport_.playing()) {
            transport_.pause();
        } else {
            transport_.play();
        }
    });

    stopButton_ = new QToolButton(this);
    stopButton_->setText(tr(kStopGlyph));
    stopButton_->setToolTip(tr("Stop (rewind)"));
    connect(stopButton_, &QToolButton::clicked, this,
            [this] { transport_.stop(); });

    stepBackButton_ = new QToolButton(this);
    stepBackButton_->setText(tr(kStepBackGlyph));
    stepBackButton_->setToolTip(tr("Step back one frame"));
    connect(stepBackButton_, &QToolButton::clicked, this,
            [this] { transport_.stepBackward(); });

    stepForwardButton_ = new QToolButton(this);
    stepForwardButton_->setText(tr(kStepForwardGlyph));
    stepForwardButton_->setToolTip(tr("Step forward one frame"));
    connect(stepForwardButton_, &QToolButton::clicked, this,
            [this] { transport_.stepForward(); });

    timecodeLabel_ = new QLabel(this);
    timecodeLabel_->setObjectName(QStringLiteral("previewTimecode"));
    timecodeLabel_->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    timecodeLabel_->setMinimumWidth(90);

    auto* transportBar = new QHBoxLayout;
    transportBar->setContentsMargins(4, 2, 4, 2);
    transportBar->setSpacing(4);
    transportBar->addWidget(playButton_);
    transportBar->addWidget(stopButton_);
    transportBar->addWidget(stepBackButton_);
    transportBar->addWidget(stepForwardButton_);
    transportBar->addSpacing(8);
    transportBar->addWidget(timecodeLabel_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(surface_, 1);
    layout->addLayout(transportBar);

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, &PreviewPanel::onTimerTick);

    transport_.setPlayheadHandler([this] { onTransportPlayhead(); });
    transport_.setStateHandler([this] { onTransportState(); });

    connect(controller_, &ProjectController::projectChanged, this,
            &PreviewPanel::onProjectChanged);

    engine_.reset();
    refresh();
    clock_.start();
}

PreviewPanel::~PreviewPanel() {
    stopAudio();
}

void PreviewPanel::onProjectChanged() {
    stopAudio();
    engine_.reset();
    refresh();
}

void PreviewPanel::rebuildEngine() {
    const auto& seq = controller_->timeline().sequence();
    MediaDecodeSource::Spec spec;
    spec.pluginDirs = pluginDirs_;
    auto engine = PreviewEngine::create(previewConfig(seq.settings, spec),
                                        controller_->mediaBin());
    if (!engine.ok()) {
        BL_LOG_WARN("preview", "preview engine create failed: " + engine.message());
        surface_->clearFrame();
        return;
    }
    engineWidth_ = seq.settings.width;
    engineHeight_ = seq.settings.height;
    engineFps_ = seq.settings.fps;
    engine_ = std::make_unique<PreviewEngine>(std::move(engine.value()));
    // A rebuilt engine starts with default decode settings; restore the
    // panel's proxy configuration so toggling proxies survives project loads
    // and preview size changes.
    engine_->setUseProxies(useProxies_);
    for (const auto& [mediaItemId, useProxy] : proxyOverrides_) {
        engine_->setMediaProxyOverride(mediaItemId, useProxy);
    }
}

void PreviewPanel::refresh() {
    const auto& seq = controller_->timeline().sequence();
    transport_.setFps(seq.settings.fps);
    transport_.setDuration(sequenceDuration(seq));

    if (!engine_ || seq.settings.width != engineWidth_ ||
        seq.settings.height != engineHeight_ ||
        !exactEqual(seq.settings.fps, engineFps_)) {
        rebuildEngine();
    }
    if (!engine_) {
        updateTimecode();
        emit frameRendered(bl::CompositorResult{});
        return;
    }

    auto result = engine_->runAt(controller_->timeline().snapshot(),
                                 transport_.playhead());
    if (result.ok()) {
        surface_->setFrame(result.value());
        emit frameRendered(result.value());
    } else {
        BL_LOG_TRACE("preview", "render skipped: " + result.message());
        surface_->clearFrame();
        emit frameRendered(bl::CompositorResult{});
    }
    updateTimecode();
}

void PreviewPanel::onTimerTick() {
    const qint64 elapsedMs = clock_.restart();
    transport_.tick(bl::Duration::fromSeconds(elapsedMs / 1000.0,
                                              {1'000'000, 1}));
    if (!transport_.playing()) {
        timer_->stop();
    }
}

void PreviewPanel::onTransportPlayhead() {
    refresh();
    emit playheadChanged(transport_.playhead());
}

void PreviewPanel::onTransportState() {
    playButton_->setText(transport_.playing() ? tr(kPauseGlyph) : tr(kPlayGlyph));
    if (transport_.playing()) {
        if (!timer_->isActive()) {
            clock_.restart();
            timer_->start();
        }
        startAudio();
    } else {
        timer_->stop();
        stopAudio();
    }
    updateTimecode();
}

void PreviewPanel::setPlayheadFromTimeline(const bl::Time& t) {
    transport_.setPlayhead(t);
    // setPlayhead already triggered onTransportPlayhead (refresh + emit) if
    // the position actually changed; nothing more to do here.
}

void PreviewPanel::onTimelineChanged() {
    refresh();
}

void PreviewPanel::updateTimecode() {
    timecodeLabel_->setText(formatTimecode(transport_.playhead(),
                                           transport_.duration(),
                                           transport_.fps()));
}

QString PreviewPanel::formatTimecode(bl::Time t, bl::Time total, bl::Rational fps) {
    const double rate = fps.toDouble() > 0.0 ? fps.toDouble() : 24.0;
    const double seconds = t.toSeconds();
    const double totalSeconds = total.toSeconds();
    const int64_t totalFrames = static_cast<int64_t>(llround(totalSeconds * rate));

    const int hh = static_cast<int>(seconds / 3600.0);
    const int mm = static_cast<int>(std::fmod(seconds / 60.0, 60.0));
    const int ss = static_cast<int>(std::fmod(seconds, 60.0));
    const int ff = static_cast<int>(
        llround(std::fmod(seconds, 1.0) * rate));
    const int tf = static_cast<int>(llround(totalSeconds * rate));

    QString tc = QString::asprintf("%02d:%02d:%02d:%02d", hh, mm, ss,
                                  std::clamp(ff, 0, std::max<int>(0, llround(rate))));
    if (totalFrames >= 0) {
        tc += QString::fromLatin1(" / %1").arg(tf);
    }
    return tc;
}

bl::Time PreviewPanel::sequenceDuration(const bl::Sequence& sequence) {
    // Counts disabled clips too: they keep their timeline slot, so they still
    // define how long the sequence is (see export_plan.cpp).
    bl::Time end;
    const auto scan = [&end](const std::vector<bl::Clip>& clips) {
        for (const auto& clip : clips) {
            const bl::Time clipEnd = clip.timelineStart + clip.effectiveDuration();
            if (clipEnd > end) end = clipEnd;
        }
    };
    for (const auto& track : sequence.videoTracks) {
        scan(track.clips());
    }
    for (const auto& track : sequence.audioTracks) {
        scan(track.clips());
    }
    return end;
}

void PreviewPanel::setUseProxies(bool use) {
    if (useProxies_ == use) return;
    useProxies_ = use;
    if (engine_) {
        engine_->setUseProxies(use);
        refresh();
    }
}

void PreviewPanel::setMediaProxyOverride(const std::string& mediaItemId, std::optional<bool> useProxy) {
    if (useProxy.has_value()) {
        proxyOverrides_[mediaItemId] = useProxy;
    } else {
        proxyOverrides_.erase(mediaItemId);
    }
    if (engine_) {
        engine_->setMediaProxyOverride(mediaItemId, useProxy);
        refresh();
    }
}

std::optional<bool> PreviewPanel::mediaProxyOverride(const std::string& mediaItemId) const {
    auto it = proxyOverrides_.find(mediaItemId);
    if (it != proxyOverrides_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void PreviewPanel::startAudio() {
    if (!audioEngine_ || audioPlaying_.load()) return;
    bl::AudioMixConfig cfg;
    cfg.sampleRate = 48000;
    cfg.channels = 2;
    auto r = audioEngine_->start(cfg);
    if (!r.ok()) return;
    audioPlaying_.store(true);
    audioPlaybackThread_ = std::thread(&PreviewPanel::audioPlaybackLoop, this);
    audioFeederThread_ = std::thread(&PreviewPanel::audioFeederLoop, this);
}

void PreviewPanel::stopAudio() {
    audioPlaying_.store(false);
    if (audioPlaybackThread_.joinable()) audioPlaybackThread_.join();
    if (audioFeederThread_.joinable()) audioFeederThread_.join();
    if (audioEngine_) audioEngine_->stop();
}

void PreviewPanel::audioPlaybackLoop() {
    while (audioPlaying_.load()) {
        if (!audioEngine_) break;
        bl::AudioBuffer out;
        auto r = audioEngine_->pullMix(out, 512);
        if (!r.ok()) break;
#ifndef _WIN32
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
    }
}

void PreviewPanel::audioFeederLoop() {
    // Best-effort: decode audio for active clips around the playhead and push
    // small chunks into the engine. Runs while playback is active.
    while (audioPlaying_.load()) {
        if (!audioEngine_ || !controller_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        const auto& seq = controller_->timeline().sequence();
        const bl::Time now = transport_.playhead();
        const double chunkSeconds = 0.05; // 50 ms chunks

        // Build a codec registry is already in audioRegistry_.
        for (size_t ti = 0; ti < seq.audioTracks.size(); ++ti) {
            const auto& track = seq.audioTracks[ti];
            if (track.muted()) continue;
            for (const auto& clip : track.clips()) {
                if (!clip.enabled || !clip.hasAudio) continue;
                const bl::Time clipEnd = clip.timelineStart + clip.timelineDuration;
                if (now < clip.timelineStart || now >= clipEnd) continue;

                // Find media path
                const std::string mediaId = clip.source.mediaItemId;
                // Resolve path via controller media bin
                std::string path;
                for (const auto& item : controller_->project().mediaBin) {
                    if (item.id == mediaId) { path = item.path; break; }
                }
                if (path.empty()) continue;

                // Decode a chunk of audio starting at 'now' mapped to source time.
                // Map timeline time -> source time (simple: sourceIn + (now - timelineStart) * speed).
                double speed = clip.speed.rateNum > 0 && clip.speed.rateDen > 0
                                   ? static_cast<double>(clip.speed.rateNum) / clip.speed.rateDen
                                   : 1.0;
                if (speed == 0) speed = 1.0;
                const double srcSec = clip.source.sourceIn.toSeconds() +
                                      (now.toSeconds() - clip.timelineStart.toSeconds()) * speed;

                // Open demuxer, seek, decode until we have ~chunkSeconds of audio.
                auto demuxed = bl::Demuxer::open(path);
                if (!demuxed.ok()) continue;
                bl::Demuxer demuxer = std::move(demuxed.value());
                const bl::StreamInfo& info = demuxer.info();
                if (info.audioStreams.empty()) continue;
                const bl::AudioStreamInfo& stream = info.audioStreams.front();

                BlCodecConfig cfg{};
                cfg.abi_version = BL_PLUGIN_ABI_VERSION;
                cfg.codec_name = stream.codecName.c_str();
                cfg.extradata = stream.extradata.empty() ? nullptr : stream.extradata.data();
                cfg.extradata_size = stream.extradata.size();
                cfg.audio.sample_rate = stream.sampleRate > 0 ? stream.sampleRate : 48000;
                cfg.audio.channels = stream.channels > 0 ? stream.channels : 2;
                const BlHostApi host = {
                    BL_PLUGIN_ABI_VERSION,
                    [](size_t s, void*) { return std::malloc(s); },
                    [](void* p, void*) { std::free(p); },
                    nullptr};
                cfg.host = &host;

                auto bridgeR = bl::DecoderBridge::create(audioRegistry_, stream.codecName, cfg);
                if (!bridgeR.ok()) continue;
                bl::DecoderBridge decoder = std::move(bridgeR.value());

                uint32_t srcRate = stream.sampleRate > 0 ? stream.sampleRate : 48000;
                uint32_t srcCh = stream.channels > 0 ? stream.channels : 2;
                // Resample not implemented: target engine rate may differ; push as-is
                // and accept pitch shift if rates differ. For typical 48k media it matches.

                if (auto sk = demuxer.seek(bl::Time::fromSeconds(srcSec, {1000000,1})); !sk.ok()) {
                    // continue anyway
                }

                const uint32_t targetFrames = static_cast<uint32_t>(chunkSeconds * srcRate);
                bl::AudioBuffer buf;
                buf.resize(targetFrames, srcCh);
                uint32_t written = 0;
                bool eos = false;
                while (written < targetFrames && !eos) {
                    auto pktR = demuxer.nextPacket();
                    if (!pktR.ok()) { eos = true; break; }
                    auto pkt = std::move(pktR.value());
                    if (!pkt.has_value()) { eos = true; break; }
                    if (pkt->streamIndex != stream.index) continue;
                    auto decR = decoder.decode(*pkt);
                    if (!decR.ok()) {
                        if (decR.message() == "need more input") continue;
                        continue;
                    }
                    const bl::Frame& f = *decR;
                    if (f.empty() || f.type != bl::Frame::Type::Audio) continue;
                    const uint32_t frames = f.sampleCount;
                    const uint32_t toCopy = std::min(frames, targetFrames - written);
                    // f.data is planar float: channel c plane at offset c*linesize floats
                    for (uint32_t i = 0; i < toCopy; ++i) {
                        for (uint32_t c = 0; c < srcCh; ++c) {
                            const float* plane = reinterpret_cast<const float*>(
                                f.data + static_cast<size_t>(c) * f.linesize * sizeof(float));
                            buf.data[(written + i) * srcCh + c] = plane[i];
                        }
                    }
                    written += toCopy;
                    if (written >= targetFrames) break;
                }
                buf.sampleCount = written;
                if (written > 0) {
                    audioEngine_->pushSamples(static_cast<uint32_t>(ti), std::move(buf));
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

} // namespace bl::ui