#include "dialogs/batch_export_dialog.hpp"

#include "dialogs/export_dialog.hpp"
#include "export/export_worker.hpp"

#include <QAction>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace bl::ui {

namespace {

QString statusText(ExportJobStatus status) {
    switch (status) {
        case ExportJobStatus::Pending:
            return QObject::tr("Pending");
        case ExportJobStatus::Running:
            return QObject::tr("Running");
        case ExportJobStatus::Done:
            return QObject::tr("Done");
        case ExportJobStatus::Failed:
            return QObject::tr("Failed");
        case ExportJobStatus::Cancelled:
            return QObject::tr("Cancelled");
    }
    return QObject::tr("Pending");
}

QString describeJob(const ExportJob& job) {
    QString desc = QString::fromStdString(job.settings.container).toUpper() +
                   QLatin1String(" / ") +
                   QString::fromStdString(job.settings.videoCodec).toUpper();
    if (job.settings.videoCq > 0) {
        desc += QStringLiteral(" / CRF %1").arg(job.settings.videoCq);
    }
    return desc;
}

} // namespace

BatchExportDialog::BatchExportDialog(const AppContext& context,
                                     QWidget* parent)
    : QDialog(parent), context_(context) {
    setObjectName(QStringLiteral("BatchExportDialog"));
    setWindowTitle(tr("Batch Export"));
    setModal(true);
    resize(720, 420);

    auto* root = new QVBoxLayout(this);

    table_ = new QTableWidget(0, 3, this);
    table_->setObjectName(QStringLiteral("batchTable"));
    table_->setHorizontalHeaderLabels(
        {tr("Output file"), tr("Format"), tr("Status")});
    table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(table_, 1);

    auto* buttons = new QWidget(this);
    auto* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    auto* addButton = new QPushButton(tr("Add Job..."), buttons);
    auto* removeButton = new QPushButton(tr("Remove"), buttons);
    removeButton->setObjectName(QStringLiteral("batchRemove"));
    runButton_ = new QPushButton(tr("Run"), buttons);
    runButton_->setObjectName(QStringLiteral("batchRun"));
    runButton_->setDefault(true);
    closeButton_ = new QPushButton(tr("Close"), buttons);
    closeButton_->setObjectName(QStringLiteral("batchClose"));

    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(runButton_);
    buttonLayout->addWidget(closeButton_);
    root->addWidget(buttons);

    connect(addButton, &QPushButton::clicked, this, [this] {
        ExportDialog dialog(this);
        dialog.setWindowTitle(tr("Add Job"));
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        ExportJob job;
        job.id = "job-" +
                 std::to_string(QDateTime::currentMSecsSinceEpoch()) + "-" +
                 std::to_string(jobs_.size());
        job.settings = dialog.settings();
        jobs_.push_back(std::move(job));
        rebuildTable();
        persist();
    });

    connect(removeButton, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0 || row >= static_cast<int>(jobs_.size())) {
            return;
        }
        if (jobs_[row].status == ExportJobStatus::Running) {
            return; // let the render finish; cancelling is via Close
        }
        jobs_.erase(jobs_.begin() + row);
        rebuildTable();
        persist();
    });

    connect(closeButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(runButton_, &QPushButton::clicked, this, &BatchExportDialog::runAll);
}

void BatchExportDialog::setJobs(std::vector<ExportJob> jobs) {
    jobs_ = std::move(jobs);
    rebuildTable();
}

std::vector<ExportJob> BatchExportDialog::jobs() const {
    return jobs_;
}

void BatchExportDialog::addJob(const ExportJob& job) {
    jobs_.push_back(job);
    rebuildTable();
    persist();
}

int BatchExportDialog::jobCount() const {
    return static_cast<int>(jobs_.size());
}

ExportJobStatus BatchExportDialog::statusAt(int index) const {
    if (index < 0 ||
        index >= static_cast<int>(jobs_.size())) {
        return ExportJobStatus::Cancelled;
    }
    return jobs_[index].status;
}

void BatchExportDialog::rebuildTable() {
    table_->setRowCount(static_cast<int>(jobs_.size()));
    for (int i = 0; i < static_cast<int>(jobs_.size()); ++i) {
        updateRow(i, jobs_[i]);
    }
    const bool rendering = currentJob_ >= 0;
    runButton_->setEnabled(
        !rendering && std::any_of(jobs_.begin(), jobs_.end(),
                                  [](const ExportJob& j) {
                                      return j.needsRender();
                                  }));
    closeButton_->setText(rendering ? tr("Cancel") : tr("Close"));
}

void BatchExportDialog::updateRow(int row, const ExportJob& job) {
    auto* file =
        new QTableWidgetItem(QString::fromStdString(job.settings.outputPath));
    file->setToolTip(QString::fromStdString(job.message));
    auto* format = new QTableWidgetItem(describeJob(job));
    auto* status = new QTableWidgetItem(statusText(job.status));
    status->setTextAlignment(Qt::AlignCenter);
    table_->setItem(row, 0, file);
    table_->setItem(row, 1, format);
    table_->setItem(row, 2, status);
}

