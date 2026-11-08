#include "panels/source_monitor_panel.hpp"

#include "app/project_controller.hpp"
#include "panels/preview_panel.hpp"

#include <bl_core/media_source.hpp>
#include <bl_timeline/clip.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/timeline.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bl::ui {

namespace {

constexpr const bl::Rational kTimeRate{1'000'000, 1};

// Short HH:MM:SS:FF timecode for the source position labels.
QString formatTime(double seconds, double fps) {
    if (!(fps > 0.0)) fps = 25.0;
    if (seconds < 0.0) seconds = 0.0;
    const int hh = static_cast<int>(seconds / 3600.0);
    const int mm = static_cast<int>(std::fmod(seconds / 60.0, 60.0));
    const int ss = static_cast<int>(std::fmod(seconds, 60.0));
    const int frames =
        static_cast<int>(std::llround(std::fmod(seconds, 1.0) * fps));
    const int ff = std::max(0, std::min(frames, static_cast<int>(std::llround(fps)) - 1));
    return QString::asprintf("%02d:%02d:%02d:%02d", hh, mm, ss, ff);
}

} // namespace

SourceMonitorPanel::SourceMonitorPanel(
    ProjectController* controller, QWidget* parent,
    std::vector<std::pair<std::string, bl::PluginOrigin>> pluginDirs)
    : QWidget(parent),
      controller_(controller),
      pluginDirs_(std::move(pluginDirs)) {
    setObjectName(QStringLiteral("SourceMonitorPanel"));

    surface_ = new PreviewSurface(this);
    surface_->setObjectName(QStringLiteral("sourceMonitorSurface"));
    surface_->setMinimumSize(160, 90);

    mediaLabel_ = new QLabel(tr("No clip selected"), this);
    mediaLabel_->setObjectName(QStringLiteral("sourceMediaLabel"));
    mediaLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    mediaLabel_->setMinimumWidth(0);

    timecodeLabel_ = new QLabel(tr("00:00:00:00 / 00:00:00:00"), this);
    timecodeLabel_->setObjectName(QStringLiteral("sourceTimecode"));
    timecodeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timecodeLabel_->setMinimumWidth(150);

    auto* infoBar = new QHBoxLayout;
    infoBar->setContentsMargins(4, 2, 4, 0);
    infoBar->addWidget(mediaLabel_, 1);
    infoBar->addWidget(timecodeLabel_, 0);

    positionSlider_ = new QSlider(Qt::Horizontal, this);
    positionSlider_->setObjectName(QStringLiteral("sourcePositionSlider"));
    positionSlider_->setRange(0, 1000);
    positionSlider_->setValue(0);
    connect(positionSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (syncingSlider_ || durationSeconds_ <= 0.0) return;
        setPositionSeconds(durationSeconds_ * static_cast<double>(value) / 1000.0);
    });

    markInButton_ = new QPushButton(tr("In (Alt+I)"), this);
    markInButton_->setObjectName(QStringLiteral("sourceMarkInButton"));
    connect(markInButton_, &QPushButton::clicked, this,
            &SourceMonitorPanel::markInHere);

    markOutButton_ = new QPushButton(tr("Out (Alt+O)"), this);
    markOutButton_->setObjectName(QStringLiteral("sourceMarkOutButton"));
    connect(markOutButton_, &QPushButton::clicked, this,
            &SourceMonitorPanel::markOutHere);

    clearMarksButton_ = new QPushButton(tr("Clear"), this);
    clearMarksButton_->setObjectName(QStringLiteral("sourceClearMarksButton"));
    connect(clearMarksButton_, &QPushButton::clicked, this,
            &SourceMonitorPanel::clearMarks);

    insertButton_ = new QPushButton(tr("Insert"), this);
    insertButton_->setObjectName(QStringLiteral("sourceInsertButton"));
    insertButton_->setToolTip(tr("Insert the marked source range at the playhead"));
    connect(insertButton_, &QPushButton::clicked, this,
            &SourceMonitorPanel::insertEdit);

    overwriteButton_ = new QPushButton(tr("Overwrite"), this);
    overwriteButton_->setObjectName(QStringLiteral("sourceOverwriteButton"));
    overwriteButton_->setToolTip(
        tr("Overwrite the marked source range at the playhead"));
    connect(overwriteButton_, &QPushButton::clicked, this,
            &SourceMonitorPanel::overwriteEdit);

    marksLabel_ = new QLabel(this);
    marksLabel_->setObjectName(QStringLiteral("sourceMarksLabel"));

    auto* markBar = new QHBoxLayout;
    markBar->setContentsMargins(4, 0, 4, 0);
    markBar->setSpacing(4);
    markBar->addWidget(markInButton_);
    markBar->addWidget(markOutButton_);
    markBar->addWidget(clearMarksButton_);
    markBar->addWidget(marksLabel_, 1);
    markBar->addWidget(insertButton_);
    markBar->addWidget(overwriteButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(surface_, 1);
    layout->addLayout(infoBar);
    layout->addWidget(positionSlider_);
    layout->addLayout(markBar);

    if (controller_) {
        connect(controller_, &ProjectController::projectChanged, this,
                &SourceMonitorPanel::refresh);
    }

    updateLabels();
}

