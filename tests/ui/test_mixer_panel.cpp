#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTest>

#include <app/project_controller.hpp>
#include <panels/master_fader_panel.hpp>
#include <panels/mixer_panel.hpp>

#include <gtest/gtest.h>

#include <bl_timeline/sequence.hpp>

namespace {

QApplication* ensureApp() {
    static QApplication* app = [] {
        int argc = 1;
        static char arg[] = "test";
        static char* argv[] = {arg, nullptr};
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return new QApplication(argc, argv);
    }();
    return app;
}

QSlider* fader(bl::ui::MixerPanel& panel, int strip) {
    return panel.findChild<QSlider*>(QStringLiteral("fader_%1").arg(strip));
}
QPushButton* muteButton(bl::ui::MixerPanel& panel, int strip) {
    return panel.findChild<QPushButton*>(QStringLiteral("mute_%1").arg(strip));
}
QPushButton* soloButton(bl::ui::MixerPanel& panel, int strip) {
    return panel.findChild<QPushButton*>(QStringLiteral("solo_%1").arg(strip));
}
QLabel* emptyLabel(bl::ui::MixerPanel& panel) {
    return panel.findChild<QLabel*>(QStringLiteral("emptyState"));
}

void releaseSlider(QSlider* slider) {
    QMetaObject::invokeMethod(slider, "sliderReleased");
}

struct PanelFixture {
    QApplication* app{nullptr};
    bl::ui::ProjectController controller;

    PanelFixture() {
        app = ensureApp();
        controller.newProject(QStringLiteral("Mix"));
    }
};

} // namespace

TEST(MixerPanel, BuildsOneStripPerAudioTrack) {
    PanelFixture f;
    bl::ui::MixerPanel panel(&f.controller);
    EXPECT_EQ(panel.stripCount(), 1);
    EXPECT_EQ(panel.audioTrackCount(), 1);
    EXPECT_NE(fader(panel, 0), nullptr);
    EXPECT_NE(muteButton(panel, 0), nullptr);
    EXPECT_NE(soloButton(panel, 0), nullptr);
    EXPECT_EQ(emptyLabel(panel), nullptr); // no empty state with tracks
}

TEST(MixerPanel, ShowsEmptyStateWithoutAudioTracks) {
    PanelFixture f;
    f.controller.timeline().sequence().audioTracks.clear();
    emit f.controller.projectChanged();
    bl::ui::MixerPanel panel(&f.controller);
    EXPECT_EQ(panel.stripCount(), 0);
    auto* label = emptyLabel(panel);
    ASSERT_NE(label, nullptr);
    EXPECT_FALSE(label->isHidden());
}

TEST(MixerPanel, FaderIsUndoable) {
    PanelFixture f;
    bl::ui::MixerPanel panel(&f.controller);
    auto* slider = fader(panel, 0);
    ASSERT_NE(slider, nullptr);

    slider->setValue(75); // gain 0.5 (range 0..150 -> 0.0..1.5)
    releaseSlider(slider);
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().audioTracks[0].gain(),
                     0.5);

    f.controller.undoStack().undo();
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().audioTracks[0].gain(),
                     1.0);
    f.controller.undoStack().redo();
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().audioTracks[0].gain(),
                     0.5);
}

TEST(MixerPanel, MuteAndSoloAreUndoable) {
    PanelFixture f;
    bl::ui::MixerPanel panel(&f.controller);

    muteButton(panel, 0)->click();
    soloButton(panel, 0)->click();
    EXPECT_TRUE(f.controller.timeline().sequence().audioTracks[0].muted());
    EXPECT_TRUE(f.controller.timeline().sequence().audioTracks[0].soloed());

    f.controller.undoStack().undo();
    EXPECT_FALSE(f.controller.timeline().sequence().audioTracks[0].soloed());
    f.controller.undoStack().undo();
    EXPECT_FALSE(f.controller.timeline().sequence().audioTracks[0].muted());
}

TEST(MixerPanel, RebuildsWhenProjectChanges) {
    PanelFixture f;
    bl::ui::MixerPanel panel(&f.controller);
    EXPECT_EQ(panel.stripCount(), 1);

    f.controller.timeline().sequence().addAudioTrack("A2");
    emit f.controller.projectChanged();
    EXPECT_EQ(panel.stripCount(), 2);
    EXPECT_NE(fader(panel, 1), nullptr);
}

TEST(MixerPanel, UndoChangedRefreshesStrips) {
    PanelFixture f;
    bl::ui::MixerPanel panel(&f.controller);

    auto* slider = fader(panel, 0);
    slider->setValue(30); // gain 0.2
    releaseSlider(slider);
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().audioTracks[0].gain(),
                     0.2);

    // Undo bumps the undo stack, which must pull the strip back to the model.
    f.controller.undoStack().undo();
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().audioTracks[0].gain(),
                     1.0);
    EXPECT_EQ(fader(panel, 0)->value(), 150);
}

TEST(MasterFaderPanel, GainAndPanAreUndoable) {
    PanelFixture f;
    bl::ui::MasterFaderPanel panel(&f.controller);
    EXPECT_DOUBLE_EQ(panel.masterGain(), 1.0);

    auto* gainSlider = panel.findChild<QSlider*>(QStringLiteral("masterFader"));
    auto* panSlider = panel.findChild<QSlider*>(QStringLiteral("masterPanSlider"));
    ASSERT_NE(gainSlider, nullptr);
    ASSERT_NE(panSlider, nullptr);

    gainSlider->setValue(105); // master gain 0.7
    releaseSlider(gainSlider);
    panSlider->setValue(-50); // -0.5
    releaseSlider(panSlider);
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().settings.masterGain,
                     0.7);
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().settings.masterPan,
                     -0.5);

    f.controller.undoStack().undo();
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().settings.masterPan,
                     0.0);
    f.controller.undoStack().undo();
    EXPECT_DOUBLE_EQ(f.controller.timeline().sequence().settings.masterGain,
                     1.0);
}

TEST(MasterFaderPanel, RebuildsFromModelOnProjectChange) {
    PanelFixture f;
    bl::ui::MasterFaderPanel panel(&f.controller);
    auto* gainSlider = panel.findChild<QSlider*>(QStringLiteral("masterFader"));
    ASSERT_NE(gainSlider, nullptr);

    f.controller.timeline().sequence().settings.masterGain = 0.4;
    emit f.controller.projectChanged();
    EXPECT_EQ(gainSlider->value(), 60);
    EXPECT_DOUBLE_EQ(panel.masterGain(), 0.4);
}