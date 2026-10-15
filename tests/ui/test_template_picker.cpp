#include <bl_timeline/project_template.hpp>

#include <dialogs/template_picker_dialog.hpp>

#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QPushButton>

#include <gtest/gtest.h>

#include <string>

namespace {

using bl::ProjectTemplate;
using bl::ui::TemplatePickerDialog;

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

} // namespace

TEST(TemplatePickerDialog, ListsBuiltinTemplates) {
    ensureApp();
    TemplatePickerDialog dialog;

    auto* list = dialog.findChild<QListWidget*>(
        QStringLiteral("templatePickerList"));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(static_cast<size_t>(list->count()),
              bl::builtinTemplates().size());

    for (const ProjectTemplate& tmpl : bl::builtinTemplates()) {
        bool found = false;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole).toString().toStdString() ==
                tmpl.id) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "missing built-in: " << tmpl.id;
        // The row shows the template's name and its description.
        EXPECT_FALSE(
            list->findItems(QString::fromStdString(tmpl.name), Qt::MatchContains)
                .isEmpty())
            << tmpl.id;
        EXPECT_FALSE(list
                         ->findItems(QString::fromStdString(tmpl.description),
                                     Qt::MatchContains)
                         .isEmpty())
            << tmpl.id;
    }
}

TEST(TemplatePickerDialog, SelectedIdTracksSelection) {
    ensureApp();
    TemplatePickerDialog dialog;
    ASSERT_GE(dialog.templates().size(), 2u);

    // Defaults to the first listed template.
    EXPECT_EQ(dialog.selectedTemplateId(), dialog.templates()[0].id);

    dialog.selectTemplate("audio-podcast");
    EXPECT_EQ(dialog.selectedTemplateId(), "audio-podcast");

    // An unknown id leaves the selection untouched.
    dialog.selectTemplate("does-not-exist");
    EXPECT_EQ(dialog.selectedTemplateId(), "audio-podcast");
}

TEST(TemplatePickerDialog, OkButtonAcceptsSelectedId) {
    ensureApp();
    TemplatePickerDialog dialog;
    dialog.selectTemplate("4k-uhd");

    auto* ok = dialog.findChild<QPushButton*>(
        QStringLiteral("templatePickerOk"));
    ASSERT_NE(ok, nullptr);
    ok->click();

    EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(dialog.selectedTemplateId(), "4k-uhd");
}
