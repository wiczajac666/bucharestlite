#include "app/project_controller.hpp"

#include "title/title_renderer.hpp"

#include <bl_core/image_sequence.hpp>

#include <QDateTime>

#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <utility>

namespace bl::ui {

namespace {

Timeline defaultTimeline(const std::string& name) {
    Sequence seq;
    seq.name = name;
    seq.addVideoTrack(QStringLiteral("V1").toStdString());
    seq.addAudioTrack(QStringLiteral("A1").toStdString());
    return Timeline(std::move(seq));
}

bool extractTimelineFromExtensions(const nlohmann::json& extensions, Timeline& out) {
    const auto it = extensions.find("timeline");
    if (it == extensions.end() || !it->is_object()) {
        return false;
    }
    try {
        out = it->get<Timeline>();
    } catch (...) {
        return false;
    }
    return true;
}

std::string fileBaseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

ProjectController::ProjectController(QObject* parent) : QObject(parent) {
    undoStack_.setStateCallback([this] {
        recomputeDirty();
        emit undoChanged();
    });
}

QString ProjectController::projectName() const {
    return QString::fromStdString(project_.settings.name);
}

QString ProjectController::filePath() const {
    return QString::fromStdString(filePath_);
}

void ProjectController::newProject(const QString& name) {
    ProjectData data;
    data.settings.name = name.toStdString();
    data.settings.author = QStringLiteral("Anonymous").toStdString();
    data.settings.createdIso8601 =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    adopt(std::move(data), defaultTimeline(data.settings.name), std::string());
    emit statusMessage(QStringLiteral("New project: %1").arg(name));
}

bool ProjectController::newProjectFromTemplate(const std::string& templateId,
                                               const QString& name) {
    const ProjectTemplate* tmpl = findTemplate(templateId);
    if (!tmpl) {
        emit statusMessage(
            QStringLiteral("Unknown template: %1")
                .arg(QString::fromStdString(templateId)));
        return false;
    }

    ProjectData data;
    const std::string templateName = tmpl->name;
    const std::string projectName =
        name.isEmpty() ? templateName : name.toStdString();
    data.settings.name = projectName;
    data.settings.author = QStringLiteral("Anonymous").toStdString();
    data.settings.createdIso8601 =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();

    Timeline timeline = instantiateTemplate(*tmpl);
    timeline.sequence().name = projectName;

    // A template project is unsaved and has no path yet, so adopt empty and
    // force it dirty — the user is prompted to save before losing work.
    adopt(std::move(data), std::move(timeline), std::string());
    forceDirty();
    emit statusMessage(
        QStringLiteral("New project from template: %1")
            .arg(QString::fromStdString(templateName)));
    return true;
}

Result<void> ProjectController::open(const QString& path) {
    ProjectRepository repository;
    Result<LoadReport> load = repository.load(path.toStdString());
    if (!load.ok()) {
        emit statusMessage(QStringLiteral("Open failed: %1").arg(QString::fromStdString(load.message())));
        return Result<void>::err(load.code(), load.message());
    }

    ProjectData data = std::move(load->project);
    Timeline timeline;
    if (!extractTimelineFromExtensions(data.extensions, timeline)) {
        timeline = defaultTimeline(data.settings.name.empty()
                                       ? QStringLiteral("Untitled").toStdString()
                                       : data.settings.name);
    }
    adopt(std::move(data), std::move(timeline), path.toStdString());
    emit statusMessage(QStringLiteral("Opened %1").arg(path));
    return Result<void>();
}

Result<void> ProjectController::save() {
    if (filePath_.empty()) {
        return Result<void>::err(Err::InvalidArgument,
                                 "Project has no file path yet — use Save As");
    }
    return doSave(filePath_);
}

Result<void> ProjectController::saveAs(const QString& path) {
    return doSave(path.toStdString());
}

nlohmann::json ProjectController::serializeDocument() {
    syncTimelineToExtensions();
    return ProjectRepository::toJson(project_, filePath_);
}

Result<void> ProjectController::restoreFromAutosave(
    const QString& autosavePath, const QString& originalPath) {
    std::ifstream in(autosavePath.toStdString(), std::ios::binary);
    if (!in) {
        emit statusMessage(QStringLiteral("Autosave restore failed: cannot open %1")
                               .arg(autosavePath));
        return Result<void>::err(Err::FileNotFound,
                                 "cannot open autosave '" + autosavePath.toStdString() + "'");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    ProjectRepository repository;
    Result<LoadReport> load =
        repository.fromDocument(buffer.str(), originalPath.toStdString());
    if (!load.ok()) {
        emit statusMessage(
            QStringLiteral("Autosave restore failed: %1")
                .arg(QString::fromStdString(load.message())));
        return Result<void>::err(load.code(), load.message());
    }

    ProjectData data = std::move(load->project);
    Timeline timeline;
    if (!extractTimelineFromExtensions(data.extensions, timeline)) {
        timeline = defaultTimeline(data.settings.name.empty()
                                       ? QStringLiteral("Untitled").toStdString()
                                       : data.settings.name);
    }
    adopt(std::move(data), std::move(timeline), originalPath.toStdString());
    forceDirty();
    emit statusMessage(
        QStringLiteral("Recovered from autosave: %1").arg(autosavePath));
    return Result<void>();
}

Result<void> ProjectController::doSave(const std::string& path) {
    syncTimelineToExtensions();
    ProjectRepository repository;
    Result<void> result = repository.save(project_, path);
    if (!result.ok()) {
        emit statusMessage(QStringLiteral("Save failed: %1").arg(QString::fromStdString(result.message())));
        return result;
    }
    filePath_ = path;
    savedIndex_ = undoStack_.index();
    recomputeDirty();
    emit saved();
    emit statusMessage(QStringLiteral("Saved %1").arg(QString::fromStdString(path)));
    return result;
}

void ProjectController::renameProject(const QString& name) {
    const std::string oldName = project_.settings.name;
    const std::string newName = name.toStdString();
    if (oldName == newName) {
        return;
    }
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Rename project").toStdString(),
        [this, newName] {
            project_.settings.name = newName;
            timeline_.sequence().name = newName;
            emit projectChanged();
        },
        [this, oldName] {
            project_.settings.name = oldName;
            timeline_.sequence().name = oldName;
            emit projectChanged();
        }));
}

bool ProjectController::addToMediaBin(const QString& path) {
    const std::string rawPath = path.toStdString();
    // Image sequences: a single dropped frame expands to the whole sequence,
    // stored under its FFmpeg pattern so the renderer decodes all frames.
    std::string binPath = rawPath;
    std::string binName = fileBaseName(rawPath);
    const auto seq = bl::detectImageSequence(rawPath);
    if (seq) {
        binPath = seq->patternPath;
        std::string stem = seq->stem;
        while (!stem.empty() && (stem.back() == '_' || stem.back() == '-' ||
                                 stem.back() == ' ' || stem.back() == '.')) {
            stem.pop_back();
        }
        binName = stem + " (" + std::to_string(seq->frameCount) + " frames)";
    }

    const auto existing = std::find_if(
        project_.mediaBin.begin(), project_.mediaBin.end(),
        [&binPath](const MediaBinItem& item) { return item.path == binPath; });
    if (existing != project_.mediaBin.end()) {
        return false;
    }

    const MediaBinItem item{std::to_string(++idSeed_), binPath, binName,
                            std::string()};
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Add media to bin").toStdString(),
        [this, item] {
            project_.mediaBin.push_back(item);
            emit projectChanged();
        },
        [this, item] {
            project_.mediaBin.erase(
                std::remove_if(project_.mediaBin.begin(), project_.mediaBin.end(),
                               [&item](const MediaBinItem& it) { return it.id == item.id; }),
                project_.mediaBin.end());
            emit projectChanged();
        }));
    return true;
}

