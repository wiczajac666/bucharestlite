#include "panels/media_bin_panel.hpp"

#include "app/project_controller.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace bl::ui {

namespace media_bin_detail {

bool matchesFilter(const std::string& name, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](char c) { return std::tolower(static_cast<unsigned char>(c)); };
    std::string lowerName = name;
    std::string lowerNeedle = needle;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), lower);
    std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), lower);
    return lowerName.find(lowerNeedle) != std::string::npos;
}

QStringList localFilePaths(const QMimeData* mime) {
    QStringList paths;
    if (!mime || !mime->hasUrls()) {
        return paths;
    }
    const QList<QUrl> urls = mime->urls();
    paths.reserve(urls.size());
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        if (path.isEmpty()) continue;
        // A dropped folder is not media. Silently skipping it keeps the
        // "drop a bunch of files" case working when the file manager happens
        // to include a directory in the selection.
        if (QFileInfo(path).isDir()) continue;
        paths.push_back(path);
    }
    return paths;
}

} // namespace media_bin_detail

namespace {

constexpr int kIdRole = Qt::UserRole;

// Neutral "media" glyph shown until a real first-frame thumbnail lands (and
// permanently for audio-only entries). Built once and shared.
QIcon placeholderIcon() {
    static const QIcon icon = [] {
        constexpr int w = 96;
        constexpr int h = 54;
        QPixmap pixmap(w, h);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(48, 48, 52));
        painter.drawRoundedRect(QRectF(0, 0, w, h), 4, 4);

        // Film-strip glyph: a lighter frame with two punched hole strips.
        painter.setBrush(QColor(92, 94, 104));
        painter.drawRoundedRect(QRectF(24, 13, 48, 28), 3, 3);
        painter.setBrush(QColor(48, 48, 52));
        painter.drawRect(QRectF(28, 17, 40, 5));
        painter.drawRect(QRectF(28, 32, 40, 5));
        painter.setBrush(QColor(92, 94, 104));
        for (int i = 0; i < 5; ++i) {
            painter.drawRect(QRectF(30 + i * 8, 18, 4, 3));
            painter.drawRect(QRectF(30 + i * 8, 33, 4, 3));
        }
        painter.end();
        return QIcon(pixmap);
    }();
    return icon;
}

// Builds the "Parent / Child" display path for a folder by walking its parent
// chain. Visited ids guard against a malformed tree.
QString folderDisplayPath(const std::vector<bl::MediaBinFolder>& folders,
                          const std::string& id) {
    std::vector<std::string> names;
    std::vector<std::string> seen;
    std::string current = id;
    while (!current.empty()) {
        if (std::find(seen.begin(), seen.end(), current) != seen.end()) break;
        seen.push_back(current);
        const auto it = std::find_if(
            folders.begin(), folders.end(),
            [&current](const bl::MediaBinFolder& f) { return f.id == current; });
        if (it == folders.end()) break;
        names.push_back(it->name);
        current = it->parentId;
    }
    std::reverse(names.begin(), names.end());
    QString path;
    for (const std::string& name : names) {
        if (!path.isEmpty()) path += QStringLiteral(" / ");
        path += QString::fromStdString(name);
    }
    return path;
}

} // namespace

MediaBinItemDelegate::MediaBinItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void MediaBinItemDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const QString name = opt.text;
    const QString detail = index.data(kMediaBinDetailRole).toString();
    const QIcon icon = opt.icon;

    // Let the style paint the row background/selection/focus, but suppress its
    // single-line text+icon pass so we can lay out two lines ourselves.
    opt.text.clear();
    opt.icon = QIcon();
    opt.features &= ~QStyleOptionViewItem::HasDisplay;

    QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const QRect content = opt.rect.adjusted(4, 2, -4, -2);
    const QSize iconSize =
        opt.decorationSize.isValid() ? opt.decorationSize : QSize(96, 54);

    QRect iconRect(content.left(),
                   content.top() + (content.height() - iconSize.height()) / 2,
                   iconSize.width(), iconSize.height());
    if (iconRect.bottom() > content.bottom()) {
        iconRect.moveBottom(content.bottom());
    }
    if (!icon.isNull()) {
        icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal);
    }

    const QRect textRect = content.adjusted(iconSize.width() + 8, 0, 0, 0);
    const QPalette::ColorRole textRole = (opt.state & QStyle::State_Selected)
                                             ? QPalette::HighlightedText
                                             : QPalette::Text;

    painter->save();
    QFont font = opt.font;
    painter->setFont(font);
    const QFontMetrics metrics(font);
    const int lineHeight = metrics.height();

    const QRect nameRect(textRect.left(), textRect.top(), textRect.width(),
                         lineHeight);
    painter->setPen(opt.palette.color(textRole));
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(name, Qt::ElideMiddle,
                                         textRect.width()));

    if (!detail.isEmpty()) {
        if (font.pointSizeF() > 0.0) {
            font.setPointSizeF(font.pointSizeF() * 0.88);
        } else if (font.pixelSize() > 1) {
            font.setPixelSize(std::max(1, font.pixelSize() * 8 / 10));
        }
        painter->setFont(font);
        const QFontMetrics detailMetrics(font);
        QColor colour = opt.palette.color(textRole);
        // 0.72f, not 0.72: Qt 6.8 narrowed QColor's *F setters from qreal to
        // float, so a bare double literal is a narrowing conversion and MSVC
        // rejects it as C4305 under /W4 /WX. GCC does not warn without
        // -Wconversion, which is why this only surfaced on the CI runner.
        colour.setAlphaF(0.72f);
        painter->setPen(colour);
        const QRect detailRect(textRect.left(), nameRect.bottom(),
                               textRect.width(), lineHeight);
        painter->drawText(detailRect, Qt::AlignVCenter | Qt::AlignLeft,
                          detailMetrics.elidedText(detail, Qt::ElideMiddle,
                                                   textRect.width()));
    }
    painter->restore();
}

