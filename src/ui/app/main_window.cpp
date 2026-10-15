#include "app/main_window.hpp"

#include "app/project_controller.hpp"
#include "dialogs/batch_export_dialog.hpp"
#include "dialogs/quality_metrics_dialog.hpp"
#include "dialogs/template_picker_dialog.hpp"
#include "dialogs/title_editor_dialog.hpp"
#include "export/export_queue.hpp"
#include "export/export_worker.hpp"
#include "panels/timeline_panel.hpp"
#include "shortcuts/shortcuts_dialog.hpp"
#include "title/title_renderer.hpp"
#include "widgets/fullscreen_preview_window.hpp"

#include <bl_core/time.hpp>
#include <bl_timeline/project_template.hpp>
#include <bl_timeline/subtitle_import.hpp>
#include <bl_timeline/title.hpp>

#include <nlohmann/json.hpp>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <utility>

Q_DECLARE_METATYPE(bl::ui::ExportWorker::Request)

namespace bl::ui {

namespace {
constexpr const char* kGeometryKey = "window/geometry";
constexpr const char* kStateKey = "window/state";
constexpr const char* kThemeKey = "window/theme";
// Bumped whenever the built-in dock layout changes so a state saved by an
// older build does not resurrect the previous arrangement.
constexpr const char* kLayoutVersionKey = "window/layoutVersion";
constexpr int kLayoutVersion = 2;
constexpr const char* kLaneOrderKey = "timeline/laneOrder";
constexpr const char* kEditModeKey = "timeline/editMode";
constexpr const char* kInsertModeKey = "timeline/insertMode";
constexpr const char* kRippleKey = "timeline/ripple";
constexpr const char* kSnapKey = "timeline/snap";

QString untitledTitle() {
    return QStringLiteral("Untitled");
}

// Media-bin display name for a title: the first line of its text, trimmed
// (callers substitute a fallback when this is empty).
QString firstLineOfTitle(const bl::TitleSpec& spec) {
    return QString::fromStdString(spec.text)
        .section(QLatin1Char('\n'), 0, 0)
        .trimmed();
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
    if (fullScreenPreviewWindow_) {
        // Put the single preview panel back in its dock before the widget tree
        // unwinds, so it is torn down in the usual place.
        if (QWidget* panel = fullScreenPreviewWindow_->takePreview()) {
            if (preview) {
                preview->setWidget(panel);
            }
        }
        delete fullScreenPreviewWindow_;
        fullScreenPreviewWindow_ = nullptr;
    }
    if (meterEngine_) {
        // Stop callbacks and cancel in-flight analysis before panels tear down.
        meterEngine_->onAnalyzed = nullptr;
        meterEngine_->clear();
    }
    if (thumbnailEngine_) {
        // Stop callbacks and cancel in-flight decodes before panels tear down.
        thumbnailEngine_->onReady = nullptr;
        thumbnailEngine_->clear();
    }
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
    // Two-row default layout:
    //   top row    : Media Bin | Preview | Scopes
    //   bottom row : Timeline (full width)
    // Inspector and Mixer still exist and are reachable from the View menu,
    // but stay hidden so the workspace above the timeline stays compact.
    mediaBinPanel_ = new MediaBinPanel(controller_, this);
    mediaBin = new QDockWidget(tr("Media Bin"), this);
    mediaBin->setObjectName(QStringLiteral("MediaBinDock"));
    mediaBin->setWidget(mediaBinPanel_);

    preview = new QDockWidget(tr("Preview"), this);
    preview->setObjectName(QStringLiteral("PreviewDock"));
    previewPanel_ = new PreviewPanel(controller_, this);
    preview->setWidget(previewPanel_);

    scopes = new QDockWidget(tr("Scopes"), this);
    scopes->setObjectName(QStringLiteral("ScopesDock"));
    scopesPanel_ = new ScopesPanel(scopes);
    scopes->setWidget(scopesPanel_);

    // Top row: three docks side by side (splitting the top area, so the
    // timeline below keeps the full window width).
    addDockWidget(Qt::TopDockWidgetArea, mediaBin);
    splitDockWidget(mediaBin, preview, Qt::Horizontal);
    splitDockWidget(preview, scopes, Qt::Horizontal);

    timeline = new QDockWidget(tr("Timeline"), this);
    timeline->setObjectName(QStringLiteral("TimelineDock"));
    timelinePanel_ = new TimelinePanel(controller_, this);
    timeline->setWidget(timelinePanel_);
    addDockWidget(Qt::BottomDockWidgetArea, timeline);

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

    // Off the default workspace: the right-hand docks stay hidden until the
    // user enables them from the View menu (Ctrl+3 / Ctrl+4).
    inspector->hide();
    mixer->hide();

    // Give the preview the widest slice of the top row and the timeline a
    // generous strip at the bottom; the user can still drag the splitters.
    resizeDocks({mediaBin, preview, scopes}, {360, 640, 360}, Qt::Horizontal);
    resizeDocks({timeline}, {420}, Qt::Vertical);

    // Keep the preview transport and the timeline playhead in lockstep: a
    // ruler scrub pulls the preview along; transport playback pushes the
    // timeline playhead forward.
    connect(previewPanel_, &PreviewPanel::playheadChanged, timelinePanel_,
            &TimelinePanel::setPlayhead);
    connect(timelinePanel_, &TimelinePanel::playheadChanged, previewPanel_,
            &PreviewPanel::setPlayheadFromTimeline);
    connect(timelinePanel_, &TimelinePanel::timelineChanged, previewPanel_,
            &PreviewPanel::onTimelineChanged);

    // Scopes sit on the post-composite preview frame.
    connect(previewPanel_, &PreviewPanel::frameRendered, scopesPanel_,
            &ScopesPanel::setFrame);

    // Feed timeline clip selection into the inspector.
    connect(timelinePanel_, &TimelinePanel::selectionChanged, this, [this] {
        inspectorPanel_->showSelection(timelinePanel_->selection());
    });

    // Media-bin context menu / double-click → append the item to the first
    // compatible lane (no drag needed). The timeline reports its own
    // refusals through the status bar.
    connect(mediaBinPanel_, &MediaBinPanel::addToTimelineRequested,
            timelinePanel_, [this](const std::string& mediaId) {
                timelinePanel_->addMediaToTimeline(mediaId);
            });

    // Re-open a title's stored design from the media bin or from the clip it
    // sits on; both funnel into the same editor.
    connect(mediaBinPanel_, &MediaBinPanel::editTitleRequested, this,
            &MainWindow::editTitleClip);
    connect(timelinePanel_, &TimelinePanel::editTitleRequested, this,
            &MainWindow::editTitleClip);

    // Persist the track-layout choice as soon as it changes (restored in
    // maybeRestoreLayout) so it survives even an abnormal shutdown.
    connect(timelinePanel_, &TimelinePanel::laneOrderChanged, this,
            [this](timeline_geometry::LaneOrder order) {
                settings_->setValue(kLaneOrderKey, static_cast<int>(order));
            });

    // Persist the edit state (mode + insert/overwrite + ripple + snap) on every
    // change so it survives even an abnormal shutdown.
    connect(timelinePanel_, &TimelinePanel::timelineStateChanged, this, [this] {
        const TimelineState& state = timelinePanel_->timelineState();
        settings_->setValue(kEditModeKey,
                            static_cast<int>(state.editMode));
        settings_->setValue(kInsertModeKey, state.insertMode);
        settings_->setValue(kRippleKey, state.ripple);
        settings_->setValue(kSnapKey, state.snap);
        settings_->setValue("timeline/autoSplitAv", state.autoSplitAv);
    });

    setupMeterEngine();
    setupThumbnailEngine();
}

void MainWindow::setupMeterEngine() {
    meterEngine_ = std::make_unique<bl::AudioMeterEngine>();
    meterEngine_->configure(previewPanel_->pluginDirs());
    meterEngine_->onAnalyzed = [this](const std::string&) {
        // Analysis settled on a worker thread; marshal repaints onto the panel
        // objects' thread (the GUI thread).
        QMetaObject::invokeMethod(mixerPanel_, [this] {
            if (mixerPanel_) mixerPanel_->onUpdateMeters();
        }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(masterFaderPanel_, [this] {
            if (masterFaderPanel_) masterFaderPanel_->onUpdateMeters();
        }, Qt::QueuedConnection);
        // Waveforms ride the same analysis: repaint each timeline clip's shape
        // now that its envelope has settled (no full rebuild, which would
        // cancel an in-progress drag).
        QMetaObject::invokeMethod(timelinePanel_, [this] {
            if (timelinePanel_) timelinePanel_->refreshWaveforms();
        }, Qt::QueuedConnection);
    };

    const auto resolver =
        [this](const std::string& mediaItemId)
        -> std::shared_ptr<const bl::AudioMeterEnvelope> {
        return meterEngine_ ? meterEngine_->envelopeFor(mediaItemId) : nullptr;
    };
    mixerPanel_->setMeterResolver(resolver);
    masterFaderPanel_->setMeterResolver(resolver);
    if (timelinePanel_) timelinePanel_->setWaveformResolver(resolver);

    // Meters follow every playhead move (playback ticks and ruler scrubs alike
    // funnel through PreviewPanel::playheadChanged).
    connect(previewPanel_, &PreviewPanel::playheadChanged, mixerPanel_,
            &MixerPanel::onPlayheadChanged);
    connect(previewPanel_, &PreviewPanel::playheadChanged, masterFaderPanel_,
            &MasterFaderPanel::onPlayheadChanged);

    syncMeterAnalysis();
}

void MainWindow::syncMeterAnalysis() {
    if (!meterEngine_ || !controller_) return;

    const auto& bin = controller_->mediaBin();
    const auto requested = [&bin] {
        std::vector<std::string> ids;
        ids.reserve(bin.size());
        for (const auto& item : bin) ids.push_back(item.id);
        return ids;
    }();

    for (const auto& id : meterEngine_->mediaIds()) {
        if (std::find(requested.begin(), requested.end(), id) ==
            requested.end()) {
            meterEngine_->remove(id);
        }
    }
    meterEngine_->analyzeMedia(bin);
}

void MainWindow::setupThumbnailEngine() {
    thumbnailEngine_ = std::make_unique<bl::MediaBinThumbnailEngine>();
    thumbnailEngine_->configure(previewPanel_->pluginDirs());
    thumbnailEngine_->onReady = [this](const std::string& mediaItemId) {
        // Decoding settled on a worker thread; marshal the row update onto the
        // panel objects' thread (the GUI thread).
        QMetaObject::invokeMethod(this,
                                  [this, mediaItemId] { applyThumbnail(mediaItemId); },
                                  Qt::QueuedConnection);
    };

    syncThumbnailJobs();
}

void MainWindow::syncThumbnailJobs() {
    if (!thumbnailEngine_ || !controller_ || !mediaBinPanel_) return;

    const auto& bin = controller_->mediaBin();
    const auto requested = [&bin] {
        std::vector<std::string> ids;
        ids.reserve(bin.size());
        for (const auto& item : bin) ids.push_back(item.id);
        return ids;
    }();

    for (const auto& id : thumbnailEngine_->mediaIds()) {
        if (std::find(requested.begin(), requested.end(), id) ==
            requested.end()) {
            thumbnailEngine_->remove(id);
        }
    }
    thumbnailEngine_->requestMedia(bin);

    // reload() has already rebuilt the rows (it is connected to the same
    // signal); re-apply anything cached so a reload does not blank icons.
    for (const auto& id : requested) {
        if (thumbnailEngine_->thumbnailFor(id)) {
            applyThumbnail(id);
        }
    }
}

void MainWindow::applyThumbnail(const std::string& mediaItemId) {
    if (!thumbnailEngine_ || !mediaBinPanel_) return;
    const auto thumbnail = thumbnailEngine_->thumbnailFor(mediaItemId);
    if (!thumbnail) return;

    QImage image;
    if (thumbnail->hasPixels()) {
        const QImage raw(reinterpret_cast<const uchar*>(thumbnail->pixels.data()),
                         static_cast<int>(thumbnail->width),
                         static_cast<int>(thumbnail->height),
                         static_cast<int>(thumbnail->linesize),
                         QImage::Format_RGB32);
        image = raw.copy();
    }

    mediaBinPanel_->applyMediaInfo(
        mediaItemId, image,
        QString::fromStdString(bl::describeMedia(thumbnail->info)));
}

void MainWindow::buildActions() {
    auto registerAction = [this](QAction* action, const QString& id,
                                 const QString& label,
                                 const QKeySequence& sequence) {
        actionRegistry_.registerAction(action, id, label, sequence);
    };

    auto* newProject = new QAction(tr("New Project"), this);
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
    registerAction(newProject, "file.new", tr("New Project"),
                   QKeySequence::New);

    newFromTemplateAction_ = new QAction(tr("New from Template..."), this);
    newFromTemplateAction_->setObjectName(
        QStringLiteral("actionNewFromTemplate"));
    connect(newFromTemplateAction_, &QAction::triggered, this,
            &MainWindow::newFromTemplate);
    registerAction(newFromTemplateAction_, "file.new_from_template",
                   tr("New from Template..."), QKeySequence());

    auto* open = new QAction(tr("Open..."), this);
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
    registerAction(open, "file.open", tr("Open..."), QKeySequence::Open);

    auto* save = new QAction(tr("Save"), this);
    connect(save, &QAction::triggered, this, [this] { doSave(); });
    registerAction(save, "file.save", tr("Save"), QKeySequence::Save);

    auto* saveAs = new QAction(tr("Save As..."), this);
    connect(saveAs, &QAction::triggered, this, [this] { doSaveAs(); });
    registerAction(saveAs, "file.save_as", tr("Save As..."),
                   QKeySequence::SaveAs);

    exportAction_ = new QAction(tr("Export..."), this);
    exportAction_->setObjectName(QStringLiteral("actionExport"));
    connect(exportAction_, &QAction::triggered, this,
            &MainWindow::startExport);
    registerAction(exportAction_, "file.export", tr("Export..."),
                   QKeySequence(Qt::CTRL | Qt::Key_E));

    batchExportAction_ = new QAction(tr("Batch Export..."), this);
    batchExportAction_->setObjectName(QStringLiteral("actionBatchExport"));
    connect(batchExportAction_, &QAction::triggered, this,
            &MainWindow::startBatchExport);
    registerAction(batchExportAction_, "file.export_batch",
                   tr("Batch Export..."),
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));

    importSubtitlesAction_ = new QAction(tr("Import Subtitles..."), this);
    importSubtitlesAction_->setObjectName(
        QStringLiteral("actionImportSubtitles"));
    connect(importSubtitlesAction_, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import Subtitles"), QString(),
            tr("Subtitle Files (*.srt *.ass *.ssa *.vtt *.webvtt);;"
               "All Files (*)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Import Subtitles"),
                                 tr("Could not read %1").arg(path));
            return;
        }
        const QByteArray bytes = file.readAll();
        const auto cues = bl::parseSubtitleFile(
            path.toStdString(), std::string(bytes.constData(),
                                            static_cast<size_t>(bytes.size())));
        if (cues.empty()) {
            QMessageBox::information(
                this, tr("Import Subtitles"),
                tr("No subtitle cues found in %1").arg(path));
            return;
        }
        if (timelinePanel_) timelinePanel_->importSubtitleCues(cues);
    });
    registerAction(importSubtitlesAction_, "file.import_subtitles",
                   tr("Import Subtitles..."), QKeySequence());

    titleEditorAction_ = new QAction(tr("Add Title..."), this);
    titleEditorAction_->setObjectName(QStringLiteral("actionAddTitle"));
    connect(titleEditorAction_, &QAction::triggered, this,
            &MainWindow::addTitleClip);
    registerAction(titleEditorAction_, "file.add_title", tr("Add Title..."),
                   QKeySequence());

    auto* quit = new QAction(tr("Quit"), this);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    registerAction(quit, "file.quit", tr("Quit"),
                   QKeySequence(Qt::CTRL | Qt::Key_Q));

    undoAction_ = new QAction(tr("Undo"), this);
    connect(undoAction_, &QAction::triggered, this, [this] { controller_->undoStack().undo(); });
    registerAction(undoAction_, "edit.undo", tr("Undo"), QKeySequence::Undo);

    redoAction_ = new QAction(tr("Redo"), this);
    connect(redoAction_, &QAction::triggered, this, [this] { controller_->undoStack().redo(); });
    registerAction(redoAction_, "edit.redo", tr("Redo"), QKeySequence::Redo);

    playPauseAction_ = new QAction(tr("Play/Pause"), this);
    connect(playPauseAction_, &QAction::triggered, this, [this] {
        if (previewPanel_) {
            previewPanel_->transport().togglePlay();
        }
    });
    registerAction(playPauseAction_, "playback.toggle", tr("Play/Pause"),
                   QKeySequence(Qt::Key_Space));

    fitTimelineAction_ = new QAction(tr("Fit Timeline to View"), this);
    fitTimelineAction_->setObjectName(QStringLiteral("actionFitTimeline"));
    connect(fitTimelineAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) {
            timelinePanel_->fitZoomToViewport();
        }
    });
    registerAction(fitTimelineAction_, "timeline.fit",
                   tr("Fit Timeline to View"),
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));

    // Timeline zone (Phase 5): set the in/out points at the playhead, clear,
    // and lift/extract the range.
    zoneInAction_ = new QAction(tr("Set Zone In"), this);
    zoneInAction_->setObjectName(QStringLiteral("actionZoneIn"));
    connect(zoneInAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) timelinePanel_->setZoneIn(timelinePanel_->playhead());
    });
    registerAction(zoneInAction_, "timeline.zone_in", tr("Set Zone In"),
                   QKeySequence(Qt::Key_I));

    zoneOutAction_ = new QAction(tr("Set Zone Out"), this);
    zoneOutAction_->setObjectName(QStringLiteral("actionZoneOut"));
    connect(zoneOutAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) timelinePanel_->setZoneOut(timelinePanel_->playhead());
    });
    registerAction(zoneOutAction_, "timeline.zone_out", tr("Set Zone Out"),
                   QKeySequence(Qt::Key_O));

    clearZoneAction_ = new QAction(tr("Clear Zone"), this);
    clearZoneAction_->setObjectName(QStringLiteral("actionClearZone"));
    connect(clearZoneAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) timelinePanel_->clearZone();
    });
    registerAction(clearZoneAction_, "timeline.zone_clear", tr("Clear Zone"),
                   QKeySequence(Qt::SHIFT | Qt::Key_Z));

    liftZoneAction_ = new QAction(tr("Lift Zone"), this);
    liftZoneAction_->setObjectName(QStringLiteral("actionLiftZone"));
    connect(liftZoneAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) timelinePanel_->liftZone();
    });
    registerAction(liftZoneAction_, "timeline.lift", tr("Lift Zone"),
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));

    extractZoneAction_ = new QAction(tr("Extract Zone"), this);
    extractZoneAction_->setObjectName(QStringLiteral("actionExtractZone"));
    connect(extractZoneAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) timelinePanel_->extractZone();
    });
    registerAction(extractZoneAction_, "timeline.extract", tr("Extract Zone"),
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));

    // Timeline clipboard and markers (Phase 7). Cut/Copy/Paste operate on the
    // timeline selection; Ctrl+C/X/V inside a text field are handled by the
    // widget itself (ShortcutOverride), so renaming a track still copies text.
    cutAction_ = new QAction(tr("Cut Clips"), this);
    cutAction_->setObjectName(QStringLiteral("actionCutClips"));
    connect(cutAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) timelinePanel_->cutSelection();
    });
    registerAction(cutAction_, "timeline.cut", tr("Cut Clips"),
                   QKeySequence::Cut);

    copyAction_ = new QAction(tr("Copy Clips"), this);
    copyAction_->setObjectName(QStringLiteral("actionCopyClips"));
    connect(copyAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) timelinePanel_->copySelection();
    });
    registerAction(copyAction_, "timeline.copy", tr("Copy Clips"),
                   QKeySequence::Copy);

    pasteAction_ = new QAction(tr("Paste Clips"), this);
    pasteAction_->setObjectName(QStringLiteral("actionPasteClips"));
    connect(pasteAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) timelinePanel_->pasteAtPlayhead();
    });
    registerAction(pasteAction_, "timeline.paste", tr("Paste Clips"),
                   QKeySequence::Paste);

    addMarkerAction_ = new QAction(tr("Add Marker"), this);
    addMarkerAction_->setObjectName(QStringLiteral("actionAddMarker"));
    connect(addMarkerAction_, &QAction::triggered, this, [this] {
        if (timelinePanel_) {
            timelinePanel_->addMarkerAt(timelinePanel_->playhead());
        }
    });
    registerAction(addMarkerAction_, "timeline.marker_add", tr("Add Marker"),
                   QKeySequence(Qt::Key_M));

    compareQualityAction_ = new QAction(tr("Compare Quality..."), this);
    compareQualityAction_->setObjectName(QStringLiteral("actionCompareQuality"));
    connect(compareQualityAction_, &QAction::triggered, this,
            &MainWindow::compareQuality);
    registerAction(compareQualityAction_, "tools.compare_quality",
                   tr("Compare Quality..."), QKeySequence());

    fullScreenPreviewAction_ = new QAction(tr("Full Screen Preview"), this);
    fullScreenPreviewAction_->setObjectName(
        QStringLiteral("actionFullScreenPreview"));
    fullScreenPreviewAction_->setCheckable(true);
    connect(fullScreenPreviewAction_, &QAction::triggered, this,
            &MainWindow::toggleFullScreenPreview);
    registerAction(fullScreenPreviewAction_, "view.fullscreen_preview",
                   tr("Full Screen Preview"),
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));

    darkThemeAction_ = new QAction(tr("Dark Theme"), this);
    darkThemeAction_->setCheckable(true);
    lightThemeAction_ = new QAction(tr("Light Theme"), this);
    lightThemeAction_->setCheckable(true);
    connect(darkThemeAction_, &QAction::triggered, this, [this] { applyTheme(Theme::Dark); });
    connect(lightThemeAction_, &QAction::triggered, this, [this] { applyTheme(Theme::Light); });
    registerAction(darkThemeAction_, "theme.dark", tr("Dark Theme"),
                   QKeySequence());
    registerAction(lightThemeAction_, "theme.light", tr("Light Theme"),
                   QKeySequence());

    const std::vector<std::pair<QDockWidget*, std::pair<QString, QKeySequence>>> docks = {
        {mediaBin, {"view.toggle.media_bin", QKeySequence(Qt::CTRL | Qt::Key_1)}},
        {preview, {"view.toggle.preview", QKeySequence(Qt::CTRL | Qt::Key_2)}},
        {inspector, {"view.toggle.inspector", QKeySequence(Qt::CTRL | Qt::Key_3)}},
        {mixer, {"view.toggle.mixer", QKeySequence(Qt::CTRL | Qt::Key_4)}},
        {timeline, {"view.toggle.timeline", QKeySequence(Qt::CTRL | Qt::Key_5)}},
        {scopes, {"view.toggle.scopes", QKeySequence(Qt::CTRL | Qt::Key_6)}},
    };
    for (const auto& [dock, spec] : docks) {
        QAction* toggle = dock->toggleViewAction();
        const QString& id = spec.first;
        const QKeySequence& sequence = spec.second;
        registerAction(toggle, id, dock->windowTitle(), sequence);
    }

    applyActionsFromRegistry();
}

