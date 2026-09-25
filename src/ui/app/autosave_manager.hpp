#pragma once

#include <bl_core/autosave_ring.hpp>

#include <QObject>
#include <QString>

#include <memory>
#include <string>

class QTimer;

namespace bl::ui {

class ProjectController;

// Owns the autosave ring for the open project and drives the snapshot policy:
// a snapshot is written every `intervalSeconds` while the document is dirty,
// immediately when it becomes dirty, and whenever rotate() is called (the
// timer fires it, and MainWindow calls it on focus loss and right before the
// close prompt). The ring is keyed by the project name; a successful manual
// save clears it.
class AutosaveManager : public QObject {
    Q_OBJECT
public:
    explicit AutosaveManager(ProjectController* controller,
                             QObject* parent = nullptr);
    ~AutosaveManager() override;

    void setAutosaveRoot(const QString& root);
    void setIntervalSeconds(int seconds);
    int intervalSeconds() const;

    // Path of the newest autosave that is newer than the open document's file,
    // or empty when there is nothing recoverable (including when the project
    // has no file path yet but an autosave ring exists).
    QString autosaveNewerThanMain() const;
    bool hasAutosaves() const;

public slots:
    void maybeAutosave();
    void clear();

signals:
    void snapshotFailed(const QString& message);

private slots:
    void onProjectChanged();
    void onDirtyChanged(bool dirty);

private:
    void rebuildRing();
    bool snapshotNow();

    ProjectController* controller_;
    std::unique_ptr<AutosaveRing> ring_;
    QString root_;
    QTimer* timer_{nullptr};
    int intervalSeconds_{60};
    std::string ringKey_;
};

} // namespace bl::ui