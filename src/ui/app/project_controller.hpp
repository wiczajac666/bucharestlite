#pragma once

#include <bl_core/project_data.hpp>
#include <bl_core/project_repository.hpp>
#include <bl_core/result.hpp>
#include <bl_core/undo_stack.hpp>
#include <bl_timeline/project_template.hpp>
#include <bl_timeline/sequence.hpp>
#include <bl_timeline/timeline.hpp>
#include <bl_timeline/title.hpp>

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
    const std::vector<MediaBinFolder>& folders() const { return project_.folders; }

    bool canUndo() const { return undoStack_.canUndo(); }
    bool canRedo() const { return undoStack_.canRedo(); }
    UndoStack& undoStack() { return undoStack_; }

    void newProject(const QString& name);
    // Creates a new unsaved project from a template: empty media bin, no file
    // path, the template's sequence settings + track skeleton + seed track
    // effects, marked dirty so the user is prompted to save. Titles are not
    // rendered here (the UI does that); look up findTemplate(id) for them.
    // Returns false for an unknown id, leaving the open document untouched.
    bool newProjectFromTemplate(const std::string& templateId,
                                const QString& name);
    Result<void> open(const QString& path);
    Result<void> save();
    Result<void> saveAs(const QString& path);

    // Serializes the open document (project + timeline extension) as JSON, the
    // exact format AutosaveRing::rotate persists and ProjectRepository loads.
    nlohmann::json serializeDocument();

    // Loads a recovered autosave document. Adopts it with `originalPath` as the
    // file path (so a subsequent save writes the recovered content to the real
    // project) and forces dirty; media paths are resolved relative to the
    // original project directory.
    Result<void> restoreFromAutosave(const QString& autosavePath,
                                     const QString& originalPath = QString());

    // Undoable project mutations (wired into UndoStack::push).
    void renameProject(const QString& name);
    bool addToMediaBin(const QString& path);
    // Adds a rendered title still plus its bl::TitleSpec (as JSON, serialised
    // with bl::to_json) to the media bin as a single undo step. The spec is
    // stored under extensions["titles"][id] so the title can be re-opened.
    bool addTitleToMediaBin(const QString& pngPath, const QString& displayName,
                            const nlohmann::json& spec);
    bool removeFromMediaBin(const QString& id);

    // Undoable bin-folder (collection) mutations. Folders are stored on the
    // project side of the document (ProjectData::folders), so they round-trip
    // through save/open alongside the media bin.
    //
    // Creates a folder under `parentId` ("" = the project root) and returns the
    // new folder's id, or "" when the name is empty or the parent does not
    // exist. One undo step.
    std::string addFolder(const QString& name, const std::string& parentId = {});
    // Renames a folder; returns false when the folder is missing, the name is
    // empty, or the name is unchanged. One undo step.
    bool renameFolder(const std::string& folderId, const QString& name);
    // Deletes a folder as one undo step. Media in the folder is rehomed to the
    // project root (folderId = "") and any child folders are re-parented to the
    // root as well.
    bool removeFolder(const std::string& folderId);
    // Moves a media item into `folderId` ("" = the project root). Returns false
    // when the media item is missing, the folder does not exist, or the item is
    // already there. One undo step.
    bool moveMediaToFolder(const std::string& mediaId,
                           const std::string& folderId);
    // Re-parents a folder under `newParentId` ("" = the project root). Rejects
    // self-parenting, a missing parent, and cycles (the new parent must not be
    // a descendant of the folder). One undo step.
    bool moveFolder(const std::string& folderId,
                    const std::string& newParentId);

    // The stored title spec for a media-bin item, or a null JSON value when the
    // item is not a title.
    nlohmann::json titleSpecFor(const std::string& mediaId) const;
    // The id of the media-bin item whose path matches, or "" when absent.
    std::string mediaIdForPath(const QString& path) const;

    // Re-renders an existing title in place as one undo step. Replaces the
    // spec under extensions["titles"][mediaId] and rewrites the still at the
    // item's path from `spec` at `canvasWidth` x `canvasHeight` (the sequence
    // resolution the title is authored for, so the rewrite is pixel-faithful).
    // Optionally renames the media-bin entry (pass the new first line of the
    // text). Undo restores the previous spec, name and PNG — the old still is
    // re-rendered from the old spec, keeping the file a pure function of the
    // design. Returns false and changes nothing when `mediaId` is not a title,
    // the stored spec is unreadable, or the PNG cannot be written.
    bool updateTitle(const std::string& mediaId, const bl::TitleSpec& spec,
                     int canvasWidth, int canvasHeight,
                     const QString& displayName = QString());

signals:
    void projectChanged();
    void dirtyChanged(bool dirty);
    void undoChanged();
    void statusMessage(const QString& message);
    // Emitted after a successful explicit save (Save/Save As).
    void saved();

private:
    Result<void> doSave(const std::string& path);
    void adopt(ProjectData data, Timeline timeline, std::string filePath);
    void syncTimelineToExtensions();
    void recomputeDirty();
    void forceDirty();
    // Updates one media-bin entry's display name (no-op when not found).
    void setMediaName(const std::string& id, const std::string& name);

    // Finds a folder by id (nullptr when absent); the non-const overload backs
    // the mutation commands, the const one the validation helpers.
    MediaBinFolder* findFolder(const std::string& id);
    const MediaBinFolder* findFolder(const std::string& id) const;
    // True when `node` sits somewhere below `ancestor` in the folder tree.
    bool isFolderDescendant(const std::string& node,
                            const std::string& ancestor) const;
    // Fresh id from the shared idSeed_ scheme that collides with no folder id.
    std::string nextFolderId();

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