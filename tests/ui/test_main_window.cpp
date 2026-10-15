#include <bl_core/autosave_ring.hpp>
#include <bl_core/undo_stack.hpp>
#include <bl_timeline/title.hpp>

#include <app/main_window.hpp>
#include <app/theme_manager.hpp>
#include <panels/inspector_panel.hpp>
#include <panels/preview_panel.hpp>
#include <panels/timeline_panel.hpp>
#include <widgets/fullscreen_preview_window.hpp>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

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

struct TempSettings {
    explicit TempSettings(const QTemporaryDir& dir)
        : settings(QDir(dir.path()).filePath(QStringLiteral("state.ini")),
                   QSettings::IniFormat) {}
    QSettings settings;
};

const std::vector<QString>& dockTitles() {
    static const std::vector<QString> titles = {
        QStringLiteral("Media Bin"), QStringLiteral("Preview"),
        QStringLiteral("Inspector"), QStringLiteral("Mixer"),
        QStringLiteral("Timeline"), QStringLiteral("Scopes")};
    return titles;
}

QAction* findAction(const QWidget& window, const QString& text) {
    const auto actions = window.findChildren<QAction*>();
    for (QAction* action : actions) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

QString autosaveRoot(const QTemporaryDir& dir) {
    return QDir(dir.path()).filePath(QStringLiteral("root"));
}

void seedAutosave(const QString& root, const std::string& project,
                  const nlohmann::json& document) {
    bl::AutosaveRing ring(project, 10, root.toStdString());
    const auto result = ring.rotate(document);
    ASSERT_TRUE(result.ok()) << result.message();
}

} // namespace

TEST(MainWindow, createsAllDockingPanels) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);

    EXPECT_EQ(window.dockCount(), static_cast<int>(dockTitles().size()));
    for (const QString& title : dockTitles()) {
        EXPECT_NE(window.dock(title), nullptr) << title.toStdString();
    }

    // Inspector and Mixer are both real panels now. The Inspector is a
    // stacked editor whose (hidden) "select a clip" page also carries the
    // placeholder label, so assert directly on the Inspector dock's widget
    // and check the Mixer dock hosts a MixerPanel instead of a label count.
    auto* inspectorDock = window.dock(QStringLiteral("Inspector"));
    ASSERT_NE(inspectorDock, nullptr);
    auto* inspectorPanel =
        qobject_cast<bl::ui::InspectorPanel*>(inspectorDock->widget());
    ASSERT_NE(inspectorPanel, nullptr);
    EXPECT_TRUE(inspectorPanel->isPlaceholderShown()); // no selection yet

    auto* mixerDock = window.dock(QStringLiteral("Mixer"));
    ASSERT_NE(mixerDock, nullptr);
    EXPECT_NE(mixerDock->widget()->findChild<bl::ui::MixerPanel*>(),
              nullptr);

    // The Scopes dock hosts a ScopesPanel fed by the preview frame tap.
    auto* scopesDock = window.dock(QStringLiteral("Scopes"));
    ASSERT_NE(scopesDock, nullptr);
    EXPECT_NE(qobject_cast<bl::ui::ScopesPanel*>(scopesDock->widget()),
              nullptr);
}

TEST(MainWindow, defaultLayoutIsPreviewRowAboveTimeline) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);
    window.resize(1600, 900);
    window.show();
    QApplication::processEvents();

    const auto area = [&window](const QString& title) {
        return window.dockWidgetArea(window.dock(title));
    };
    // Top row: Media Bin | Preview | Scopes.
    EXPECT_EQ(area(QStringLiteral("Media Bin")), Qt::TopDockWidgetArea);
    EXPECT_EQ(area(QStringLiteral("Preview")), Qt::TopDockWidgetArea);
    EXPECT_EQ(area(QStringLiteral("Scopes")), Qt::TopDockWidgetArea);
    // Bottom row: the timeline spans the full width.
    EXPECT_EQ(area(QStringLiteral("Timeline")), Qt::BottomDockWidgetArea);

    // Inspector and Mixer still exist (View menu, Ctrl+3/Ctrl+4) but are off
    // the default workspace.
    EXPECT_FALSE(window.dock(QStringLiteral("Inspector"))->isVisible());
    EXPECT_FALSE(window.dock(QStringLiteral("Mixer"))->isVisible());
    EXPECT_TRUE(window.dock(QStringLiteral("Timeline"))->isVisible());

    // The three top panels sit side by side, left to right, and the timeline
    // reaches the full window width.
    auto* mediaBin = window.dock(QStringLiteral("Media Bin"));
    auto* preview = window.dock(QStringLiteral("Preview"));
    auto* scopes = window.dock(QStringLiteral("Scopes"));
    auto* timeline = window.dock(QStringLiteral("Timeline"));
    EXPECT_LT(mediaBin->geometry().right(), preview->geometry().left());
    EXPECT_LT(preview->geometry().right(), scopes->geometry().left());
    EXPECT_EQ(timeline->geometry().x(), 0);
    EXPECT_EQ(timeline->geometry().width(), window.width());
}

