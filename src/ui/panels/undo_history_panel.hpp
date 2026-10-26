#pragma once

#include <bl_core/undo_stack.hpp>

#include <QDockWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

class QLabel;
class QPushButton;
class QAbstractItemView;

namespace bl::ui {

class UndoHistoryPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit UndoHistoryPanel(QWidget* parent = nullptr);
    ~UndoHistoryPanel() override;

    // Set the undo stack to observe. The panel subscribes to the stack's
    // state notifications without disturbing the primary callback that
    // ProjectController installs for dirty-state/action updates.
    void setUndoStack(bl::UndoStack* stack);

    // Refresh the list from the undo stack.
    void refresh();

signals:
    // Emitted when the user clicks a history entry to jump to it.
    void jumpToIndexRequested(size_t index);

private:
    void onStackChanged();
    void onItemClicked(QListWidgetItem* item);
    void populateList();

    bl::UndoStack* stack_{nullptr};
    bl::UndoStack::StateCallbackId callbackId_{0};
    QListWidget* list_{nullptr};
    QLabel* statusLabel_{nullptr};
    QPushButton* clearButton_{nullptr};
};

} // namespace bl::ui