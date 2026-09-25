#include "app/main_window.hpp"

#include "app/project_controller.hpp"
#include "dialogs/batch_export_dialog.hpp"
#include "export/export_queue.hpp"
#include "export/export_worker.hpp"
#include "panels/timeline_panel.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QSettings>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>

#include <memory>
#include <utility>

Q_DECLARE_METATYPE(bl::ui::ExportWorker::Request)

namespace bl::ui {

namespace {
constexpr const char* kGeometryKey = "window/geometry";
constexpr const char* kStateKey = "window/state";
constexpr const char* kThemeKey = "window/theme";

QString untitledTitle() {
    return QStringLiteral("Untitled");
}
} // namespace

MainWindow::MainWindow(QWidget* parent, QSettings* settings) : QMainWindow(parent) {
    qRegisterMetaType<ExportWorker::Request>();

    if (settings) {
        settings_ = settings;
    } else {
        settings_ = new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                  QStringLiteral("BucharestLite"), QStringLiteral("bl_lite"));
        ownsSettings_ = true;
    }

    setWindowTitle(QStringLiteral("Bucharest Lite"));

    controller_ = new ProjectController(this);
    autosaveManager_ = new AutosaveManager(controller_, this);
    autosaveManager_->setIntervalSeconds(
        settings_->value(QStringLiteral("autosave/intervalSeconds"), 60).toInt());
    connect(autosaveManager_, &AutosaveManager::snapshotFailed,
            this, [this](const QString& message) {
        statusBar()->showMessage(tr("Autosave failed: %1").arg(message), 8000);
    });

    buildDocks();
    buildActions();
    buildMenuAndToolbar();
    connectController();

    installEventFilter(this);
    maybeRestoreLayout();
    updateTitle();
}

MainWindow::~MainWindow() {
    if (exportWorker_) {
        exportWorker_->cancel();
        exportThread_->quit();
        exportThread_->wait(3000);
        delete exportWorker_;
        delete exportThread_;
        exportWorker_ = nullptr;
        exportThread_ = nullptr;
    }
    if (ownsSettings_) {
        delete settings_;
    }
}

void MainWindow::buildDocks() {
    mediaBinPanel_ = new MediaBinPanel(controller_, this);
    mediaBin = new QDockWidget(tr("Media Bin"), this);
    mediaBin->setObjectName(QStringLiteral("MediaBinDock"));
    mediaBin->setWidget(mediaBinPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, mediaBin);

    preview = new QDockWidget(tr("Preview"), this);
    preview->setObjectName(QStringLiteral("PreviewDock"));
    previewPanel_ = new PreviewPanel(controller_, this);
    preview->setWidget(previewPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, preview);

    inspector = new QDockWidget(tr("Inspector"), this);
    inspector->setObjectName(QStringLiteral("InspectorDock"));
    inspectorPanel_ = new InspectorPanel(controller_, this);
    inspector->setWidget(inspectorPanel_);
    addDockWidget(Qt::RightDockWidgetArea, inspector);

    mixer = new QDockWidget(tr("Mixer"), this);
    mixer->setObjectName(QStringLiteral("MixerDock"));
    auto* mixerHost = new QWidget(this);
    auto* mixerLayout = new QVBoxLayout(mixerHost);
    mixerLayout->setContentsMargins(0, 0, 0, 0);
    mixerPanel_ = new MixerPanel(controller_, mixerHost);
    masterFaderPanel_ = new MasterFaderPanel(controller_, mixerHost);
    mixerLayout->addWidget(mixerPanel_, 1);
    mixerLayout->addWidget(masterFaderPanel_, 0);
    mixer->setWidget(mixerHost);
    addDockWidget(Qt::RightDockWidgetArea, mixer);

    timeline = new QDockWidget(tr("Timeline"), this);
    timeline->setObjectName(QStringLiteral("TimelineDock"));
    timelinePanel_ = new TimelinePanel(controller_, this);
    timeline->setWidget(timelinePanel_);
    addDockWidget(Qt::BottomDockWidgetArea, timeline);

    // Keep the preview transport and the timeline playhead in lockstep: a
    // ruler scrub pulls the preview along; transport playback pushes the
    // timeline playhead forward.
    connect(previewPanel_, &PreviewPanel::playheadChanged, timelinePanel_,
            &TimelinePanel::setPlayhead);
    connect(timelinePanel_, &TimelinePanel::playheadChanged, previewPanel_,
            &PreviewPanel::setPlayheadFromTimeline);
    connect(timelinePanel_, &TimelinePanel::timelineChanged, previewPanel_,
            &PreviewPanel::onTimelineChanged);

    // Feed timeline clip selection into the inspector.
    connect(timelinePanel_, &TimelinePanel::selectionChanged, this, [this] {
        inspectorPanel_->showSelection(timelinePanel_->selection());
    });
}

