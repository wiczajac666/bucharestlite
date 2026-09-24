#pragma once

#include "export/export_plan.hpp"
#include "export/export_runner.hpp"
#include "export/export_settings.hpp"

#include <bl_core/project_data.hpp>
#include <bl_render/media_decode_source.hpp>
#include <bl_timeline/timeline.hpp>

#include <QObject>
#include <QString>

#include <atomic>
#include <vector>

namespace bl::ui {

// Runs an export in a background thread. The request is captured on the GUI
// thread (dialog + controller state), then run() moves it to the worker's
// thread; progress and completion are reported via signals so the UI never
// touches the pipeline directly.
class ExportWorker : public QObject {
    Q_OBJECT
public:
    struct Request {
        ExportSettings settings;
        TimelineSnapshot snapshot;  // shared_ptr-backed; safe to copy over
        std::vector<MediaBinItem> mediaBin;
        MediaDecodeSource::Spec plugins;
    };

    explicit ExportWorker(QObject* parent = nullptr);

signals:
    void progress(int percent, int frame, int total, double fps, double eta);
    void finished(bool ok, const QString& error);

public slots:
    void cancel();
    void run(const Request& request);

private:
    std::atomic<bool> cancelled_{false};
};

} // namespace bl::ui