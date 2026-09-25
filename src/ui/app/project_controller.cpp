#include "app/project_controller.hpp"

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
    const auto existing = std::find_if(
        project_.mediaBin.begin(), project_.mediaBin.end(),
        [&rawPath](const MediaBinItem& item) { return item.path == rawPath; });
    if (existing != project_.mediaBin.end()) {
        return false;
    }

    const MediaBinItem item{std::to_string(++idSeed_), rawPath, fileBaseName(rawPath)};
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

bool ProjectController::removeFromMediaBin(const QString& id) {
    const std::string rawId = id.toStdString();
    const auto it = std::find_if(
        project_.mediaBin.begin(), project_.mediaBin.end(),
        [&rawId](const MediaBinItem& item) { return item.id == rawId; });
    if (it == project_.mediaBin.end()) {
        return false;
    }
    const MediaBinItem item = *it;
    const size_t index = static_cast<size_t>(it - project_.mediaBin.begin());
    undoStack_.push(std::make_unique<FunctionCommand>(
        QStringLiteral("Remove media from bin").toStdString(),
        [this, item] {
            project_.mediaBin.erase(
                std::remove_if(project_.mediaBin.begin(), project_.mediaBin.end(),
                               [&item](const MediaBinItem& it) { return it.id == item.id; }),
                project_.mediaBin.end());
            emit projectChanged();
        },
        [this, item, index] {
            const auto after = std::min(index, project_.mediaBin.size());
            project_.mediaBin.insert(project_.mediaBin.begin() + static_cast<ptrdiff_t>(after), item);
            emit projectChanged();
        }));
    return true;
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