TEST(MainWindow, titleFollowsProjectAndDirtyState) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);

    EXPECT_TRUE(window.windowTitle().contains(QStringLiteral("Untitled")));
    EXPECT_TRUE(window.windowTitle().contains(QStringLiteral("Bucharest Lite")));

    window.controller()->newProject(QStringLiteral("Smoke"));
    EXPECT_TRUE(window.windowTitle().startsWith(QStringLiteral("Smoke")));
    EXPECT_FALSE(window.windowTitle().contains(QStringLiteral(" *")));

    window.controller()->renameProject(QStringLiteral("Smoke v2"));
    EXPECT_TRUE(window.windowTitle().contains(QStringLiteral(" *")));
}

TEST(MainWindow, undoRedoActionsTrackStack) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);
    window.controller()->newProject(QStringLiteral("Demo"));

    window.controller()->renameProject(QStringLiteral("B"));
    QAction* undo = findAction(window, QStringLiteral("Undo"));
    ASSERT_NE(undo, nullptr);
    EXPECT_TRUE(undo->isEnabled());

    undo->trigger();
    EXPECT_EQ(window.controller()->projectName(), QStringLiteral("Demo"));

    QAction* redo = findAction(window, QStringLiteral("Redo"));
    ASSERT_NE(redo, nullptr);
    redo->trigger();
    EXPECT_EQ(window.controller()->projectName(), QStringLiteral("B"));
}

TEST(MainWindow, themeTogglePersistsAcrossWindows) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);
    window.controller()->newProject(QStringLiteral("Persist"));

    window.toggleTheme();
    EXPECT_EQ(window.theme(), bl::ui::Theme::Light);
    window.persistLayout();

    bl::ui::MainWindow restored(nullptr, &storage.settings);
    restored.setPromptOnCloseEnabled(false);
    EXPECT_EQ(restored.theme(), bl::ui::Theme::Light);
    EXPECT_EQ(restored.dockCount(), static_cast<int>(dockTitles().size()));
    EXPECT_TRUE(restored.controller()->projectName().isEmpty());

    restored.toggleTheme();
    restored.persistLayout();
}

TEST(MainWindow, layoutPersistsDockVisibility) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);

    {
        bl::ui::MainWindow window(nullptr, &storage.settings);
        window.setPromptOnCloseEnabled(false);
        window.dock(QStringLiteral("Mixer"))->setVisible(false);
        window.persistLayout();
    }

    {
        bl::ui::MainWindow restored(nullptr, &storage.settings);
        restored.setPromptOnCloseEnabled(false);
        restored.show();
        EXPECT_FALSE(restored.dock(QStringLiteral("Mixer"))->isVisible());
        EXPECT_TRUE(restored.dock(QStringLiteral("Timeline"))->isVisible());
    }
}

TEST(MainWindow, laneOrderPersistsAcrossWindows) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);

    {
        bl::ui::MainWindow window(nullptr, &storage.settings);
        window.setPromptOnCloseEnabled(false);
        auto* panel = window.findChild<bl::ui::TimelinePanel*>();
        ASSERT_NE(panel, nullptr);
        EXPECT_EQ(panel->laneOrder(),
                  bl::ui::timeline_geometry::LaneOrder::Split);

        panel->setLaneOrder(bl::ui::timeline_geometry::LaneOrder::Mixed);
        // The choice is written as soon as it changes (survives crashes).
        EXPECT_EQ(
            storage.settings.value(QStringLiteral("timeline/laneOrder")).toInt(),
            static_cast<int>(bl::ui::timeline_geometry::LaneOrder::Mixed));
    }

    {
        bl::ui::MainWindow restored(nullptr, &storage.settings);
        restored.setPromptOnCloseEnabled(false);
        auto* panel = restored.findChild<bl::ui::TimelinePanel*>();
        ASSERT_NE(panel, nullptr);
        EXPECT_EQ(panel->laneOrder(),
                  bl::ui::timeline_geometry::LaneOrder::Mixed);
        // The restored order is reflected by the toolbar combo.
        auto* combo = panel->findChild<QComboBox*>(QStringLiteral("laneOrderCombo"));
        ASSERT_NE(combo, nullptr);
        EXPECT_EQ(combo->currentData().toInt(),
                  static_cast<int>(bl::ui::timeline_geometry::LaneOrder::Mixed));

        // The View menu's Fit action drives the timeline zoom.
        QAction* fit = findAction(restored, QStringLiteral("Fit Timeline to View"));
        ASSERT_NE(fit, nullptr);
        fit->trigger();
        EXPECT_GE(panel->zoom(), 0.25);
        EXPECT_LE(panel->zoom(), 64.0);
    }
}