void MainWindow::buildActions() {
    auto* newProject = new QAction(tr("New Project"), this);
    newProject->setShortcut(QKeySequence::New);
    connect(newProject, &QAction::triggered, this, [this] {
        if (!confirmClose()) {
            return;
        }
        const QString name = QInputDialog::getText(this, tr("New Project"),
                                                   tr("Project name:"), QLineEdit::Normal,
                                                   untitledTitle());
        if (!name.isEmpty()) {
            controller_->newProject(name);
            maybePromptRecovery();
        }
    });

    auto* open = new QAction(tr("Open..."), this);
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        if (!confirmClose()) {
            return;
        }
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Open Project"), QString(),
                                         tr("Bucharest Projects (*.blproj);;All Files (*)"));
        if (!path.isEmpty()) {
            if (controller_->open(path).ok()) {
                maybePromptRecovery();
            }
        }
    });

    auto* save = new QAction(tr("Save"), this);
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, [this] { doSave(); });

    auto* saveAs = new QAction(tr("Save As..."), this);
    saveAs->setShortcut(QKeySequence::SaveAs);
    connect(saveAs, &QAction::triggered, this, [this] { doSaveAs(); });

    exportAction_ = new QAction(tr("Export..."), this);
    exportAction_->setObjectName(QStringLiteral("actionExport"));
    exportAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(exportAction_, &QAction::triggered, this,
            &MainWindow::startExport);

    batchExportAction_ = new QAction(tr("Batch Export..."), this);
    batchExportAction_->setObjectName(QStringLiteral("actionBatchExport"));
    connect(batchExportAction_, &QAction::triggered, this,
            &MainWindow::startBatchExport);

    auto* quit = new QAction(tr("Quit"), this);
    quit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(quit, &QAction::triggered, this, &QWidget::close);

    undoAction_ = new QAction(tr("Undo"), this);
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] { controller_->undoStack().undo(); });

    redoAction_ = new QAction(tr("Redo"), this);
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] { controller_->undoStack().redo(); });

    darkThemeAction_ = new QAction(tr("Dark Theme"), this);
    darkThemeAction_->setCheckable(true);
    lightThemeAction_ = new QAction(tr("Light Theme"), this);
    lightThemeAction_->setCheckable(true);
    connect(darkThemeAction_, &QAction::triggered, this, [this] { applyTheme(Theme::Dark); });
    connect(lightThemeAction_, &QAction::triggered, this, [this] { applyTheme(Theme::Light); });

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(newProject);
    fileMenu->addAction(open);
    fileMenu->addAction(save);
    fileMenu->addAction(saveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(exportAction_);
    fileMenu->addAction(batchExportAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(quit);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(mediaBin->toggleViewAction());
    viewMenu->addAction(preview->toggleViewAction());
    viewMenu->addAction(inspector->toggleViewAction());
    viewMenu->addAction(mixer->toggleViewAction());
    viewMenu->addAction(timeline->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(darkThemeAction_);
    viewMenu->addAction(lightThemeAction_);

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolBar"));
    toolbar->setMovable(false);
    toolbar->addAction(newProject);
    toolbar->addAction(open);
    toolbar->addAction(save);
    toolbar->addAction(saveAs);
    toolbar->addAction(exportAction_);
    toolbar->addSeparator();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);

    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
}

void MainWindow::buildMenuAndToolbar() {
    statusBar()->showMessage(QStringLiteral("Ready"));
}

void MainWindow::startBatchExport() {
    BatchExportDialog::AppContext context;
    context.snapshotOf = [this] { return controller_->timeline().snapshot(); };
    context.mediaBinOf = [this] { return controller_->mediaBin(); };
    context.pluginSpecOf = [this] {
        bl::MediaDecodeSource::Spec spec;
        spec.pluginDirs = previewPanel_->pluginDirs();
        return spec;
    };
    context.exportBusy = [this] { return exportBusy(); };
    context.queuePath =
        QString::fromStdString(defaultExportQueuePath());

    BatchExportDialog dialog(context, this);
    dialog.setJobs(loadExportQueue(context.queuePath.toStdString()));
    dialog.exec();
}

bool MainWindow::exportBusy() const {
    return exportBusy_;
}

void MainWindow::ensureExportWorker() {
    if (exportWorker_) {
        return;
    }
    exportThread_ = new QThread(this);
    exportThread_->setObjectName(QStringLiteral("ExportThread"));
    exportWorker_ = new ExportWorker();
    exportWorker_->moveToThread(exportThread_);
    exportThread_->start();
}

void MainWindow::startExport() {
    if (exportBusy()) {
        statusBar()->showMessage(tr("An export is already running."), 3000);
        return;
    }

    if (controller_->mediaBin().empty()) {
        QMessageBox::information(this, tr("Export"),
                                 tr("Add media to the media bin first."));
        return;
    }
    if (controller_->timeline().sequence().videoTracks.empty()) {
        QMessageBox::information(this, tr("Export"),
                                 tr("The timeline has no video tracks yet."));
        return;
    }

    ExportDialog dialog(this);
    dialog.setSequenceDefaults(
        controller_->timeline().sequence().settings);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    ensureExportWorker();
    exportBusy_ = true;
    exportAction_->setEnabled(false);

    ExportWorker::Request request{dialog.settings(),
                                  controller_->timeline().snapshot(),
                                  controller_->mediaBin(),
                                  previewPanel_->pluginDirs()};

    auto* progress = new ExportProgressDialog(this);
    progress->attach(exportWorker_);
    QObject::connect(exportWorker_, &ExportWorker::finished, this,
                     [this, progress](bool ok, const QString&) {
                         exportBusy_ = false;
                         exportAction_->setEnabled(true);
                         statusBar()->showMessage(
                             ok ? tr("Export complete.")
                                : tr("Export stopped."),
                             5000);
                         progress->deleteLater();
                     });

    QMetaObject::invokeMethod(exportWorker_, "run", Qt::QueuedConnection,
                              Q_ARG(ExportWorker::Request, request));
    progress->exec();
}

void MainWindow::connectController() {
    connect(controller_, &ProjectController::projectChanged, this, &MainWindow::updateTitle);
    connect(controller_, &ProjectController::dirtyChanged, this, &MainWindow::updateTitle);
    connect(controller_, &ProjectController::undoChanged, this, [this] {
        undoAction_->setEnabled(controller_->canUndo());
        redoAction_->setEnabled(controller_->canRedo());
    });
    connect(controller_, &ProjectController::statusMessage,
            this, [this](const QString& message) { statusBar()->showMessage(message, 3000); });
}

void MainWindow::updateTitle() {
    QString title = controller_->projectName();
    if (title.isEmpty()) {
        title = untitledTitle();
    }
    if (controller_->dirty()) {
        title += QStringLiteral(" *");
    }
    title += QStringLiteral(" — Bucharest Lite");
    setWindowTitle(title);
}

void MainWindow::toggleTheme() {
    applyTheme(theme_ == Theme::Dark ? Theme::Light : Theme::Dark);
}

void MainWindow::applyTheme(Theme theme) {
    theme_ = theme;
    if (darkThemeAction_) {
        darkThemeAction_->setChecked(theme == Theme::Dark);
    }
    if (lightThemeAction_) {
        lightThemeAction_->setChecked(theme == Theme::Light);
    }
    qApp->setStyleSheet(themeStylesheet(theme));
}

void MainWindow::persistLayout() {
    Q_ASSERT(settings_);
    settings_->setValue(kGeometryKey, saveGeometry());
    settings_->setValue(kStateKey, saveState());
    settings_->setValue(kThemeKey, themeKey(theme_));
}

void MainWindow::maybeRestoreLayout() {
    Q_ASSERT(settings_);
    const QByteArray geometry = settings_->value(kGeometryKey).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QByteArray state = settings_->value(kStateKey).toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }
    const QString theme = settings_->value(kThemeKey).toString();
    applyTheme(theme == QStringLiteral("light") ? Theme::Light : Theme::Dark);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmClose()) {
        persistLayout();
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::confirmClose() {
    // Flush the latest state to the autosave ring before any discard prompt,
    // so a crash while closing still leaves recoverable work behind.
    autosaveManager_->maybeAutosave();
    if (!promptOnClose_ || !controller_->dirty()) {
        return true;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("The project has unsaved changes. Save them before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (choice) {
    case QMessageBox::Save:
        return doSave();
    case QMessageBox::Discard:
        return true;
    default:
        return false;
    }
}

bool MainWindow::doSave() {
    if (controller_->filePath().isEmpty()) {
        return doSaveAs();
    }
    return controller_->save().ok();
}

bool MainWindow::doSaveAs() {
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Save Project"), QString(),
                                     tr("Bucharest Projects (*.blproj);;All Files (*)"));
    if (path.isEmpty()) {
        return false;
    }
    return controller_->saveAs(path).ok();
}

void MainWindow::setAutosaveRoot(const QString& root) {
    autosaveManager_->setAutosaveRoot(root);
}

void MainWindow::setRecoveryDecider(std::function<bool()> decider) {
    recoveryDecider_ = std::move(decider);
}

bool MainWindow::maybePromptRecovery() {
    const QString autosave = autosaveManager_->autosaveNewerThanMain();
    if (autosave.isEmpty()) {
        return false;
    }

    bool restore = false;
    if (recoveryDecider_) {
        restore = recoveryDecider_();
    } else {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this, tr("Recover unsaved changes"),
            tr("An autosave of this project is newer than the last saved "
               "version. Restore the autosaved changes?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        restore = choice == QMessageBox::Yes;
    }
    if (!restore) {
        return false;
    }

    const auto result = controller_->restoreFromAutosave(
        autosave, controller_->filePath());
    if (!result.ok()) {
        statusBar()->showMessage(
            tr("Recovery failed: %1").arg(QString::fromStdString(result.message())),
            8000);
        return false;
    }
    return true;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == this && event->type() == QEvent::WindowDeactivate) {
        autosaveManager_->maybeAutosave();
    }
    return QMainWindow::eventFilter(watched, event);
}

int MainWindow::dockCount() const {
    return findChildren<QDockWidget*>().size();
}

QDockWidget* MainWindow::dock(const QString& title) const {
    const auto docks = findChildren<QDockWidget*>();
    for (QDockWidget* d : docks) {
        if (d->windowTitle() == title) {
            return d;
        }
    }
    return nullptr;
}

} // namespace bl::ui