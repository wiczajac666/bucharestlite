#include "panels/inspector_panel.hpp"
#include "panels/param_slider.hpp"

#include <bl_timeline/clip.hpp>
#include <bl_timeline/keyframes.hpp>
#include <bl_render/effect.hpp>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>
#include <sstream>

namespace bl::ui {

namespace {

constexpr int kMaxColorLabels = 8;

struct ChannelInfo {
    const char* label;
    KeyChannel channel;
};

const std::vector<ChannelInfo>& channelList() {
    static const std::vector<ChannelInfo> channels = {
        {"Scale", KeyChannel::Scale},
        {"Rotate X", KeyChannel::RotX},
        {"Rotate Y", KeyChannel::RotY},
        {"Position X", KeyChannel::PosX},
        {"Position Y", KeyChannel::PosY},
        {"Opacity", KeyChannel::Opacity},
        {"Volume", KeyChannel::Volume},
    };
    return channels;
}

KeyChannel channelAt(int index) {
    return channelList().at(static_cast<size_t>(index)).channel;
}

double gainToDb(double gain) {
    if (gain <= 0.0) return -60.0;
    return 20.0 * std::log10(gain);
}

double dbToGain(double db) {
    if (db <= -60.0) return 0.0;
    return std::pow(10.0, db / 20.0);
}

} // namespace

InspectorPanel::InspectorPanel(ProjectController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setObjectName(QStringLiteral("InspectorPanel"));

    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("inspectorStack"));

    // Page 0: placeholder.
    placeholderLabel_ = new QLabel(tr("Select a single clip"), this);
    placeholderLabel_->setAlignment(Qt::AlignCenter);
    placeholderLabel_->setObjectName(QStringLiteral("placeholder"));
    stack_->addWidget(placeholderLabel_);

    // Page 1: editor widget.
    editorWidget_ = new QWidget(this);
    auto* outerLayout = new QVBoxLayout(editorWidget_);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(editorWidget_);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget();
    auto* layout = new QVBoxLayout(scrollContent);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);

    layout->addWidget(buildNameSection(scrollContent));
    layout->addWidget(buildSourceSection(scrollContent));
    layout->addWidget(buildSpeedSection(scrollContent));
    layout->addWidget(buildAudioSection(scrollContent));
    layout->addWidget(buildColorSection(scrollContent));
    layout->addWidget(buildKeyframeSection(scrollContent));
    layout->addWidget(buildEffectSection(scrollContent));
    layout->addStretch(1);

    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea);
    stack_->addWidget(editorWidget_);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(stack_);

    if (controller_) {
        connect(controller_, &ProjectController::projectChanged, this, [this] {
            if (currentFlat_.has_value()) {
                reloadFromModel();
            }
        });
    }
}

bool InspectorPanel::isPlaceholderShown() const {
    return stack_->currentIndex() == 0;
}

std::optional<InspectorPanel::ClipLocation>
InspectorPanel::findSelectedClip(const QSet<ClipId>& ids) const {
    if (ids.size() != 1) return std::nullopt;
    const ClipId target = *ids.begin();
    const Sequence& seq = controller_->timeline().sequence();
    int flat = 0;
    for (const auto& track : seq.videoTracks) {
        for (const auto& clip : track.clips()) {
            if (clip.id == target) return ClipLocation{flat, target};
        }
        ++flat;
    }
    for (const auto& track : seq.audioTracks) {
        for (const auto& clip : track.clips()) {
            if (clip.id == target) return ClipLocation{flat, target};
        }
        ++flat;
    }
    return std::nullopt;
}

void InspectorPanel::showSelection(const QSet<ClipId>& ids) {
    auto loc = findSelectedClip(ids);
    if (!loc) {
        clearUI();
        stack_->setCurrentIndex(0);
        currentFlat_.reset();
        currentId_.clear();
        return;
    }
    currentFlat_ = loc->flat;
    currentId_ = loc->id;
    reloadFromModel();
    stack_->setCurrentIndex(1);
}

