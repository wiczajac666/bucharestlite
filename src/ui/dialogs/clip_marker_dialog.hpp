#pragma once

#include <QDialog>

#include <cstdint>
#include <QString>

class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QPlainTextEdit;

namespace bl::ui {

// Editor for a clip marker's label, comment and color. Pure data in / out: the
// caller applies the result through TimelinePanel so the edit lands on the
// undo stack. Widgets carry objectNames so offscreen tests can drive the dialog
// without relying on modal event loops.
class ClipMarkerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ClipMarkerDialog(QString label = QString(),
                              QString comment = QString(),
                              uint32_t color = 3,
                              QWidget* parent = nullptr);

    QString label() const;
    QString comment() const;
    uint32_t color() const;

private:
    QLineEdit* labelEdit_{nullptr};
    QPlainTextEdit* commentEdit_{nullptr};
    QComboBox* colorCombo_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
};

} // namespace bl::ui
