#pragma once

#include "shortcuts/action_registry.hpp"

#include <QDialog>
#include <QString>

class QKeySequenceEdit;
class QSettings;
class QTableWidget;

namespace bl::ui {

// Editable table of the application's keyboard shortcuts. Double-clicking a
// keys cell installs an inline QKeySequenceEdit; committing the capture applies
// the binding to the registered action and persists it to QSettings. The
// capture entry points are public so offscreen tests can drive them directly.
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    ShortcutsDialog(ActionRegistry& registry, QSettings& settings,
                    QWidget* parent = nullptr);
    ~ShortcutsDialog() override;

    void beginCapture(const QString& id);
    void commitCapture();

private:
    void populateTable();
    int rowForId(const QString& id) const;
    void closeEditor();

    ActionRegistry& registry_;
    QSettings& settings_;
    QTableWidget* table_{nullptr};
    QKeySequenceEdit* editor_{nullptr};
    QString editingId_;
};

} // namespace bl::ui