void InspectorPanel::clearUI() {
    suppress_ = true;
    if (nameEdit_) nameEdit_->clear();
    if (sourceInEdit_) sourceInEdit_->clear();
    if (sourceOutEdit_) sourceOutEdit_->clear();
    if (timelineStartLabel_) timelineStartLabel_->setText(QStringLiteral("-"));
    if (timelineDurationLabel_)
        timelineDurationLabel_->setText(QStringLiteral("-"));
    if (speedNum_) speedNum_->setValue(1);
    if (speedDen_) speedDen_->setValue(1);
    if (speedReverse_) speedReverse_->setChecked(false);
    if (gainSpin_) gainSpin_->setValue(1.0);
    if (panSpin_) panSpin_->setValue(0.0);
    if (keyframeTable_) keyframeTable_->setRowCount(0);
    if (effectList_) effectList_->clear();
    if (effectEnabledCheck_) effectEnabledCheck_->setChecked(true);
    if (effectParamsEdit_) effectParamsEdit_->clear();
    if (effectSliders_) {
        while (auto* item = effectSlidersLayout_->takeAt(0)) {
            if (QWidget* widget = item->widget()) delete widget;
            delete item;
        }
        paramSliders_.clear();
    }
    suppress_ = false;
}

void InspectorPanel::reloadFromModel() {
    if (!currentFlat_.has_value() || !controller_) return;
    const Sequence& seq = controller_->timeline().sequence();
    editor_.reset(controller_->timeline(), controller_->undoStack());

    const Clip* clip = editor_.findClip(seq, *currentFlat_, currentId_);
    if (!clip) {
        clearUI();
        stack_->setCurrentIndex(0);
        currentFlat_.reset();
        return;
    }

    suppress_ = true;

    // Name.
    if (nameEdit_) nameEdit_->setText(QString::fromStdString(clip->name));

    // Subtitle.
    if (subtitleEdit_)
        subtitleEdit_->setText(
            QString::fromStdString(clip->subtitleText.value_or("")));

    // Source.
    if (sourceInEdit_) {
        sourceInEdit_->setText(
            QString::number(clip->source.sourceIn.toSeconds(), 'f', 4));
    }
    if (sourceOutEdit_) {
        sourceOutEdit_->setText(
            QString::number(clip->source.sourceOut.toSeconds(), 'f', 4));
    }
    if (timelineStartLabel_) {
        timelineStartLabel_->setText(
            QString::number(clip->timelineStart.toSeconds(), 'f', 4));
    }
    if (timelineDurationLabel_) {
        timelineDurationLabel_->setText(
            QString::number(clip->timelineDuration.toSeconds(), 'f', 4));
    }

    // Speed.
    if (speedNum_) speedNum_->setValue(clip->speed.rateNum);
    if (speedDen_) speedDen_->setValue(clip->speed.rateDen);
    if (speedReverse_) speedReverse_->setChecked(clip->speed.reversed);

    // Audio.
    if (audioGroup_) {
        const bool isAudio =
            static_cast<size_t>(*currentFlat_) >= seq.videoTracks.size();
        audioGroup_->setVisible(isAudio);
        if (isAudio && gainSpin_) {
            gainSpin_->setValue(gainToDb(clip->audio.gain));
        }
        if (isAudio && panSpin_) panSpin_->setValue(clip->audio.pan);
    }

    // Color.
    for (size_t i = 0; i < colorButtons_.size(); ++i) {
        colorButtons_[i]->setChecked(clip->colorLabel == i);
    }

    // Keyframes.
    if (keyframeTable_) {
        keyframeTable_->blockSignals(true);
        keyframeTable_->setRowCount(0);
        if (clip->keyframes) {
            int channelIdx = keyframeChannel_ ? keyframeChannel_->currentIndex() : 0;
            if (channelIdx >= 0 &&
                static_cast<size_t>(channelIdx) < channelList().size()) {
                const KeyChannel ch = channelAt(channelIdx);
                const KeyframeTrack* track = clip->keyframes->track(ch);
                if (track) {
                    for (const Keyframe& kf : track->samples()) {
                        int row = keyframeTable_->rowCount();
                        keyframeTable_->insertRow(row);
                        keyframeTable_->setItem(
                            row, 0,
                            new QTableWidgetItem(
                                QString::number(kf.t.toSeconds(), 'f', 4)));
                        keyframeTable_->setItem(
                            row, 1,
                            new QTableWidgetItem(
                                QString::number(kf.value, 'f', 4)));
                        auto* interpCombo = new QComboBox();
                        interpCombo->addItem("Hold");
                        interpCombo->addItem("Linear");
                        interpCombo->addItem("Bezier");
                        interpCombo->setCurrentIndex(
                            static_cast<int>(kf.interpolation));
                        keyframeTable_->setCellWidget(row, 2, interpCombo);
                    }
                }
            }
        }
        keyframeTable_->blockSignals(false);
    }

    // Effects.
    const int previousEffectRow =
        effectList_ ? effectList_->currentRow() : -1;
    if (effectList_) {
        effectList_->clear();
        for (const EffectInstance& eff : clip->effects) {
            QString label = QString::fromStdString(eff.effectId);
            if (!eff.enabled) label += " (disabled)";
            effectList_->addItem(label);
        }
        if (previousEffectRow >= 0 &&
            previousEffectRow < effectList_->count()) {
            effectList_->setCurrentRow(previousEffectRow);
        }
        onEffectEnabledChanged();
    }

    suppress_ = false;
}

