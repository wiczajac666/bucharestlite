#pragma once

#include <bl_timeline/title.hpp>

#include <QColor>
#include <QDialog>
#include <QImage>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace bl::ui {

// Modal title editor (Kdenlive-style "Add Title"): edits a bl::TitleSpec and
// shows a live preview at the sequence resolution. On accept the caller
// rasterises the spec (see title_renderer.hpp) and either imports the still as
// a new title or re-renders an existing one in place.
class TitleEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit TitleEditorDialog(QWidget* parent = nullptr);

    // Sequence resolution the title is authored for (drives preview + render).
    void setCanvasSize(int width, int height);
    int canvasWidth() const { return canvasWidth_; }
    int canvasHeight() const { return canvasHeight_; }

    void setSpec(const TitleSpec& spec);
    TitleSpec spec() const;

    // Default on-timeline length of the created title clip.
    void setDurationSeconds(double seconds);
    double durationSeconds() const;

    // Duration only matters when creating a title; edit mode hides it so it is
    // not mistaken for a timeline-length control.
    void setDurationVisible(bool visible);

    // Renders the current spec at the canvas resolution (used by tests and the
    // preview).
    QImage renderAtCanvas() const;

private:
    void buildUi();
    void updatePreview();
    void pickColor(TitleColor& target, QPushButton* button);
    void refreshColorButton(const QColor& color, QPushButton* button);
    TitleSpec specFromWidgets() const;

    int canvasWidth_{1920};
    int canvasHeight_{1080};

    QPlainTextEdit* textEdit_{nullptr};
    QFontComboBox* fontCombo_{nullptr};
    QSpinBox* sizeSpin_{nullptr};
    QCheckBox* boldCheck_{nullptr};
    QCheckBox* italicCheck_{nullptr};
    QPushButton* textColorButton_{nullptr};
    QComboBox* alignCombo_{nullptr};
    QSlider* anchorXSlider_{nullptr};
    QSlider* anchorYSlider_{nullptr};
    QCheckBox* bgCheck_{nullptr};
    QPushButton* bgColorButton_{nullptr};
    QSpinBox* paddingSpin_{nullptr};
    QSpinBox* outlineSpin_{nullptr};
    QPushButton* outlineColorButton_{nullptr};
    QSpinBox* durationSpin_{nullptr};
    QWidget* durationRow_{nullptr};
    QLabel* previewLabel_{nullptr};

    TitleColor textColor_{255, 255, 255, 255};
    TitleColor bgColor_{0, 0, 0, 180};
    TitleColor outlineColor_{0, 0, 0, 255};
};

} // namespace bl::ui
