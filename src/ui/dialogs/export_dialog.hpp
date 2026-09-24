#pragma once

#include "export/export_settings.hpp"

#include <QDialog>

#include <cstdint>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;

namespace bl::ui {

// Modal export configuration dialog (User Guide §"Export"): video container +
// codec, constant-quality level, output resolution and target file. Audio
// tracks are not yet produced by the renderer, so the audio section is shown
// disabled with an explanatory note.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(QWidget* parent = nullptr);

    void setSequenceDefaults(const SequenceSettings& seq);

    ExportSettings settings() const;

private:
    void browseOutput();
    void updateWidgets();

    QLineEdit* outputPathEdit_{nullptr};
    QComboBox* containerCombo_{nullptr};
    QComboBox* videoCodecCombo_{nullptr};
    QComboBox* scaleCombo_{nullptr};
    QSpinBox* cqSpin_{nullptr};
    QSpinBox* customWidthSpin_{nullptr};
    QSpinBox* customHeightSpin_{nullptr};
    QLabel* resolutionLabel_{nullptr};
    QPushButton* exportButton_{nullptr};
    QPushButton* cancelButton_{nullptr};

    SequenceSettings sequence_{};
};

} // namespace bl::ui