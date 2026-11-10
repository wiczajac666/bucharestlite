#pragma once

#include "transport/transport_controller.hpp"

#include <bl_audio/audio_engine.hpp>
#include <bl_audio/audio_meter_analysis.hpp>
#include <bl_audio/playback_audio_source.hpp>
#include <bl_core/codec_registry.hpp>
#include <bl_core/decoder_bridge.hpp>
#include <bl_core/demuxer.hpp>
#include <bl_core/plugin_loader.hpp>
#include <bl_core/time.hpp>
#include <bl_render/compositor.hpp>
#include <bl_render/preview_engine.hpp>
#include <bl_timeline/sequence.hpp>

#include <QElapsedTimer>
#include <QImage>
#include <QMetaType>
#include <QWidget>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

class QTimer;
class QLabel;
class QToolButton;

// Let a composited frame travel through Qt signal/slot connections (the scopes
// dock taps PreviewPanel::frameRendered).
Q_DECLARE_METATYPE(bl::CompositorResult)

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
    ~PreviewPanel();

    TransportController& transport() { return transport_; }
    const TransportController& transport() const { return transport_; }

    // Whether the preview decoder is currently allowed to use proxy files.
    bool useProxies() const { return useProxies_; }

    // Compositor backend of the current preview engine ("cpu", "opengl", or
    // "none" before the first engine build). GPU compositing is opt-in through
    // BL_COMPOSITOR=gpu|auto.
    std::string_view compositorBackendName() const;
    bool usingGpu() const;

    PreviewSurface* surface() { return surface_; }
    const PreviewSurface* surface() const { return surface_; }

    // Plugin discovery roots this panel was configured with; the export
    // pipeline (which loads its own plugins) reuses them so the app and the
    // renderer agree on which codec set is active.
    const std::vector<std::pair<std::string, bl::PluginOrigin>>& pluginDirs()
        const {
        return pluginDirs_;
    }

signals:
    void playheadChanged(const bl::Time& current);
    // Emitted after each render with the composited frame (an empty frame when
    // none is available) so scope views can tap the post-composite image.
    void frameRendered(const bl::CompositorResult& frame);

public slots:
    // Called by the timeline when the user scrubs the ruler; mirrors the new
    // position into the transport without re-entering a render cycle.
    void setPlayheadFromTimeline(const bl::Time& t);
    // Called by the timeline when its contents change; re-derives the sequence
    // duration and refreshes the current frame.
    void onTimelineChanged();

    // Playback loop (zone-aware when a loop range is set, otherwise full
    // sequence).
    void setLoop(bool enabled);
    bool loop() const;
    void setLoopRange(bl::Time in, bl::Time out);
    void clearLoopRange();

    // Per-media-item proxy override for the preview decoder.
    void setUseProxies(bool use);
    void setMediaProxyOverride(const std::string& mediaItemId, std::optional<bool> useProxy);
    std::optional<bool> mediaProxyOverride(const std::string& mediaItemId) const;

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

    // Proxy settings outlive engine rebuilds (project changes and size/fps
    // changes recreate the engine), so they live on the panel and are
    // re-applied whenever a fresh engine is created.
    bool useProxies_{false};
    std::map<std::string, std::optional<bool>> proxyOverrides_;

    PreviewSurface* surface_{nullptr};
    bl::CodecRegistry audioRegistry_;
    std::vector<std::unique_ptr<bl::PluginHandle>> audioHandles_;
    std::unique_ptr<bl::AudioEngine> audioEngine_;
    std::unordered_map<std::string, std::unique_ptr<bl::PlaybackAudioSource>>
        audioReaders_;
    double audioFeederCursor_{-1.0};
    std::atomic<bool> audioPlaying_{false};
    std::thread audioPlaybackThread_;
    std::thread audioFeederThread_;
    void startAudio();
    void stopAudio();
    void resetAudioReaders();
    void audioPlaybackLoop();
    void audioFeederLoop();
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