void MainWindow::applyActionsFromRegistry() {
    actionRegistry_.loadOverrides(*settings_, QStringLiteral("shortcuts"));
}

void MainWindow::buildMenuAndToolbar() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(actionRegistry_.action("file.new"));
    fileMenu->addAction(newFromTemplateAction_);
    fileMenu->addAction(actionRegistry_.action("file.open"));
    fileMenu->addAction(actionRegistry_.action("file.save"));
    fileMenu->addAction(actionRegistry_.action("file.save_as"));
    fileMenu->addSeparator();
    fileMenu->addAction(importSubtitlesAction_);
    fileMenu->addAction(titleEditorAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(exportAction_);
    fileMenu->addAction(batchExportAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(actionRegistry_.action("file.quit"));

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);
    editMenu->addSeparator();
    editMenu->addAction(cutAction_);
    editMenu->addAction(copyAction_);
    editMenu->addAction(pasteAction_);
    editMenu->addSeparator();
    editMenu->addAction(playPauseAction_);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(mediaBin->toggleViewAction());
    viewMenu->addAction(preview->toggleViewAction());
    viewMenu->addAction(inspector->toggleViewAction());
    viewMenu->addAction(mixer->toggleViewAction());
    viewMenu->addAction(timeline->toggleViewAction());
    viewMenu->addAction(scopes->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(fitTimelineAction_);
    viewMenu->addAction(fullScreenPreviewAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(darkThemeAction_);
    viewMenu->addAction(lightThemeAction_);

    auto* timelineMenu = menuBar()->addMenu(tr("&Timeline"));
    timelineMenu->addAction(cutAction_);
    timelineMenu->addAction(copyAction_);
    timelineMenu->addAction(pasteAction_);
    timelineMenu->addSeparator();
    timelineMenu->addAction(addMarkerAction_);
    timelineMenu->addSeparator();
    timelineMenu->addAction(zoneInAction_);
    timelineMenu->addAction(zoneOutAction_);
    timelineMenu->addAction(clearZoneAction_);
    timelineMenu->addSeparator();
    timelineMenu->addAction(liftZoneAction_);
    timelineMenu->addAction(extractZoneAction_);

    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(compareQualityAction_);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* shortcutsAction = new QAction(tr("Keyboard Shortcuts..."), this);
    connect(shortcutsAction, &QAction::triggered, this,
            &MainWindow::showShortcutsDialog);
    helpMenu->addAction(shortcutsAction);

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolBar"));
    toolbar->setMovable(false);
    toolbar->addAction(actionRegistry_.action("file.new"));
    toolbar->addAction(actionRegistry_.action("file.open"));
    toolbar->addAction(actionRegistry_.action("file.save"));
    toolbar->addAction(actionRegistry_.action("file.save_as"));
    toolbar->addAction(exportAction_);
    toolbar->addSeparator();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);
    toolbar->addAction(playPauseAction_);

    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
}

void MainWindow::showShortcutsDialog() {
    ShortcutsDialog dialog(actionRegistry_, *settings_, this);
    dialog.exec();
}

void MainWindow::compareQuality() {
    QualityMetricsDialog dialog(this);
    connect(&dialog, &QualityMetricsDialog::compareRequested, this,
            [this, &dialog](const QString& reference, const QString& test,
                            int maxFrames) {
                ExportPluginSet plugins;
                MediaDecodeSource::Spec spec;
                if (previewPanel_) {
                    spec.pluginDirs = previewPanel_->pluginDirs();
                }
                if (auto loaded = plugins.load(spec); !loaded.ok()) {
                    dialog.showError(
                        tr("could not load codec plugins: %1")
                            .arg(QString::fromStdString(loaded.message())));
                    return;
                }
                QApplication::setOverrideCursor(Qt::WaitCursor);
                auto report = bl::compareVideo(reference.toStdString(),
                                               test.toStdString(),
                                               plugins.registry, maxFrames);
                QApplication::restoreOverrideCursor();
                if (!report.ok()) {
                    dialog.showError(QString::fromStdString(report.message()));
                    return;
                }
                dialog.showReport(report.value());
            });
    dialog.exec();
}

void MainWindow::toggleFullScreenPreview() {
    if (!previewPanel_ || !preview) {
        return;
    }

    if (fullScreenPreviewWindow_) {
        // Exiting: reclaim the single panel into its dock.
        previewPanel_ =
            static_cast<PreviewPanel*>(fullScreenPreviewWindow_->takePreview());
        preview->setWidget(previewPanel_);
        previewPanel_->show();

        QObject::disconnect(fullScreenPreviewWindow_, nullptr, this, nullptr);
        fullScreenPreviewWindow_->hide();
        fullScreenPreviewWindow_->deleteLater();
        fullScreenPreviewWindow_ = nullptr;

        if (fullScreenPreviewAction_) {
            fullScreenPreviewAction_->setChecked(false);
        }
        statusBar()->showMessage(tr("Preview docked."), 3000);
        return;
    }

    // Entering: pick the monitor to fill. With a single screen (the offscreen
    // test platform included) this is dialog-free.
    QScreen* target = QGuiApplication::primaryScreen();
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.size() > 1) {
        QStringList names;
        names.reserve(screens.size());
        for (QScreen* screen : screens) {
            names << screen->name();
        }
        // Default to the first non-primary monitor when there is one.
        int defaultIndex = 0;
        for (int i = 0; i < screens.size(); ++i) {
            if (screens[i] != QGuiApplication::primaryScreen()) {
                defaultIndex = i;
                break;
            }
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(
            this, tr("Full Screen Preview"),
            tr("Show the preview on monitor:"), names, defaultIndex, false, &ok);
        if (!ok || chosen.isEmpty()) {
            if (fullScreenPreviewAction_) {
                fullScreenPreviewAction_->setChecked(false);
            }
            return;
        }
        const int index = names.indexOf(chosen);
        if (index >= 0) {
            target = screens[index];
        }
    }

    fullScreenPreviewWindow_ = new FullScreenPreviewWindow(this);
    // Detach the one existing panel from the dock; no second preview pipeline
    // is created (transport, engine and scopes tap all move with it).
    preview->setWidget(nullptr);
    fullScreenPreviewWindow_->setPreview(previewPanel_);
    if (target) {
        fullScreenPreviewWindow_->setScreen(target);
    }
    fullScreenPreviewWindow_->showFullScreen();
    // The window loses focus to the panel's transport buttons; keep Escape a
    // window-level trigger by giving the window itself focus.
    fullScreenPreviewWindow_->setFocus();

    connect(fullScreenPreviewWindow_,
            &FullScreenPreviewWindow::closeRequested, this, [this] {
                if (fullScreenPreviewWindow_) {
                    toggleFullScreenPreview();
                }
            });

    if (fullScreenPreviewAction_) {
        fullScreenPreviewAction_->setChecked(true);
    }
    statusBar()->showMessage(
        tr("Full-screen preview on %1. Press Escape to return.")
            .arg(target ? target->name() : QString()),
        5000);
}

void MainWindow::addTitleClip() {
    if (!controller_ || !timelinePanel_) {
        return;
    }

    const bl::Sequence& seq = timelinePanel_->sequence();
    const int width = seq.settings.width;
    const int height = seq.settings.height;

    TitleEditorDialog dialog(this);
    dialog.setCanvasSize(width, height);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const bl::TitleSpec spec = dialog.spec();
    const QString path = newTitleFilePath();
    if (!saveTitlePng(spec, width, height, path)) {
        QMessageBox::warning(this, tr("Add Title"),
                             tr("Could not write the title image to %1")
                                 .arg(path));
        return;
    }

    QString name = firstLineOfTitle(spec);
    if (name.isEmpty()) {
        name = tr("Title");
    }

    if (!controller_->addTitleToMediaBin(path, name, nlohmann::json(spec))) {
        QMessageBox::warning(this, tr("Add Title"),
                             tr("Could not add the title to the project."));
        return;
    }
    const std::string id = controller_->mediaIdForPath(path);
    if (id.empty()) {
        return;
    }

    const bl::Duration duration = bl::Duration::fromSeconds(
        dialog.durationSeconds(), bl::Rational{1'000'000, 1});
    timelinePanel_->addMediaToTimeline(id, duration);
    statusBar()->showMessage(tr("Added title: %1").arg(name), 3000);
}

void MainWindow::editTitleClip(const std::string& mediaId) {
    if (!controller_ || !timelinePanel_) {
        return;
    }
    const nlohmann::json stored = controller_->titleSpecFor(mediaId);
    if (stored.is_null()) {
        return;  // not a title
    }

    bl::TitleSpec spec;
    try {
        spec = stored.get<bl::TitleSpec>();
    } catch (const std::exception&) {
        statusBar()->showMessage(
            tr("Could not read the stored title design."), 4000);
        return;
    }

    const bl::Sequence& seq = timelinePanel_->sequence();
    const int width = seq.settings.width;
    const int height = seq.settings.height;

    TitleEditorDialog dialog(this);
    dialog.setCanvasSize(width, height);
    dialog.setSpec(spec);
    // The timeline owns the clip length; the Duration control would only
    // mislead here, so it is hidden in edit mode.
    dialog.setDurationVisible(false);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString name = firstLineOfTitle(dialog.spec());
    if (name.isEmpty()) {
        name = tr("Title");
    }
    if (!applyTitleEdit(mediaId, dialog.spec(), width, height, name)) {
        QMessageBox::warning(this, tr("Edit Title"),
                             tr("Could not update the title image."));
    }
}

bool MainWindow::applyTitleEdit(const std::string& mediaId,
                                const bl::TitleSpec& spec, int canvasWidth,
                                int canvasHeight, const QString& displayName) {
    if (!controller_) {
        return false;
    }
    if (!controller_->updateTitle(mediaId, spec, canvasWidth, canvasHeight,
                                  displayName)) {
        return false;  // caller reports it (kept modal-free for tests)
    }
    statusBar()->showMessage(tr("Updated title: %1").arg(displayName), 3000);
    return true;
}

void MainWindow::newFromTemplate() {
    if (!controller_ || !timelinePanel_) {
        return;
    }
    if (!confirmClose()) {
        return;
    }

    const std::vector<ProjectTemplate> templates = discoverTemplates();
    TemplatePickerDialog dialog(this, templates);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const std::string templateId = dialog.selectedTemplateId();
    if (templateId.empty()) {
        return;
    }

    // Resolve from the local copy: this survives even if the registry is
    // rebuilt while the dialog is up, and carries the seed titles.
    const ProjectTemplate* tmpl = nullptr;
    for (const ProjectTemplate& candidate : templates) {
        if (candidate.id == templateId) {
            tmpl = &candidate;
            break;
        }
    }

    const QString defaultName =
        tmpl ? QString::fromStdString(tmpl->name) : untitledTitle();
    const QString name = QInputDialog::getText(this, tr("New from Template"),
                                               tr("Project name:"),
                                               QLineEdit::Normal, defaultName);
    if (name.isEmpty()) {
        return;
    }

    if (!controller_->newProjectFromTemplate(templateId, name)) {
        QMessageBox::warning(
            this, tr("New from Template"),
            tr("Could not create a project from the selected template."));
        return;
    }

    if (tmpl) {
        addTemplateTitles(*tmpl);
    }
}

void MainWindow::addTemplateTitles(const bl::ProjectTemplate& tmpl) {
    if (!controller_ || !timelinePanel_) {
        return;
    }
    const bl::Sequence& seq = timelinePanel_->sequence();
    const int width = seq.settings.width;
    const int height = seq.settings.height;

    int added = 0;
    for (const TemplateTitle& title : tmpl.titles) {
        QString name = QString::fromStdString(title.name);
        if (name.isEmpty()) {
            name = firstLineOfTitle(title.spec);
        }
        if (name.isEmpty()) {
            name = tr("Title");
        }

        const QString path = newTitleFilePath();
        if (!saveTitlePng(title.spec, width, height, path)) {
            statusBar()->showMessage(
                tr("Skipped template title \"%1\": could not render it.")
                    .arg(name),
                4000);
            continue;
        }
        if (!controller_->addTitleToMediaBin(path, name,
                                             nlohmann::json(title.spec))) {
            statusBar()->showMessage(
                tr("Skipped template title \"%1\": could not add it.")
                    .arg(name),
                4000);
            continue;
        }
        const std::string id = controller_->mediaIdForPath(path);
        if (id.empty()) {
            continue;
        }
        timelinePanel_->addMediaToTimeline(id, title.duration);
        ++added;
    }

    if (added > 0) {
        statusBar()->showMessage(
            tr("Created from template \"%1\" with %2 title(s).")
                .arg(QString::fromStdString(tmpl.name))
                .arg(added),
            3000);
    }
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
    if (timelinePanel_) {
        dialog.setZone(timelinePanel_->hasZone(), timelinePanel_->zoneIn(),
                       timelinePanel_->zoneOut());
        bl::Time clipIn{};
        bl::Time clipOut{};
        if (timelinePanel_->selectedClipRange(clipIn, clipOut)) {
            dialog.setClipRange(true, clipIn, clipOut);
        } else {
            dialog.setClipRange(false, bl::Time{}, bl::Time{});
        }
    }
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
    connect(controller_, &ProjectController::projectChanged, this, &MainWindow::syncMeterAnalysis);
    connect(controller_, &ProjectController::projectChanged, this, &MainWindow::syncThumbnailJobs);
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
    if (timelinePanel_) {
        settings_->setValue(kLaneOrderKey,
                            static_cast<int>(timelinePanel_->laneOrder()));
        const TimelineState& state = timelinePanel_->timelineState();
        settings_->setValue(kEditModeKey, static_cast<int>(state.editMode));
        settings_->setValue(kInsertModeKey, state.insertMode);
        settings_->setValue(kRippleKey, state.ripple);
        settings_->setValue(kSnapKey, state.snap);
    }
    actionRegistry_.saveOverrides(*settings_, QStringLiteral("shortcuts"));
}

void MainWindow::maybeRestoreLayout() {
    Q_ASSERT(settings_);
    const QByteArray geometry = settings_->value(kGeometryKey).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    // Only restore a saved dock arrangement if it was written by this layout
    // generation; otherwise keep the new built-in default (see kLayoutVersion).
    const int savedLayoutVersion = settings_->value(kLayoutVersionKey, 0).toInt();
    if (savedLayoutVersion >= kLayoutVersion) {
        const QByteArray state = settings_->value(kStateKey).toByteArray();
        if (!state.isEmpty()) {
            restoreState(state);
        }
    }
    settings_->setValue(kLayoutVersionKey, kLayoutVersion);
    const QString theme = settings_->value(kThemeKey).toString();
    applyTheme(theme == QStringLiteral("light") ? Theme::Light : Theme::Dark);

    // Restore the track layout (Kdenlive parity); clamp against a stale file.
    if (timelinePanel_) {
        const int laneOrder = settings_->value(kLaneOrderKey, 0).toInt();
        timelinePanel_->setLaneOrder(static_cast<timeline_geometry::LaneOrder>(
            std::clamp(laneOrder, 0, 2)));

        // Named distinctly from the saved-window `state` above: MSVC's /W4
        // /WX treats the shadowing C4456 as an error (GCC does not warn).
        TimelineState timelineState = timelinePanel_->timelineState();
        timelineState.editMode = static_cast<TimelineState::EditMode>(
            std::clamp(settings_->value(kEditModeKey, 0).toInt(), 0, 5));
        timelineState.insertMode = settings_->value(kInsertModeKey, false).toBool();
        timelineState.ripple = settings_->value(kRippleKey, false).toBool();
        timelineState.snap = settings_->value(kSnapKey, true).toBool();
        timelineState.autoSplitAv = settings_->value("timeline/autoSplitAv", true).toBool();
        timelinePanel_->setTimelineState(timelineState);
    }
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