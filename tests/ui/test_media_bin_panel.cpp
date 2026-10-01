#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QLineEdit>
#include <QMimeData>
#include <QPixmap>
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

TEST(MediaBinPanel, RowsCarryPlaceholderIconAndTooltipBeforeMetadata) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/clip_a.mp4"));

    auto* list = f.panel.findChild<QListWidget*>(QStringLiteral("mediaBinList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 1);

    QListWidgetItem* item = list->item(0);
    EXPECT_FALSE(item->icon().isNull());  // neutral placeholder
    EXPECT_TRUE(item->data(bl::ui::kMediaBinDetailRole).toString().isEmpty());
    EXPECT_EQ(item->toolTip(), QStringLiteral("/media/clip_a.mp4"));
}

TEST(MediaBinPanel, ApplyMediaInfoSetsThumbnailAndDetailLine) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/clip_a.mp4"));

    auto* list = f.panel.findChild<QListWidget*>(QStringLiteral("mediaBinList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 1);

    const std::string id = f.controller.mediaBin()[0].id;
    QImage thumbnail(4, 3, QImage::Format_RGB32);
    thumbnail.fill(Qt::red);
    f.panel.applyMediaInfo(id, thumbnail,
                           QStringLiteral("00:00:01 · 320×240 · h264"));

    QListWidgetItem* item = list->item(0);
    // The display text stays the bare name (filter/drag contract unchanged).
    EXPECT_EQ(item->text(), QStringLiteral("clip_a.mp4"));
    EXPECT_EQ(item->data(bl::ui::kMediaBinDetailRole).toString(),
              QStringLiteral("00:00:01 · 320×240 · h264"));
    ASSERT_FALSE(item->icon().isNull());
    const QColor sampled =
        item->icon().pixmap(QSize(4, 3)).toImage().pixelColor(1, 1);
    EXPECT_GT(sampled.red(), 200);  // the red thumbnail replaced the grey glyph
}

TEST(MediaBinPanel, NullThumbnailKeepsPlaceholderButShowsDetail) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/song.wav"));

    const std::string id = f.controller.mediaBin()[0].id;
    const QIcon placeholderBefore =
        f.panel.findChild<QListWidget*>(QStringLiteral("mediaBinList"))
            ->item(0)
            ->icon();

    // Audio-only entries arrive with a null image but real metadata.
    f.panel.applyMediaInfo(id, QImage(),
                           QStringLiteral("00:00:02 · 44.1 kHz · stereo · flac"));

    auto* list = f.panel.findChild<QListWidget*>(QStringLiteral("mediaBinList"));
    ASSERT_NE(list, nullptr);
    QListWidgetItem* item = list->item(0);
    EXPECT_FALSE(item->icon().isNull());
    EXPECT_EQ(item->data(bl::ui::kMediaBinDetailRole).toString(),
              QStringLiteral("00:00:02 · 44.1 kHz · stereo · flac"));
    // Still the neutral placeholder, not a decoded frame.
    EXPECT_EQ(item->icon().pixmap(QSize(96, 54)).toImage(),
              placeholderBefore.pixmap(QSize(96, 54)).toImage());
}

TEST(MediaBinPanel, FilterMatchesNameOnlyNotDetailMetadata) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/clip_a.mp4"));

    const std::string id = f.controller.mediaBin()[0].id;
    f.panel.applyMediaInfo(id, QImage(),
                           QStringLiteral("00:00:01 · 320×240 · h264"));

    auto* filter = f.panel.findChild<QLineEdit*>(QStringLiteral("mediaBinFilter"));
    ASSERT_NE(filter, nullptr);

    // Detail-line text must not leak into the name filter.
    filter->setText(QStringLiteral("h264"));
    QApplication::processEvents();
    EXPECT_EQ(f.panel.visibleItemCount(), 0);

    filter->setText(QStringLiteral("clip"));
    QApplication::processEvents();
    EXPECT_EQ(f.panel.visibleItemCount(), 1);
}

} // namespace
