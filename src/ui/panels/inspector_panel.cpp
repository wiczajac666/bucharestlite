#include "panels/inspector_panel.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace bl::ui {

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("InspectorPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(tr("Inspector (UI-4)"), this);
    label->setAlignment(Qt::AlignCenter);
    label->setObjectName(QStringLiteral("placeholder"));
    layout->addWidget(label);
}

} // namespace bl::ui