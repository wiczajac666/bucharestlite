#include <bl_core/undo_stack.hpp>

#include <app/main_window.hpp>
#include <app/theme_manager.hpp>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <string>
#include <vector>

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

    const auto labels = window.findChildren<QLabel*>(QStringLiteral("placeholder"));
    EXPECT_EQ(labels.size(), static_cast<int>(dockTitles().size()));
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