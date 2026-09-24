#include "panels/master_fader_panel.hpp"

#include "app/project_controller.hpp"
#include "panels/mixer_controller.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>
#include <sstream>

namespace bl::ui {

namespace {

constexpr int kFaderMax{150}; // gain 0.0 .. 1.5
constexpr int kPanMax{100};   // pan -1.0 .. 1.0

double gainToDb(double gain) {
    if (gain <= 0.0) return -60.0;
    return 20.0 * std::log10(gain);
}

QString fmtGain(double gain) {
    return QStringLiteral("Gain: %1x")
        .arg(QString::number(gain, 'f', 2));
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

MasterFaderPanel::MasterFaderPanel(ProjectController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller), mixer_(new MixerController) {
    setObjectName(QStringLiteral("MasterFaderPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    auto* titleLabel = new QLabel(tr("Master Fader"), this);
    titleLabel->setObjectName(QStringLiteral("masterTitle"));
    titleLabel->setAlignment(Qt::AlignLeft);
    layout->addWidget(titleLabel);

    auto* panelRow = new QWidget(this);
    auto* rowLayout = new QHBoxLayout(panelRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto* column = new QWidget(panelRow);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);

    fader_ = new QSlider(Qt::Vertical, column);
    fader_->setObjectName(QStringLiteral("masterFader"));
    fader_->setRange(0, kFaderMax);
    columnLayout->addWidget(fader_, 1);

    dbLabel_ = new QLabel(fmtDb(1.0), column);
    dbLabel_->setObjectName(QStringLiteral("masterDb"));
    dbLabel_->setAlignment(Qt::AlignCenter);
    columnLayout->addWidget(dbLabel_);

    rowLayout->addWidget(column);

    auto* rightColumn = new QWidget(panelRow);
    auto* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    gainLabel_ = new QLabel(fmtGain(1.0), rightColumn);
    gainLabel_->setObjectName(QStringLiteral("masterGain"));
    rightLayout->addWidget(gainLabel_);

    auto* panSliderColumn = new QWidget(rightColumn);
    auto* panSliderLayout = new QVBoxLayout(panSliderColumn);
    panSliderLayout->setContentsMargins(0, 0, 0, 0);
    pan_ = new QSlider(Qt::Horizontal, panSliderColumn);
    pan_->setObjectName(QStringLiteral("masterPanSlider"));
    pan_->setRange(-kPanMax, kPanMax);
    panSliderLayout->addWidget(pan_);
    panLabel_ = new QLabel(fmtPan(0.0), panSliderColumn);
    panLabel_->setObjectName(QStringLiteral("masterPan"));
    panLabel_->setAlignment(Qt::AlignCenter);
    panSliderLayout->addWidget(panLabel_);
    rightLayout->addWidget(panSliderColumn);

    rowLayout->addWidget(rightColumn, 1);
    layout->addWidget(panelRow, 1);

    if (controller_) {
        mixer_->reset(controller->timeline(), controller->undoStack());
        connect(controller_, &ProjectController::projectChanged,
                this, &MasterFaderPanel::refresh);
        connect(controller_, &ProjectController::undoChanged,
                this, &MasterFaderPanel::refresh);
        connect(fader_, &QSlider::sliderReleased, this,
                [this] { applyGain(fader_->value()); });
        connect(pan_, &QSlider::sliderReleased, this,
                [this] { applyPan(pan_->value()); });
    }

    refresh();
}

void MasterFaderPanel::refresh() {
    if (!controller_) return;
    refreshing_ = true;

    masterGain_ = controller_->timeline().sequence().settings.masterGain;
    const double masterPan = controller_->timeline().sequence().settings.masterPan;

    fader_->setValue(static_cast<int>(masterGain_ * kFaderMax));
    pan_->setValue(static_cast<int>(masterPan * kPanMax));
    gainLabel_->setText(fmtGain(masterGain_));
    dbLabel_->setText(fmtDb(masterGain_));
    panLabel_->setText(fmtPan(masterPan));

    refreshing_ = false;
}

void MasterFaderPanel::applyGain(int value) {
    if (refreshing_) return;
    const double gain = value / static_cast<double>(kFaderMax);
    if (mixer_->setMasterGain(gain)) {
        masterGain_ = gain;
        updateDbLabel();
    }
}

void MasterFaderPanel::applyPan(int value) {
    if (refreshing_) return;
    mixer_->setMasterPan(value / static_cast<double>(kPanMax));
}

void MasterFaderPanel::updateDbLabel() {
    gainLabel_->setText(fmtGain(masterGain_));
    dbLabel_->setText(fmtDb(masterGain_));
}

} // namespace bl::ui