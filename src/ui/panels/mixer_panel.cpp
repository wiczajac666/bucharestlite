#include "panels/mixer_panel.hpp"

#include "app/project_controller.hpp"
#include "panels/mixer_controller.hpp"

#include <bl_timeline/sequence.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>
#include <sstream>

namespace bl::ui {

namespace {

constexpr int kFaderMax{150};   // gain 0.0 .. 1.5
constexpr int kPanMax{100};     // pan -1.0 .. 1.0

// Linear gain to dB, matching InspectorPanel's readout convention.
double gainToDb(double gain) {
    if (gain <= 0.0) return -60.0;
    return 20.0 * std::log10(gain);
}

QString fmtDb(double gain) {
    return QString::fromStdString(std::to_string(gainToDb(gain)).substr(0, 5)) +
           QStringLiteral(" dB");
}

QString fmtPan(double pan) {
    return QStringLiteral("Pan: %1")
        .arg(QString::number(pan, 'f', 2));
}

} // namespace

MixerPanel::MixerPanel(ProjectController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller), mixer_(new MixerController) {
    setObjectName(QStringLiteral("MixerPanel"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    host_ = new QWidget(scroll_);
    scroll_->setWidget(host_);

    stripsLayout_ = new QHBoxLayout(host_);
    stripsLayout_->setContentsMargins(4, 4, 4, 4);
    stripsLayout_->setSpacing(6);

    emptyLabel_ = nullptr;

    rootLayout->addWidget(scroll_, 1);

    if (controller_) {
        mixer_->reset(controller->timeline(), controller->undoStack());
        connect(controller_, &ProjectController::projectChanged,
                this, &MixerPanel::refresh);
        connect(controller_, &ProjectController::undoChanged,
                this, &MixerPanel::refresh);
    }

    refresh();
}

void MixerPanel::refresh() {
    rebuild();
}

void MixerPanel::rebuild() {
    if (!controller_) return;
    refreshing_ = true;

    while (auto* item = stripsLayout_->takeAt(0)) {
        if (auto* w = item->widget()) {
            delete w;
        }
        delete item;
    }
    emptyLabel_ = nullptr;

    const auto& seq = controller_->timeline().sequence();
    const int audioCount = static_cast<int>(seq.audioTracks.size());
    const int videoCount = static_cast<int>(seq.videoTracks.size());
    strips_ = audioCount;

    if (audioCount == 0) {
        emptyLabel_ = new QLabel(tr("No audio tracks"), host_);
        emptyLabel_->setObjectName(QStringLiteral("emptyState"));
        emptyLabel_->setAlignment(Qt::AlignCenter);
        emptyLabel_->setVisible(true);
        stripsLayout_->addWidget(emptyLabel_, 1);
        refreshing_ = false;
        return;
    }

    for (int i = 0; i < audioCount; ++i) {
        const auto& track = seq.audioTracks[static_cast<size_t>(i)];

        auto* strip = new QWidget(host_);
        strip->setObjectName(QStringLiteral("trackStrip_%1").arg(i));
        strip->setFixedWidth(90);
        auto* stripLayout = new QVBoxLayout(strip);
        stripLayout->setContentsMargins(2, 2, 2, 2);
        stripLayout->setSpacing(2);

        auto* name = new QLabel(QString::fromStdString(track.name()), strip);
        name->setObjectName(QStringLiteral("trackName_%1").arg(i));
        name->setAlignment(Qt::AlignCenter);
        stripLayout->addWidget(name);

        auto* fader = new QSlider(Qt::Vertical, strip);
        fader->setObjectName(QStringLiteral("fader_%1").arg(i));
        fader->setRange(0, kFaderMax);
        fader->setValue(static_cast<int>(track.gain() * kFaderMax));
        stripLayout->addWidget(fader, 1);

        auto* dbLabel = new QLabel(fmtDb(track.gain()), strip);
        dbLabel->setObjectName(QStringLiteral("db_%1").arg(i));
        dbLabel->setAlignment(Qt::AlignCenter);
        stripLayout->addWidget(dbLabel);

        auto* pan = new QSlider(Qt::Horizontal, strip);
        pan->setObjectName(QStringLiteral("pan_%1").arg(i));
        pan->setRange(-kPanMax, kPanMax);
        pan->setValue(static_cast<int>(track.pan() * kPanMax));
        stripLayout->addWidget(pan);

        auto* panLabel = new QLabel(fmtPan(track.pan()), strip);
        panLabel->setObjectName(QStringLiteral("panLabel_%1").arg(i));
        panLabel->setAlignment(Qt::AlignCenter);
        stripLayout->addWidget(panLabel);

        auto* msRow = new QWidget(strip);
        auto* msLayout = new QHBoxLayout(msRow);
        msLayout->setContentsMargins(0, 0, 0, 0);
        auto* mute = new QPushButton(tr("M"), msRow);
        mute->setObjectName(QStringLiteral("mute_%1").arg(i));
        mute->setCheckable(true);
        mute->setChecked(track.muted());
        auto* solo = new QPushButton(tr("S"), msRow);
        solo->setObjectName(QStringLiteral("solo_%1").arg(i));
        solo->setCheckable(true);
        solo->setChecked(track.soloed());
        msLayout->addWidget(mute);
        msLayout->addWidget(solo);
        stripLayout->addWidget(msRow);

        connect(fader, &QSlider::valueChanged, this,
                [this, i, dbLabel](int value) {
                    if (refreshing_) return;
                    updateDbLabel(dbLabel, value / static_cast<double>(kFaderMax));
                });
        connect(fader, &QSlider::sliderReleased, this,
                [this, i, fader] { applyFader(i, fader->value()); });
        connect(pan, &QSlider::sliderReleased, this,
                [this, i, pan, panLabel] {
                    applyPan(i, pan->value());
                    panLabel->setText(fmtPan(pan->value() / static_cast<double>(kPanMax)));
                });
        connect(mute, &QPushButton::toggled, this, [this, videoCount, i](bool checked) {
            if (refreshing_) return;
            mixer_->setTrackMuted(videoCount + i, checked);
        });
        connect(solo, &QPushButton::toggled, this, [this, videoCount, i](bool checked) {
            if (refreshing_) return;
            mixer_->setTrackSoloed(videoCount + i, checked);
        });

        stripsLayout_->addWidget(strip);
    }

    stripsLayout_->addStretch(1);
    refreshing_ = false;
}

void MixerPanel::applyFader(int audioIndex, int value) {
    if (!controller_ || refreshing_) return;
    const int videoCount =
        static_cast<int>(controller_->timeline().sequence().videoTracks.size());
    mixer_->setTrackGain(videoCount + audioIndex,
                         value / static_cast<double>(kFaderMax));
}

void MixerPanel::applyPan(int audioIndex, int value) {
    if (!controller_ || refreshing_) return;
    const int videoCount =
        static_cast<int>(controller_->timeline().sequence().videoTracks.size());
    mixer_->setTrackPan(videoCount + audioIndex,
                        value / static_cast<double>(kPanMax));
}

void MixerPanel::updateDbLabel(QLabel* label, double gain) {
    if (label) {
        label->setText(fmtDb(gain));
    }
}

} // namespace bl::ui