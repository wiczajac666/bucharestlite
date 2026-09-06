#include "panels/mixer_panel.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace bl::ui {

MixerPanel::MixerPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("MixerPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(tr("Audio Mixer (UI-7)"), this);
    label->setAlignment(Qt::AlignCenter);
    label->setObjectName(QStringLiteral("placeholder"));
    layout->addWidget(label);
}

} // namespace bl::ui