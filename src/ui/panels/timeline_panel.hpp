#pragma once

#include <QWidget>

namespace bl::ui {

// Placeholder for the QGraphicsView timeline (UI-2). UI-1 only stubs the
// shell: object name + dock title are what MainWindow/layout persistence key
// on.
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(QWidget* parent = nullptr);
};

} // namespace bl::ui