// ---------------------------------------------------------------------------
// Section builders.
// ---------------------------------------------------------------------------

QWidget* InspectorPanel::buildNameSection(QWidget* parent) {
    auto* group = new QGroupBox(tr("Clip"), parent);
    auto* form = new QFormLayout(group);
    nameEdit_ = new QLineEdit(group);
    nameEdit_->setObjectName(QStringLiteral("inspectorName"));
    form->addRow(tr("Name:"), nameEdit_);
    connect(nameEdit_, &QLineEdit::editingFinished, this,
            &InspectorPanel::onNameChanged);

    subtitleEdit_ = new QLineEdit(group);
    subtitleEdit_->setObjectName(QStringLiteral("inspectorSubtitle"));
    subtitleEdit_->setPlaceholderText(tr("No subtitle"));
    form->addRow(tr("Subtitle:"), subtitleEdit_);
    connect(subtitleEdit_, &QLineEdit::editingFinished, this,
            &InspectorPanel::onSubtitleChanged);
    return group;
}

QWidget* InspectorPanel::buildSourceSection(QWidget* parent) {
    auto* group = new QGroupBox(tr("Source"), parent);
    auto* form = new QFormLayout(group);
    sourceInEdit_ = new QLineEdit(group);
    sourceInEdit_->setReadOnly(true);
    sourceInEdit_->setObjectName(QStringLiteral("inspectorSourceIn"));
    sourceOutEdit_ = new QLineEdit(group);
    sourceOutEdit_->setReadOnly(true);
    sourceOutEdit_->setObjectName(QStringLiteral("inspectorSourceOut"));
    timelineStartLabel_ = new QLabel("-", group);
    timelineStartLabel_->setObjectName(QStringLiteral("inspectorTimelineStart"));
    timelineDurationLabel_ = new QLabel("-", group);
    timelineDurationLabel_->setObjectName(
        QStringLiteral("inspectorTimelineDuration"));
    form->addRow(tr("Source In:"), sourceInEdit_);
    form->addRow(tr("Source Out:"), sourceOutEdit_);
    form->addRow(tr("Timeline Start:"), timelineStartLabel_);
    form->addRow(tr("Duration:"), timelineDurationLabel_);
    return group;
}

QWidget* InspectorPanel::buildSpeedSection(QWidget* parent) {
    auto* group = new QGroupBox(tr("Speed"), parent);
    auto* form = new QFormLayout(group);
    auto* speedRow = new QWidget(group);
    auto* speedLayout = new QHBoxLayout(speedRow);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    speedNum_ = new QSpinBox(speedRow);
    speedNum_->setMinimum(1);
    speedNum_->setMaximum(1024);
    speedNum_->setObjectName(QStringLiteral("inspectorSpeedNum"));
    speedDen_ = new QSpinBox(speedRow);
    speedDen_->setMinimum(1);
    speedDen_->setMaximum(1024);
    speedDen_->setObjectName(QStringLiteral("inspectorSpeedDen"));
    speedLayout->addWidget(speedNum_);
    speedLayout->addWidget(new QLabel(" / ", speedRow));
    speedLayout->addWidget(speedDen_);
    speedLayout->addStretch(1);
    speedReverse_ = new QCheckBox(tr("Reverse"), group);
    speedReverse_->setObjectName(QStringLiteral("inspectorSpeedReverse"));
    form->addRow(tr("Rate:"), speedRow);
    form->addRow(QString(), speedReverse_);
    connect(speedNum_, &QSpinBox::editingFinished, this,
            &InspectorPanel::onSpeedChanged);
    connect(speedDen_, &QSpinBox::editingFinished, this,
            &InspectorPanel::onSpeedChanged);
    connect(speedReverse_, &QCheckBox::toggled, this,
            &InspectorPanel::onSpeedChanged);
    return group;
}

