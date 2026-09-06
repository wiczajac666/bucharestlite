#include "panels/preview_panel.hpp"

#include "app/project_controller.hpp"

#include <bl_core/logger.hpp>
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

    surface_ = new PreviewSurface(this);

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

void PreviewPanel::onProjectChanged() {
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
        return;
    }

    auto result = engine_->runAt(controller_->timeline().snapshot(),
                                 transport_.playhead());
    if (result.ok()) {
        surface_->setFrame(result.value());
    } else {
        BL_LOG_TRACE("preview", "render skipped: " + result.message());
        surface_->clearFrame();
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
    } else {
        timer_->stop();
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

} // namespace bl::ui