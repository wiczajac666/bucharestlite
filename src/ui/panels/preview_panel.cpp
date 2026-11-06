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
#include <chrono>
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
    if (!audioEngine_->start(cfg).ok()) return;

    audioFeederCursor_ = -1.0;
    resetAudioReaders();
    audioPlaying_.store(true);
    audioPlaybackThread_ = std::thread(&PreviewPanel::audioPlaybackLoop, this);
    audioFeederThread_ = std::thread(&PreviewPanel::audioFeederLoop, this);
}

void PreviewPanel::stopAudio() {
    audioPlaying_.store(false);
    if (audioPlaybackThread_.joinable()) audioPlaybackThread_.join();
    if (audioFeederThread_.joinable()) audioFeederThread_.join();
    if (audioEngine_) audioEngine_->stop();
    resetAudioReaders();
    audioFeederCursor_ = -1.0;
}

void PreviewPanel::resetAudioReaders() { audioReaders_.clear(); }

void PreviewPanel::audioPlaybackLoop() {
    // pullMix writes a fixed block to the device; on blocking backends (ALSA,
    // WinMM) that write is what paces the loop. A null sink cannot pace, so
    // sleep instead of spinning.
    while (audioPlaying_.load()) {
        if (!audioEngine_) break;
        bl::AudioBuffer out;
        if (!audioEngine_->pullMix(out, 512).ok()) break;
        if (!audioEngine_->deviceBlocksOnWrite()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void PreviewPanel::audioFeederLoop() {
    constexpr double kRate = 48000.0;
    constexpr uint32_t kChunk = 2400; // 50 ms at 48 kHz

    while (audioPlaying_.load()) {
        if (!audioEngine_ || !controller_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        const double playhead = transport_.playhead().toSeconds();
        if (audioFeederCursor_ < 0.0 ||
            std::abs(playhead - audioFeederCursor_) > 0.15) {
            resetAudioReaders();
            audioEngine_->flush();
            audioFeederCursor_ = playhead;
        }

        const auto& seq = controller_->timeline().sequence();

        bool anySolo = false;
        for (const auto& track : seq.audioTracks) {
            if (track.soloed()) {
                anySolo = true;
                break;
            }
        }

        bl::AudioBuffer mix;
        mix.resize(kChunk, 2);
        mix.clear();

        const double chunkStart = audioFeederCursor_;
        const double chunkEnd = chunkStart + static_cast<double>(kChunk) / kRate;

        for (const auto& track : seq.audioTracks) {
            if (track.muted() || (anySolo && !track.soloed())) continue;

            const auto [trackL, trackR] = bl::PanLaw::compute(track.pan());

            for (const auto& clip : track.clips()) {
                if (!clip.enabled || !clip.hasAudio) continue;

                const double clipStart = clip.timelineStart.toSeconds();
                const double clipDuration = clip.timelineDuration.toSeconds();
                if (clipDuration <= 0.0) continue;
                const double clipEnd = clipStart + clipDuration;
                if (chunkEnd <= clipStart || chunkStart >= clipEnd) continue;

                std::string path;
                for (const auto& item : controller_->project().mediaBin) {
                    if (item.id == clip.source.mediaItemId) {
                        path = item.path;
                        break;
                    }
                }
                if (path.empty()) continue;

                const double overlapStart = std::max(chunkStart, clipStart);
                const double overlapEnd = std::min(chunkEnd, clipEnd);
                if (overlapEnd <= overlapStart) continue;

                const uint32_t offset = static_cast<uint32_t>(
                    std::llround((overlapStart - chunkStart) * kRate));
                if (offset >= kChunk) continue;
                const uint32_t count = std::min<uint32_t>(
                    static_cast<uint32_t>(
                        std::llround((overlapEnd - overlapStart) * kRate)),
                    kChunk - offset);
                if (count == 0) continue;

                auto it = audioReaders_.find(clip.id);
                if (it == audioReaders_.end()) {
                    auto opened = bl::PlaybackAudioSource::open(
                        {path, 48000, 2}, audioRegistry_);
                    if (!opened.ok()) continue;
                    it = audioReaders_
                             .emplace(clip.id,
                                      std::make_unique<bl::PlaybackAudioSource>(
                                          std::move(opened.value())))
                             .first;
                }
                bl::PlaybackAudioSource& reader = *it->second;

                const double speed =
                    clip.speed.rateNum > 0 && clip.speed.rateDen > 0
                        ? static_cast<double>(clip.speed.rateNum) /
                              static_cast<double>(clip.speed.rateDen)
                        : 1.0;

                const auto offsetStart = bl::Duration::fromSeconds(
                    overlapStart - clipStart, {1000000, 1});
                const auto offsetEnd = bl::Duration::fromSeconds(
                    overlapEnd - clipStart, {1000000, 1});
                const double srcStart =
                    clip.sourceTimeAt(offsetStart).toSeconds();
                const double srcEnd = clip.sourceTimeAt(offsetEnd).toSeconds();

                // Reversed clips play the source window backwards, so each
                // chunk is decoded forward from its end and flipped below.
                if (clip.speed.reversed) {
                    (void)reader.seekToSeconds(srcEnd);
                } else if (std::abs(reader.positionSeconds() - srcStart) > 0.06) {
                    (void)reader.seekToSeconds(srcStart);
                }

                const uint32_t want = std::max<uint32_t>(
                    1, static_cast<uint32_t>(std::llround(count * speed)));

                bl::AudioBuffer tmp;
                const uint32_t produced = reader.pull(tmp, want);
                if (produced == 0) continue;

                if (clip.speed.reversed) {
                    for (uint32_t c = 0; c < 2; ++c) {
                        std::reverse(tmp.channelData(c),
                                     tmp.channelData(c) + produced);
                    }
                }

                const double gain =
                    track.gain() * clip.audio.gain;
                const auto [clipL, clipR] = bl::PanLaw::compute(clip.audio.pan);
                const double gainL = gain * trackL * clipL;
                const double gainR = gain * trackR * clipR;

                const uint32_t copy = std::min(produced, count);
                const bool needsStretch = std::abs(speed - 1.0) > 1e-6;

                for (uint32_t i = 0; i < copy; ++i) {
                    float left = 0.0f;
                    float right = 0.0f;
                    if (!needsStretch) {
                        left = tmp.channelData(0)[i];
                        right = tmp.channelData(1)[i];
                    } else {
                        const double pos = static_cast<double>(i) * speed;
                        const auto i0 = static_cast<size_t>(pos);
                        if (i0 >= produced) break;
                        const size_t i1 = std::min<size_t>(
                            i0 + 1, static_cast<size_t>(produced) - 1);
                        const auto frac = static_cast<float>(pos - i0);
                        left = tmp.channelData(0)[i0] +
                               (tmp.channelData(0)[i1] -
                                tmp.channelData(0)[i0]) * frac;
                        right = tmp.channelData(1)[i0] +
                                (tmp.channelData(1)[i1] -
                                 tmp.channelData(1)[i0]) * frac;
                    }
                    mix.channelData(0)[offset + i] +=
                        static_cast<float>(left * gainL);
                    mix.channelData(1)[offset + i] +=
                        static_cast<float>(right * gainR);
                }
            }
        }

        const uint32_t accepted = audioEngine_->pushSamples(0, std::move(mix));
        if (accepted == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        audioFeederCursor_ += static_cast<double>(accepted) / kRate;
    }
}

} // namespace bl::ui