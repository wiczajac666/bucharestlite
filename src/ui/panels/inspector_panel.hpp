#pragma once

#include <QWidget>

namespace bl::ui {

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);
};

} // namespace bl::ui