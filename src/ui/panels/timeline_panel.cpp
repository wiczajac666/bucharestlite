#include "panels/timeline_panel.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace bl::ui {

TimelinePanel::TimelinePanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("TimelinePanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(tr("Timeline (UI-2)"), this);
    label->setAlignment(Qt::AlignCenter);
    label->setObjectName(QStringLiteral("placeholder"));
    layout->addWidget(label);
}

} // namespace bl::ui