double SourceMonitorPanel::fps() const {
    if (controller_) {
        const auto& rate = controller_->timeline().sequence().settings.fps;
        if (rate.toDouble() > 0.0) return rate.toDouble();
    }
    return 25.0;
}

bool SourceMonitorPanel::hasMarks() const {
    return !mediaId_.empty() && markIn_ < markOut_;
}

void SourceMonitorPanel::setMedia(const std::string& mediaId) {
    if (mediaId.empty()) {
        clearMedia();
        return;
    }

    const MediaBinItem* entry = nullptr;
    if (controller_) {
        for (const MediaBinItem& item : controller_->mediaBin()) {
            if (item.id == mediaId) {
                entry = &item;
                break;
            }
        }
    }
    if (!entry) {
        clearMedia();
        emit statusMessage(tr("Media is no longer in the project"));
        return;
    }

    mediaId_ = mediaId;
    mediaPath_ = entry->path;
    mediaName_ = entry->name;
    positionSeconds_ = 0.0;

    auto probe = MediaSource::probe(MediaLocator{mediaPath_});
    if (!probe) {
        clearMedia();
        emit statusMessage(tr("Cannot read media: %1")
                               .arg(QString::fromStdString(mediaName_)));
        return;
    }

    hasVideo_ = !probe->videoStreams.empty();
    hasAudio_ = !probe->audioStreams.empty();
    durationSeconds_ = probe->duration.toSeconds();
    // Stills and titles report no duration; give them a usable default window.
    if (durationSeconds_ <= 0.0) durationSeconds_ = 5.0;

    markIn_ = bl::Time{};
    markOut_ = bl::Time::fromSeconds(durationSeconds_, kTimeRate);

    rebuildEngine();
    refreshFrame();
    syncSlider();
    updateLabels();
}

void SourceMonitorPanel::clearMedia() {
    mediaId_.clear();
    mediaPath_.clear();
    mediaName_.clear();
    durationSeconds_ = 0.0;
    positionSeconds_ = 0.0;
    markIn_ = bl::Time{};
    markOut_ = bl::Time{};
    hasVideo_ = false;
    hasAudio_ = false;
    engine_.reset();
    engineWidth_ = 0;
    engineHeight_ = 0;
    surface_->clearFrame();
    syncSlider();
    updateLabels();
}

void SourceMonitorPanel::refresh() {
    if (mediaId_.empty()) return;

    bool stillPresent = false;
    if (controller_) {
        for (const MediaBinItem& item : controller_->mediaBin()) {
            if (item.id == mediaId_) {
                stillPresent = true;
                break;
            }
        }
    }
    if (!stillPresent) {
        clearMedia();
        return;
    }

    rebuildEngine();
    refreshFrame();
    syncSlider();
    updateLabels();
}

void SourceMonitorPanel::rebuildEngine() {
    engine_.reset();
    if (mediaId_.empty() || !controller_) return;

    const bl::SequenceSettings& settings =
        controller_->timeline().sequence().settings;
    constexpr uint32_t kMaxEdge = 640;
    const double scale =
        std::min(1.0, static_cast<double>(kMaxEdge) /
                          std::max<uint32_t>(
                              1, std::max<uint32_t>(settings.width,
                                                    settings.height)));
    const auto dim = [&](int32_t value) -> uint32_t {
        return static_cast<uint32_t>(
            std::max<int64_t>(1, std::llround(value * scale)));
    };

    bl::MediaDecodeSource::Spec spec;
    spec.pluginDirs = pluginDirs_;

    PreviewEngine::Config cfg;
    cfg.outputWidth = dim(settings.width);
    cfg.outputHeight = dim(settings.height);
    cfg.cacheBytes = 32 * 1024 * 1024;
    cfg.plugins = spec;

    auto created = PreviewEngine::create(cfg, controller_->mediaBin());
    if (!created.ok()) {
        engineWidth_ = 0;
        engineHeight_ = 0;
        return;
    }
    engineWidth_ = settings.width;
    engineHeight_ = settings.height;
    engineFps_ = settings.fps;
    engine_ = std::make_unique<PreviewEngine>(std::move(created.value()));
}

