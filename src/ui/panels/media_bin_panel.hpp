#pragma once

#include <QWidget>

namespace bl::ui {

class MediaBinPanel : public QWidget {
    Q_OBJECT
public:
    explicit MediaBinPanel(QWidget* parent = nullptr);
};

} // namespace bl::ui