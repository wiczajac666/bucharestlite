#include "panels/preview_panel.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace bl::ui {

PreviewPanel::PreviewPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("PreviewPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(tr("Preview (UI-3)"), this);
    label->setAlignment(Qt::AlignCenter);
    label->setObjectName(QStringLiteral("placeholder"));
    layout->addWidget(label);
}

} // namespace bl::ui