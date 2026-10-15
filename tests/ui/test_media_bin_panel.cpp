#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QUrl>

#include <app/project_controller.hpp>
#include <panels/media_bin_panel.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using bl::ui::MediaBinList;

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
    // DragDrop (not DragOnly): the list is both a drag source and a drop
    // target for media files coming from the OS file manager.
    EXPECT_EQ(list->dragDropMode(), QAbstractItemView::DragDrop);
    EXPECT_TRUE(list->acceptDrops());

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

// ---------------------------------------------------------------------------
// External media import: files dropped onto the bin from the OS file manager.
// ---------------------------------------------------------------------------

MediaBinList* listOf(bl::ui::MediaBinPanel& panel) {
    return panel.findChild<bl::ui::MediaBinList*>(
        QStringLiteral("mediaBinList"));
}

// Finds a top-level action on a menu by its label (menus now carry more than
// the original Add/Remove pair, so tests must not index by position).
QAction* actionWithText(QMenu* menu, const QString& text) {
    for (QAction* action : menu->actions()) {
        if (action->text() == text) return action;
    }
    return nullptr;
}

void fillUrlMime(QMimeData& mime, const QStringList& paths) {
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString& path : paths) urls.push_back(QUrl::fromLocalFile(path));
    mime.setUrls(urls);
}

// Creates real files on disk so the payload is not filtered out as a directory,
// and returns their absolute paths.
QStringList makeTempFiles(const QString& prefix, int count) {
    static QTemporaryDir dir;
    QStringList paths;
    for (int i = 0; i < count; ++i) {
        const QString path =
            dir.filePath(QStringLiteral("%1_%2.mp4").arg(prefix).arg(i));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write("not really media");
        paths.push_back(path);
    }
    return paths;
}

TEST(MediaBinPanel, LocalFilePathsReadsUriList) {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("clip.mp4"));
    QFile f(file);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    QMimeData mime;
    fillUrlMime(mime, {file});
    const QStringList paths = bl::ui::media_bin_detail::localFilePaths(&mime);
    ASSERT_EQ(paths.size(), 1);
    EXPECT_EQ(paths.front(), file);
}

TEST(MediaBinPanel, LocalFilePathsSkipsRemoteUrlsAndDirectories) {
    QTemporaryDir dir;
    QMimeData mime;
    mime.setUrls({QUrl(QStringLiteral("https://example.com/clip.mp4")),
                  QUrl::fromLocalFile(dir.path())});  // a directory
    EXPECT_TRUE(bl::ui::media_bin_detail::localFilePaths(&mime).isEmpty());

    QMimeData plain;
    plain.setText(QStringLiteral("hello"));
    EXPECT_TRUE(bl::ui::media_bin_detail::localFilePaths(&plain).isEmpty());
    EXPECT_TRUE(bl::ui::media_bin_detail::localFilePaths(nullptr).isEmpty());
}

