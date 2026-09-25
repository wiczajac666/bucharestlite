#include "shortcuts/shortcuts_dialog.hpp"
#include "shortcuts/action_registry.hpp"

#include <QAction>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

QSettings makeSettings(const QDir& dir) {
    return QSettings(dir.filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
}

} // namespace

namespace blui = bl::ui;

namespace {
QKeySequenceEdit* findEditor(blui::ShortcutsDialog& dialog) {
    return dialog.findChild<QKeySequenceEdit*>();
}
} // namespace

TEST(ShortcutsDialog, listsEveryRegisteredAction) {
    blui::ActionRegistry registry;
    QAction a(nullptr);
    QAction b(nullptr);
    registry.registerAction(&a, QStringLiteral("one"), QStringLiteral("One"),
                            QKeySequence(Qt::CTRL | Qt::Key_1));
    registry.registerAction(&b, QStringLiteral("two"), QStringLiteral("Two"),
                            QKeySequence(Qt::CTRL | Qt::Key_2));

    QTemporaryDir dir;
    QSettings settings = makeSettings(QDir(dir.path()));
    blui::ShortcutsDialog dialog(registry, settings, nullptr);

    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 2);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("One"));
    EXPECT_EQ(table->item(1, 0)->text(), QStringLiteral("Two"));
}

TEST(ShortcutsDialog, captureAppliesAndPersistsOverride) {
    blui::ActionRegistry registry;
    QAction action(nullptr);
    registry.registerAction(&action, QStringLiteral("file.open"),
                            QStringLiteral("Open..."), QKeySequence::Open);

    QTemporaryDir dir;
    QSettings settings = makeSettings(QDir(dir.path()));
    blui::ShortcutsDialog dialog(registry, settings, nullptr);

    dialog.beginCapture(QStringLiteral("file.open"));
    QKeySequenceEdit* editor = findEditor(dialog);
    ASSERT_NE(editor, nullptr);

    editor->setKeySequence(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    dialog.commitCapture();

    EXPECT_EQ(action.shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    EXPECT_FALSE(settings.value(QStringLiteral("shortcuts/file.open")).toString().isEmpty());
    EXPECT_EQ(findEditor(dialog), nullptr); // editor closed after commit

    blui::ActionRegistry reloaded;
    QAction restored(nullptr);
    reloaded.registerAction(&restored, QStringLiteral("file.open"),
                            QStringLiteral("Open..."), QKeySequence::Open);
    EXPECT_GE(reloaded.loadOverrides(settings, QStringLiteral("shortcuts")), 1);
    EXPECT_EQ(restored.shortcut(),
              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
}