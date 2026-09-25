#include <app/autosave_manager.hpp>
#include <app/project_controller.hpp>

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

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

using bl::ui::AutosaveManager;
using bl::ui::ProjectController;

fs::path ringPath(const QTemporaryDir& dir, const std::string& project,
                  const std::string& slot = "autosave_01.blproj") {
    return fs::path(dir.path().toStdString()) / "root" / "autosave" / project /
           slot;
}

} // namespace

TEST(AutosaveManager, SnapshotWritesSlotWhenDirty) {
    ensureApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    ProjectController controller;
    AutosaveManager manager(&controller);
    manager.setAutosaveRoot(QDir(dir.path()).filePath(QStringLiteral("root")));

    controller.newProject(QStringLiteral("P1"));
    controller.addToMediaBin(QStringLiteral("/media/a.mp4"));
    EXPECT_TRUE(controller.dirty());
    EXPECT_TRUE(fs::exists(ringPath(dir, "P1")));

    // Focus-loss flush path also writes.
    manager.maybeAutosave();
    EXPECT_TRUE(fs::exists(ringPath(dir, "P1")));
}

TEST(AutosaveManager, CleanProjectSkipsSnapshot) {
    ensureApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    ProjectController controller;
    AutosaveManager manager(&controller);
    manager.setAutosaveRoot(QDir(dir.path()).filePath(QStringLiteral("root")));

    controller.newProject(QStringLiteral("P2"));
    EXPECT_FALSE(controller.dirty());
    manager.maybeAutosave();
    EXPECT_FALSE(fs::exists(ringPath(dir, "P2")));
}

TEST(AutosaveManager, ManualSaveClearsRing) {
    ensureApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root =
        QDir(dir.path()).filePath(QStringLiteral("root"));
    const QString mainPath =
        QDir(dir.path()).filePath(QStringLiteral("main.blproj"));

    ProjectController controller;
    AutosaveManager manager(&controller);
    manager.setAutosaveRoot(root);

    controller.newProject(QStringLiteral("P3"));
    controller.addToMediaBin(QStringLiteral("/media/a.mp4"));
    EXPECT_TRUE(fs::exists(ringPath(dir, "P3")));

    ASSERT_TRUE(controller.saveAs(mainPath).ok());
    EXPECT_FALSE(controller.dirty());
    EXPECT_FALSE(manager.hasAutosaves());
    EXPECT_FALSE(fs::exists(ringPath(dir, "P3")));
}

TEST(AutosaveManager, RenameRedirectsRingKey) {
    ensureApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    ProjectController controller;
    AutosaveManager manager(&controller);
    manager.setAutosaveRoot(QDir(dir.path()).filePath(QStringLiteral("root")));

    controller.newProject(QStringLiteral("P4"));
    controller.addToMediaBin(QStringLiteral("/media/a.mp4"));
    controller.renameProject(QStringLiteral("P4b"));
    manager.maybeAutosave();

    EXPECT_TRUE(fs::exists(ringPath(dir, "P4b")));
}

TEST(AutosaveManager, IntervalSetterTakesEffect) {
    ensureApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    ProjectController controller;
    AutosaveManager manager(&controller);
    manager.setIntervalSeconds(3);
    EXPECT_EQ(manager.intervalSeconds(), 3);
}

TEST(AutosaveManager, NewestAutosaveDetectedWhileMainMissing) {
    ensureApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString mainPath =
        QDir(dir.path()).filePath(QStringLiteral("main.blproj"));

    ProjectController controller;
    AutosaveManager manager(&controller);
    manager.setAutosaveRoot(QDir(dir.path()).filePath(QStringLiteral("root")));

    controller.newProject(QStringLiteral("P5"));
    EXPECT_FALSE(manager.hasAutosaves());
    EXPECT_TRUE(manager.autosaveNewerThanMain().isEmpty());

    controller.addToMediaBin(QStringLiteral("/media/a.mp4"));
    EXPECT_TRUE(manager.hasAutosaves());
    EXPECT_FALSE(manager.autosaveNewerThanMain().isEmpty());

    // Once saved, the ring is cleared and nothing is offered for recovery.
    ASSERT_TRUE(controller.saveAs(mainPath).ok());
    EXPECT_FALSE(manager.hasAutosaves());
    EXPECT_TRUE(manager.autosaveNewerThanMain().isEmpty());
}

TEST(AutosaveManager, AutosaveNewerThanExistingMainIsOffered) {
    ensureApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString mainPath =
        QDir(dir.path()).filePath(QStringLiteral("main.blproj"));

    ProjectController controller;
    AutosaveManager manager(&controller);
    manager.setAutosaveRoot(QDir(dir.path()).filePath(QStringLiteral("root")));

    controller.newProject(QStringLiteral("P7"));
    controller.addToMediaBin(QStringLiteral("/media/a.mp4"));
    ASSERT_TRUE(controller.saveAs(mainPath).ok());
    EXPECT_TRUE(manager.autosaveNewerThanMain().isEmpty());

    // A subsequent edit leaves a snapshot on disk that is newer than the
    // manual save - the recovery offer must surface it.
    controller.addToMediaBin(QStringLiteral("/media/b.mp4"));
    EXPECT_FALSE(manager.autosaveNewerThanMain().isEmpty());
}