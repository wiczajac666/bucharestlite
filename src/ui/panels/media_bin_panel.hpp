#pragma once

#include <QListWidget>
#include <QStyledItemDelegate>
#include <QWidget>

#include <QStringList>

#include <string>
#include <unordered_map>

class QImage;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QModelIndex;
class QComboBox;
class QLineEdit;
class QListWidgetItem;
class QPainter;
class QPushButton;
class QStyleOptionViewItem;

namespace bl::ui {

class ProjectController;

// Mime type carried by media-bin drags onto the timeline (payload = the
// media-bin item id as UTF-8).
inline constexpr char kMediaBinMime[] = "application/x-bucharest-media";

// Extra item role holding the metadata detail line (duration · resolution ·
// codec). The row's DisplayRole stays the bare file name so filtering and drag
// behaviour are unchanged.
inline constexpr int kMediaBinDetailRole = Qt::UserRole + 1;

// Filtering / id-lookup helpers for the media bin, kept Qt-widget-free so they
// are directly assertable from plain unit tests.
namespace media_bin_detail {

bool matchesFilter(const std::string& name, const std::string& needle);

// Local file paths carried by an external drop, read from the text/uri-list
// payload every OS file manager and shell publishes. Entries that are not
// local files (http(s):// URLs) and directories are filtered out, so a drop
// can never be mistaken for a media import; the result is empty when the
// payload holds no usable file at all.
QStringList localFilePaths(const QMimeData* mime);

} // namespace media_bin_detail

// Paints a media row as a thumbnail icon plus a name and a dimmer detail line.
// The item's DisplayRole is left as the bare name (unlike a multi-line text
// role), so the filter and drag paths keep working untouched.
class MediaBinItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit MediaBinItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

// The media list, which is both ends of a drag: rows drag out of the bin
// carrying their media-bin id under kMediaBinMime, and media files dropped in
// from the OS shell are reported through filesDropped(). dragData() is public
// so offscreen tests can assert the payload directly.
class MediaBinList : public QListWidget {
    Q_OBJECT
public:
    explicit MediaBinList(QWidget* parent = nullptr);

    // Mime payload for the row at `index`, or nullptr when it has no id.
    QMimeData* dragData(const QModelIndex& index) const;

signals:
    // Absolute local file paths dropped onto the list from outside the app.
    void filesDropped(const QStringList& paths);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

// Media Bin dock: lists the project's imported media (name per row), with
// Add / Remove and a live name filter. All mutations go through the undoable
// ProjectController mutations, so add/remove/undo/redo/open/new are reflected
// by reloading on projectChanged.
//
// Media can also be organised into folders (collections): a folder combo at the
// top selects which folder's contents the list shows (the root entry shows
// items with an empty folderId), the New/Rename/Delete buttons manage folders
// (deleting rehomes the folder's media and child folders to the root), and the
// row context menu's "Move to Folder" submenu re-parents an item.
class MediaBinPanel : public QWidget {
    Q_OBJECT
public:
    explicit MediaBinPanel(ProjectController* controller, QWidget* parent = nullptr);

    int itemCount() const;
    int visibleItemCount() const;
    std::string selectedId() const;

    // Update one row's thumbnail (a null QImage keeps the placeholder, e.g.
    // audio-only or failed decodes) and metadata detail line. Safe to call
    // after reload(); ids that are not currently shown are ignored.
    void applyMediaInfo(const std::string& id, const QImage& thumbnail,
                        const QString& detail);

signals:
    // The user asked for a media item to go onto the timeline (right-click
    // "Add to Timeline" or double-click). Emitted by the panel; the receiver
    // (the timeline) resolves the id and reports its own failures.
    void addToTimelineRequested(const std::string& id);
    // The user chose "Edit Title…" on a title row. The receiver (MainWindow)
    // opens the title editor pre-filled with the stored spec.
    void editTitleRequested(const std::string& id);

public slots:
    void reload();
    void addFiles(const QStringList& paths);

    // Folder operations shared by the buttons and the tests. The buttons prompt
    // through QInputDialog first; these entry points take the resolved name so
    // the behaviour is directly assertable.
    void createFolder(const QString& name);
    void renameCurrentFolder(const QString& name);
    void deleteCurrentFolder();
    // Selects the folder with `folderId` in the combo (no-op when absent).
    void selectFolder(const std::string& folderId);

private slots:
    void removeSelected();
    void onSelectionChanged();
    void onFilterChanged(const QString& text);
    void onFolderChanged(int index);

private:
    void applyFilter();
    // Rebuilds the folder combo from the controller, preserving the current
    // selection when that folder still exists (falling back to the root).
    void reloadFolderCombo();
    // Rebuilds the media list for the currently selected folder.
    void rebuildList();
    // Id of the selected folder ("" = root).
    std::string selectedFolderId() const;
    // Enables rename/delete only when a non-root folder is selected.
    void updateFolderButtons();
    // Right-click menu on a row: "Add to Timeline" / "Remove from Project" /
    // "Move to Folder". A click in the empty area shows no menu.
    void showContextMenu(const QPoint& listPos);
    // Shared tail of the context menu and double-click paths.
    void requestAddToTimeline(QListWidgetItem* item);

    ProjectController* controller_{nullptr};
    MediaBinList* list_{nullptr};
    QComboBox* folderCombo_{nullptr};
    QLineEdit* filter_{nullptr};
    QPushButton* addButton_{nullptr};
    QPushButton* removeButton_{nullptr};
    QPushButton* newFolderButton_{nullptr};
    QPushButton* renameFolderButton_{nullptr};
    QPushButton* deleteFolderButton_{nullptr};
    std::unordered_map<std::string, QListWidgetItem*> itemsById_;
};

} // namespace bl::ui
