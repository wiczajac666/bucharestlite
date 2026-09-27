#pragma once

#include "app/project_controller.hpp"
#include "panels/clip_edit_controller.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QWidget>

#include <bl_timeline/clip.hpp>

class QCheckBox;
class QLineEdit;
class QMenu;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace bl::ui {

class ParamSlider;

// Full inspector panel: clip properties, speed, keyframes (list-based), and
// effect stack. Single-clip editing only — showing or editing nothing when
// zero or two-or-more clips are selected.
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(ProjectController* controller,
                            QWidget* parent = nullptr);

    // Reloads the entire inspector from the model. Called by MainWindow after
    // wiring selectionChanged → showSelection.
    void showSelection(const QSet<ClipId>& ids);

    // Adds an effect to the selected clip by registry id (test + menu entry).
    void addEffectById(const QString& effectId);

    // Test helpers.
    bool hasClip() const { return currentFlat_.has_value(); }
    bool isPlaceholderShown() const;

signals:
    void clipChanged();

private:
    // Helpers.
    struct ClipLocation {
        int flat{0};
        ClipId id;
    };
    std::optional<ClipLocation> findSelectedClip(const QSet<ClipId>& ids) const;
    void clearUI();
    void reloadFromModel();

    // Sections.
    QWidget* buildNameSection(QWidget* parent);
    QWidget* buildSourceSection(QWidget* parent);
    QWidget* buildSpeedSection(QWidget* parent);
    QWidget* buildAudioSection(QWidget* parent);
    QWidget* buildColorSection(QWidget* parent);
    QWidget* buildKeyframeSection(QWidget* parent);
    QWidget* buildEffectSection(QWidget* parent);

    void onNameChanged();
    void onSubtitleChanged();
    void onSourceChanged();
    void onSpeedChanged();
    void onGainChanged();
    void onPanChanged();
    void onColorButton(int index);
    void onKeyframeChannelChanged();
    void onAddKeyframe();
    void onRemoveKeyframe();
    void onKeyframeCellChanged(int row, int column);
    void onRemoveEffect();
    void onReorderEffectUp();
    void onReorderEffectDown();
    void onEffectEnabledChanged();
    void onEffectParamsChanged();
    void rebuildEffectParams(const EffectInstance& effect);
    void onParamCommitted(const std::string& key, double value);

    ProjectController* controller_{nullptr};
    ClipEditController editor_;

    std::optional<int> currentFlat_;
    ClipId currentId_;
    bool suppress_{false};

    // Stacked layout: index 0 = placeholder, index 1 = clip editor.
    QStackedWidget* stack_{nullptr};
    QLabel* placeholderLabel_{nullptr};
    QWidget* editorWidget_{nullptr};

    // Name.
    QLineEdit* nameEdit_{nullptr};

    // Subtitle text (soft subtitles; exported as a subtitle stream).
    QLineEdit* subtitleEdit_{nullptr};

    // Source / timeline readouts.
    QLineEdit* sourceInEdit_{nullptr};
    QLineEdit* sourceOutEdit_{nullptr};
    QLabel* timelineStartLabel_{nullptr};
    QLabel* timelineDurationLabel_{nullptr};

    // Speed.
    QSpinBox* speedNum_{nullptr};
    QSpinBox* speedDen_{nullptr};
    QCheckBox* speedReverse_{nullptr};

    // Audio (gain/pan).
    QWidget* audioGroup_{nullptr};
    QDoubleSpinBox* gainSpin_{nullptr};
    QDoubleSpinBox* panSpin_{nullptr};

    // Color label buttons.
    std::vector<QPushButton*> colorButtons_;

    // Keyframes.
    QComboBox* keyframeChannel_{nullptr};
    QTableWidget* keyframeTable_{nullptr};
    QPushButton* keyframeAdd_{nullptr};
    QPushButton* keyframeRemove_{nullptr};

    // Effects.
    QListWidget* effectList_{nullptr};
    QPushButton* effectAdd_{nullptr};
    QMenu* effectAddMenu_{nullptr};
    QPushButton* effectRemove_{nullptr};
    QPushButton* effectUp_{nullptr};
    QPushButton* effectDown_{nullptr};
    QLabel* effectEnabledLabel_{nullptr};
    QCheckBox* effectEnabledCheck_{nullptr};
    QLabel* effectParamsLabel_{nullptr};
    QLineEdit* effectParamsEdit_{nullptr};
    QWidget* effectSliders_{nullptr};
    QVBoxLayout* effectSlidersLayout_{nullptr};
    std::vector<ParamSlider*> paramSliders_;
};

} // namespace bl::ui