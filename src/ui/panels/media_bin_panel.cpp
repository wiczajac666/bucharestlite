#include "panels/media_bin_panel.hpp"

#include "app/project_controller.hpp"

#include <QApplication>
#include <QDrag>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionViewItem>
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
        colour.setAlphaF(0.72);
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
    list_->setIconSize(QSize(96, 54));
    list_->setItemDelegate(new MediaBinItemDelegate(list_));

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
    list_->clear();
    itemsById_.clear();
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
        item->setIcon(placeholderIcon());
        item->setToolTip(QString::fromStdString(entry.path));
        itemsById_[entry.id] = item;
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
