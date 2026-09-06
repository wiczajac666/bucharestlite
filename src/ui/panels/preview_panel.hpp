#pragma once

#include "transport/transport_controller.hpp"

#include <bl_core/plugin_loader.hpp>
#include <bl_core/time.hpp>
#include <bl_render/compositor.hpp>
#include <bl_render/preview_engine.hpp>
#include <bl_timeline/sequence.hpp>

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include <memory>
#include <string>
#include <utility>
#include <vector>

class QTimer;
class QLabel;
class QToolButton;

namespace bl::ui {

class ProjectController;

// Frame surface: paints the latest composited preview frame, letterboxed and
// scaled to fit, on a black backdrop.
class PreviewSurface : public QWidget {
    Q_OBJECT
public:
    explicit PreviewSurface(QWidget* parent = nullptr);

    void setFrame(const CompositorResult& result);
    void clearFrame();
    const QImage& lastFrame() const { return image_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
    QString message_;
};

// Preview dock: transport controls + the live frame surface. Drives playback
// through a Qt-free TransportController; the playhead is synced with the
// timeline via the PreviewPanel::playheadChanged signal (wired by MainWindow).
class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(ProjectController* controller,
                          QWidget* parent = nullptr,
                          std::vector<std::pair<std::string, bl::PluginOrigin>>
                              pluginDirs = {});

    TransportController& transport() { return transport_; }
    const TransportController& transport() const { return transport_; }

    PreviewSurface* surface() { return surface_; }
    const PreviewSurface* surface() const { return surface_; }

signals:
    void playheadChanged(const bl::Time& current);

public slots:
    // Called by the timeline when the user scrubs the ruler; mirrors the new
    // position into the transport without re-entering a render cycle.
    void setPlayheadFromTimeline(const bl::Time& t);
    // Called by the timeline when its contents change; re-derives the sequence
    // duration and refreshes the current frame.
    void onTimelineChanged();

private slots:
    void onTimerTick();
    void onProjectChanged();
    void onTransportPlayhead();
    void onTransportState();

private:
    void rebuildEngine();
    void refresh();
    void updateTimecode();
    static QString formatTimecode(bl::Time t, bl::Time total, bl::Rational fps);
    static bl::Time sequenceDuration(const bl::Sequence& sequence);

    ProjectController* controller_{nullptr};
    TransportController transport_;
    int32_t engineWidth_{0};
    int32_t engineHeight_{0};
    bl::Rational engineFps_{0, 1};
    std::unique_ptr<PreviewEngine> engine_;

    PreviewSurface* surface_{nullptr};
    QToolButton* playButton_{nullptr};
    QToolButton* stopButton_{nullptr};
    QToolButton* stepBackButton_{nullptr};
    QToolButton* stepForwardButton_{nullptr};
    QLabel* timecodeLabel_{nullptr};
    QTimer* timer_{nullptr};
    QElapsedTimer clock_;
    std::vector<std::pair<std::string, bl::PluginOrigin>> pluginDirs_;
};

} // namespace bl::ui