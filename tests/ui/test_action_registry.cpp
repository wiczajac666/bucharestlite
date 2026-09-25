#include "shortcuts/action_registry.hpp"

#include <QAction>
#include <QKeySequence>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

QSettings makeSettings(const QDir& dir) {
    return QSettings(dir.filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
}

} // namespace

namespace blui = bl::ui;

TEST(ActionRegistry, registerAppliesDefaultSequence) {
    blui::ActionRegistry registry;
    QAction action(nullptr);

    registry.registerAction(&action, QStringLiteral("test.id"),
                            QStringLiteral("Test"), QKeySequence(Qt::CTRL | Qt::Key_T));

    EXPECT_TRUE(registry.contains(QStringLiteral("test.id")));
    EXPECT_EQ(registry.action(QStringLiteral("test.id")), &action);
    EXPECT_EQ(registry.sequence(QStringLiteral("test.id")),
              QKeySequence(Qt::CTRL | Qt::Key_T));
    EXPECT_EQ(action.shortcut(), QKeySequence(Qt::CTRL | Qt::Key_T));
}

TEST(ActionRegistry, setOverrideUpdatesAction) {
    blui::ActionRegistry registry;
    QAction action(nullptr);
    registry.registerAction(&action, QStringLiteral("test.id"),
                            QStringLiteral("Test"), QKeySequence(Qt::CTRL | Qt::Key_T));

    registry.setOverride(QStringLiteral("test.id"),
                         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));

    EXPECT_EQ(action.shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    EXPECT_EQ(registry.sequence(QStringLiteral("test.id")),
              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
}

TEST(ActionRegistry, resetReturnsToDefault) {
    blui::ActionRegistry registry;
    QAction action(nullptr);
    registry.registerAction(&action, QStringLiteral("test.id"),
                            QStringLiteral("Test"), QKeySequence(Qt::CTRL | Qt::Key_T));

    registry.setOverride(QStringLiteral("test.id"),
                         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    registry.reset(QStringLiteral("test.id"));

    EXPECT_EQ(action.shortcut(), QKeySequence(Qt::CTRL | Qt::Key_T));
}

TEST(ActionRegistry, resetAllClearsEveryOverride) {
    blui::ActionRegistry registry;
    QAction first(nullptr);
    QAction second(nullptr);
    registry.registerAction(&first, QStringLiteral("one"), QStringLiteral("One"),
                            QKeySequence(Qt::CTRL | Qt::Key_1));
    registry.registerAction(&second, QStringLiteral("two"), QStringLiteral("Two"),
                            QKeySequence(Qt::CTRL | Qt::Key_2));

    registry.setOverride(QStringLiteral("one"),
                         QKeySequence(Qt::CTRL | Qt::Key_9));
    registry.setOverride(QStringLiteral("two"),
                         QKeySequence(Qt::CTRL | Qt::Key_8));
    registry.resetAll();

    EXPECT_EQ(first.shortcut(), QKeySequence(Qt::CTRL | Qt::Key_1));
    EXPECT_EQ(second.shortcut(), QKeySequence(Qt::CTRL | Qt::Key_2));
}

TEST(ActionRegistry, persistRoundTripsOverrides) {
    QTemporaryDir dir;
    const QDir base(dir.path());

    blui::ActionRegistry first;
    QAction a(nullptr);
    QAction b(nullptr);
    first.registerAction(&a, QStringLiteral("one"), QStringLiteral("One"),
                         QKeySequence(Qt::CTRL | Qt::Key_1));
    first.registerAction(&b, QStringLiteral("two"), QStringLiteral("Two"),
                         QKeySequence(Qt::CTRL | Qt::Key_2));
    first.setOverride(QStringLiteral("one"),
                      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));

    {
        QSettings settings = makeSettings(base);
        first.saveOverrides(settings, QStringLiteral("shortcuts"));
        settings.sync();
    }

    blui::ActionRegistry second;
    QAction restoredA(nullptr);
    QAction restoredB(nullptr);
    second.registerAction(&restoredA, QStringLiteral("one"),
                          QStringLiteral("One"), QKeySequence(Qt::CTRL | Qt::Key_1));
    second.registerAction(&restoredB, QStringLiteral("two"),
                          QStringLiteral("Two"), QKeySequence(Qt::CTRL | Qt::Key_2));

    QSettings settings = makeSettings(base);
    const int applied = second.loadOverrides(settings, QStringLiteral("shortcuts"));

    EXPECT_GE(applied, 1);
    EXPECT_EQ(restoredA.shortcut(),
              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    EXPECT_EQ(restoredB.shortcut(), QKeySequence(Qt::CTRL | Qt::Key_2));
}

TEST(ActionRegistry, unknownIdIsIgnoredSafely) {
    blui::ActionRegistry registry;
    QAction action(nullptr);
    registry.registerAction(&action, QStringLiteral("known"),
                            QStringLiteral("Known"), QKeySequence(Qt::Key_Space));

    registry.setOverride(QStringLiteral("nope"), QKeySequence(Qt::CTRL | Qt::Key_X));

    EXPECT_FALSE(registry.contains(QStringLiteral("nope")));
    EXPECT_EQ(registry.action(QStringLiteral("nope")), nullptr);
    EXPECT_TRUE(registry.sequence(QStringLiteral("nope")).isEmpty());
    EXPECT_EQ(action.shortcut(), QKeySequence(Qt::Key_Space));
}