TEST(MainWindow, editStatePersistsAcrossWindows) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);

    {
        bl::ui::MainWindow window(nullptr, &storage.settings);
        window.setPromptOnCloseEnabled(false);
        auto* panel = window.findChild<bl::ui::TimelinePanel*>();
        ASSERT_NE(panel, nullptr);
        panel->setEditMode(bl::ui::TimelineState::EditMode::Slip);
        panel->setInsertMode(true);
        panel->setRippleEnabled(true);
        panel->setSnapEnabled(false);
        // Written as soon as it changes (survives crashes).
        EXPECT_EQ(storage.settings.value(QStringLiteral("timeline/editMode")).toInt(),
                  static_cast<int>(bl::ui::TimelineState::EditMode::Slip));
        EXPECT_TRUE(
            storage.settings.value(QStringLiteral("timeline/insertMode")).toBool());
    }

    {
        bl::ui::MainWindow restored(nullptr, &storage.settings);
        restored.setPromptOnCloseEnabled(false);
        auto* panel = restored.findChild<bl::ui::TimelinePanel*>();
        ASSERT_NE(panel, nullptr);
        EXPECT_EQ(panel->editMode(), bl::ui::TimelineState::EditMode::Slip);
        EXPECT_TRUE(panel->insertMode());
        EXPECT_TRUE(panel->rippleEnabled());
        EXPECT_FALSE(panel->snapEnabled());
        auto* combo = panel->findChild<QComboBox*>(QStringLiteral("editModeCombo"));
        ASSERT_NE(combo, nullptr);
        EXPECT_EQ(combo->currentData().toInt(),
                  static_cast<int>(bl::ui::TimelineState::EditMode::Slip));
    }
}

TEST(MainWindow, zoneActionsDriveTimelineZone) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);
    auto* panel = window.findChild<bl::ui::TimelinePanel*>();
    ASSERT_NE(panel, nullptr);
    EXPECT_FALSE(panel->hasZone());

    QAction* zoneIn = findAction(window, QStringLiteral("Set Zone In"));
    QAction* zoneOut = findAction(window, QStringLiteral("Set Zone Out"));
    QAction* clear = findAction(window, QStringLiteral("Clear Zone"));
    QAction* lift = findAction(window, QStringLiteral("Lift Zone"));
    QAction* extract = findAction(window, QStringLiteral("Extract Zone"));
    ASSERT_NE(zoneIn, nullptr);
    ASSERT_NE(zoneOut, nullptr);
    ASSERT_NE(clear, nullptr);
    ASSERT_NE(lift, nullptr);
    ASSERT_NE(extract, nullptr);

    const bl::Rational rate{1'000'000, 1};
    panel->setPlayhead(bl::Time::fromTicks(1'000'000, rate));
    zoneIn->trigger();
    EXPECT_TRUE(panel->hasZone());

    panel->setPlayhead(bl::Time::fromTicks(2'000'000, rate));
    zoneOut->trigger();
    EXPECT_EQ(panel->zoneIn().ticks, 1'000'000);
    EXPECT_EQ(panel->zoneOut().ticks, 2'000'000);

    clear->trigger();
    EXPECT_FALSE(panel->hasZone());
}

TEST(MainWindow, timelineWaveformResolverIsWired) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);
    auto* panel = window.findChild<bl::ui::TimelinePanel*>();
    ASSERT_NE(panel, nullptr);
    // Background audio analysis feeds each clip's waveform through this
    // resolver; a MainWindow that forgot to wire it would draw none.
    EXPECT_TRUE(static_cast<bool>(panel->waveformResolver()));
}

