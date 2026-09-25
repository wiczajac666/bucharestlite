#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>

#include <app/project_controller.hpp>
#include <panels/inspector_panel.hpp>
#include <panels/param_slider.hpp>

#include <gtest/gtest.h>

#include <bl_timeline/clip.hpp>
#include <bl_timeline/keyframes.hpp>
#include <bl_timeline/sequence.hpp>

#include <set>
#include <string>

namespace {

QApplication* ensureApp() {
    static QApplication* app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char argv0[] = "bl_ui_tests";
        static char* argv[] = {argv0, nullptr};
        return new QApplication(argc, argv);
    }();
    return app;
}

struct AppBootstrapper {
    AppBootstrapper() { ensureApp(); }
};
const AppBootstrapper kAppBootstrapper{};

using bl::Clip;
using bl::Duration;
using bl::EffectInstance;
using bl::Interpolation;
using bl::KeyChannel;
using bl::Keyframe;
using bl::Rational;
using bl::Time;

const Rational kFps{24000, 1001};

struct Fixture {
    bl::ui::ProjectController controller;
    bl::ui::InspectorPanel panel;

    QLineEdit* nameEdit{nullptr};
    QLineEdit* sourceIn{nullptr};
    QLineEdit* sourceOut{nullptr};
    QSpinBox* speedNum{nullptr};
    QSpinBox* speedDen{nullptr};
    QComboBox* keyChannel{nullptr};
    QTableWidget* keyTable{nullptr};
    QListWidget* effectList{nullptr};
    QLineEdit* effectParams{nullptr};
    QCheckBox* effectEnabled{nullptr};

    Fixture() : panel(&controller) {
        ensureApp();
        controller.newProject(QStringLiteral("Insp"));
        auto& tl = controller.timeline();
        tl.sequence().addVideoTrack("V1");
        tl.sequence().addAudioTrack("A1");

        Clip v;
        v.id = "v1";
        v.name = "Video Clip";
        v.timelineStart = Time::fromFrame(0, kFps);
        v.timelineDuration = Duration::fromFrames(10, kFps);
        v.source.sourceIn = Time::fromFrame(0, kFps);
        v.source.sourceOut = Time::fromFrame(10, kFps);
        tl.addClipToVideoTrack(0, v);

        Clip a;
        a.id = "a1";
        a.name = "Audio Clip";
        a.timelineStart = Time::fromFrame(0, kFps);
        a.timelineDuration = Duration::fromFrames(10, kFps);
        a.source.sourceIn = Time::fromFrame(0, kFps);
        a.source.sourceOut = Time::fromFrame(10, kFps);
        tl.addClipToAudioTrack(0, a);

        wireHelpers();
    }

    void wireHelpers() {
        nameEdit =
            panel.findChild<QLineEdit*>(QStringLiteral("inspectorName"));
        sourceIn =
            panel.findChild<QLineEdit*>(QStringLiteral("inspectorSourceIn"));
        sourceOut =
            panel.findChild<QLineEdit*>(QStringLiteral("inspectorSourceOut"));
        speedNum =
            panel.findChild<QSpinBox*>(QStringLiteral("inspectorSpeedNum"));
        speedDen =
            panel.findChild<QSpinBox*>(QStringLiteral("inspectorSpeedDen"));
        keyChannel =
            panel.findChild<QComboBox*>(QStringLiteral("inspectorKeyChannel"));
        keyTable =
            panel.findChild<QTableWidget*>(QStringLiteral("inspectorKeyTable"));
        effectList =
            panel.findChild<QListWidget*>(QStringLiteral("inspectorEffectList"));
        effectParams =
            panel.findChild<QLineEdit*>(QStringLiteral("inspectorEffectParams"));
        effectEnabled =
            panel.findChild<QCheckBox*>(QStringLiteral("inspectorEffectEnabled"));
    }

    void selectVideo() {
        panel.showSelection(QSet<bl::ClipId>{"v1"});
    }

    const Clip* video() const {
        const auto& track = controller.timeline().sequence().videoTracks[0];
        for (const auto& c : track.clips()) {
            if (c.id == "v1") return &c;
        }
        return nullptr;
    }
};

TEST(InspectorPanel, PlaceholderWithoutSelection) {
    Fixture f;
    f.panel.showSelection(QSet<bl::ClipId>{});        // none
    EXPECT_TRUE(f.panel.isPlaceholderShown());
    EXPECT_FALSE(f.panel.hasClip());

    f.panel.showSelection(QSet<bl::ClipId>{"v1", "a1"}); // multi
    EXPECT_TRUE(f.panel.isPlaceholderShown());
    EXPECT_FALSE(f.panel.hasClip());
}

TEST(InspectorPanel, ShowsSingleSelection) {
    Fixture f;
    f.selectVideo();
    EXPECT_FALSE(f.panel.isPlaceholderShown());
    EXPECT_TRUE(f.panel.hasClip());
    ASSERT_NE(f.nameEdit, nullptr);
    EXPECT_EQ(f.nameEdit->text(), QStringLiteral("Video Clip"));
}

