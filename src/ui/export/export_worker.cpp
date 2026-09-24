#include "export/export_worker.hpp"

#include <bl_core/result.hpp>
#include <bl_timeline/sequence.hpp>

#include <QString>

namespace bl::ui {

ExportWorker::ExportWorker(QObject* parent) : QObject(parent) {}

void ExportWorker::cancel() { cancelled_.store(true); }

void ExportWorker::run(const Request& request) {
    cancelled_.store(false);

    ExportPlan plan;
    plan.settings = request.settings;
    const Sequence& sequence = request.snapshot.sequence();
    const Duration total = sequenceDuration(sequence);

    auto build = plan.build(sequence, total);
    if (!build.ok()) {
        emit finished(false, QString::fromStdString(build.message()));
        return;
    }

    ExportRunner::Input input{plan, request.snapshot, request.mediaBin,
                              request.plugins, &cancelled_};

    auto result = ExportRunner::run(
        std::move(input),
        [this](const ExportProgressInfo& info) {
            emit progress(info.percent, static_cast<int>(info.frame),
                          static_cast<int>(info.totalFrames),
                          static_cast<int>(info.framesPerSecond),
                          info.etaSeconds);
        });

    if (!result.ok()) {
        emit finished(false, QString::fromStdString(result.message()));
        return;
    }
    emit finished(true, QString());
}

} // namespace bl::ui