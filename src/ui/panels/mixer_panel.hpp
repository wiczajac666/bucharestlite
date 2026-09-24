#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QScrollArea;

namespace bl::ui {

class MixerController;
class ProjectController;

// Mixer dock: one horizontal strip per audio track (fader with dB readout,
// pan, mute/solo toggles) followed by an empty-state label when the sequence
// has no audio tracks. Strips edit track-level mixer state through
// MixerController, so every interaction is undo/redoable. The panel rebuilds
// from the model on projectChanged/undoChanged. Live meters are deferred:
// strips show a dB readout derived from the gain value only.
class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(ProjectController* controller, QWidget* parent = nullptr);

    int stripCount() const { return strips_; }
    int audioTrackCount() const { return strips_; }

public slots:
    void refresh();

private:
    void rebuild();
    void applyFader(int audioIndex, int value);
    void applyPan(int audioIndex, int value);
    void updateDbLabel(QLabel* label, double gain);

    ProjectController* controller_{nullptr};
    MixerController* mixer_{nullptr};
    QScrollArea* scroll_{nullptr};
    QWidget* host_{nullptr};
    QHBoxLayout* stripsLayout_{nullptr};
    QLabel* emptyLabel_{nullptr};
    bool refreshing_{false};
    int strips_{0};
};

} // namespace bl::ui