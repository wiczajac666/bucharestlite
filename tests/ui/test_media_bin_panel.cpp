#include <QAbstractItemView>
#include <QApplication>
#include <QListWidget>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>

#include <app/project_controller.hpp>
#include <panels/media_bin_panel.hpp>

#include <gtest/gtest.h>

#include <memory>
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

struct Fixture {
    bl::ui::ProjectController controller;
    bl::ui::MediaBinPanel panel;

    Fixture() : panel(&controller) {
        ensureApp();
        controller.newProject(QStringLiteral("Bin"));
    }
};

void addMedia(Fixture& f, const QString& path) {
    f.controller.addToMediaBin(path);
    QApplication::processEvents();
}

TEST(MediaBinPanel, EmptyOnNewProject) {
    Fixture f;
    EXPECT_EQ(f.panel.itemCount(), 0);
    EXPECT_EQ(f.panel.visibleItemCount(), 0);
    EXPECT_TRUE(f.panel.selectedId().empty());
}

TEST(MediaBinPanel, AddReflectsItemsAndNames) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    addMedia(f, QStringLiteral("/media/b.mov"));
    QApplication::processEvents();

    EXPECT_EQ(f.panel.itemCount(), 2);
    EXPECT_EQ(f.panel.visibleItemCount(), 2);

    auto* list = f.panel.findChild<QListWidget*>(QStringLiteral("mediaBinList"));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("a.mp4"));
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("b.mov"));

    // Every row carries the authoritative media-bin id.
    const std::string id0 = f.controller.mediaBin()[0].id;
    EXPECT_FALSE(id0.empty());
}

TEST(MediaBinPanel, DuplicateAddIsBlockedSingleRow) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    addMedia(f, QStringLiteral("/media/a.mp4"));
    QApplication::processEvents();
    EXPECT_EQ(f.panel.itemCount(), 1);
}

TEST(MediaBinPanel, RemoveViaSelectionClearsRowAndUndoRestores) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    addMedia(f, QStringLiteral("/media/b.mp4"));

    auto* list = f.panel.findChild<QListWidget*>(QStringLiteral("mediaBinList"));
    ASSERT_NE(list, nullptr);
    list->setCurrentRow(0);
    QApplication::processEvents();

    auto* removeButton =
        f.panel.findChild<QPushButton*>(QStringLiteral("mediaBinRemove"));
    ASSERT_NE(removeButton, nullptr);
    EXPECT_TRUE(removeButton->isEnabled());

    // Removal happens through the controller's undoable mutation.
    const std::string id0 = f.controller.mediaBin()[0].id;
    f.controller.removeFromMediaBin(QString::fromStdString(id0));
    QApplication::processEvents();

    EXPECT_EQ(f.panel.itemCount(), 1);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("b.mp4"));

    f.controller.undoStack().undo();
    QApplication::processEvents();
    EXPECT_EQ(f.panel.itemCount(), 2);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("a.mp4"));
}

TEST(MediaBinPanel, RemoveButtonDisabledWithoutSelection) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    auto* removeButton =
        f.panel.findChild<QPushButton*>(QStringLiteral("mediaBinRemove"));
    ASSERT_NE(removeButton, nullptr);
    EXPECT_FALSE(removeButton->isEnabled());
}

TEST(MediaBinPanel, DragSourceCarriesMediaId) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));

    auto* list = f.panel.findChild<bl::ui::MediaBinList*>(
        QStringLiteral("mediaBinList"));
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(list->dragEnabled());
    EXPECT_EQ(list->dragDropMode(), QAbstractItemView::DragOnly);

    const QModelIndex row = list->model()->index(0, 0);
    std::unique_ptr<QMimeData> mime(list->dragData(row));
    ASSERT_NE(mime, nullptr);
    const QString type = QString::fromLatin1(bl::ui::kMediaBinMime);
    EXPECT_TRUE(mime->hasFormat(type));
    EXPECT_EQ(QString::fromUtf8(mime->data(type)),
              QString::fromStdString(f.controller.mediaBin()[0].id));
}

TEST(MediaBinPanel, DragDataAbsentWithoutMedia) {
    Fixture f;
    auto* list = f.panel.findChild<bl::ui::MediaBinList*>(
        QStringLiteral("mediaBinList"));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->dragData(list->model()->index(0, 0)), nullptr);
}

TEST(MediaBinPanel, FilterNarrowsAndRestores) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/clip_a.mp4"));
    addMedia(f, QStringLiteral("/media/clip_b.mp4"));
    addMedia(f, QStringLiteral("/media/title.mov"));

    auto* filter = f.panel.findChild<QLineEdit*>(QStringLiteral("mediaBinFilter"));
    ASSERT_NE(filter, nullptr);

    filter->setText(QStringLiteral("clip"));
    QApplication::processEvents();
    EXPECT_EQ(f.panel.visibleItemCount(), 2);

    filter->setText(QStringLiteral("CLIP_B"));
    QApplication::processEvents();
    EXPECT_EQ(f.panel.visibleItemCount(), 1);

    filter->setText(QStringLiteral("nope"));
    QApplication::processEvents();
    EXPECT_EQ(f.panel.visibleItemCount(), 0);

    filter->setText(QString());
    QApplication::processEvents();
    EXPECT_EQ(f.panel.visibleItemCount(), 3);
}

TEST(MediaBinPanel, MatchesFilterHelper) {
    using bl::ui::media_bin_detail::matchesFilter;
    EXPECT_TRUE(matchesFilter("vlog_final.mp4", ""));
    EXPECT_TRUE(matchesFilter("vlog_final.mp4", "vlog"));
    EXPECT_TRUE(matchesFilter("vlog_final.mp4", "FINAL"));
    EXPECT_TRUE(matchesFilter("vlog_final.mp4", "final.mp4"));
    EXPECT_FALSE(matchesFilter("vlog_final.mp4", "title"));
}

} // namespace
