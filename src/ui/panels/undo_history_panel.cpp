#include "panels/undo_history_panel.hpp"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace bl::ui {

UndoHistoryPanel::UndoHistoryPanel(QWidget* parent)
    : QDockWidget(tr("Undo History"), parent) {
    setObjectName(QStringLiteral("UndoHistoryPanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    statusLabel_ = new QLabel(tr("No undo stack connected"), container);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    list_ = new QListWidget(container);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    layout->addWidget(list_, 1);

    clearButton_ = new QPushButton(tr("Clear History"), container);
    clearButton_->setEnabled(false);
    layout->addWidget(clearButton_);

    setWidget(container);

    connect(list_, &QListWidget::itemClicked, this,
            &UndoHistoryPanel::onItemClicked);
    connect(clearButton_, &QPushButton::clicked, this, [this] {
        if (stack_) {
            stack_->clear();
            refresh();
        }
    });
}

UndoHistoryPanel::~UndoHistoryPanel() {
    if (stack_ && callbackId_ != 0) {
        stack_->removeStateCallback(callbackId_);
    }
}

void UndoHistoryPanel::setUndoStack(bl::UndoStack* stack) {
    if (stack_ && callbackId_ != 0) {
        stack_->removeStateCallback(callbackId_);
        callbackId_ = 0;
    }
    stack_ = stack;
    if (stack_) {
        callbackId_ = stack_->addStateCallback([this] { onStackChanged(); });
    }
    clearButton_->setEnabled(stack_ != nullptr);
    refresh();
}

void UndoHistoryPanel::refresh() {
    populateList();
}

void UndoHistoryPanel::onStackChanged() {
    // Use queued connection to ensure we're on the GUI thread
    QMetaObject::invokeMethod(this, [this] { populateList(); },
                              Qt::QueuedConnection);
}

void UndoHistoryPanel::populateList() {
    list_->clear();
    if (!stack_) {
        statusLabel_->setText(tr("No undo stack connected"));
        return;
    }

    const auto history = stack_->getHistory();
    const size_t count = stack_->count();
    const size_t index = stack_->index();

    statusLabel_->setText(
        tr("History: %1 commands, at position %2 of %3")
            .arg(static_cast<qlonglong>(count))
            .arg(static_cast<qlonglong>(index))
            .arg(static_cast<qlonglong>(count)));

    // Show newest at top (reverse order)
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        const auto& entry = *it;
        QString text;
        if (entry.isMacro) {
            text = QStringLiteral("📦 %1 (%2)")
                       .arg(QString::fromStdString(entry.name))
                       .arg(tr("macro"));
        } else {
            text = QString::fromStdString(entry.name);
        }

        // Add byte size info
        const double kb = entry.bytes / 1024.0;
        if (kb >= 1.0) {
            text += QStringLiteral("  (%1 KB)").arg(kb, 0, 'f', 1);
        } else {
            text += QStringLiteral("  (%1 B)").arg(static_cast<qlonglong>(entry.bytes));
        }

        // Mark current position
        if (entry.index + 1 == index) {
            text = QStringLiteral("▶ ") + text;
        }

        auto* item = new QListWidgetItem(text, list_);
        item->setData(Qt::UserRole, static_cast<qlonglong>(entry.index));
        item->setToolTip(tr("Click to jump to this state"));

        // Color the current state
        if (entry.index + 1 == index) {
            item->setBackground(QColor(0x4d, 0xa3, 0xf0, 60));
            item->setForeground(QColor(0xff, 0xff, 0xff));
        }

        // Gray out future states (redo stack)
        if (entry.index >= index) {
            item->setForeground(QColor(0x88, 0x88, 0x88));
        }

        // Add macro children as sub-items (indented)
        if (entry.isMacro && !entry.macroChildren.empty()) {
            for (const auto& childName : entry.macroChildren) {
                auto* childItem = new QListWidgetItem(
                    QStringLiteral("    ↳ %1").arg(QString::fromStdString(childName)), list_);
                childItem->setData(Qt::UserRole, static_cast<qlonglong>(entry.index));
                childItem->setForeground(QColor(0xaa, 0xaa, 0xaa));
                childItem->setFlags(childItem->flags() & ~Qt::ItemIsSelectable);
            }
        }
    }

    // Scroll to the current position
    if (index > 0 && index <= history.size()) {
        const size_t listIndex = history.size() - index;
        if (listIndex < static_cast<size_t>(list_->count())) {
            list_->scrollToItem(list_->item(static_cast<int>(listIndex)));
        }
    }
}

void UndoHistoryPanel::onItemClicked(QListWidgetItem* item) {
    if (!stack_ || !item) return;

    const bool isSelectable = item->flags() & Qt::ItemIsSelectable;
    if (!isSelectable) return; // Don't jump on macro children

    const size_t targetIndex = static_cast<size_t>(item->data(Qt::UserRole).toLongLong());
    emit jumpToIndexRequested(targetIndex + 1); // +1 because index_ points to next redo
}

} // namespace bl::ui