QWidget* InspectorPanel::buildAudioSection(QWidget* parent) {
    audioGroup_ = new QGroupBox(tr("Audio"), parent);
    auto* form = new QFormLayout(audioGroup_);
    gainSpin_ = new QDoubleSpinBox(audioGroup_);
    gainSpin_->setRange(-60.0, 20.0);
    gainSpin_->setDecimals(1);
    gainSpin_->setSuffix(" dB");
    gainSpin_->setObjectName(QStringLiteral("inspectorGain"));
    panSpin_ = new QDoubleSpinBox(audioGroup_);
    panSpin_->setRange(-1.0, 1.0);
    panSpin_->setDecimals(2);
    panSpin_->setObjectName(QStringLiteral("inspectorPan"));
    form->addRow(tr("Gain:"), gainSpin_);
    form->addRow(tr("Pan:"), panSpin_);
    connect(gainSpin_,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &InspectorPanel::onGainChanged);
    connect(panSpin_,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &InspectorPanel::onPanChanged);
    return audioGroup_;
}

QWidget* InspectorPanel::buildColorSection(QWidget* parent) {
    auto* group = new QGroupBox(tr("Label"), parent);
    auto* row = new QHBoxLayout(group);
    row->setContentsMargins(4, 4, 4, 4);
    colorButtons_.clear();
    for (int i = 0; i < kMaxColorLabels; ++i) {
        auto* btn = new QPushButton(QString::number(i + 1), group);
        btn->setCheckable(true);
        btn->setObjectName(QStringLiteral("inspectorColor"));
        btn->setMinimumSize(24, 24);
        btn->setMaximumSize(24, 24);
        connect(btn, &QPushButton::clicked, this,
                [this, i] { onColorButton(i); });
        row->addWidget(btn);
        colorButtons_.push_back(btn);
    }
    row->addStretch(1);
    return group;
}

QWidget* InspectorPanel::buildKeyframeSection(QWidget* parent) {
    auto* group = new QGroupBox(tr("Keyframes"), parent);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* header = new QHBoxLayout();
    keyframeChannel_ = new QComboBox(group);
    keyframeChannel_->setObjectName(QStringLiteral("inspectorKeyChannel"));
    for (const auto& ch : channelList()) {
        keyframeChannel_->addItem(tr(ch.label));
    }
    header->addWidget(keyframeChannel_, 1);
    keyframeAdd_ = new QPushButton(tr("Add"), group);
    keyframeAdd_->setObjectName(QStringLiteral("inspectorKeyAdd"));
    header->addWidget(keyframeAdd_);
    keyframeRemove_ = new QPushButton(tr("Remove"), group);
    keyframeRemove_->setObjectName(QStringLiteral("inspectorKeyRemove"));
    header->addWidget(keyframeRemove_);
    layout->addLayout(header);

    keyframeTable_ = new QTableWidget(0, 3, group);
    keyframeTable_->setObjectName(QStringLiteral("inspectorKeyTable"));
    keyframeTable_->setHorizontalHeaderLabels({tr("Time"), tr("Value"),
                                               tr("Interpolation")});
    keyframeTable_->horizontalHeader()->setStretchLastSection(true);
    keyframeTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    keyframeTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    keyframeTable_->verticalHeader()->hide();
    layout->addWidget(keyframeTable_, 1);

    connect(keyframeChannel_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &InspectorPanel::onKeyframeChannelChanged);
    connect(keyframeAdd_, &QPushButton::clicked, this,
            &InspectorPanel::onAddKeyframe);
    connect(keyframeRemove_, &QPushButton::clicked, this,
            &InspectorPanel::onRemoveKeyframe);
    connect(keyframeTable_, &QTableWidget::cellChanged, this,
            &InspectorPanel::onKeyframeCellChanged);
    return group;
}

