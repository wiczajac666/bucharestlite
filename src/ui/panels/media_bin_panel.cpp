#include "panels/media_bin_panel.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace bl::ui {

MediaBinPanel::MediaBinPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("MediaBinPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* label = new QLabel(tr("Media Bin (UI-3)"), this);
    label->setAlignment(Qt::AlignCenter);
    label->setObjectName(QStringLiteral("placeholder"));
    layout->addWidget(label);
}

} // namespace bl::ui