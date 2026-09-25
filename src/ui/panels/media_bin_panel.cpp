#include "panels/media_bin_panel.hpp"

#include "app/project_controller.hpp"

#include <QDrag>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
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

} // namespace media_bin_detail

namespace {

constexpr int kIdRole = Qt::UserRole;

} // namespace

MediaBinList::MediaBinList(QWidget* parent) : QListWidget(parent) {}

QMimeData* MediaBinList::dragData(const QModelIndex& index) const {
    const QVariant id = model()->data(index, kIdRole);
    if (!id.isValid() || id.toString().isEmpty()) return nullptr;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMediaBinMime), id.toString().toUtf8());
    return mime;
}

void MediaBinList::startDrag(Qt::DropActions supportedActions) {
    QMimeData* mime = dragData(currentIndex());
    if (!mime) return;
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(supportedActions, Qt::CopyAction);
}

MediaBinPanel::MediaBinPanel(ProjectController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setObjectName(QStringLiteral("MediaBinPanel"));

    list_ = new MediaBinList(this);
    list_->setObjectName(QStringLiteral("mediaBinList"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAlternatingRowColors(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);

    filter_ = new QLineEdit(this);
    filter_->setObjectName(QStringLiteral("mediaBinFilter"));
    filter_->setClearButtonEnabled(true);
    filter_->setPlaceholderText(tr("Filter media..."));

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
    layout->addLayout(topBar);
    layout->addWidget(list_, 1);

    connect(addButton_, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("Add Media"), QString(),
            tr("Media Files (*.mp4 *.mov *.mkv *.webm *.ogv *.avi *.m4a *.wav *.flac *.ogg *.opus);;All Files (*)"));
        addFiles(files);
    });
    connect(removeButton_, &QPushButton::clicked, this,
            &MediaBinPanel::removeSelected);
    connect(filter_, &QLineEdit::textChanged, this,
            &MediaBinPanel::onFilterChanged);
    connect(list_, &QListWidget::itemSelectionChanged, this,
            &MediaBinPanel::onSelectionChanged);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) {
        removeSelected();
    });

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

void MediaBinPanel::reload() {
    list_->clear();
    if (!controller_) {
        return;
    }
    const std::vector<MediaBinItem>& bin = controller_->mediaBin();
    for (const MediaBinItem& entry : bin) {
        if (!media_bin_detail::matchesFilter(entry.name, filter_->text().toStdString())) {
            continue;
        }
        auto* item = new QListWidgetItem(QString::fromStdString(entry.name), list_);
        item->setData(kIdRole, QString::fromStdString(entry.id));
    }
    onSelectionChanged();
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
