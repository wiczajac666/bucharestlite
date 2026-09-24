#include "dialogs/export_progress_dialog.hpp"

#include "export/export_worker.hpp"

#include <QPushButton>
#include <QTimer>

namespace bl::ui {

ExportProgressDialog::ExportProgressDialog(QWidget* parent)
    : QProgressDialog(parent) {
    setObjectName(QStringLiteral("ExportProgressDialog"));
    setWindowTitle(tr("Exporting"));
    setLabelText(tr("Preparing..."));
    setMinimum(0);
    setMaximum(100);
    setMinimumDuration(200);
    setAutoClose(false);
    setAutoReset(false);
    setWindowModality(Qt::WindowModal);
    setCancelButtonText(tr("Cancel"));
}

void ExportProgressDialog::attach(ExportWorker* worker) {
    worker_ = worker;
    connect(this, &QProgressDialog::canceled, worker_,
            &ExportWorker::cancel);
    connect(worker_, &ExportWorker::progress, this,
            &ExportProgressDialog::updateProgress);
    connect(worker_, &ExportWorker::finished, this,
            &ExportProgressDialog::onFinished);
}

void ExportProgressDialog::updateProgress(int percent, int frame, int total,
                                          double fps, double eta) {
    setValue(percent);
    const QString speed = fps > 0
                              ? tr("%1 fps").arg(fps, 0, 'f', 1)
                              : QStringLiteral("...");
    const QString remaining =
        eta > 0 ? tr("%1 s remaining").arg(eta, 0, 'f', 0)
                : QStringLiteral("");
    setLabelText(tr("Frame %1 of %2  ·  %3  %4")
                     .arg(frame)
                     .arg(total)
                     .arg(speed)
                     .arg(remaining));
}

void ExportProgressDialog::onFinished(bool ok, const QString& error) {
    setValue(ok ? maximum() : 0);
    if (ok) {
        setLabelText(tr("Export complete."));
    } else {
        setLabelText(
            error.contains(QLatin1String("cancelled"),
                           Qt::CaseInsensitive)
                ? tr("Export cancelled.")
                : tr("Export failed: %1").arg(error));
    }
    // Let the user see the final state, then close.
    QTimer::singleShot(600, this, &QProgressDialog::accept);
}

} // namespace bl::ui