TEST(MediaBinPanel, DropExternalFilesImportsThem) {
    Fixture f;
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    const QStringList paths = makeTempFiles(QStringLiteral("dropped"), 2);
    ASSERT_EQ(paths.size(), 2);
    QMimeData mime;
    fillUrlMime(mime, paths);

    QDragEnterEvent enter(list->viewport()->rect().center(), Qt::CopyAction,
                          &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &enter);
    EXPECT_TRUE(enter.isAccepted());

    QDropEvent drop(list->viewport()->rect().center(), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(list->viewport(), &drop);
    EXPECT_TRUE(drop.isAccepted());
    QApplication::processEvents();

    EXPECT_EQ(f.controller.mediaBin().size(), 2u);
    EXPECT_EQ(f.panel.itemCount(), 2);
}

TEST(MediaBinPanel, DropExternalFilesIsUndoable) {
    Fixture f;
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    QMimeData mime;
    fillUrlMime(mime, makeTempFiles(QStringLiteral("undoable"), 1));

    // Qt always delivers drag-enter before the drop in a real drag.
    QDragEnterEvent enter(list->viewport()->rect().center(), Qt::CopyAction,
                          &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &enter);
    ASSERT_TRUE(enter.isAccepted());

    QDropEvent drop(list->viewport()->rect().center(), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(list->viewport(), &drop);
    QApplication::processEvents();
    ASSERT_EQ(f.controller.mediaBin().size(), 1u);

    f.controller.undoStack().undo();
    QApplication::processEvents();
    EXPECT_EQ(f.controller.mediaBin().size(), 0u);
    EXPECT_EQ(f.panel.itemCount(), 0);
}

TEST(MediaBinPanel, DragEnterRejectsInternalMediaMime) {
    // With DragDrop mode enabled, accepting the internal media-id payload would
    // let a row be dropped back onto the list and reordered by QListWidget.
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    QMimeData mime;
    mime.setData(QString::fromLatin1(bl::ui::kMediaBinMime), QByteArray("1"));
    QDragEnterEvent enter(list->viewport()->rect().center(), Qt::CopyAction,
                          &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &enter);
    EXPECT_FALSE(enter.isAccepted());
}

TEST(MediaBinPanel, DropIgnoresForeignMime) {
    Fixture f;
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    QMimeData mime;
    mime.setText(QStringLiteral("just text"));
    QDragEnterEvent enter(list->viewport()->rect().center(), Qt::CopyAction,
                          &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &enter);
    EXPECT_FALSE(enter.isAccepted());

    QDropEvent drop(list->viewport()->rect().center(), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QApplication::sendEvent(list->viewport(), &drop);
    EXPECT_FALSE(drop.isAccepted());
    EXPECT_EQ(f.controller.mediaBin().size(), 0u);
}

// ---------------------------------------------------------------------------
// Context menu / activation: sending a media row to the timeline.
// ---------------------------------------------------------------------------

TEST(MediaBinPanel, ContextMenuOffersAddAndRemoveForTheClickedRow) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    addMedia(f, QStringLiteral("/media/b.mp4"));
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    std::vector<std::string> requested;
    QObject::connect(&f.panel, &bl::ui::MediaBinPanel::addToTimelineRequested,
                     [&requested](const std::string& id) {
                         requested.push_back(id);
                     });

    // Right-click the second row: emit the context-menu request at a point
    // inside it (the signal position is a viewport coordinate in real use;
    // a row-centre point works under either convention).
    const QPoint pos = list->visualRect(list->model()->index(1, 0)).center();
    emit list->customContextMenuRequested(pos);

    QMenu* menu = f.panel.findChild<QMenu*>();
    ASSERT_NE(menu, nullptr);
    QAction* addAction = actionWithText(menu, QStringLiteral("Add to Timeline"));
    QAction* removeAction =
        actionWithText(menu, QStringLiteral("Remove from Project"));
    ASSERT_NE(addAction, nullptr);
    ASSERT_NE(removeAction, nullptr);
    // The Move to Folder submenu was added for folder organisation.
    EXPECT_NE(actionWithText(menu, QStringLiteral("Move to Folder")), nullptr);

    // The right-clicked row was promoted to the selection.
    ASSERT_EQ(f.controller.mediaBin().size(), 2u);
    EXPECT_EQ(f.panel.selectedId(), f.controller.mediaBin()[1].id);

    addAction->trigger();
    ASSERT_EQ(requested.size(), 1u);
    EXPECT_EQ(requested[0], f.controller.mediaBin()[1].id);
    // Adding to the timeline must not remove anything from the project.
    EXPECT_EQ(f.controller.mediaBin().size(), 2u);
    EXPECT_EQ(f.panel.itemCount(), 2);
}

TEST(MediaBinPanel, ContextMenuRemoveDeletesRowAndUndoRestores) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    const QPoint pos = list->visualRect(list->model()->index(0, 0)).center();
    emit list->customContextMenuRequested(pos);
    QMenu* menu = f.panel.findChild<QMenu*>();
    ASSERT_NE(menu, nullptr);
    QAction* removeAction =
        actionWithText(menu, QStringLiteral("Remove from Project"));
    ASSERT_NE(removeAction, nullptr);

    removeAction->trigger();  // "Remove from Project"
    QApplication::processEvents();
    EXPECT_EQ(f.controller.mediaBin().size(), 0u);
    EXPECT_EQ(f.panel.itemCount(), 0);

    f.controller.undoStack().undo();
    QApplication::processEvents();
    EXPECT_EQ(f.panel.itemCount(), 1);
}

TEST(MediaBinPanel, ContextMenuOnEmptyAreaPresentsNothing) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    // Far below the only row: no item under the point → no menu at all.
    const QPoint pos(list->viewport()->width() / 2, 10000);
    emit list->customContextMenuRequested(pos);
    EXPECT_EQ(f.panel.findChild<QMenu*>(), nullptr);
}

