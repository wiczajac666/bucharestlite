#pragma once

#include <bl_core/plugin_loader.hpp>
#include <bl_core/time.hpp>
#include <bl_render/preview_engine.hpp>

#include <QWidget>

#include <memory>
#include <string>
#include <utility>
#include <vector>

class QLabel;
class QPushButton;
class QSlider;

namespace bl::ui {

class ProjectController;
class PreviewSurface;

// Source monitor for three-point editing.
//
// Watches one media-bin item, shows the source frame at a scrubbable position,
// and holds an in/out mark pair. Insert/overwrite requests are emitted with the
// marked source window; the receiver (MainWindow → TimelinePanel) performs the
// actual edit at the timeline playhead. The panel never touches the timeline
// model directly, so it can also drive a future second monitor.
class SourceMonitorPanel : public QWidget {
    Q_OBJECT
public:
    explicit SourceMonitorPanel(
        ProjectController* controller, QWidget* parent = nullptr,
        std::vector<std::pair<std::string, bl::PluginOrigin>> pluginDirs = {});

    std::string mediaId() const { return mediaId_; }
    bl::Time markIn() const { return markIn_; }
    bl::Time markOut() const { return markOut_; }
    double positionSeconds() const { return positionSeconds_; }
    double durationSeconds() const { return durationSeconds_; }
    bool hasMarks() const;

    // Test / scripting entry points (the buttons and shortcuts call these).
    bool insertEdit();
    bool overwriteEdit();

public slots:
    // Loads `mediaId` from the project media bin. Unknown ids clear the panel.
    void setMedia(const std::string& mediaId);
    void clearMedia();
    void setPositionSeconds(double seconds);
    void markInHere();
    void markOutHere();
    void clearMarks();
    void refresh();

signals:
    void insertRequested(const std::string& mediaId, bl::Time in,
                         bl::Time out);
    void overwriteRequested(const std::string& mediaId, bl::Time in,
                            bl::Time out);
    void statusMessage(const QString& message);

private:
    void rebuildEngine();
    void refreshFrame();
    void updateLabels();
    void syncSlider();
    double fps() const;

    ProjectController* controller_{nullptr};
    std::vector<std::pair<std::string, bl::PluginOrigin>> pluginDirs_;

    std::string mediaId_;
    std::string mediaPath_;
    std::string mediaName_;
    double durationSeconds_{0.0};
    double positionSeconds_{0.0};
    bl::Time markIn_{};
    bl::Time markOut_{};
    bool hasVideo_{false};
    bool hasAudio_{false};

    std::unique_ptr<bl::PreviewEngine> engine_;
    uint32_t engineWidth_{0};
    uint32_t engineHeight_{0};
    bl::Rational engineFps_{};
    bool syncingSlider_{false};

    PreviewSurface* surface_{nullptr};
    QLabel* mediaLabel_{nullptr};
    QLabel* timecodeLabel_{nullptr};
    QLabel* marksLabel_{nullptr};
    QSlider* positionSlider_{nullptr};
    QPushButton* markInButton_{nullptr};
    QPushButton* markOutButton_{nullptr};
    QPushButton* clearMarksButton_{nullptr};
    QPushButton* insertButton_{nullptr};
    QPushButton* overwriteButton_{nullptr};
};

} // namespace bl::ui