TEST(MainWindow, closeWithCleanProjectPersistsLayout) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);
    window.controller()->newProject(QStringLiteral("Clean"));
    window.persistLayout();

    EXPECT_TRUE(storage.settings.contains(QStringLiteral("window/state")));
    EXPECT_TRUE(storage.settings.contains(QStringLiteral("window/geometry")));
}

TEST(MainWindow, recoveryPromptRestoresNewerAutosave) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    const QString root = autosaveRoot(dir);
    const QString main = QDir(dir.path()).filePath(QStringLiteral("main.blproj"));

    {
        bl::ui::ProjectController seed;
        seed.newProject(QStringLiteral("Recoverable"));
        ASSERT_TRUE(seed.addToMediaBin(QStringLiteral("/media/a.mp4")));
        ASSERT_TRUE(seed.saveAs(main).ok());
        // The crash happens after this rename, leaving only the autosave with it.
        seed.renameProject(QStringLiteral("Recovered"));
        seedAutosave(root, "Recoverable", seed.serializeDocument());
    }

    {
        bl::ui::MainWindow window(nullptr, &storage.settings);
        window.setPromptOnCloseEnabled(false);
        window.setAutosaveRoot(root);
        window.setRecoveryDecider([] { return true; });

        ASSERT_TRUE(window.controller()->open(main).ok());
        EXPECT_FALSE(window.controller()->dirty());
        EXPECT_TRUE(window.maybePromptRecovery());
        EXPECT_EQ(window.controller()->projectName(),
                  QStringLiteral("Recovered"));
        EXPECT_TRUE(window.controller()->dirty());
        EXPECT_EQ(window.controller()->filePath(), main);
    }

    {
        bl::ui::MainWindow window(nullptr, &storage.settings);
        window.setPromptOnCloseEnabled(false);
        window.setAutosaveRoot(root);
        window.setRecoveryDecider([] { return false; });

        ASSERT_TRUE(window.controller()->open(main).ok());
        EXPECT_FALSE(window.maybePromptRecovery());
        EXPECT_EQ(window.controller()->projectName(),
                  QStringLiteral("Recoverable"));
        EXPECT_FALSE(window.controller()->dirty());
    }
}

TEST(MainWindow, autosaveFlushesOnWindowDeactivate) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);
    window.setAutosaveRoot(autosaveRoot(dir));

    window.controller()->newProject(QStringLiteral("Focus"));
    window.controller()->addToMediaBin(QStringLiteral("/media/a.mp4"));

    std::unique_ptr<QEvent> deactivate(new QEvent(QEvent::WindowDeactivate));
    QApplication::sendEvent(&window, deactivate.get());

    const auto slot = fs::path(autosaveRoot(dir).toStdString()) / "autosave" /
                      "Focus" / "autosave_01.blproj";
    EXPECT_TRUE(fs::exists(slot));
}

TEST(MainWindow, registryBindsNewAndExistingShortcuts) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);

    QAction* batch =
        findAction(window, QStringLiteral("Batch Export..."));
    ASSERT_NE(batch, nullptr);
    EXPECT_EQ(batch->shortcut(),
              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));

    QAction* play =
        findAction(window, QStringLiteral("Play/Pause"));
    ASSERT_NE(play, nullptr);
    EXPECT_EQ(play->shortcut(), QKeySequence(Qt::Key_Space));

    const std::vector<QKeySequence> toggleShortcuts = {
        QKeySequence(Qt::CTRL | Qt::Key_1), QKeySequence(Qt::CTRL | Qt::Key_2),
        QKeySequence(Qt::CTRL | Qt::Key_3), QKeySequence(Qt::CTRL | Qt::Key_4),
        QKeySequence(Qt::CTRL | Qt::Key_5), QKeySequence(Qt::CTRL | Qt::Key_6)};
    for (size_t i = 0; i < dockTitles().size(); ++i) {
        EXPECT_EQ(window.dock(dockTitles()[i])->toggleViewAction()->shortcut(),
                  toggleShortcuts[i]);
    }

    QAction* help = findAction(window, QStringLiteral("Keyboard Shortcuts..."));
    EXPECT_NE(help, nullptr);
}