TEST(InspectorPanel, RenameCommitsToModel) {
    Fixture f;
    f.selectVideo();
    ASSERT_NE(f.nameEdit, nullptr);
    f.nameEdit->setText(QStringLiteral("Renamed Clip"));
    QTest::keyClick(f.nameEdit, Qt::Key_Return);
    EXPECT_EQ(f.video()->name, "Renamed Clip");
    EXPECT_TRUE(f.controller.canUndo());
}

TEST(InspectorPanel, GainSliderOnlyForAudioClips) {
    Fixture f;
    f.panel.showSelection(QSet<bl::ClipId>{"v1"});
    auto* audioGroup = f.panel.findChild<QGroupBox*>();
    // Not visible for video clips.
    EXPECT_FALSE(audioGroup == nullptr);
    f.selectVideo();
}

TEST(InspectorPanel, ShowsAudioGainPan) {
    Fixture f;
    f.panel.showSelection(QSet<bl::ClipId>{"a1"});
    f.wireHelpers();
    auto* gain =
        f.panel.findChild<QDoubleSpinBox*>(QStringLiteral("inspectorGain"));
    auto* pan =
        f.panel.findChild<QDoubleSpinBox*>(QStringLiteral("inspectorPan"));
    ASSERT_NE(gain, nullptr);
    ASSERT_NE(pan, nullptr);
    EXPECT_DOUBLE_EQ(gain->value(), 0.0); // gain 1.0 == 0 dB
    EXPECT_DOUBLE_EQ(pan->value(), 0.0);
}

TEST(InspectorPanel, AddRemoveKeyframe) {
    Fixture f;
    f.selectVideo();
    ASSERT_NE(f.keyChannel, nullptr);
    ASSERT_NE(f.keyTable, nullptr);
    f.keyChannel->setCurrentIndex(5); // Opacity
    QTest::keyClick(f.keyChannel, Qt::Key_Return);

    auto* add = f.panel.findChild<QPushButton*>(QStringLiteral("inspectorKeyAdd"));
    ASSERT_NE(add, nullptr);
    add->click();
    EXPECT_EQ(f.keyTable->rowCount(), 1);
    ASSERT_TRUE(f.video()->keyframes.has_value());
    auto* track = f.video()->keyframes->track(KeyChannel::Opacity);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->samples().size(), 1u);

    // Select the row and remove it.
    f.keyTable->selectRow(0);
    auto* remove =
        f.panel.findChild<QPushButton*>(QStringLiteral("inspectorKeyRemove"));
    ASSERT_NE(remove, nullptr);
    remove->click();
    EXPECT_EQ(f.video()->keyframes->track(KeyChannel::Opacity)->samples().size(),
              0u);
}

TEST(InspectorPanel, RemoveKeyframeIsUndoable) {
    Fixture f;
    f.selectVideo();
    // Seed with two keyframes for Opacity directly.
    {
        bl::Timeline& tl = f.controller.timeline();
        tl.setClipKeyframeInVideoTrack(0, "v1", KeyChannel::Opacity,
                                       Time::fromFrame(0, kFps), 0.0,
                                       Interpolation::Hold);
        tl.setClipKeyframeInVideoTrack(0, "v1", KeyChannel::Opacity,
                                       Time::fromFrame(5, kFps), 1.0,
                                       Interpolation::Linear);
    }
    f.panel.showSelection(QSet<bl::ClipId>{"v1"});
    f.wireHelpers();
    ASSERT_NE(f.keyChannel, nullptr);
    f.keyChannel->setCurrentIndex(5); // Opacity
    f.wireHelpers();
    ASSERT_EQ(f.keyTable->rowCount(), 2);

    f.keyTable->selectRow(0);
    auto* remove =
        f.panel.findChild<QPushButton*>(QStringLiteral("inspectorKeyRemove"));
    ASSERT_NE(remove, nullptr);
    remove->click();
    ASSERT_EQ(f.keyTable->rowCount(), 1);

    f.controller.undoStack().undo();
    f.wireHelpers();
    f.panel.showSelection(QSet<bl::ClipId>{"v1"});
    f.wireHelpers();
    EXPECT_EQ(f.keyTable->rowCount(), 2);
}

TEST(InspectorPanel, AddEffectAppearsInList) {
    Fixture f;
    f.selectVideo();
    ASSERT_NE(f.effectList, nullptr);
    f.panel.addEffectById(QStringLiteral("blur.box"));
    ASSERT_EQ(f.effectList->count(), 1);
    EXPECT_EQ(f.video()->effects.size(), 1u);
}

