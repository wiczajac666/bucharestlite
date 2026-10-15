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
#include "panels/scopes_panel.hpp"
#include "panels/timeline_panel.hpp"
#include "shortcuts/action_registry.hpp"

#include <bl_audio/audio_meter_engine.hpp>
#include <bl_render/media_bin_thumbnail_engine.hpp>

#include <QMainWindow>

#include <functional>
#include <memory>
#include <string>

class QSettings;
class QThread;

namespace bl {
struct ProjectTemplate;
} // namespace bl

namespace bl::ui {

class FullScreenPreviewWindow;

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

    // Applies an edited title design to a media-bin title: re-renders its still
    // at `canvasWidth` x `canvasHeight`, updates the stored spec and optionally
    // renames the bin entry (one undo step). Returns false (and changes
    // nothing) when the id is not a title or the still cannot be written. This
    // is the tail of editTitleClip, split out so it is callable without the
    // modal dialog.
    bool applyTitleEdit(const std::string& mediaId, const bl::TitleSpec& spec,
                        int canvasWidth, int canvasHeight,
                        const QString& displayName);

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
    void applyActionsFromRegistry();
    void buildMenuAndToolbar();
    void connectController();
    void updateTitle();
    void applyTheme(Theme theme);
    bool confirmClose();
    bool doSave();
    bool doSaveAs();
    void showShortcutsDialog();
    void compareQuality();
    // Detaches the single PreviewPanel into a frameless full-screen window on a
    // chosen monitor, and restores it into its Preview dock on the next toggle
    // (View → Full Screen Preview, Ctrl+Shift+P).
    void toggleFullScreenPreview();
    void setupMeterEngine();
    void syncMeterAnalysis();

    void setupThumbnailEngine();
    void syncThumbnailJobs();
    void applyThumbnail(const std::string& mediaItemId);

    void startExport();
    void startBatchExport();
    void ensureExportWorker();
    bool exportBusy() const;

    // Opens the title editor and, on accept, rasterises the title to a PNG in
    // the user data dir, imports it to the media bin (with its spec, so it can
    // be re-opened) and appends it to the timeline at the editor's duration.
    void addTitleClip();

    // File → New from Template…: picks a built-in or user template, prompts for
    // a project name, creates the project via the controller and renders the
    // template's seed titles to stills, appending them to the timeline.
    void newFromTemplate();

    // Renders each of `tmpl`'s seed titles to a still and appends it to the
    // timeline (best-effort: a title that cannot be rendered is skipped).
    void addTemplateTitles(const bl::ProjectTemplate& tmpl);

    // Re-opens an existing title's stored design in the Title Editor and, on
    // accept, re-renders its still in place (one undo step). No-op for an id
    // that is not a title.
    void editTitleClip(const std::string& mediaId);

    void maybeRestoreLayout();
    void saveLayout();

    ProjectController* controller_{nullptr};
    AutosaveManager* autosaveManager_{nullptr};
    QSettings* settings_{nullptr};
    bool ownsSettings_{false};
    bool promptOnClose_{true};
    std::function<bool()> recoveryDecider_;
    Theme theme_{Theme::Dark};
    ActionRegistry actionRegistry_;

    QDockWidget* mediaBin{nullptr};
    QDockWidget* preview{nullptr};
    QDockWidget* inspector{nullptr};
    QDockWidget* mixer{nullptr};
    QDockWidget* timeline{nullptr};
    QDockWidget* scopes{nullptr};

    PreviewPanel* previewPanel_{nullptr};
    InspectorPanel* inspectorPanel_{nullptr};
    TimelinePanel* timelinePanel_{nullptr};
    MediaBinPanel* mediaBinPanel_{nullptr};
    MixerPanel* mixerPanel_{nullptr};
    MasterFaderPanel* masterFaderPanel_{nullptr};
    ScopesPanel* scopesPanel_{nullptr};

    QAction* undoAction_{nullptr};
    QAction* redoAction_{nullptr};
    QAction* darkThemeAction_{nullptr};
    QAction* lightThemeAction_{nullptr};
    QAction* exportAction_{nullptr};
    QAction* batchExportAction_{nullptr};
    QAction* importSubtitlesAction_{nullptr};
    QAction* titleEditorAction_{nullptr};
    QAction* newFromTemplateAction_{nullptr};
    QAction* playPauseAction_{nullptr};
    QAction* fitTimelineAction_{nullptr};
    QAction* zoneInAction_{nullptr};
    QAction* zoneOutAction_{nullptr};
    QAction* clearZoneAction_{nullptr};
    QAction* liftZoneAction_{nullptr};
    QAction* extractZoneAction_{nullptr};
    QAction* cutAction_{nullptr};
    QAction* copyAction_{nullptr};
    QAction* pasteAction_{nullptr};
    QAction* addMarkerAction_{nullptr};
    QAction* compareQualityAction_{nullptr};
    QAction* fullScreenPreviewAction_{nullptr};

    FullScreenPreviewWindow* fullScreenPreviewWindow_{nullptr};

    QThread* exportThread_{nullptr};
    ExportWorker* exportWorker_{nullptr};
    bool exportBusy_{false};

    std::unique_ptr<bl::AudioMeterEngine> meterEngine_;
    std::unique_ptr<bl::MediaBinThumbnailEngine> thumbnailEngine_;
};

} // namespace bl::ui