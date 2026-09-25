#pragma once

#include <bl_core/project_data.hpp>
#include <bl_core/project_repository.hpp>
#include <bl_core/result.hpp>
#include <bl_core/undo_stack.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/timeline.hpp>

#include <nlohmann/json.hpp>

#include <QObject>
#include <QString>

#include <string>

namespace bl::ui {

// Owns the open project: ProjectData + Timeline, current file path and dirty
// flag. Persistence goes through bl::core's ProjectRepository; the timeline is
// stored inside the project's `extensions["timeline"]` blob so a project file
// round-trips both documents together.
class ProjectController : public QObject {
    Q_OBJECT
public:
    explicit ProjectController(QObject* parent = nullptr);

    bool isOpen() const { return !project_.settings.name.empty(); }
    bool dirty() const { return dirty_; }
    QString projectName() const;
    QString filePath() const;

    const ProjectData& project() const { return project_; }
    const Timeline& timeline() const { return timeline_; }
    Timeline& timeline() { return timeline_; }
    const std::vector<MediaBinItem>& mediaBin() const { return project_.mediaBin; }

    bool canUndo() const { return undoStack_.canUndo(); }
    bool canRedo() const { return undoStack_.canRedo(); }
    UndoStack& undoStack() { return undoStack_; }

    void newProject(const QString& name);
    Result<void> open(const QString& path);
    Result<void> save();
    Result<void> saveAs(const QString& path);

    // Undoable project mutations (wired into UndoStack::push).
    void renameProject(const QString& name);
    bool addToMediaBin(const QString& path);
    bool removeFromMediaBin(const QString& id);

signals:
    void projectChanged();
    void dirtyChanged(bool dirty);
    void undoChanged();
    void statusMessage(const QString& message);

private:
    Result<void> doSave(const std::string& path);
    void adopt(ProjectData data, Timeline timeline, std::string filePath);
    void syncTimelineToExtensions();
    void recomputeDirty();
    void forceDirty();

    ProjectData project_;
    Timeline timeline_;
    std::string filePath_;
    // A project is dirty when the undo index has moved away from the index the
    // document was last saved/adopted at; every mutation goes through the undo
    // stack, so this stays exact for all edit kinds (including clip/mixer edits
    // that never call an explicit mark-dirty).
    size_t savedIndex_{0};
    bool dirty_{false};
    UndoStack undoStack_;
    size_t idSeed_{0};
};

} // namespace bl::ui