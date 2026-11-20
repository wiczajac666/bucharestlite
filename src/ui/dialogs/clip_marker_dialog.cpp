#include "dialogs/clip_marker_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace bl::ui {

ClipMarkerDialog::ClipMarkerDialog(QString label, QString comment,
                                   uint32_t color, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Clip Marker"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Label:"), this));
    labelEdit_ = new QLineEdit(label, this);
    labelEdit_->setObjectName(QStringLiteral("clipMarkerLabelEdit"));
    labelEdit_->selectAll();
    layout->addWidget(labelEdit_);

    layout->addWidget(new QLabel(tr("Comment:"), this));
    commentEdit_ = new QPlainTextEdit(comment, this);
    commentEdit_->setObjectName(QStringLiteral("clipMarkerCommentEdit"));
    commentEdit_->setMaximumHeight(120);
    layout->addWidget(commentEdit_);

    // The eight entries mirror ClipItem's color-label palette so a marker can
    // share its clip's tag color.
    layout->addWidget(new QLabel(tr("Color:"), this));
    colorCombo_ = new QComboBox(this);
    colorCombo_->setObjectName(QStringLiteral("clipMarkerColorCombo"));
    const QString names[] = {tr("Slate"),   tr("Blue"),   tr("Green"),
                             tr("Amber"),   tr("Red"),    tr("Purple"),
                             tr("Teal"),    tr("Magenta")};
    for (int i = 0; i < 8; ++i) {
        colorCombo_->addItem(names[i], i);
    }
    colorCombo_->setCurrentIndex(static_cast<int>(color % 8));
    layout->addWidget(colorCombo_);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(QStringLiteral("clipMarkerButtons"));
    buttons_->button(QDialogButtonBox::Ok)->setObjectName(
        QStringLiteral("clipMarkerOk"));
    buttons_->button(QDialogButtonBox::Cancel)->setObjectName(
        QStringLiteral("clipMarkerCancel"));
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString ClipMarkerDialog::label() const { return labelEdit_->text().trimmed(); }

QString ClipMarkerDialog::comment() const {
    return commentEdit_->toPlainText();
}

uint32_t ClipMarkerDialog::color() const {
    return static_cast<uint32_t>(colorCombo_->currentData().toInt());
}

} // namespace bl::ui
