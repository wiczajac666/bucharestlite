#ifndef BL_EXPORT_BATCH_QUEUE_H
#define BL_EXPORT_BATCH_QUEUE_H

#include <bl_export/export_types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bl {
namespace export_ {

class ExportEngine;

class BatchQueue {
public:
    explicit BatchQueue(ExportEngine& engine);
    ~BatchQueue();

    // Job management
    void addJob(BlExportJob* job);
    void removeJob(const char* job_name);
    void cancelJob(const char* job_name);
    void pauseJob(const char* job_name);
    void resumeJob(const char* job_name);

    // Processing
    void processNext();   // Process one job step
    void processAll();    // Process all jobs in queue

    // Status
    size_t pendingCount() const;
    size_t runningCount() const;
    size_t completedCount() const;
    size_t failedCount() const;
    BlExportJob* currentJob() const;
    const std::vector<BlExportJob*>& jobs() const;

    // Progress callback
    using QueueProgressCallback = void (*)(const BlExportJob* job, int percent,
                                                          size_t current_frame,
                                                          size_t total_frames,
                                                          void* user_data);
    void setProgressCallback(QueueProgressCallback cb, void* user_data);

private:
    ExportEngine& engine_;
    std::vector<BlExportJob*> jobs_;
    QueueProgressCallback progress_callback_ = nullptr;
    void* progress_user_data_ = nullptr;

    BlExportJob* findJob(const char* job_name);
    void updateProgress(BlExportJob* job);
    void invokeProgressCallback(BlExportJob* job);
};

}  // namespace export_
}  // namespace bl

#endif /* BL_EXPORT_BATCH_QUEUE_H */