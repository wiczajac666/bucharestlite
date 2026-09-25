#pragma once

#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_core/time.hpp>

#include <QWidget>

#include <functional>

class QLabel;
class QSlider;

namespace bl::ui {

class LevelMeterWidget;
class MixerController;
class ProjectController;

// Master Fader dock: master-bus strip bound to SequenceSettings' master gain
// and pan through MixerController, so edits are undo/redoable. Follows the
// same rebuild-from-model pattern as MixerPanel. A live L/R level meter shows
// the summed master bus at the current playhead, computed from the precomputed
// envelopes supplied through setMeterResolver.
class MasterFaderPanel : public QWidget {
    Q_OBJECT
public:
    explicit MasterFaderPanel(ProjectController* controller, QWidget* parent = nullptr);

    double masterGain() const { return masterGain_; }

    void setMeterResolver(bl::SourceMeterResolver resolver);
    LevelMeterWidget* masterMeter() const { return masterMeter_; }

public slots:
    void refresh();
    void onPlayheadChanged(const bl::Time& playhead);
    void onUpdateMeters();

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
    LevelMeterWidget* masterMeter_{nullptr};
    bool refreshing_{false};
    double masterGain_{1.0};
    bl::SourceMeterResolver resolver_;
    bl::Time playhead_;
};

} // namespace bl::ui