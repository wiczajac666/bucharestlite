#include "bl_export/batch_queue.h"

#include <bl_export/export_types.h>

#include <algorithm>
#include <cstring>

namespace bl {
namespace export_ {

BatchQueue::BatchQueue(ExportEngine& engine)
    : engine_(engine), progress_callback_(nullptr),
      progress_user_data_(nullptr) {}

BatchQueue::~BatchQueue() {
    jobs_.clear();
}

void BatchQueue::addJob(BlExportJob* job) {
    if (!job) return;
    job->status = (int)BlExportJobStatus::Pending;
    job->frames_encoded = 0;
    job->bytes_written = 0;
    job->elapsed_seconds = 0.0;
    job->eta_seconds = 0.0;
    jobs_.push_back(job);
}

void BatchQueue::removeJob(const char* job_name) {
    if (!job_name) return;
    auto it = std::remove_if(jobs_.begin(), jobs_.end(),
        [job_name](BlExportJob* j) {
            return j->name && strcmp(j->name, job_name) == 0;
        });
    jobs_.erase(it, jobs_.end());
}

void BatchQueue::cancelJob(const char* job_name) {
    BlExportJob* job = findJob(job_name);
    if (job) {
        job->status = (int)BlExportJobStatus::Cancelled;
    }
}

void BatchQueue::pauseJob(const char* job_name) {
    BlExportJob* job = findJob(job_name);
    if (job && job->status == (int)BlExportJobStatus::Running) {
        job->status = (int)BlExportJobStatus::Paused;
    }
}

void BatchQueue::resumeJob(const char* job_name) {
    BlExportJob* job = findJob(job_name);
    if (job && job->status == (int)BlExportJobStatus::Paused) {
        job->status = (int)BlExportJobStatus::Running;
    }
}

void BatchQueue::processNext() {
    // Find the next pending job
    for (BlExportJob* job : jobs_) {
        if (job->status == (int)BlExportJobStatus::Pending) {
            job->status = (int)BlExportJobStatus::Running;
            engine_.setJob(job);
            return;
        }
    }
}

void BatchQueue::processAll() {
    for (BlExportJob* job : jobs_) {
        if (job->status == (int)BlExportJobStatus::Pending) {
            job->status = (int)BlExportJobStatus::Running;
            engine_.setJob(job);
            // Process this job (in a real implementation, this would
            // encode all frames and write to muxer)
            // For now, mark as completed
            job->status = (int)BlExportJobStatus::Completed;
            updateProgress(job);
            invokeProgressCallback(job);
        }
    }
}

size_t BatchQueue::pendingCount() const {
    size_t count = 0;
    for (const auto* job : jobs_) {
        if (job->status == (int)BlExportJobStatus::Pending) ++count;
    }
    return count;
}

size_t BatchQueue::runningCount() const {
    size_t count = 0;
    for (const auto* job : jobs_) {
        if (job->status == (int)BlExportJobStatus::Running) ++count;
    }
    return count;
}

size_t BatchQueue::completedCount() const {
    size_t count = 0;
    for (const auto* job : jobs_) {
        if (job->status == (int)BlExportJobStatus::Completed) ++count;
    }
    return count;
}

size_t BatchQueue::failedCount() const {
    size_t count = 0;
    for (const auto* job : jobs_) {
        if (job->status == (int)BlExportJobStatus::Failed) ++count;
    }
    return count;
}

BlExportJob* BatchQueue::currentJob() const {
    for (auto* job : jobs_) {
        if (job->status == (int)BlExportJobStatus::Running) {
            return job;
        }
    }
    return nullptr;
}

const std::vector<BlExportJob*>& BatchQueue::jobs() const {
    return jobs_;
}

void BatchQueue::setProgressCallback(QueueProgressCallback cb,
                                                       void* user_data) {
    progress_callback_ = cb;
    progress_user_data_ = user_data;
}

BlExportJob* BatchQueue::findJob(const char* job_name) {
    if (!job_name) return nullptr;
    for (auto* job : jobs_) {
        if (job->name && strcmp(job->name, job_name) == 0) {
            return job;
        }
    }
    return nullptr;
}

void BatchQueue::updateProgress(BlExportJob* job) {
    if (!job) return;

    // Calculate elapsed time (simplified - in a real implementation,
    // this would track actual encode time)
    if (job->frames_total > 0) {
        job->elapsed_seconds =
            static_cast<double>(job->frames_encoded) / 30.0;  // Assume 30fps
        double remaining_frames =
            static_cast<double>(job->frames_total - job->frames_encoded);
        job->eta_seconds = remaining_frames / 30.0;
    }
}

void BatchQueue::invokeProgressCallback(BlExportJob* job) {
    if (!progress_callback_ || !job) return;

    int percent = 0;
    if (job->frames_total > 0) {
        percent = static_cast<int>(
            (static_cast<double>(job->frames_encoded) /
             static_cast<double>(job->frames_total)) * 100.0);
    }

    progress_callback_(job, percent, job->frames_encoded,
                       job->frames_total, progress_user_data_);
}

}  // namespace export_
}  // namespace bl