QSize MediaBinItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const QSize iconSize =
        option.decorationSize.isValid() ? option.decorationSize : QSize(96, 54);
    size.setHeight(std::max(size.height(), iconSize.height() + 6));
    return size;
}

MediaBinList::MediaBinList(QWidget* parent) : QListWidget(parent) {
    // Accepts media files dropped in from the file manager. DragOnly would
    // refuse those drops outright, which is why external import needs an
    // explicit DragDrop mode here.
    setAcceptDrops(true);
}

QMimeData* MediaBinList::dragData(const QModelIndex& index) const {
    const QVariant id = model()->data(index, kIdRole);
    if (!id.isValid() || id.toString().isEmpty()) return nullptr;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMediaBinMime), id.toString().toUtf8());
    return mime;
}

void MediaBinList::startDrag(Qt::DropActions supportedActions) {
    const QModelIndex index = currentIndex();
    QMimeData* mime = dragData(index);
    if (!mime) return;
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);

    // Drag feedback: paint the dragged row (thumbnail + name + detail) into a
    // pixmap so the user can see what they are carrying. The item delegate
    // does the layout, so the ghost matches the row exactly.
    const QRect rect = visualRect(index);
    QAbstractItemDelegate* delegate = itemDelegateForIndex(index);
    if (delegate && rect.isValid() && !rect.isEmpty()) {
        QPixmap pixmap(rect.size());
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        QStyleOptionViewItem option;
        option.initFrom(this);
        option.widget = this;
        option.index = index;
        option.rect = QRect(QPoint(0, 0), rect.size());
        option.decorationSize = iconSize();
        if (index == currentIndex()) option.state |= QStyle::State_Selected;
        delegate->paint(&painter, option, index);
        painter.end();
        drag->setPixmap(pixmap);
        drag->setHotSpot(pixmap.rect().center());
    }
    drag->exec(supportedActions, Qt::CopyAction);
}