bool ProjectController::addTitleToMediaBin(const QString& pngPath,
                                           const QString& displayName,
                                           const nlohmann::json& spec) {
    const std::string binPath = pngPath.toStdString();
    const std::string binName = displayName.isEmpty()
                                    ? fileBaseName(binPath)
                                    : displayName.toStdString();
    const auto existing = std::find_if(
        project_.mediaBin.begin(), project_.mediaBin.end(),
        [&binPath](const MediaBinItem& item) { return item.path == binPath; });
    if (existing != project_.mediaBin.end()) {
        return false;
    }

    const MediaBinItem item{std::to_string(++idSeed_), binPath, binName,
                            std::string()};
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Add title to media bin").toStdString(),
        [this, item, spec] {
            project_.mediaBin.push_back(item);
            project_.extensions["titles"][item.id] = spec;
            emit projectChanged();
        },
        [this, item] {
            project_.mediaBin.erase(
                std::remove_if(project_.mediaBin.begin(), project_.mediaBin.end(),
                               [&item](const MediaBinItem& it) { return it.id == item.id; }),
                project_.mediaBin.end());
            if (auto titles = project_.extensions.find("titles");
                titles != project_.extensions.end() && titles->is_object()) {
                titles->erase(item.id);
            }
            emit projectChanged();
        }));
    return true;
}

