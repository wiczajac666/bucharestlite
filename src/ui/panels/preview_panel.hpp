#pragma once

#include <QWidget>

namespace bl::ui {

class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);
};

} // namespace bl::ui