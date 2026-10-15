#include "widgets/fullscreen_preview_window.hpp"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QVBoxLayout>

namespace bl::ui {

FullScreenPreviewWindow::FullScreenPreviewWindow(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("FullScreenPreviewWindow"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(tr("Preview"));

    // MainWindow deletes this window on restore; a self-delete would dangle the
    // pointer it keeps while detached.
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAutoFillBackground(true);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
}

void FullScreenPreviewWindow::setPreview(QWidget* panel) {
    if (panel_ == panel) {
        return;
    }
    if (panel_) {
        takePreview();
    }
    panel_ = panel;
    if (!panel_) {
        return;
    }
    panel_->setParent(this);
    layout_->addWidget(panel_, 1);
    panel_->show();
}

QWidget* FullScreenPreviewWindow::takePreview() {
    QWidget* panel = panel_;
    panel_ = nullptr;
    if (panel) {
        layout_->removeWidget(panel);
        panel->setParent(nullptr);
    }
    return panel;
}

void FullScreenPreviewWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && !event->modifiers()) {
        event->accept();
        emit closeRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

void FullScreenPreviewWindow::closeEvent(QCloseEvent* event) {
    emit closeRequested();
    event->accept();
}

} // namespace bl::ui