TEST(MediaBinPanel, ContextMenuViaRealContextMenuEventMapsToTheRightRow) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    addMedia(f, QStringLiteral("/media/b.mp4"));
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    // A real right-click delivers the QContextMenuEvent to the widget under
    // the cursor — the viewport — with a VIEWPORT-relative position, and Qt
    // forwards it to the view without remapping. The panel must still land on
    // the row under the pointer.
    const QPoint viewPos = list->visualRect(list->model()->index(1, 0)).center();
    const QPoint viewportPos = viewPos - list->viewport()->geometry().topLeft();
    QContextMenuEvent ev(QContextMenuEvent::Mouse, viewportPos,
                         list->viewport()->mapToGlobal(viewportPos));
    QApplication::sendEvent(list->viewport(), &ev);

    QMenu* menu = f.panel.findChild<QMenu*>();
    ASSERT_NE(menu, nullptr);
    ASSERT_NE(actionWithText(menu, QStringLiteral("Add to Timeline")), nullptr);
    ASSERT_NE(actionWithText(menu, QStringLiteral("Remove from Project")),
              nullptr);
    ASSERT_EQ(f.controller.mediaBin().size(), 2u);
    // Row 1 (not row 0) was resolved and promoted to the selection.
    EXPECT_EQ(f.panel.selectedId(), f.controller.mediaBin()[1].id);
}

TEST(MediaBinPanel, ActivationAddsToTimelineInsteadOfRemoving) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);

    std::vector<std::string> requested;
    QObject::connect(&f.panel, &bl::ui::MediaBinPanel::addToTimelineRequested,
                     [&requested](const std::string& id) {
                         requested.push_back(id);
                     });

    // Double-click / Enter reaches the panel as itemActivated.
    emit list->itemActivated(list->item(0));
    QApplication::processEvents();

    ASSERT_EQ(requested.size(), 1u);
    EXPECT_EQ(requested[0], f.controller.mediaBin()[0].id);
    // The old behaviour called removeSelected() here and deleted the row;
    // activation must leave the project untouched.
    EXPECT_EQ(f.controller.mediaBin().size(), 1u);
    EXPECT_EQ(f.panel.itemCount(), 1);
}

// ---------------------------------------------------------------------------
// Bin folders (collections): combo selection, create/rename/delete, move.
// ---------------------------------------------------------------------------

QComboBox* folderComboOf(bl::ui::MediaBinPanel& panel) {
    return panel.findChild<QComboBox*>(QStringLiteral("mediaBinFolderCombo"));
}

// Returns the combo index whose stored folder id matches, or -1.
int indexForFolder(QComboBox* combo, const std::string& folderId) {
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toString().toStdString() == folderId) return i;
    }
    return -1;
}

TEST(MediaBinPanel, FolderComboStartsAtRootOnly) {
    Fixture f;
    auto* combo = folderComboOf(f.panel);
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->count(), 1);
    EXPECT_EQ(combo->itemText(0), QStringLiteral("All / Root"));
    EXPECT_TRUE(combo->currentData().toString().isEmpty());
}

TEST(MediaBinPanel, ComboPopulatesWithFolderPaths) {
    Fixture f;
    const std::string parent = f.controller.addFolder(QStringLiteral("Parent"));
    const std::string child =
        f.controller.addFolder(QStringLiteral("Child"), parent);
    QApplication::processEvents();

    auto* combo = folderComboOf(f.panel);
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->count(), 3);
    const int parentIndex = indexForFolder(combo, parent);
    const int childIndex = indexForFolder(combo, child);
    ASSERT_GE(parentIndex, 0);
    ASSERT_GE(childIndex, 0);
    EXPECT_EQ(combo->itemText(parentIndex), QStringLiteral("Parent"));
    EXPECT_EQ(combo->itemText(childIndex), QStringLiteral("Parent / Child"));
}

TEST(MediaBinPanel, SelectingFolderFiltersList) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/root.mp4"));
    f.panel.createFolder(QStringLiteral("Folder"));
    QApplication::processEvents();
    addMedia(f, QStringLiteral("/media/infolder.mp4"));

    const std::string folderId = f.controller.folders()[0].id;
    const std::string mediaId = f.controller.mediaBin()[1].id;
    ASSERT_TRUE(f.controller.moveMediaToFolder(mediaId, folderId));
    QApplication::processEvents();

    // The folder is selected after creation: only its item shows.
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 1);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("infolder.mp4"));

    // Switching to the root swaps which item is visible.
    f.panel.selectFolder(std::string());
    QApplication::processEvents();
    ASSERT_EQ(list->count(), 1);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("root.mp4"));
}

TEST(MediaBinPanel, CreateFolderAddsSelectsAndRootStillWorks) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));

    f.panel.createFolder(QStringLiteral("Footage"));
    QApplication::processEvents();

    ASSERT_EQ(f.controller.folders().size(), 1u);
    auto* combo = folderComboOf(f.panel);
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->count(), 2);
    EXPECT_EQ(combo->itemText(1), QStringLiteral("Footage"));
    // The freshly created folder is selected (empty list).
    EXPECT_EQ(combo->currentData().toString(),
              QString::fromStdString(f.controller.folders()[0].id));
    EXPECT_EQ(f.panel.itemCount(), 0);

    f.panel.selectFolder(std::string());
    QApplication::processEvents();
    EXPECT_EQ(f.panel.itemCount(), 1);
}

