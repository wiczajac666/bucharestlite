#pragma once

#include <bl_core/time.hpp>

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;

namespace bl::ui {

// Dialog to set a clip duration numerically. The duration is edited as
// HH:MM:SS:FF timecode at the given sequence fps.
class DurationDialog : public QDialog {
    Q_OBJECT
public:
    DurationDialog(bl::Duration initial, bl::Rational fps,
                   QWidget* parent = nullptr);

    bl::Duration duration() const { return duration_; }

    static QString format(bl::Duration d, bl::Rational fps);
    static bool parse(const QString& text, bl::Rational fps, bl::Duration& out);

private slots:
    void onAccept();

private:
    void updateError(const QString& msg);

    bl::Rational fps_;
    bl::Duration duration_;
    QLineEdit* edit_{nullptr};
    QLabel* errorLabel_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
};

} // namespace bl::ui