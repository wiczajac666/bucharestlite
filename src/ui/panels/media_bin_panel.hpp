#pragma once

#include <QWidget>

#include <string>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace bl::ui {

class ProjectController;

// Filtering / id-lookup helpers for the media bin, kept Qt-widget-free so they
// are directly assertable from plain unit tests.
namespace media_bin_detail {

bool matchesFilter(const std::string& name, const std::string& needle);

} // namespace media_bin_detail

// Media Bin dock: lists the project's imported media (name per row), with
// Add / Remove and a live name filter. All mutations go through the undoable
// ProjectController mutations, so add/remove/undo/redo/open/new are reflected
// by reloading on projectChanged.
class MediaBinPanel : public QWidget {
    Q_OBJECT
public:
    explicit MediaBinPanel(ProjectController* controller, QWidget* parent = nullptr);

    int itemCount() const;
    int visibleItemCount() const;
    std::string selectedId() const;

public slots:
    void reload();
    void addFiles(const QStringList& paths);

private slots:
    void removeSelected();
    void onSelectionChanged();
    void onFilterChanged(const QString& text);

private:
    void applyFilter();

    ProjectController* controller_{nullptr};
    QListWidget* list_{nullptr};
    QLineEdit* filter_{nullptr};
    QPushButton* addButton_{nullptr};
    QPushButton* removeButton_{nullptr};
};

} // namespace bl::ui
