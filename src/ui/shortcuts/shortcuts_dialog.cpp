#include "shortcuts/shortcuts_dialog.hpp"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace bl::ui {

namespace {
constexpr int kIdRole = Qt::UserRole;
} // namespace

ShortcutsDialog::ShortcutsDialog(ActionRegistry& registry, QSettings& settings,
                                 QWidget* parent)
    : QDialog(parent), registry_(registry), settings_(settings) {
    setWindowTitle(tr("Keyboard Shortcuts"));

    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels(
        {tr("Action"), tr("Shortcut")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* resetAll = new QPushButton(tr("Reset All"), this);
    connect(resetAll, &QPushButton::clicked, this, [this] {
        registry_.resetAll();
        populateTable();
    });

    auto* closeButton = new QPushButton(tr("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(resetAll);
    buttons->addStretch();
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table_, 1);
    layout->addLayout(buttons);

    populateTable();

    connect(table_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem* item) {
        const QString id = item->data(kIdRole).toString();
        if (!id.isEmpty()) {
            beginCapture(id);
        }
    });
    connect(this, &QDialog::accepted, this, &ShortcutsDialog::commitCapture);
}

ShortcutsDialog::~ShortcutsDialog() = default;

void ShortcutsDialog::populateTable() {
    table_->clearContents();
    const auto shortcuts = registry_.shortcuts();
    table_->setRowCount(static_cast<int>(shortcuts.size()));
    for (int row = 0; row < static_cast<int>(shortcuts.size()); ++row) {
        const auto& shortcut = shortcuts[static_cast<size_t>(row)];
        auto* label = new QTableWidgetItem(shortcut.label);
        label->setData(kIdRole, shortcut.id);
        label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        const QKeySequence current = registry_.sequence(shortcut.id);
        auto* keys = new QTableWidgetItem(
            current.isEmpty() ? tr("(none)")
                              : current.toString(QKeySequence::NativeText));
        keys->setData(kIdRole, shortcut.id);
        keys->setFlags(keys->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 0, label);
        table_->setItem(row, 1, keys);
    }
    table_->resizeColumnsToContents();
}

int ShortcutsDialog::rowForId(const QString& id) const {
    for (int row = 0; row < table_->rowCount(); ++row) {
        QTableWidgetItem* item = table_->item(row, 0);
        if (item && item->data(kIdRole).toString() == id) {
            return row;
        }
    }
    return -1;
}

void ShortcutsDialog::beginCapture(const QString& id) {
    closeEditor();
    const int row = rowForId(id);
    if (row < 0) {
        return;
    }
    editingId_ = id;
    editor_ = new QKeySequenceEdit(
        registry_.sequence(id), table_);
    table_->setCellWidget(row, 1, editor_);
    table_->setCurrentCell(row, 1);
    editor_->setFocus();
}

void ShortcutsDialog::commitCapture() {
    if (!editor_) {
        return;
    }
    const QKeySequence sequence = editor_->keySequence();
    closeEditor();
    registry_.setOverride(editingId_, sequence);
    registry_.saveOverrides(settings_, QStringLiteral("shortcuts"));
    populateTable();
    editingId_.clear();
}

void ShortcutsDialog::closeEditor() {
    if (!editor_) {
        return;
    }
    const int row = rowForId(editingId_);
    if (row >= 0) {
        const QKeySequence current =
            editingId_.isEmpty()
                ? QKeySequence()
                : registry_.sequence(editingId_);
        table_->setItem(row, 1,
                        new QTableWidgetItem(
                            current.isEmpty()
                                ? tr("(none)")
                                : current.toString(QKeySequence::NativeText)));
        table_->setCellWidget(row, 1, nullptr);
    }
    delete editor_;
    editor_ = nullptr;
}

} // namespace bl::ui