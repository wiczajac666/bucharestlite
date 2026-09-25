#pragma once

#include "app/autosave_manager.hpp"
#include "app/project_controller.hpp"
#include "app/theme_manager.hpp"
#include "dialogs/export_dialog.hpp"
#include "dialogs/export_progress_dialog.hpp"
#include "export/export_worker.hpp"
#include "panels/inspector_panel.hpp"
#include "panels/master_fader_panel.hpp"
#include "panels/media_bin_panel.hpp"
#include "panels/mixer_panel.hpp"
#include "panels/preview_panel.hpp"
#include "panels/timeline_panel.hpp"

#include <QMainWindow>

#include <functional>

class QSettings;
class QThread;

namespace bl::ui {

// Dockable application shell (arch §8.6 "MainWindow"): hosts the panel docks,
// the action bar and the theme/layout persistence. Layout state lives in a
// QSettings scope so tests can inject an isolated settings file.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr, QSettings* settings = nullptr);
    ~MainWindow() override;

    ProjectController* controller() const { return controller_; }

    Theme theme() const { return theme_; }
    int dockCount() const;
    QDockWidget* dock(const QString& title) const;

    void toggleTheme();
    void persistLayout();

    void setPromptOnCloseEnabled(bool enabled) { promptOnClose_ = enabled; }

    void setAutosaveRoot(const QString& root);
    void setRecoveryDecider(std::function<bool()> decider);
    AutosaveManager* autosaveManager() const { return autosaveManager_; }

    // After a project is opened or created, checks the autosave ring of the
    // now-active project and, when a newer snapshot exists, asks (via the
    // injected decider or a message box) whether to restore it.
    bool maybePromptRecovery();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildDocks();
    void buildActions();
    void buildMenuAndToolbar();
    void connectController();
    void updateTitle();
    void applyTheme(Theme theme);
    bool confirmClose();
    bool doSave();
    bool doSaveAs();

    void startExport();
    void startBatchExport();
    void ensureExportWorker();
    bool exportBusy() const;

    void maybeRestoreLayout();
    void saveLayout();

    ProjectController* controller_{nullptr};
    AutosaveManager* autosaveManager_{nullptr};
    QSettings* settings_{nullptr};
    bool ownsSettings_{false};
    bool promptOnClose_{true};
    std::function<bool()> recoveryDecider_;
    Theme theme_{Theme::Dark};

    QDockWidget* mediaBin{nullptr};
    QDockWidget* preview{nullptr};
    QDockWidget* inspector{nullptr};
    QDockWidget* mixer{nullptr};
    QDockWidget* timeline{nullptr};

    PreviewPanel* previewPanel_{nullptr};
    InspectorPanel* inspectorPanel_{nullptr};
    TimelinePanel* timelinePanel_{nullptr};
    MediaBinPanel* mediaBinPanel_{nullptr};
    MixerPanel* mixerPanel_{nullptr};
    MasterFaderPanel* masterFaderPanel_{nullptr};

    QAction* undoAction_{nullptr};
    QAction* redoAction_{nullptr};
    QAction* darkThemeAction_{nullptr};
    QAction* lightThemeAction_{nullptr};
    QAction* exportAction_{nullptr};
    QAction* batchExportAction_{nullptr};

    QThread* exportThread_{nullptr};
    ExportWorker* exportWorker_{nullptr};
    bool exportBusy_{false};
};

} // namespace bl::ui