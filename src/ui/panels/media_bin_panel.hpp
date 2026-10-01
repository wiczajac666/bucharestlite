#pragma once

#include <QListWidget>
#include <QStyledItemDelegate>
#include <QWidget>

#include <string>
#include <unordered_map>

class QImage;
class QMimeData;
class QModelIndex;
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

// The media list with drag support: rows are draggable out of the bin
// carrying their media-bin id under kMediaBinMime. dragData() is public so
// offscreen tests can assert the payload directly.
class MediaBinList : public QListWidget {
    Q_OBJECT
public:
    explicit MediaBinList(QWidget* parent = nullptr);

    // Mime payload for the row at `index`, or nullptr when it has no id.
    QMimeData* dragData(const QModelIndex& index) const;

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};

// Media Bin dock: lists the project's imported media (name per row), with
// Add / Remove and a live name filter. All mutations go through the undoable
// ProjectController mutations, so add/remove/undo/redo/open/new are reflected
// by reloading on projectChanged.
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

public slots:
    void reload();
    void addFiles(const QStringList& paths);

private slots:
    void removeSelected();
    void onSelectionChanged();
    void onFilterChanged(const QString& text);

private:
    void applyFilter();

    ProjectController* controller_{nullptr};
    MediaBinList* list_{nullptr};
    QLineEdit* filter_{nullptr};
    QPushButton* addButton_{nullptr};
    QPushButton* removeButton_{nullptr};
    std::unordered_map<std::string, QListWidgetItem*> itemsById_;
};

} // namespace bl::ui