MediaBinFolder* ProjectController::findFolder(const std::string& id) {
    const auto it = std::find_if(
        project_.folders.begin(), project_.folders.end(),
        [&id](const MediaBinFolder& f) { return f.id == id; });
    return it == project_.folders.end() ? nullptr : &*it;
}

const MediaBinFolder* ProjectController::findFolder(
    const std::string& id) const {
    const auto it = std::find_if(
        project_.folders.begin(), project_.folders.end(),
        [&id](const MediaBinFolder& f) { return f.id == id; });
    return it == project_.folders.end() ? nullptr : &*it;
}

bool ProjectController::isFolderDescendant(
    const std::string& node, const std::string& ancestor) const {
    std::vector<std::string> seen;
    std::string current = node;
    while (!current.empty()) {
        if (current == ancestor) {
            return true;
        }
        // Defensive: a malformed tree must not spin forever.
        if (std::find(seen.begin(), seen.end(), current) != seen.end()) {
            return false;
        }
        seen.push_back(current);
        const MediaBinFolder* folder = findFolder(current);
        if (!folder) {
            return false;
        }
        current = folder->parentId;
    }
    return false;
}

std::string ProjectController::nextFolderId() {
    for (;;) {
        const std::string candidate = std::to_string(++idSeed_);
        const bool taken = std::any_of(
            project_.folders.begin(), project_.folders.end(),
            [&candidate](const MediaBinFolder& f) {
                return f.id == candidate;
            });
        if (!taken) {
            return candidate;
        }
    }
}

std::string ProjectController::addFolder(const QString& name,
                                         const std::string& parentId) {
    const std::string folderName = name.toStdString();
    if (folderName.empty()) {
        return std::string();
    }
    if (!parentId.empty() && !findFolder(parentId)) {
        return std::string();
    }
    const std::string id = nextFolderId();
    const MediaBinFolder folder{id, folderName, parentId};
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Add folder").toStdString(),
        [this, folder] {
            project_.folders.push_back(folder);
            emit projectChanged();
        },
        [this, folder] {
            project_.folders.erase(
                std::remove_if(project_.folders.begin(), project_.folders.end(),
                               [&folder](const MediaBinFolder& f) {
                                   return f.id == folder.id;
                               }),
                project_.folders.end());
            emit projectChanged();
        }));
    return id;
}

bool ProjectController::renameFolder(const std::string& folderId,
                                     const QString& name) {
    MediaBinFolder* folder = findFolder(folderId);
    if (!folder) {
        return false;
    }
    const std::string newName = name.toStdString();
    if (newName.empty() || newName == folder->name) {
        return false;
    }
    const std::string oldName = folder->name;
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Rename folder").toStdString(),
        [this, folderId, newName] {
            if (MediaBinFolder* f = findFolder(folderId)) {
                f->name = newName;
            }
            emit projectChanged();
        },
        [this, folderId, oldName] {
            if (MediaBinFolder* f = findFolder(folderId)) {
                f->name = oldName;
            }
            emit projectChanged();
        }));
    return true;
}