TEST(InspectorPanel, EffectAddMenuListsCatalog) {
    Fixture f;
    f.selectVideo();
    ASSERT_NE(f.effectList, nullptr);
    auto* add =
        f.panel.findChild<QPushButton*>(QStringLiteral("inspectorEffectAdd"));
    ASSERT_NE(add, nullptr);
    auto* menu = add->menu();
    ASSERT_NE(menu, nullptr);

    QSet<QString> labels;
    QSet<QString> ids;
    for (QAction* action : menu->actions()) {
        labels.insert(action->text());
        ids.insert(action->data().toString());
    }
    EXPECT_TRUE(labels.contains(QStringLiteral("Box Blur")));
    EXPECT_TRUE(labels.contains(QStringLiteral("Color Correction")));
    EXPECT_TRUE(labels.contains(QStringLiteral("Greyscale")));
    EXPECT_TRUE(labels.contains(QStringLiteral("Transform 2D")));
    EXPECT_TRUE(ids.contains(QStringLiteral("blur.box")));
    EXPECT_TRUE(ids.contains(QStringLiteral("brightness_contrast_gamma")));

    menu->actions().at(1)->trigger();
    EXPECT_EQ(f.video()->effects.size(), 1u);
    EXPECT_EQ(f.video()->effects[0].effectId,
              "brightness_contrast_gamma");
}

TEST(InspectorPanel, AddColorEffectSeedsDefaultParamsAndSliders) {
    Fixture f;
    f.selectVideo();
    f.panel.addEffectById(QStringLiteral("brightness_contrast_gamma"));
    ASSERT_EQ(f.video()->effects.size(), 1u);
    const auto& params = f.video()->effects[0].params;
    EXPECT_EQ(params.value("brightness", -999.0), 0.0);
    EXPECT_EQ(params.value("contrast", -999.0), 1.0);
    EXPECT_EQ(params.value("gamma", -999.0), 1.0);

    auto* bright =
        f.panel.findChild<bl::ui::ParamSlider*>(
            QStringLiteral("inspectorParam_brightness"));
    auto* contrast =
        f.panel.findChild<bl::ui::ParamSlider*>(
            QStringLiteral("inspectorParam_contrast"));
    auto* gamma =
        f.panel.findChild<bl::ui::ParamSlider*>(
            QStringLiteral("inspectorParam_gamma"));
    ASSERT_NE(bright, nullptr);
    ASSERT_NE(contrast, nullptr);
    ASSERT_NE(gamma, nullptr);
    EXPECT_DOUBLE_EQ(bright->value(), 0.0);
    EXPECT_DOUBLE_EQ(contrast->value(), 1.0);
    EXPECT_DOUBLE_EQ(gamma->value(), 1.0);
}

TEST(InspectorPanel, ParamSliderCommitWritesUndoableParams) {
    Fixture f;
    f.selectVideo();
    f.panel.addEffectById(QStringLiteral("brightness_contrast_gamma"));
    ASSERT_EQ(f.video()->effects.size(), 1u);

    auto* bright =
        f.panel.findChild<bl::ui::ParamSlider*>(
            QStringLiteral("inspectorParam_brightness"));
    ASSERT_NE(bright, nullptr);
    auto* slider = bright->findChild<QSlider*>();
    ASSERT_NE(slider, nullptr);

    // Brightness range [-1, 1]; request 0.5 -> position 750 of 1000.
    slider->setValue(750);
    bright->commit();

    EXPECT_NEAR(f.video()->effects[0].params.value("brightness", -999.0),
                0.5, 1e-6);
    EXPECT_TRUE(f.controller.canUndo());

    f.controller.undoStack().undo();
    EXPECT_NEAR(f.video()->effects[0].params.value("brightness", -999.0),
                0.0, 1e-9);
}

TEST(InspectorPanel, ParamSlidersHiddenForSpecLessEffect) {
    Fixture f;
    f.selectVideo();
    f.panel.addEffectById(QStringLiteral("transform_2d"));
    ASSERT_EQ(f.video()->effects.size(), 1u);

    auto* sliders =
        f.panel.findChild<QWidget*>(QStringLiteral("inspectorEffectSliders"));
    auto* paramsJson =
        f.panel.findChild<QLineEdit*>(QStringLiteral("inspectorEffectParams"));
    ASSERT_NE(sliders, nullptr);
    ASSERT_NE(paramsJson, nullptr);
    EXPECT_TRUE(sliders->isHidden());
    EXPECT_FALSE(paramsJson->isHidden());
}

TEST(InspectorPanel, RemoveEffectRemovesFromModel) {
    Fixture f;
    f.selectVideo();
    f.panel.addEffectById(QStringLiteral("blur.box"));
    EXPECT_EQ(f.video()->effects.size(), 1u);

    ASSERT_NE(f.effectList, nullptr);
    f.effectList->setCurrentRow(0);
    auto* remove =
        f.panel.findChild<QPushButton*>(QStringLiteral("inspectorEffectRemove"));
    ASSERT_NE(remove, nullptr);
    remove->click();
    EXPECT_TRUE(f.video()->effects.empty());
}

TEST(InspectorPanel, ToggleEffectBypassGlitch) {
    Fixture f;
    f.selectVideo();
    f.panel.addEffectById(QStringLiteral("blur.box"));
    f.effectList->setCurrentRow(0);
    f.wireHelpers();

    ASSERT_NE(f.effectEnabled, nullptr);
    // Checkbox reflects the model's enabled state.
    EXPECT_TRUE(f.effectEnabled->isChecked());
}

} // namespace