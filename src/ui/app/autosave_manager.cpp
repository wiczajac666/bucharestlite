#include "app/autosave_manager.hpp"
#include "app/project_controller.hpp"

#include <bl_core/platform/paths.hpp>

#include <QTimer>

namespace bl::ui {

AutosaveManager::AutosaveManager(ProjectController* controller,
                                 QObject* parent)
    : QObject(parent), controller_(controller),
      root_(QString::fromStdString(bl::platform::userDataRoot())) {
    timer_ = new QTimer(this);
    timer_->setInterval(intervalSeconds_ * 1000);
    connect(timer_, &QTimer::timeout, this, &AutosaveManager::maybeAutosave);
    connect(controller_, &ProjectController::projectChanged, this,
            &AutosaveManager::onProjectChanged);
    connect(controller_, &ProjectController::dirtyChanged, this,
            &AutosaveManager::onDirtyChanged);
    connect(controller_, &ProjectController::saved, this,
            &AutosaveManager::clear);
    rebuildRing();
}

AutosaveManager::~AutosaveManager() = default;

void AutosaveManager::setAutosaveRoot(const QString& root) {
    if (root == root_) {
        return;
    }
    root_ = root;
    ringKey_.clear();
    rebuildRing();
}

void AutosaveManager::setIntervalSeconds(int seconds) {
    intervalSeconds_ = seconds > 0 ? seconds : 60;
    timer_->setInterval(intervalSeconds_ * 1000);
}

int AutosaveManager::intervalSeconds() const {
    return intervalSeconds_;
}

QString AutosaveManager::autosaveNewerThanMain() const {
    if (!controller_->isOpen()) {
        return QString();
    }
    const auto newest = ring_->newest();
    if (!newest.ok()) {
        return QString();
    }
    if (!bl::isAutosaveNewerThan(newest->path,
                                 controller_->filePath().toStdString())) {
        return QString();
    }
    return QString::fromStdString(newest->path);
}

bool AutosaveManager::hasAutosaves() const {
    return !ring_->list().empty();
}

void AutosaveManager::maybeAutosave() {
    if (!controller_->isOpen() || !controller_->dirty()) {
        return;
    }
    snapshotNow();
}

void AutosaveManager::clear() {
    ring_->removeAll();
}

void AutosaveManager::onProjectChanged() {
    rebuildRing();
    onDirtyChanged(controller_->dirty());
}

void AutosaveManager::onDirtyChanged(bool dirty) {
    if (dirty) {
        timer_->start();
        snapshotNow();
    } else {
        timer_->stop();
    }
}

void AutosaveManager::rebuildRing() {
    const std::string key = controller_->projectName().toStdString();
    if (ring_ && key == ringKey_) {
        return;
    }
    ringKey_ = key;
    ring_ = std::make_unique<AutosaveRing>(
        key, AutosaveRing::kDefaultCapacity, root_.toStdString());
}

bool AutosaveManager::snapshotNow() {
    if (!ring_) {
        return false;
    }
    auto result = ring_->rotate(controller_->serializeDocument());
    if (!result.ok()) {
        emit snapshotFailed(QString::fromStdString(result.message()));
        return false;
    }
    return true;
}

} // namespace bl::ui