QWidget* InspectorPanel::buildEffectSection(QWidget* parent) {
    auto* group = new QGroupBox(tr("Effects"), parent);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* listRow = new QHBoxLayout();
    effectList_ = new QListWidget(group);
    effectList_->setObjectName(QStringLiteral("inspectorEffectList"));
    effectList_->setAlternatingRowColors(true);
    listRow->addWidget(effectList_, 1);

    auto* btnCol = new QVBoxLayout();
    effectAdd_ = new QPushButton(tr("Add..."), group);
    effectAdd_->setObjectName(QStringLiteral("inspectorEffectAdd"));
    effectAddMenu_ = new QMenu(effectAdd_);
    for (const std::string& id : bl::EffectRegistry::instance().catalog()) {
        const bl::IEffect* fx = bl::EffectRegistry::instance().find(id);
        if (!fx) continue;
        auto* action = effectAddMenu_->addAction(
            QString::fromStdString(std::string(fx->displayName())));
        action->setData(QString::fromStdString(id));
    }
    effectAdd_->setMenu(effectAddMenu_);
    effectRemove_ = new QPushButton(tr("Remove"), group);
    effectRemove_->setObjectName(QStringLiteral("inspectorEffectRemove"));
    effectUp_ = new QPushButton(tr("Up"), group);
    effectUp_->setObjectName(QStringLiteral("inspectorEffectUp"));
    effectDown_ = new QPushButton(tr("Down"), group);
    effectDown_->setObjectName(QStringLiteral("inspectorEffectDown"));
    btnCol->addWidget(effectAdd_);
    btnCol->addWidget(effectRemove_);
    btnCol->addWidget(effectUp_);
    btnCol->addWidget(effectDown_);
    btnCol->addStretch(1);
    listRow->addLayout(btnCol);
    layout->addLayout(listRow);

    // Enable toggle.
    auto* enabledRow = new QHBoxLayout();
    effectEnabledLabel_ = new QLabel(tr("Enabled:"), group);
    effectEnabledCheck_ = new QCheckBox(group);
    effectEnabledCheck_->setObjectName(QStringLiteral("inspectorEffectEnabled"));
    enabledRow->addWidget(effectEnabledLabel_);
    enabledRow->addWidget(effectEnabledCheck_);
    enabledRow->addStretch(1);
    layout->addLayout(enabledRow);

    // Params editor: schema-driven sliders when the effect exposes specs,
    // raw JSON line editor otherwise.
    effectSliders_ = new QWidget(group);
    effectSliders_->setObjectName(QStringLiteral("inspectorEffectSliders"));
    effectSlidersLayout_ = new QVBoxLayout(effectSliders_);
    effectSlidersLayout_->setContentsMargins(0, 0, 0, 0);
    effectSlidersLayout_->setSpacing(4);
    layout->addWidget(effectSliders_);

    auto* paramsRow = new QHBoxLayout();
    effectParamsLabel_ = new QLabel(tr("Params JSON:"), group);
    effectParamsEdit_ = new QLineEdit(group);
    effectParamsEdit_->setObjectName(QStringLiteral("inspectorEffectParams"));
    paramsRow->addWidget(effectParamsLabel_);
    paramsRow->addWidget(effectParamsEdit_, 1);
    layout->addLayout(paramsRow);

    connect(effectList_, &QListWidget::currentRowChanged, this,
            &InspectorPanel::onEffectEnabledChanged);
    connect(effectAddMenu_, &QMenu::triggered, this,
            [this](QAction* action) { addEffectById(action->data().toString()); });
    connect(effectRemove_, &QPushButton::clicked, this,
            &InspectorPanel::onRemoveEffect);
    connect(effectUp_, &QPushButton::clicked, this,
            &InspectorPanel::onReorderEffectUp);
    connect(effectDown_, &QPushButton::clicked, this,
            &InspectorPanel::onReorderEffectDown);
    connect(effectEnabledCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (suppress_ || !currentFlat_.has_value()) return;
        int row = effectList_ ? effectList_->currentRow() : -1;
        if (row < 0) return;
        editor_.setEffectEnabled(*currentFlat_, currentId_,
                                 static_cast<size_t>(row), on);
        reloadFromModel();
        emit clipChanged();
    });
    connect(effectParamsEdit_, &QLineEdit::editingFinished, this,
            &InspectorPanel::onEffectParamsChanged);
    return group;
}