TEST(MainWindow, persistedShortcutOverrideIsApplied) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    storage.settings.setValue(QStringLiteral("shortcuts/file.export"),
                              QStringLiteral("Ctrl+Alt+E"));

    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);

    QAction* exportAction = window.findChild<QAction*>(
        QStringLiteral("actionExport"));
    ASSERT_NE(exportAction, nullptr);
    EXPECT_EQ(exportAction->shortcut(),
              QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_E));
}

TEST(MainWindow, AddTitleActionIsRegisteredInFileMenu) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);

    QAction* addTitle =
        window.findChild<QAction*>(QStringLiteral("actionAddTitle"));
    ASSERT_NE(addTitle, nullptr);
    EXPECT_EQ(findAction(window, QStringLiteral("Add Title...")), addTitle);

    bool inFileMenu = false;
    for (QAction* menuAction : window.menuBar()->actions()) {
        QMenu* menu = menuAction->menu();
        if (!menu) continue;
        for (QAction* item : menu->actions()) {
            if (item == addTitle) inFileMenu = true;
        }
    }
    EXPECT_TRUE(inFileMenu);
}

TEST(MainWindow, FullScreenPreviewActionIsRegistered) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);

    QAction* fullScreen = findAction(window, QStringLiteral("Full Screen Preview"));
    ASSERT_NE(fullScreen, nullptr);
    EXPECT_TRUE(fullScreen->isCheckable());
    EXPECT_FALSE(fullScreen->isChecked());
    EXPECT_EQ(fullScreen->shortcut(),
              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
}

TEST(MainWindow, FullScreenPreviewTogglesDetachAndRestore) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);

    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("PreviewDock"));
    ASSERT_NE(dock, nullptr);
    auto* panel = window.findChild<bl::ui::PreviewPanel*>();
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(dock->widget(), panel);

    QAction* fullScreen = findAction(window, QStringLiteral("Full Screen Preview"));
    ASSERT_NE(fullScreen, nullptr);

    // Detach: the dock is emptied, the window hosts the very same panel (no
    // second preview pipeline) and the action reflects the new state.
    fullScreen->trigger();
    EXPECT_EQ(dock->widget(), nullptr);
    EXPECT_TRUE(fullScreen->isChecked());
    auto* detachedWindow =
        window.findChild<bl::ui::FullScreenPreviewWindow*>();
    ASSERT_NE(detachedWindow, nullptr);
    EXPECT_EQ(window.findChild<bl::ui::PreviewPanel*>(), panel);
    EXPECT_EQ(detachedWindow->isWindow(), true);

    // Restore: the same panel is back under the dock and the standalone window
    // is gone.
    fullScreen->trigger();
    EXPECT_EQ(dock->widget(), panel);
    EXPECT_EQ(qobject_cast<bl::ui::PreviewPanel*>(dock->widget()), panel);
    EXPECT_FALSE(fullScreen->isChecked());
    // The window is torn down via deleteLater() (the restore may be triggered
    // from inside its own closeEvent), so drain the deferred delete first.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(window.findChild<bl::ui::FullScreenPreviewWindow*>(), nullptr);
}

TEST(MainWindow, ApplyTitleEditReRendersAndRefusesNonTitles) {
    ensureApp();
    QTemporaryDir dir;
    TempSettings storage(dir);
    bl::ui::MainWindow window(nullptr, &storage.settings);
    window.setPromptOnCloseEnabled(false);

    QTemporaryDir mediaDir;
    ASSERT_TRUE(mediaDir.isValid());
    const QString png =
        QDir(mediaDir.path()).filePath(QStringLiteral("title.png"));

    bl::TitleSpec spec;
    spec.text = "One";
    ASSERT_TRUE(window.controller()->addTitleToMediaBin(
        png, QStringLiteral("One"), nlohmann::json(spec)));
    const std::string id = window.controller()->mediaBin().back().id;

    bl::TitleSpec edited;
    edited.text = "Two";
    EXPECT_TRUE(window.applyTitleEdit(id, edited, 320, 180,
                                      QStringLiteral("Two")));
    EXPECT_EQ(window.controller()->titleSpecFor(id).get<bl::TitleSpec>().text,
              "Two");
    EXPECT_TRUE(QFile::exists(png));

    // A plain media item is not a title: refuse (no modal dialog involved).
    ASSERT_TRUE(
        window.controller()->addToMediaBin(QStringLiteral("/media/clip.mp4")));
    const std::string plainId = window.controller()->mediaBin().back().id;
    EXPECT_FALSE(window.applyTitleEdit(plainId, edited, 320, 180,
                                       QStringLiteral("Two")));
}