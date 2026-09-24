#pragma once

#include <QProgressDialog>

namespace bl::ui {

class ExportWorker;

// Minimal modal progress overlay for a running export: shows frame, speed and
// ETA, with a Cancel button that asks the worker to abort cooperatively.
// The dialog is scriptable by tests through runExportModeless() aliases.
class ExportProgressDialog : public QProgressDialog {
    Q_OBJECT
public:
    explicit ExportProgressDialog(QWidget* parent = nullptr);

    void attach(ExportWorker* worker);

public slots:
    void updateProgress(int percent, int frame, int total, double fps,
                        double eta);
    void onFinished(bool ok, const QString& error);

private:
    void resetLabel();

    ExportWorker* worker_{nullptr};
};

} // namespace bl::ui