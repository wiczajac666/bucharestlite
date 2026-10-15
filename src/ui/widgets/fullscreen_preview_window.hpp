#pragma once

#include <QWidget>

class QVBoxLayout;

namespace bl::ui {

// Frameless top-level window that temporarily hosts the one PreviewPanel while
// the user watches a full-screen preview (e.g. on a second monitor). It owns no
// preview engine, transport or render tap of its own: MainWindow reparents the
// existing panel into it and hands it back on close, so there is still exactly
// one preview pipeline. The window itself is parented to MainWindow and deleted
// by it, so Qt::WA_DeleteOnClose stays off.
class FullScreenPreviewWindow : public QWidget {
    Q_OBJECT
public:
    explicit FullScreenPreviewWindow(QWidget* parent = nullptr);

    // Reparents `panel` into this window (takes temporary custody) and shows it.
    void setPreview(QWidget* panel);
    // Unparents and returns the hosted panel without deleting it.
    QWidget* takePreview();

signals:
    // Emitted when the user asks to leave full-screen: Escape or the window's
    // own close (Alt+F4 / window manager). MainWindow reacts by restoring.
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QVBoxLayout* layout_{nullptr};
    QWidget* panel_{nullptr};
};

} // namespace bl::ui
