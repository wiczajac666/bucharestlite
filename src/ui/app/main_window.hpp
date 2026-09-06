#pragma once

#include "app/project_controller.hpp"
#include "app/theme_manager.hpp"
#include "panels/inspector_panel.hpp"
#include "panels/media_bin_panel.hpp"
#include "panels/mixer_panel.hpp"
#include "panels/preview_panel.hpp"
#include "panels/timeline_panel.hpp"

#include <QMainWindow>

class QSettings;

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

protected:
    void closeEvent(QCloseEvent* event) override;

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

    void maybeRestoreLayout();
    void saveLayout();

    ProjectController* controller_{nullptr};
    QSettings* settings_{nullptr};
    bool ownsSettings_{false};
    bool promptOnClose_{true};
    Theme theme_{Theme::Dark};

    QDockWidget* mediaBin{nullptr};
    QDockWidget* preview{nullptr};
    QDockWidget* inspector{nullptr};
    QDockWidget* mixer{nullptr};
    QDockWidget* timeline{nullptr};

    QAction* undoAction_{nullptr};
    QAction* redoAction_{nullptr};
    QAction* darkThemeAction_{nullptr};
    QAction* lightThemeAction_{nullptr};
};

} // namespace bl::ui