// ---------------------------------------------------------------------------
// Mutator callbacks.
// ---------------------------------------------------------------------------

void InspectorPanel::onNameChanged() {
    if (suppress_ || !currentFlat_.has_value() || !nameEdit_) return;
    editor_.setClipName(*currentFlat_, currentId_,
                        nameEdit_->text().toStdString());
    emit clipChanged();
}

void InspectorPanel::onSubtitleChanged() {
    if (suppress_ || !currentFlat_.has_value() || !subtitleEdit_) return;
    editor_.setSubtitleText(*currentFlat_, currentId_,
                            subtitleEdit_->text().toStdString());
    emit clipChanged();
}

void InspectorPanel::onSourceChanged() {
    // Source fields are read-only display.
}

void InspectorPanel::onSpeedChanged() {
    if (suppress_ || !currentFlat_.has_value() || !speedNum_ || !speedDen_ ||
        !speedReverse_) {
        return;
    }
    bl::SpeedRemap speed;
    speed.rateNum = speedNum_->value();
    speed.rateDen = speedDen_->value();
    speed.reversed = speedReverse_->isChecked();
    if (!editor_.setSpeed(*currentFlat_, currentId_, speed)) return;
    emit clipChanged();
    reloadFromModel();
}

void InspectorPanel::onGainChanged() {
    if (suppress_ || !currentFlat_.has_value() || !gainSpin_) return;
    const double gain = dbToGain(gainSpin_->value());
    editor_.setGain(*currentFlat_, currentId_, gain);
    emit clipChanged();
}

void InspectorPanel::onPanChanged() {
    if (suppress_ || !currentFlat_.has_value() || !panSpin_) return;
    editor_.setPan(*currentFlat_, currentId_, panSpin_->value());
    emit clipChanged();
}

void InspectorPanel::onColorButton(int index) {
    if (suppress_ || !currentFlat_.has_value()) return;
    uint32_t label = (colorButtons_[index]->isChecked()) ? index : 0;
    editor_.setColorLabel(*currentFlat_, currentId_, label);
    reloadFromModel();
    emit clipChanged();
}

void InspectorPanel::onKeyframeChannelChanged() {
    if (suppress_) return;
    reloadFromModel();
}

void InspectorPanel::onAddKeyframe() {
    if (suppress_ || !currentFlat_.has_value() || !keyframeChannel_) return;
    if (!keyframeTable_) return;
    const Sequence& seq = controller_->timeline().sequence();
    const Clip* clip =
        editor_.findClip(seq, *currentFlat_, currentId_);
    if (!clip) return;

    const KeyChannel ch = channelAt(keyframeChannel_->currentIndex());
    // Default to time 0 / value 1.0 / Linear.
    const Rational fps = seq.settings.fps;
    const Rational rate{1'000'000, 1};
    Time t = Time::fromFrameAt(0, fps, rate);
    if (clip->keyframes) {
        if (const KeyframeTrack* track = clip->keyframes->track(ch)) {
            if (!track->empty()) {
                t = track->samples().back().t +
                    Duration::fromFrames(1, fps);
            }
        }
    }
    editor_.setKeyframe(*currentFlat_, currentId_, ch, t, 1.0,
                        Interpolation::Linear);
    reloadFromModel();
    emit clipChanged();
}

