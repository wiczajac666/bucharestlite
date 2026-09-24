#pragma once

#include <QWidget>

class QLabel;
class QSlider;

namespace bl::ui {

class MixerController;
class ProjectController;

// Master Fader dock: master-bus strip bound to SequenceSettings' master gain
// and pan through MixerController, so edits are undo/redoable. Follows the
// same rebuild-from-model pattern as MixerPanel.
class MasterFaderPanel : public QWidget {
    Q_OBJECT
public:
    explicit MasterFaderPanel(ProjectController* controller, QWidget* parent = nullptr);

    double masterGain() const { return masterGain_; }

public slots:
    void refresh();

private:
    void applyGain(int value);
    void applyPan(int value);
    void updateDbLabel();

    ProjectController* controller_{nullptr};
    MixerController* mixer_{nullptr};
    QLabel* gainLabel_{nullptr};
    QLabel* dbLabel_{nullptr};
    QLabel* panLabel_{nullptr};
    QSlider* fader_{nullptr};
    QSlider* pan_{nullptr};
    bool refreshing_{false};
    double masterGain_{1.0};
};

} // namespace bl::ui