void SourceMonitorPanel::refreshFrame() {
    if (!surface_) return;
    if (mediaId_.empty() || !controller_) {
        surface_->clearFrame();
        return;
    }

    const bl::SequenceSettings& settings =
        controller_->timeline().sequence().settings;
    if (!engine_ || static_cast<int32_t>(engineWidth_) != settings.width ||
        static_cast<int32_t>(engineHeight_) != settings.height ||
        !(settings.fps == engineFps_)) {
        rebuildEngine();
    }
    if (!engine_) {
        surface_->clearFrame();
        return;
    }

    auto clipOpt = makeThreePointClip(
        SourceRef{mediaId_, bl::Time{},
                  bl::Time::fromSeconds(durationSeconds_, kTimeRate)},
        SpeedRemap{}, mediaName_);
    if (!clipOpt) {
        surface_->clearFrame();
        return;
    }

    bl::Sequence seq;
    seq.settings = settings;
    bl::Clip clip = *clipOpt;
    clip.timelineStart = bl::Time{};
    clip.hasVideo = hasVideo_;
    clip.hasAudio = false;
    auto& track = seq.addVideoTrack(QStringLiteral("Source").toStdString());
    if (!track.addClip(clip)) {
        surface_->clearFrame();
        return;
    }

    auto result = engine_->runAt(
        bl::TimelineSnapshot(std::move(seq)),
        bl::Time::fromSeconds(positionSeconds_, kTimeRate));
    if (result.ok()) {
        surface_->setFrame(result.value());
    } else {
        surface_->clearFrame();
    }
}

void SourceMonitorPanel::setPositionSeconds(double seconds) {
    const double upper = durationSeconds_ > 0.0 ? durationSeconds_ : 0.0;
    positionSeconds_ = std::clamp(seconds, 0.0, upper);
    refreshFrame();
    syncSlider();
    updateLabels();
}

void SourceMonitorPanel::markInHere() {
    if (mediaId_.empty()) {
        emit statusMessage(tr("Select a clip in the Media Bin first"));
        return;
    }
    markIn_ = bl::Time::fromSeconds(positionSeconds_, kTimeRate);
    if (!(markIn_ < markOut_)) {
        markOut_ = bl::Time::fromSeconds(durationSeconds_, kTimeRate);
    }
    updateLabels();
    emit statusMessage(
        tr("In point: %1").arg(formatTime(positionSeconds_, fps())));
}

void SourceMonitorPanel::markOutHere() {
    if (mediaId_.empty()) {
        emit statusMessage(tr("Select a clip in the Media Bin first"));
        return;
    }
    markOut_ = bl::Time::fromSeconds(positionSeconds_, kTimeRate);
    if (!(markIn_ < markOut_)) {
        markIn_ = bl::Time{};
    }
    updateLabels();
    emit statusMessage(
        tr("Out point: %1").arg(formatTime(positionSeconds_, fps())));
}

void SourceMonitorPanel::clearMarks() {
    if (mediaId_.empty()) return;
    markIn_ = bl::Time{};
    markOut_ = bl::Time::fromSeconds(durationSeconds_, kTimeRate);
    updateLabels();
    emit statusMessage(tr("Cleared source marks"));
}

bool SourceMonitorPanel::insertEdit() {
    if (mediaId_.empty()) {
        emit statusMessage(tr("Select a clip in the Media Bin first"));
        return false;
    }
    if (!hasMarks()) {
        emit statusMessage(tr("Set an in and out point first"));
        return false;
    }
    emit insertRequested(mediaId_, markIn_, markOut_);
    return true;
}

bool SourceMonitorPanel::overwriteEdit() {
    if (mediaId_.empty()) {
        emit statusMessage(tr("Select a clip in the Media Bin first"));
        return false;
    }
    if (!hasMarks()) {
        emit statusMessage(tr("Set an in and out point first"));
        return false;
    }
    emit overwriteRequested(mediaId_, markIn_, markOut_);
    return true;
}

void SourceMonitorPanel::syncSlider() {
    if (!positionSlider_) return;
    syncingSlider_ = true;
    const double fraction =
        durationSeconds_ > 0.0 ? positionSeconds_ / durationSeconds_ : 0.0;
    positionSlider_->setValue(
        static_cast<int>(std::llround(std::clamp(fraction, 0.0, 1.0) * 1000.0)));
    syncingSlider_ = false;
}

void SourceMonitorPanel::updateLabels() {
    if (mediaLabel_) {
        mediaLabel_->setText(mediaId_.empty()
                                 ? tr("No clip selected")
                                 : QString::fromStdString(mediaName_));
    }
    if (timecodeLabel_) {
        timecodeLabel_->setText(
            formatTime(positionSeconds_, fps()) + QStringLiteral(" / ") +
            formatTime(durationSeconds_, fps()));
    }
    if (marksLabel_) {
        if (mediaId_.empty()) {
            marksLabel_->setText(QString());
        } else if (hasMarks()) {
            marksLabel_->setText(
                tr("In %1  Out %2")
                    .arg(formatTime(markIn_.toSeconds(), fps()))
                    .arg(formatTime(markOut_.toSeconds(), fps())));
        } else {
            marksLabel_->setText(tr("No marks"));
        }
    }
    const bool loaded = !mediaId_.empty();
    if (markInButton_) markInButton_->setEnabled(loaded);
    if (markOutButton_) markOutButton_->setEnabled(loaded);
    if (clearMarksButton_) clearMarksButton_->setEnabled(loaded);
    if (positionSlider_) positionSlider_->setEnabled(loaded);
    if (insertButton_) insertButton_->setEnabled(hasMarks());
    if (overwriteButton_) overwriteButton_->setEnabled(hasMarks());
}

} // namespace bl::ui