void MediaBinList::dragEnterEvent(QDragEnterEvent* event) {
    // Only external media payloads are accepted here. The internal
    // kMediaBinMime drag must NOT be accepted: with DragDrop mode enabled,
    // letting it through would let a row be dropped back onto the list and
    // reordered by QListWidget's own move handling.
    if (media_bin_detail::localFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MediaBinList::dragMoveEvent(QDragMoveEvent* event) {
    if (media_bin_detail::localFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MediaBinList::dropEvent(QDropEvent* event) {
    const QStringList paths = media_bin_detail::localFilePaths(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    emit filesDropped(paths);
    event->acceptProposedAction();
}

MediaBinPanel::MediaBinPanel(ProjectController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setObjectName(QStringLiteral("MediaBinPanel"));

    list_ = new MediaBinList(this);
    list_->setObjectName(QStringLiteral("mediaBinList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAlternatingRowColors(true);
    // DragDrop (not DragOnly) so media files dropped from the file manager are
    // accepted. MediaBinList::dropEvent deliberately ignores the internal
    // media-id mime so rows still cannot be reordered within the list.
    list_->setDragDropMode(QAbstractItemView::DragDrop);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setIconSize(QSize(96, 54));
    list_->setItemDelegate(new MediaBinItemDelegate(list_));

    filter_ = new QLineEdit(this);
    filter_->setObjectName(QStringLiteral("mediaBinFilter"));
    filter_->setClearButtonEnabled(true);
    filter_->setPlaceholderText(tr("Filter media..."));

    folderCombo_ = new QComboBox(this);
    folderCombo_->setObjectName(QStringLiteral("mediaBinFolderCombo"));
    folderCombo_->setToolTip(tr("Bin folder (collection) to show"));
    newFolderButton_ = new QPushButton(tr("New Folder"), this);
    newFolderButton_->setObjectName(QStringLiteral("mediaBinNewFolderButton"));
    renameFolderButton_ = new QPushButton(tr("Rename Folder"), this);
    renameFolderButton_->setObjectName(
        QStringLiteral("mediaBinRenameFolderButton"));
    deleteFolderButton_ = new QPushButton(tr("Delete Folder"), this);
    deleteFolderButton_->setObjectName(
        QStringLiteral("mediaBinDeleteFolderButton"));
    renameFolderButton_->setToolTip(tr("Rename the selected folder"));
    deleteFolderButton_->setToolTip(
        tr("Delete the selected folder (its media moves to the root)"));
    renameFolderButton_->setEnabled(false);
    deleteFolderButton_->setEnabled(false);

    auto* folderBar = new QHBoxLayout;
    folderBar->addWidget(folderCombo_, 1);
    folderBar->addWidget(newFolderButton_);
    folderBar->addWidget(renameFolderButton_);
    folderBar->addWidget(deleteFolderButton_);

    addButton_ = new QPushButton(tr("Add..."), this);
    addButton_->setObjectName(QStringLiteral("mediaBinAdd"));
    removeButton_ = new QPushButton(tr("Remove"), this);
    removeButton_->setObjectName(QStringLiteral("mediaBinRemove"));
    removeButton_->setEnabled(false);

    auto* topBar = new QHBoxLayout;
    topBar->addWidget(filter_, 1);
    topBar->addWidget(addButton_);
    topBar->addWidget(removeButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(folderBar);
    layout->addLayout(topBar);
    layout->addWidget(list_, 1);

    connect(addButton_, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("Add Media"), QString(),
            tr("Media Files (*.mp4 *.mov *.mkv *.webm *.ogv *.avi *.m4a *.wav *.flac *.ogg *.opus);;"
               "Image Sequences (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.exr *.dpx *.tga *.webp);;"
               "All Files (*)"));
        addFiles(files);
    });
    connect(removeButton_, &QPushButton::clicked, this,
            &MediaBinPanel::removeSelected);
    connect(filter_, &QLineEdit::textChanged, this,
            &MediaBinPanel::onFilterChanged);
    connect(folderCombo_, &QComboBox::currentIndexChanged, this,
            &MediaBinPanel::onFolderChanged);
    connect(list_, &QListWidget::itemSelectionChanged, this,
            &MediaBinPanel::onSelectionChanged);
    connect(list_, &MediaBinList::filesDropped, this,
            [this](const QStringList& paths) { addFiles(paths); });
    connect(list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) {
                // Double-click / Enter sends the row to the timeline. This
                // used to call removeSelected(), which deleted the row — the
                // opposite of what activating a clip should do.
                requestAddToTimeline(item);
            });
    connect(list_, &QWidget::customContextMenuRequested, this,
            &MediaBinPanel::showContextMenu);

    // Folder management. The buttons prompt through QInputDialog, then defer to
    // the shared create/rename/delete entry points so the behaviour is
    // identical whether or not a dialog ran.
    connect(newFolderButton_, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal,
            QString(), &ok);
        if (ok) createFolder(name);
    });
    connect(renameFolderButton_, &QPushButton::clicked, this, [this] {
        const std::string id = selectedFolderId();
        if (id.empty()) return;
        QString current;
        for (const MediaBinFolder& folder : controller_->folders()) {
            if (folder.id == id) {
                current = QString::fromStdString(folder.name);
                break;
            }
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Rename Folder"), tr("Folder name:"), QLineEdit::Normal,
            current, &ok);
        if (ok) renameCurrentFolder(name);
    });
    connect(deleteFolderButton_, &QPushButton::clicked, this,
            &MediaBinPanel::deleteCurrentFolder);

    if (controller_) {
        connect(controller_, &ProjectController::projectChanged, this,
                &MediaBinPanel::reload);
    }

    reload();
}

int MediaBinPanel::itemCount() const { return list_->count(); }

int MediaBinPanel::visibleItemCount() const {
    int count = 0;
    for (int i = 0; i < list_->count(); ++i) {
        if (!list_->item(i)->isHidden()) {
            ++count;
        }
    }
    return count;
}

std::string MediaBinPanel::selectedId() const {
    QListWidgetItem* item = list_->currentItem();
    return item ? item->data(kIdRole).toString().toStdString() : std::string();
}

void MediaBinPanel::applyMediaInfo(const std::string& id,
                                   const QImage& thumbnail,
                                   const QString& detail) {
    auto it = itemsById_.find(id);
    if (it == itemsById_.end() || !it->second) {
        return;
    }
    QListWidgetItem* item = it->second;
    if (!thumbnail.isNull()) {
        item->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
    } else if (item->icon().isNull()) {
        item->setIcon(placeholderIcon());
    }
    item->setData(kMediaBinDetailRole, detail);
    list_->viewport()->update();
}

void MediaBinPanel::reload() {
    if (!controller_) {
        list_->clear();
        itemsById_.clear();
        return;
    }
    reloadFolderCombo();
    rebuildList();
}

void MediaBinPanel::reloadFolderCombo() {
    // Preserve the current selection across a rebuild when that folder still
    // exists; otherwise fall back to the root entry at index 0.
    const std::string previous = selectedFolderId();
    const QSignalBlocker blocker(folderCombo_);
    folderCombo_->clear();
    folderCombo_->addItem(tr("All / Root"),
                          QString::fromStdString(std::string()));

    const std::vector<MediaBinFolder>& folders = controller_->folders();
    std::vector<std::pair<QString, std::string>> entries;
    entries.reserve(folders.size());
    for (const MediaBinFolder& folder : folders) {
        entries.emplace_back(folderDisplayPath(folders, folder.id), folder.id);
    }
    // Sorted lexicographically, which keeps a parent ("Parent") ahead of its
    // children ("Parent / Child").
    std::sort(entries.begin(), entries.end(),
              [](const std::pair<QString, std::string>& a,
                 const std::pair<QString, std::string>& b) {
                  return a.first < b.first;
              });
    for (const auto& entry : entries) {
        folderCombo_->addItem(entry.first,
                              QString::fromStdString(entry.second));
    }

    int index = 0;
    for (int i = 0; i < folderCombo_->count(); ++i) {
        if (folderCombo_->itemData(i).toString().toStdString() == previous) {
            index = i;
            break;
        }
    }
    folderCombo_->setCurrentIndex(index);
    updateFolderButtons();
}

void MediaBinPanel::rebuildList() {
    list_->clear();
    itemsById_.clear();
    if (!controller_) {
        return;
    }
    // Only the media that lives directly in the selected folder is shown; the
    // root selection (folderId "") is the project root.
    const std::string folder = selectedFolderId();
    for (const MediaBinItem& entry : controller_->mediaBin()) {
        if (entry.folderId != folder) {
            continue;
        }
        auto* item = new QListWidgetItem(QString::fromStdString(entry.name), list_);
        item->setData(kIdRole, QString::fromStdString(entry.id));
        item->setIcon(placeholderIcon());
        item->setToolTip(QString::fromStdString(entry.path));
        itemsById_[entry.id] = item;
    }
    applyFilter();
    onSelectionChanged();
}

std::string MediaBinPanel::selectedFolderId() const {
    if (!folderCombo_) {
        return std::string();
    }
    const QVariant selected = folderCombo_->currentData();
    return selected.isValid() ? selected.toString().toStdString()
                              : std::string();
}

void MediaBinPanel::updateFolderButtons() {
    const bool hasFolder = !selectedFolderId().empty();
    renameFolderButton_->setEnabled(hasFolder);
    deleteFolderButton_->setEnabled(hasFolder);
}

void MediaBinPanel::onFolderChanged(int) {
    updateFolderButtons();
    rebuildList();
}

void MediaBinPanel::selectFolder(const std::string& folderId) {
    if (!folderCombo_) {
        return;
    }
    for (int i = 0; i < folderCombo_->count(); ++i) {
        if (folderCombo_->itemData(i).toString().toStdString() == folderId) {
            folderCombo_->setCurrentIndex(i);
            return;
        }
    }
}

void MediaBinPanel::createFolder(const QString& name) {
    if (!controller_) {
        return;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    // New folders nest under whatever is currently selected (root when the
    // root entry is selected).
    const std::string id = controller_->addFolder(trimmed, selectedFolderId());
    if (!id.empty()) {
        selectFolder(id);
    }
}

void MediaBinPanel::renameCurrentFolder(const QString& name) {
    if (!controller_) {
        return;
    }
    const std::string id = selectedFolderId();
    if (id.empty()) {
        return;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    controller_->renameFolder(id, trimmed);
}

void MediaBinPanel::deleteCurrentFolder() {
    if (!controller_) {
        return;
    }
    const std::string id = selectedFolderId();
    if (id.empty()) {
        return;
    }
    // Deleting rehomes the folder's media and child folders to the root; the
    // combo then falls back to the root entry on the resulting reload.
    controller_->removeFolder(id);
}

void MediaBinPanel::addFiles(const QStringList& paths) {
    if (!controller_) {
        return;
    }
    for (const QString& path : paths) {
        controller_->addToMediaBin(path);
    }
    // reload() is triggered by the projectChanged signal.
}

void MediaBinPanel::removeSelected() {
    if (!controller_) {
        return;
    }
    const std::string id = selectedId();
    if (id.empty()) {
        return;
    }
    controller_->removeFromMediaBin(QString::fromStdString(id));
}

void MediaBinPanel::showContextMenu(const QPoint& listPos) {
    // Qt forwards the viewport's context-menu event without remapping, so the
    // position arrives in viewport coordinates; map it into view space for
    // itemAt() (the fallback also copes with keyboard-triggered menus, which
    // already carry view coordinates — both are within one frame pixel).
    QListWidgetItem* item = list_->itemAt(list_->viewport()->mapTo(list_, listPos));
    if (!item) item = list_->itemAt(listPos);
    if (!item) return;  // empty area: nothing to act on, no menu.

    // Right-clicking a non-selected row promotes it to the selection, so the
    // menu and the Remove button refer to what was clicked.
    list_->setCurrentItem(item);
    const std::string id = item->data(kIdRole).toString().toStdString();
    if (id.empty()) return;

    auto* menu = new QMenu(list_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* addAction = menu->addAction(tr("Add to Timeline"));
    QAction* removeAction = menu->addAction(tr("Remove from Project"));
    // Titles can be re-opened in the Title Editor (their design is stored in
    // the project, so the still is re-rendered in place).
    QAction* editAction = nullptr;
    if (controller_ && !controller_->titleSpecFor(id).is_null()) {
        menu->addSeparator();
        editAction = menu->addAction(tr("Edit Title…"));
    }
    connect(addAction, &QAction::triggered, this, [this, id] {
        emit addToTimelineRequested(id);
    });
    connect(removeAction, &QAction::triggered, this, [this, id] {
        if (controller_) {
            controller_->removeFromMediaBin(QString::fromStdString(id));
        }
    });
    if (editAction) {
        connect(editAction, &QAction::triggered, this, [this, id] {
            emit editTitleRequested(id);
        });
    }
    // Organise without dragging: a submenu listing the root plus every folder.
    // Folders are addressed by id (captured per-action) so a reload triggered
    // by a move cannot dangle the pointer.
    if (controller_) {
        menu->addSeparator();
        QMenu* moveMenu = menu->addMenu(tr("Move to Folder"));
        QAction* rootAction = moveMenu->addAction(tr("All / Root"));
        connect(rootAction, &QAction::triggered, this, [this, id] {
            controller_->moveMediaToFolder(id, std::string());
        });
        const std::vector<MediaBinFolder>& folders = controller_->folders();
        for (const MediaBinFolder& folder : folders) {
            QAction* action =
                moveMenu->addAction(folderDisplayPath(folders, folder.id));
            const std::string folderId = folder.id;
            connect(action, &QAction::triggered, this, [this, id, folderId] {
                controller_->moveMediaToFolder(id, folderId);
            });
        }
    }
    menu->popup(list_->viewport()->mapToGlobal(listPos));
}

void MediaBinPanel::requestAddToTimeline(QListWidgetItem* item) {
    if (!item) return;
    const QString id = item->data(kIdRole).toString();
    if (id.isEmpty()) return;
    emit addToTimelineRequested(id.toStdString());
}

void MediaBinPanel::onSelectionChanged() {
    removeButton_->setEnabled(list_->currentItem() != nullptr);
}

void MediaBinPanel::onFilterChanged(const QString&) { applyFilter(); }

void MediaBinPanel::applyFilter() {
    const std::string needle = filter_->text().toStdString();
    if (needle.empty()) {
        // Blank filter: show every row already in the list.
        for (int i = 0; i < list_->count(); ++i) {
            list_->item(i)->setHidden(false);
        }
    } else {
        for (int i = 0; i < list_->count(); ++i) {
            QListWidgetItem* item = list_->item(i);
            const std::string name = item->text().toStdString();
            item->setHidden(!media_bin_detail::matchesFilter(name, needle));
        }
    }
}

} // namespace bl::ui
