#include "export/export_queue.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>

namespace bl::ui {

namespace {

using IntegerType = unsigned long long;

std::string statusName(ExportJobStatus status) {
    switch (status) {
        case ExportJobStatus::Pending:
            return "pending";
        case ExportJobStatus::Running:
            return "running";
        case ExportJobStatus::Done:
            return "done";
        case ExportJobStatus::Failed:
            return "failed";
        case ExportJobStatus::Cancelled:
            return "cancelled";
    }
    return "pending";
}

bool statusFromName(const std::string& name, ExportJobStatus& out) {
    if (name == "pending")
        out = ExportJobStatus::Pending;
    else if (name == "running")
        out = ExportJobStatus::Running;
    else if (name == "done")
        out = ExportJobStatus::Done;
    else if (name == "failed")
        out = ExportJobStatus::Failed;
    else if (name == "cancelled")
        out = ExportJobStatus::Cancelled;
    else
        return false;
    return true;
}

nlohmann::json jobToJson(const ExportJob& job) {
    const ExportSettings& s = job.settings;
    return {{"id", job.id},
            {"outputPath", s.outputPath},
            {"container", s.container},
            {"videoCodec", s.videoCodec},
            {"audioCodec", s.audioCodec},
            {"includeAudio", s.includeAudio},
            {"videoCq", s.videoCq},
            {"audioBitrate", s.audioBitrate},
            {"scale", static_cast<IntegerType>(s.scale)},
            {"customWidth", s.customWidth},
            {"customHeight", s.customHeight},
            {"status", statusName(job.status)},
            {"message", job.message}};
}

void jobFromJson(const nlohmann::json& in, ExportJob& job) {
    const auto readStr = [&in](const char* key) {
        auto it = in.find(key);
        if (it != in.end() && it->is_string())
            return it->get<std::string>();
        return std::string{};
    };
    job.id = in.value("id", std::string{});
    if (job.id.empty()) {
        job.id = "job-" + in.value("outputPath", std::string{});
    }
    job.settings.outputPath = readStr("outputPath");
    job.settings.container = readStr("container");
    job.settings.videoCodec = readStr("videoCodec");
    job.settings.audioCodec = readStr("audioCodec");
    job.settings.includeAudio = in.value("includeAudio", true);
    job.settings.videoCq = in.value("videoCq", 0);
    job.settings.audioBitrate = in.value("audioBitrate", 192000);
    if (in.contains("scale") && in["scale"].is_number_integer()) {
        switch (in["scale"].get<unsigned long long>()) {
            case 1:
                job.settings.scale = ResolutionScale::Half;
                break;
            case 2:
                job.settings.scale = ResolutionScale::Quarter;
                break;
            case 3:
                job.settings.scale = ResolutionScale::Custom;
                break;
            default:
                job.settings.scale = ResolutionScale::Full;
                break;
        }
    }
    job.settings.customWidth = in.value("customWidth", 1920u);
    job.settings.customHeight = in.value("customHeight", 1080u);
    ExportJobStatus status = ExportJobStatus::Pending;
    if (statusFromName(readStr("status"), status)) {
        job.status = status;
    }
    job.message = readStr("message");
}

} // namespace

std::string defaultExportQueuePath() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(dir).filePath(QStringLiteral("export_queue.json")).toStdString();
}

std::vector<ExportJob> loadExportQueue(const std::string& path) {
    std::vector<ExportJob> jobs;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return jobs;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception&) {
        return jobs;
    }
    if (!root.is_array()) {
        return jobs;
    }
    for (const auto& entry : root) {
        if (!entry.is_object()) {
            continue;
        }
        ExportJob job;
        jobFromJson(entry, job);
        jobs.push_back(std::move(job));
    }
    return jobs;
}

bool saveExportQueue(const std::vector<ExportJob>& jobs,
                     const std::string& path) {
    const QString parent = QFileInfo(QString::fromStdString(path)).absolutePath();
    if (!QDir().mkpath(parent)) {
        return false;
    }
    QSaveFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    nlohmann::json array = nlohmann::json::array();
    for (const auto& job : jobs) {
        array.push_back(jobToJson(job));
    }
    const std::string body = array.dump(2);
    if (file.write(body.data(), static_cast<qint64>(body.size())) !=
        static_cast<qint64>(body.size())) {
        return false;
    }
    return file.commit();
}

} // namespace bl::ui