void BatchExportDialog::persist() {
    if (context_.queuePath.isEmpty()) {
        return;
    }
    saveExportQueue(jobs_, context_.queuePath.toStdString());
}

void BatchExportDialog::runAll() {
    if (context_.exportBusy && context_.exportBusy()) {
        QMessageBox::information(
            this, tr("Batch Export"),
            tr("Wait for the running export to finish first."));
        return;
    }
    if (currentJob_ >= 0) {
        return; // already rendering
    }

    // Resolve the shared render inputs once, on the GUI thread.
    if (!(context_.snapshotOf && context_.mediaBinOf && context_.pluginSpecOf)) {
        QMessageBox::warning(this, tr("Batch Export"),
                             tr("Missing app context; cannot render."));
        return;
    }
    const bl::TimelineSnapshot snapshot = context_.snapshotOf();
    const std::vector<bl::MediaBinItem> mediaBinValue = context_.mediaBinOf();
    const bl::MediaDecodeSource::Spec pluginSpecValue =
        context_.pluginSpecOf();

    bool any = false;
    for (ExportJob& job : jobs_) {
        if (job.needsRender()) {
            job.status = ExportJobStatus::Pending;
            job.message.clear();
            any = true;
        }
    }
    if (!any) {
        emit queueFinished();
        return;
    }
    ExportWorker::Request sharedRequest{ExportSettings{}, snapshot,
                                        mediaBinValue, pluginSpecValue};
    requestQueue_.emplace(std::move(sharedRequest));
    rebuildTable();
    persist();

    thread_ = new QThread(this);
    thread_->setObjectName(QStringLiteral("BatchExportThread"));
    worker_ = new ExportWorker();
    worker_->moveToThread(thread_);
    connect(worker_, &ExportWorker::finished, this,
            &BatchExportDialog::handleJobFinished);
    connect(worker_, &ExportWorker::progress, this,
            [this](int percent, int, int, double, double) {
                if (currentJob_ >= 0 &&
                    currentJob_ < static_cast<int>(jobs_.size())) {
                    const QString status = tr("Running %1%").arg(percent);
                    if (table_->item(currentJob_, 2)) {
                        table_->item(currentJob_, 2)->setText(status);
                    }
                }
            });
    thread_->start();
    cancelled_.store(false);
    startNextJob();
}

void BatchExportDialog::startNextJob() {
    // Only jobs that are still pending render in this chain; a job that just
    // failed is left Failed so the user can retry it with a fresh Run. That
    // avoids looping forever on a permanently broken job.
    for (int i = 0; i < static_cast<int>(jobs_.size()); ++i) {
        if (jobs_[i].status == ExportJobStatus::Pending) {
            currentJob_ = i;
            jobs_[i].status = ExportJobStatus::Running;
            rebuildTable();
            persist();

            ExportWorker::Request request = *requestQueue_;
            request.settings = jobs_[i].settings;
            request.settings.outputPath = jobs_[i].settings.outputPath;
            QMetaObject::invokeMethod(worker_, "run", Qt::QueuedConnection,
                                      Q_ARG(ExportWorker::Request, request));
            return;
        }
    }

    // Nothing left to render: the batch is finished.
    currentJob_ = -1;
    tearDownWorker();
    rebuildTable();
    persist();
    emit queueFinished();
}

void BatchExportDialog::handleJobFinished(bool ok, const QString& error) {
    if (currentJob_ < 0 || currentJob_ >= static_cast<int>(jobs_.size())) {
        tearDownWorker();
        return;
    }
    ExportJob& job = jobs_[currentJob_];
    if (cancelled_.load()) {
        job.status = ExportJobStatus::Cancelled;
        job.message = tr("Cancelled by user").toStdString();
    } else if (ok) {
        job.status = ExportJobStatus::Done;
        job.message = tr("Written to %1")
                          .arg(QString::fromStdString(job.settings.outputPath))
                          .toStdString();
    } else {
        job.status = ExportJobStatus::Failed;
        job.message = error.toStdString();
    }
    currentJob_ = -1;
    const bool anyLeft = std::any_of(
        jobs_.begin(), jobs_.end(),
        [](const ExportJob& j) {
            return j.status == ExportJobStatus::Pending;
        });
    rebuildTable();
    persist();
    if (!anyLeft) {
        tearDownWorker();
        emit queueFinished();
        return;
    }
    startNextJob();
}

void BatchExportDialog::tearDownWorker() {
    if (thread_) {
        thread_->quit();
        thread_->wait(5000);
        delete worker_;
        worker_ = nullptr;
        delete thread_;
        thread_ = nullptr;
    }
}

void BatchExportDialog::closeEvent(QCloseEvent* event) {
    if (currentJob_ >= 0) {
        const auto answer = QMessageBox::question(
            this, tr("Batch Export"),
            tr("An export is running. Cancel it and save the pending queue?"));
        if (answer == QMessageBox::No) {
            event->ignore();
            return;
        }
        cancelled_.store(true);
        if (worker_) {
            QMetaObject::invokeMethod(worker_, "cancel", Qt::QueuedConnection);
        }
    }
    persist();
    QDialog::closeEvent(event);
}

} // namespace bl::ui