TEST(MediaBinPanel, DeleteFolderRehomesMediaToRoot) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    f.panel.createFolder(QStringLiteral("Footage"));
    QApplication::processEvents();

    const std::string folderId = f.controller.folders()[0].id;
    const std::string mediaId = f.controller.mediaBin()[0].id;
    ASSERT_TRUE(f.controller.moveMediaToFolder(mediaId, folderId));
    QApplication::processEvents();
    EXPECT_EQ(f.panel.itemCount(), 1);  // the folder view shows a.mp4

    f.panel.deleteCurrentFolder();
    QApplication::processEvents();

    EXPECT_TRUE(f.controller.folders().empty());
    // The combo fell back to the root, where the rehomed item is now visible.
    EXPECT_EQ(f.controller.mediaBin()[0].folderId, std::string());
    EXPECT_EQ(f.panel.itemCount(), 1);
    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 1);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("a.mp4"));

    // Undo restores the folder and its membership.
    f.controller.undoStack().undo();
    QApplication::processEvents();
    EXPECT_EQ(f.controller.folders().size(), 1u);
    EXPECT_EQ(f.controller.mediaBin()[0].folderId, folderId);
}

TEST(MediaBinPanel, FolderButtonsEnabledOnlyForNonRoot) {
    Fixture f;
    auto* renameButton =
        f.panel.findChild<QPushButton*>(QStringLiteral("mediaBinRenameFolderButton"));
    auto* deleteButton =
        f.panel.findChild<QPushButton*>(QStringLiteral("mediaBinDeleteFolderButton"));
    auto* newButton =
        f.panel.findChild<QPushButton*>(QStringLiteral("mediaBinNewFolderButton"));
    ASSERT_NE(renameButton, nullptr);
    ASSERT_NE(deleteButton, nullptr);
    ASSERT_NE(newButton, nullptr);
    EXPECT_TRUE(newButton->isEnabled());
    EXPECT_FALSE(renameButton->isEnabled());
    EXPECT_FALSE(deleteButton->isEnabled());

    f.panel.createFolder(QStringLiteral("Footage"));
    QApplication::processEvents();
    EXPECT_TRUE(renameButton->isEnabled());
    EXPECT_TRUE(deleteButton->isEnabled());

    f.panel.selectFolder(std::string());
    QApplication::processEvents();
    EXPECT_FALSE(renameButton->isEnabled());
    EXPECT_FALSE(deleteButton->isEnabled());
}

TEST(MediaBinPanel, RenameFolderUpdatesCombo) {
    Fixture f;
    f.panel.createFolder(QStringLiteral("Old"));
    QApplication::processEvents();

    f.panel.renameCurrentFolder(QStringLiteral("New"));
    QApplication::processEvents();

    ASSERT_EQ(f.controller.folders().size(), 1u);
    EXPECT_EQ(f.controller.folders()[0].name, "New");
    auto* combo = folderComboOf(f.panel);
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->itemText(1), QStringLiteral("New"));
}

TEST(MediaBinPanel, ContextMenuMoveToFolderSubmenu) {
    Fixture f;
    addMedia(f, QStringLiteral("/media/a.mp4"));
    f.panel.createFolder(QStringLiteral("Footage"));
    QApplication::processEvents();
    const std::string folderId = f.controller.folders()[0].id;

    // The new folder is selected; switch back to the root to expose the row.
    f.panel.selectFolder(std::string());
    QApplication::processEvents();

    auto* list = listOf(f.panel);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 1);
    const QPoint pos = list->visualRect(list->model()->index(0, 0)).center();
    emit list->customContextMenuRequested(pos);

    QMenu* menu = f.panel.findChild<QMenu*>();
    ASSERT_NE(menu, nullptr);
    QAction* moveAction = actionWithText(menu, QStringLiteral("Move to Folder"));
    ASSERT_NE(moveAction, nullptr);
    QMenu* submenu = moveAction->menu();
    ASSERT_NE(submenu, nullptr);
    // Root entry plus the one folder.
    ASSERT_EQ(submenu->actions().size(), 2);
    EXPECT_EQ(submenu->actions()[0]->text(), QStringLiteral("All / Root"));
    QAction* folderAction = actionWithText(submenu, QStringLiteral("Footage"));
    ASSERT_NE(folderAction, nullptr);

    folderAction->trigger();
    QApplication::processEvents();
    EXPECT_EQ(f.controller.mediaBin()[0].folderId, folderId);
}

} // namespace