bool ProjectController::removeFolder(const std::string& folderId) {
    const MediaBinFolder* found = findFolder(folderId);
    if (!found) {
        return false;
    }
    const MediaBinFolder folder = *found;
    const size_t index = static_cast<size_t>(found - project_.folders.data());

    // Items and child folders that the delete rehomes to the root; captured up
    // front so undo can put every one of them back exactly where it was.
    std::vector<std::string> mediaIds;
    for (const MediaBinItem& item : project_.mediaBin) {
        if (item.folderId == folderId) {
            mediaIds.push_back(item.id);
        }
    }
    std::vector<std::string> childIds;
    for (const MediaBinFolder& f : project_.folders) {
        if (f.parentId == folderId) {
            childIds.push_back(f.id);
        }
    }

    const auto contains = [](const std::vector<std::string>& ids,
                             const std::string& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };

    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Delete folder").toStdString(),
        [this, folder, mediaIds, childIds, contains] {
            project_.folders.erase(
                std::remove_if(project_.folders.begin(), project_.folders.end(),
                               [&folder](const MediaBinFolder& f) {
                                   return f.id == folder.id;
                               }),
                project_.folders.end());
            for (MediaBinItem& item : project_.mediaBin) {
                if (contains(mediaIds, item.id)) {
                    item.folderId.clear();
                }
            }
            for (MediaBinFolder& f : project_.folders) {
                if (contains(childIds, f.id)) {
                    f.parentId.clear();
                }
            }
            emit projectChanged();
        },
        [this, folder, mediaIds, childIds, index, contains] {
            const size_t at = std::min(index, project_.folders.size());
            project_.folders.insert(
                project_.folders.begin() + static_cast<ptrdiff_t>(at), folder);
            for (MediaBinItem& item : project_.mediaBin) {
                if (contains(mediaIds, item.id)) {
                    item.folderId = folder.id;
                }
            }
            for (MediaBinFolder& f : project_.folders) {
                if (contains(childIds, f.id)) {
                    f.parentId = folder.id;
                }
            }
            emit projectChanged();
        }));
    return true;
}

bool ProjectController::moveMediaToFolder(const std::string& mediaId,
                                          const std::string& folderId) {
    if (!folderId.empty() && !findFolder(folderId)) {
        return false;
    }
    const auto it = std::find_if(
        project_.mediaBin.begin(), project_.mediaBin.end(),
        [&mediaId](const MediaBinItem& item) { return item.id == mediaId; });
    if (it == project_.mediaBin.end() || it->folderId == folderId) {
        return false;
    }
    const std::string oldFolderId = it->folderId;
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Move media to folder").toStdString(),
        [this, mediaId, folderId] {
            for (MediaBinItem& item : project_.mediaBin) {
                if (item.id == mediaId) {
                    item.folderId = folderId;
                    break;
                }
            }
            emit projectChanged();
        },
        [this, mediaId, oldFolderId] {
            for (MediaBinItem& item : project_.mediaBin) {
                if (item.id == mediaId) {
                    item.folderId = oldFolderId;
                    break;
                }
            }
            emit projectChanged();
        }));
    return true;
}

bool ProjectController::moveFolder(const std::string& folderId,
                                   const std::string& newParentId) {
    MediaBinFolder* folder = findFolder(folderId);
    if (!folder) {
        return false;
    }
    if (folderId == newParentId) {
        return false;  // cannot be its own parent
    }
    if (!newParentId.empty() && !findFolder(newParentId)) {
        return false;  // missing parent
    }
    if (isFolderDescendant(newParentId, folderId)) {
        return false;  // would create a cycle
    }
    const std::string oldParentId = folder->parentId;
    if (oldParentId == newParentId) {
        return false;
    }
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Move folder").toStdString(),
        [this, folderId, newParentId] {
            if (MediaBinFolder* f = findFolder(folderId)) {
                f->parentId = newParentId;
            }
            emit projectChanged();
        },
        [this, folderId, oldParentId] {
            if (MediaBinFolder* f = findFolder(folderId)) {
                f->parentId = oldParentId;
            }
            emit projectChanged();
        }));
    return true;
}

nlohmann::json ProjectController::titleSpecFor(const std::string& mediaId) const {
    const auto titles = project_.extensions.find("titles");
    if (titles == project_.extensions.end() || !titles->is_object()) {
        return nlohmann::json();
    }
    const auto spec = titles->find(mediaId);
    return spec == titles->end() ? nlohmann::json() : *spec;
}

std::string ProjectController::mediaIdForPath(const QString& path) const {
    const std::string rawPath = path.toStdString();
    for (const MediaBinItem& item : project_.mediaBin) {
        if (item.path == rawPath) return item.id;
    }
    return std::string();
}