void InspectorPanel::onRemoveKeyframe() {
    if (suppress_ || !currentFlat_.has_value() || !keyframeChannel_) return;
    if (!keyframeTable_) return;
    int row = keyframeTable_->currentRow();
    if (row < 0) return;

    const Sequence& seq = controller_->timeline().sequence();
    const Clip* clip =
        editor_.findClip(seq, *currentFlat_, currentId_);
    if (!clip || !clip->keyframes) return;
    const KeyChannel ch = channelAt(keyframeChannel_->currentIndex());
    const KeyframeTrack* track = clip->keyframes->track(ch);
    if (!track || static_cast<size_t>(row) >= track->samples().size()) return;
    const Time t = track->samples()[static_cast<size_t>(row)].t;

    editor_.removeKeyframe(*currentFlat_, currentId_, ch, t);
    reloadFromModel();
    emit clipChanged();
}

void InspectorPanel::onKeyframeCellChanged(int row, int column) {
    if (suppress_ || !currentFlat_.has_value() || !keyframeChannel_) return;
    if (column != 1) return; // only the "value" column is editable.

    const Sequence& seq = controller_->timeline().sequence();
    const Clip* clip =
        editor_.findClip(seq, *currentFlat_, currentId_);
    if (!clip || !clip->keyframes) return;
    const KeyChannel ch = channelAt(keyframeChannel_->currentIndex());
    const KeyframeTrack* track = clip->keyframes->track(ch);
    if (!track || static_cast<size_t>(row) >= track->samples().size()) return;

    const Keyframe& kf = track->samples()[static_cast<size_t>(row)];
    QTableWidgetItem* cell = keyframeTable_->item(row, column);
    if (!cell) return;
    bool ok = false;
    double val = cell->text().toDouble(&ok);
    if (!ok) return;

    suppress_ = true;
    editor_.setKeyframe(*currentFlat_, currentId_, ch, kf.t, val,
                        kf.interpolation);
    suppress_ = false;
    emit clipChanged();
}

void InspectorPanel::addEffectById(const QString& effectId) {
    if (suppress_ || !currentFlat_.has_value()) return;
    if (effectId.isEmpty()) return;
    const Sequence& seq = controller_->timeline().sequence();
    const Clip* clip =
        editor_.findClip(seq, *currentFlat_, currentId_);
    if (!clip) return;

    EffectInstance effect;
    effect.effectId = effectId.toStdString();
    if (const bl::IEffect* fx =
            bl::EffectRegistry::instance().find(effect.effectId)) {
        effect.params = bl::makeDefaultParams(fx->paramSpecs());
    }

    editor_.addEffect(*currentFlat_, currentId_, effect);
    reloadFromModel();
    if (effectList_ && effectList_->count() > 0) {
        effectList_->setCurrentRow(effectList_->count() - 1);
    }
    emit clipChanged();
}

void InspectorPanel::onRemoveEffect() {
    if (suppress_ || !currentFlat_.has_value()) return;
    if (!effectList_) return;
    int row = effectList_->currentRow();
    if (row < 0) return;
    editor_.removeEffect(*currentFlat_, currentId_, static_cast<size_t>(row));
    reloadFromModel();
    emit clipChanged();
}

void InspectorPanel::onReorderEffectUp() {
    if (suppress_ || !currentFlat_.has_value()) return;
    if (!effectList_) return;
    int row = effectList_->currentRow();
    if (row <= 0) return;
    editor_.reorderEffect(*currentFlat_, currentId_, static_cast<size_t>(row),
                          static_cast<size_t>(row - 1));
    reloadFromModel();
    effectList_->setCurrentRow(row - 1);
    emit clipChanged();
}

void InspectorPanel::onReorderEffectDown() {
    if (suppress_ || !currentFlat_.has_value()) return;
    if (!effectList_) return;
    int row = effectList_->currentRow();
    if (row < 0) return;
    const Sequence& seq = controller_->timeline().sequence();
    const Clip* clip =
        editor_.findClip(seq, *currentFlat_, currentId_);
    if (!clip || static_cast<size_t>(row + 1) >= clip->effects.size()) return;
    editor_.reorderEffect(*currentFlat_, currentId_, static_cast<size_t>(row),
                          static_cast<size_t>(row + 1));
    reloadFromModel();
    effectList_->setCurrentRow(row + 1);
    emit clipChanged();
}

