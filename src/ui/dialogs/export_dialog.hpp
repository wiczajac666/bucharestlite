#pragma once

#include "export/export_settings.hpp"

#include <QDialog>

#include <cstdint>

class QCheckBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;

namespace bl::ui {

// Modal export configuration dialog (User Guide §"Export"): video container &
// codec, constant-quality level, output resolution, an audio on/off switch with
// codec choice, and the target file.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(QWidget* parent = nullptr);

    void setSequenceDefaults(const SequenceSettings& seq);

    // Enables the "Timeline zone" range option and stores the in/out points
    // used when that range is selected. With no zone the option is disabled
    // and the export falls back to the whole project.
    void setZone(bool hasZone, Time in, Time out);

    // Enables the "Selected clip" range option and stores the clip's span.
    // Disabled (and forced back to whole-project) when no clip is selected.
    void setClipRange(bool hasClip, Time in, Time out);

    ExportSettings settings() const;

private:
    void browseOutput();
    void updateWidgets();
    void updateTargetRows();
    void rebuildAudioCodecItems();
    void applyPreset(int index);

    QComboBox* presetCombo_{nullptr};
    QComboBox* targetCombo_{nullptr};
    QLineEdit* outputPathEdit_{nullptr};
    QLineEdit* streamUrlEdit_{nullptr};
    QPushButton* browseButton_{nullptr};
    QComboBox* rangeCombo_{nullptr};
    QComboBox* containerCombo_{nullptr};
    QComboBox* videoCodecCombo_{nullptr};
    QComboBox* scaleCombo_{nullptr};
    QCheckBox* includeAudioCheck_{nullptr};
    QComboBox* audioCodecCombo_{nullptr};
    QSpinBox* cqSpin_{nullptr};
    QSpinBox* customWidthSpin_{nullptr};
    QSpinBox* customHeightSpin_{nullptr};
    QLabel* resolutionLabel_{nullptr};
    QPushButton* exportButton_{nullptr};
    QPushButton* cancelButton_{nullptr};

    SequenceSettings sequence_{};
    bool hasZone_{false};
    Time zoneIn_{};
    Time zoneOut_{};
    bool hasClipRange_{false};
    Time clipIn_{};
    Time clipOut_{};
};

} // namespace bl::ui