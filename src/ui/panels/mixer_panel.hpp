#pragma once

#include <QWidget>

namespace bl::ui {

class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(QWidget* parent = nullptr);
};

} // namespace bl::ui