void InspectorPanel::onEffectEnabledChanged() {
    if (suppress_ || !currentFlat_.has_value()) return;
    if (!effectList_ || !effectEnabledCheck_) return;
    int row = effectList_->currentRow();
    if (row < 0) {
        effectEnabledCheck_->setEnabled(false);
        effectEnabledLabel_->setEnabled(false);
        effectParamsLabel_->setEnabled(false);
        effectParamsEdit_->setEnabled(false);
        effectSliders_->hide();
        return;
    }
    const Sequence& seq = controller_->timeline().sequence();
    const Clip* clip =
        editor_.findClip(seq, *currentFlat_, currentId_);
    if (!clip || static_cast<size_t>(row) >= clip->effects.size()) return;

    const EffectInstance& eff = clip->effects[static_cast<size_t>(row)];
    effectEnabledLabel_->setEnabled(true);
    effectEnabledCheck_->setEnabled(true);
    effectParamsLabel_->setEnabled(true);
    effectParamsEdit_->setEnabled(true);
    effectEnabledCheck_->blockSignals(true);
    effectEnabledCheck_->setChecked(eff.enabled);
    effectEnabledCheck_->blockSignals(false);
    effectParamsEdit_->setText(
        QString::fromStdString(eff.params.dump()));
    rebuildEffectParams(eff);
}

void InspectorPanel::onEffectParamsChanged() {
    if (suppress_ || !currentFlat_.has_value()) return;
    if (!effectList_ || !effectParamsEdit_) return;
    int row = effectList_->currentRow();
    if (row < 0) return;

    nlohmann::json params;
    try {
        params = nlohmann::json::parse(
            effectParamsEdit_->text().toStdString());
    } catch (...) {
        return; // ignore invalid JSON.
    }
    if (!params.is_object()) return;

    editor_.setEffectParams(*currentFlat_, currentId_, static_cast<size_t>(row),
                            params);
    reloadFromModel();
    emit clipChanged();
}

void InspectorPanel::rebuildEffectParams(const EffectInstance& effect) {
    if (!effectSliders_ || !effectSlidersLayout_ || !effectParamsEdit_) return;

    while (auto* item = effectSlidersLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) delete widget;
        delete item;
    }
    paramSliders_.clear();

    std::vector<bl::ParamSpec> specs;
    if (const bl::IEffect* fx =
            bl::EffectRegistry::instance().find(effect.effectId)) {
        specs = fx->paramSpecs();
    }

    // Effects without parameter specs keep the raw JSON editor.
    effectSliders_->setVisible(!specs.empty());
    effectParamsLabel_->setVisible(specs.empty());
    effectParamsEdit_->setVisible(specs.empty());

    if (specs.empty()) return;

    for (const bl::ParamSpec& spec : specs) {
        double value = spec.def;
        if (effect.params.is_object() && effect.params.contains(spec.key)) {
            const nlohmann::json& jv = effect.params[spec.key];
            if (jv.is_number()) value = jv.get<double>();
        }
        auto* slider = new ParamSlider(spec, effectSliders_);
        slider->setObjectName(QStringLiteral("inspectorParam_%1").arg(
            QString::fromStdString(spec.key)));
        slider->setValue(value);
        connect(slider, &ParamSlider::valueCommitted, this,
                [this, key = spec.key](double val) {
                    onParamCommitted(key, val);
                });
        effectSlidersLayout_->addWidget(slider);
        paramSliders_.push_back(slider);
    }
}

void InspectorPanel::onParamCommitted(const std::string& key, double value) {
    if (suppress_ || !currentFlat_.has_value()) return;
    if (!effectList_) return;
    int row = effectList_->currentRow();
    if (row < 0) return;

    const Sequence& seq = controller_->timeline().sequence();
    const Clip* clip =
        editor_.findClip(seq, *currentFlat_, currentId_);
    if (!clip || static_cast<size_t>(row) >= clip->effects.size()) return;

    nlohmann::json params = clip->effects[static_cast<size_t>(row)].params;
    if (!params.is_object()) params = nlohmann::json::object();
    params[key] = value;

    const int effectRow = row;
    editor_.setEffectParams(*currentFlat_, currentId_,
                            static_cast<size_t>(effectRow), params);
    reloadFromModel();
    if (effectList_ && effectList_->count() > 0) {
        effectList_->setCurrentRow(effectRow);
    }
    emit clipChanged();
}

} // namespace bl::ui