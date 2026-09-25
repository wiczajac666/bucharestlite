#include <bl_core/autosave_ring.hpp>
#include <bl_core/undo_stack.hpp>

#include <app/main_window.hpp>
#include <app/theme_manager.hpp>
#include <panels/inspector_panel.hpp>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QLabel>
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
        QStringLiteral("Timeline")};
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