bool ProjectController::removeFromMediaBin(const QString& id) {
    const std::string rawId = id.toStdString();
    const auto it = std::find_if(
        project_.mediaBin.begin(), project_.mediaBin.end(),
        [&rawId](const MediaBinItem& item) { return item.id == rawId; });
    if (it == project_.mediaBin.end()) {
        return false;
    }
    const MediaBinItem item = *it;
    const nlohmann::json spec = titleSpecFor(rawId);
    const size_t index = static_cast<size_t>(it - project_.mediaBin.begin());
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Remove media from bin").toStdString(),
        [this, item, spec] {
            project_.mediaBin.erase(
                std::remove_if(project_.mediaBin.begin(), project_.mediaBin.end(),
                               [&item](const MediaBinItem& it) { return it.id == item.id; }),
                project_.mediaBin.end());
            if (auto titles = project_.extensions.find("titles");
                titles != project_.extensions.end() && titles->is_object()) {
                titles->erase(item.id);
            }
            emit projectChanged();
        },
        [this, item, index, spec] {
            const auto after = std::min(index, project_.mediaBin.size());
            project_.mediaBin.insert(project_.mediaBin.begin() + static_cast<ptrdiff_t>(after), item);
            if (!spec.is_null()) {
                project_.extensions["titles"][item.id] = spec;
            }
            emit projectChanged();
        }));
    return true;
}

bool ProjectController::updateTitle(const std::string& mediaId,
                                    const bl::TitleSpec& spec,
                                    int canvasWidth, int canvasHeight,
                                    const QString& displayName) {
    const auto titles = project_.extensions.find("titles");
    if (titles == project_.extensions.end() || !titles->is_object()) {
        return false;
    }
    const auto specIt = titles->find(mediaId);
    if (specIt == titles->end()) {
        return false;  // not a title
    }
    const auto mediaIt = std::find_if(
        project_.mediaBin.begin(), project_.mediaBin.end(),
        [&mediaId](const MediaBinItem& item) { return item.id == mediaId; });
    if (mediaIt == project_.mediaBin.end()) {
        return false;
    }

    const nlohmann::json oldJson = *specIt;
    bl::TitleSpec oldSpec;
    try {
        oldSpec = oldJson.get<bl::TitleSpec>();
    } catch (const std::exception&) {
        return false;  // an unreadable old design cannot back a faithful undo
    }

    const std::string oldName = mediaIt->name;
    const std::string newName =
        displayName.isEmpty() ? oldName : displayName.toStdString();
    const QString path = QString::fromStdString(mediaIt->path);

    // Rendering lives in the command so undo/redo re-render symmetrically; the
    // up-front write below only exists to surface a write failure synchronously.
    const auto writePng = [canvasWidth, canvasHeight, path](const bl::TitleSpec& s) {
        return bl::ui::saveTitlePng(s, canvasWidth, canvasHeight, path);
    };
    if (!writePng(spec)) {
        return false;
    }

    const nlohmann::json newJson = nlohmann::json(spec);
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Edit title").toStdString(),
        [this, mediaId, newJson, newName, spec, writePng] {
            project_.extensions["titles"][mediaId] = newJson;
            setMediaName(mediaId, newName);
            writePng(spec);
            emit projectChanged();
        },
        [this, mediaId, oldJson, oldName, oldSpec, writePng] {
            project_.extensions["titles"][mediaId] = oldJson;
            setMediaName(mediaId, oldName);
            writePng(oldSpec);
            emit projectChanged();
        }));
    return true;
}

void ProjectController::setMediaName(const std::string& id,
                                     const std::string& name) {
    for (MediaBinItem& item : project_.mediaBin) {
        if (item.id == id) {
            item.name = name;
            return;
        }
    }
}

void ProjectController::adopt(ProjectData data, Timeline timeline, std::string filePath) {
    project_ = std::move(data);
    timeline_ = std::move(timeline);
    filePath_ = std::move(filePath);
    savedIndex_ = 0;
    undoStack_.clear();
    dirty_ = false;
    emit dirtyChanged(false);
    emit projectChanged();
}

void ProjectController::syncTimelineToExtensions() {
    project_.extensions["timeline"] = timeline_;
}

void ProjectController::recomputeDirty() {
    const bool dirty = undoStack_.index() != savedIndex_;
    if (dirty != dirty_) {
        dirty_ = dirty;
        emit dirtyChanged(dirty);
    }
}

void ProjectController::forceDirty() {
    savedIndex_ = undoStack_.index() + 1;
    recomputeDirty();
}

} // namespace bl::ui