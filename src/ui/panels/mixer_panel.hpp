#pragma once

#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_core/time.hpp>

#include <QWidget>

#include <functional>
#include <vector>

class QHBoxLayout;
class QLabel;
class QScrollArea;

namespace bl::ui {

class LevelMeterWidget;
class MixerController;
class ProjectController;

// Mixer dock: one horizontal strip per audio track (live L/R level meter,
// fader with dB readout, pan, mute/solo toggles) followed by an empty-state
// label when the sequence has no audio tracks. Strips edit track-level mixer
// state through MixerController, so every interaction is undo/redoable. The
// panel rebuilds from the model on projectChanged/undoChanged. Meter levels
// are computed on the playhead from precomputed per-source envelopes (set via
// setMeterResolver) and refreshed by onPlayheadChanged/onUpdateMeters.
class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(ProjectController* controller, QWidget* parent = nullptr);

    int stripCount() const { return strips_; }
    int audioTrackCount() const { return strips_; }

    // Feed for the playhead-driven meters. Leave unset (or clear) for analysis
    // voids, which render as silence.
    void setMeterResolver(bl::SourceMeterResolver resolver);
    const bl::SourceMeterResolver& meterResolver() const { return resolver_; }

    LevelMeterWidget* meter(int strip) const;

public slots:
    void refresh();
    void onPlayheadChanged(const bl::Time& playhead);
    void onUpdateMeters();

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
    std::vector<LevelMeterWidget*> meterWidgets_;
    bl::SourceMeterResolver resolver_;
    bl::Time playhead_;
};

} // namespace bl::ui