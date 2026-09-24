#pragma once

#include "export/export_queue.hpp"
#include "export/export_worker.hpp"

#include <QDialog>
#include <QPushButton>
#include <QString>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QCloseEvent;
class QTableWidget;

namespace bl::ui {

class ExportDialog;

// Modal batch queue manager. Jobs are added/removed by the user, persisted to
// disk, and executed one at a time through an ExportWorker living on its own
// QThread. A failed job stays queued so the user can retry the remainder.
class BatchExportDialog : public QDialog {
    Q_OBJECT
public:
    // The dialog is UI-only; every dependency is captured lazily when Run is
    // pressed, so the caller supplies closures over the live app state rather
    // than snapshots that could go stale while the dialog is open.
    struct AppContext {
        std::function<bl::TimelineSnapshot()> snapshotOf;
        std::function<std::vector<bl::MediaBinItem>()> mediaBinOf;
        std::function<bl::MediaDecodeSource::Spec()> pluginSpecOf;
        std::function<bool()> exportBusy; // true while a single export runs
        QString queuePath;
    };

    explicit BatchExportDialog(const AppContext& context,
                               QWidget* parent = nullptr);

    void setJobs(std::vector<ExportJob> jobs);
    std::vector<ExportJob> jobs() const;

    // Programmatic access for tests: appends a job and refreshes the table.
    void addJob(const ExportJob& job);
    int jobCount() const;
    ExportJobStatus statusAt(int index) const;

public slots:
    void runAll(); // starts/continues rendering pending and failed jobs

signals:
    void queueFinished();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void rebuildTable();
    void updateRow(int row, const ExportJob& job);
    void persist();
    void startNextJob();
    void handleJobFinished(bool ok, const QString& error);
    void tearDownWorker();

    AppContext context_;
    std::vector<ExportJob> jobs_;
    QTableWidget* table_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;

    int currentJob_ = -1;
    QThread* thread_ = nullptr;
    ExportWorker* worker_ = nullptr;
    std::optional<ExportWorker::Request> requestQueue_;
    std::atomic<bool> cancelled_{false};
};

} // namespace bl::ui