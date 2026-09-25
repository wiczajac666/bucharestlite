#include "panels/param_slider.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>

#include <cmath>

namespace bl::ui {

namespace {

constexpr int kSteps = 1000;

int toPosition(const bl::ParamSpec& spec, double value) {
    const double t = (value - spec.min) / (spec.max - spec.min);
    return static_cast<int>(std::lround(t * kSteps));
}

double toValue(const bl::ParamSpec& spec, int position) {
    const double t = position / static_cast<double>(kSteps);
    return spec.min + t * (spec.max - spec.min);
}

} // namespace

ParamSlider::ParamSlider(const bl::ParamSpec& spec, QWidget* parent)
    : QWidget(parent), spec_(spec), value_(spec.def) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* nameLabel = new QLabel(QString::fromStdString(spec.label), this);
    nameLabel->setMinimumWidth(72);
    layout->addWidget(nameLabel);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, kSteps);
    slider_->setValue(toPosition(spec_, value_));
    slider_->setObjectName(QStringLiteral("paramSlider"));
    layout->addWidget(slider_, 1);

    valueLabel_ = new QLabel(this);
    layout->addWidget(valueLabel_);

    connect(slider_, &QSlider::valueChanged, this, [this](int position) {
        value_ = toValue(spec_, position);
        updateReadout();
    });
    connect(slider_, &QSlider::sliderReleased, this, [this] {
        emit valueCommitted(value_);
    });

    updateReadout();
}

void ParamSlider::setValue(double value) {
    value_ = value;
    slider_->blockSignals(true);
    slider_->setValue(toPosition(spec_, value_));
    slider_->blockSignals(false);
    updateReadout();
}

void ParamSlider::commit() { emit valueCommitted(value_); }

void ParamSlider::updateReadout() {
    if (!valueLabel_) return;
    valueLabel_->setText(QString::number(value_, 'g', 3));
}

} // namespace bl::ui