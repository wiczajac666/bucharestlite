#include <bl_core/demuxer.hpp>
#include <bl_core/job_manager.hpp>
#include <bl_core/media_source.hpp>
#include "test_media_utils.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifndef BL_TEST_MEDIA_DIR
#define BL_TEST_MEDIA_DIR "."
#endif

namespace {

using bl::CancelToken;
using bl::Demuxer;
using bl::Duration;
using bl::Err;
using bl::IJob;
using bl::ICancelToken;
using bl::JobEventType;
using bl::JobManager;
using bl::JobPriority;
using bl::LambdaJob;
using bl::MediaLocator;
using bl::MediaSource;
using bl::Packet;
using bl::Rational;
using bl::Result;
using bl::StreamInfo;
using bl::Time;

std::string videoFixture() { return bltest::mediaPath("test_video.mp4"); }
std::string audioFixture() { return bltest::mediaPath("test_audio.wav"); }
std::string avFixture() { return bltest::mediaPath("test_av.mp4"); }
std::string mkvVideoFixture() { return bltest::mediaPath("test_video.mkv"); }
std::string mkvAvFixture() { return bltest::mediaPath("test_av.mkv"); }

class EventCollector {
public:
    void attach(JobManager& manager) {
        manager.onEvent([this](const bl::JobEvent& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            events_.push_back(e);
            cv_.notify_all();
        });
    }

    std::vector<bl::JobEvent> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

    template <typename Predicate>
    bool waitFor(Predicate pred, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] {
            return pred(events_);
        });
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<bl::JobEvent> events_;
};

TEST(MediaProbeTest, MissingFileReportsFileNotFound) {
    auto result = MediaSource::probe(MediaLocator{"/nonexistent/nope.mp4"});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::FileNotFound);
}

TEST(MediaProbeTest, EmptyPathRejected) {
    auto result = MediaSource::probe(MediaLocator{""});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::InvalidArgument);
}

TEST(MediaProbeTest, VideoFixtureProperties) {
    auto result = MediaSource::probe(MediaLocator{videoFixture()});
    ASSERT_TRUE(result.ok()) << result.message();
    const StreamInfo& info = *result;

    EXPECT_EQ(info.videoStreams.size(), 1u);
    EXPECT_EQ(info.audioStreams.size(), 0u);

    const auto& v = info.videoStreams.front();
    EXPECT_EQ(v.width, 320u);
    EXPECT_EQ(v.height, 240u);
    EXPECT_NEAR(v.fps.toDouble(), 24.0, 0.01);
    EXPECT_FALSE(v.codecName.empty());

    const double seconds = info.duration.toSeconds();
    EXPECT_GT(seconds, 0.9);
    EXPECT_LT(seconds, 1.2);
}

TEST(MediaProbeTest, AudioFixtureProperties) {
    auto result = MediaSource::probe(MediaLocator{audioFixture()});
    ASSERT_TRUE(result.ok()) << result.message();
    const StreamInfo& info = *result;

    EXPECT_EQ(info.videoStreams.size(), 0u);
    EXPECT_EQ(info.audioStreams.size(), 1u);

    const auto& a = info.audioStreams.front();
    EXPECT_EQ(a.sampleRate, 48000u);
    EXPECT_EQ(a.channels, 2u);

    const double seconds = info.duration.toSeconds();
    EXPECT_GT(seconds, 0.9);
    EXPECT_LT(seconds, 1.2);
}

TEST(MediaProbeTest, MuxedFixtureHasBothStreams) {
    auto result = MediaSource::probe(MediaLocator{avFixture()});
    ASSERT_TRUE(result.ok()) << result.message();

    EXPECT_EQ((*result).videoStreams.size(), 1u);
    EXPECT_EQ((*result).audioStreams.size(), 1u);
}

// Matroska stores codec configuration in CodecPrivate, so an import must
// surface it as StreamInfo extradata. Without it a later remux/re-encode to MKV
// writes incomplete track headers (the reason MKV export was deferred).
TEST(MediaProbeTest, MkvVideoFixturePreservesVideoExtradata) {
    auto result = MediaSource::probe(MediaLocator{mkvVideoFixture()});
    ASSERT_TRUE(result.ok()) << result.message();
    const StreamInfo& info = *result;

    ASSERT_EQ(info.videoStreams.size(), 1u);
    EXPECT_EQ(info.audioStreams.size(), 0u);

    const auto& v = info.videoStreams.front();
    EXPECT_EQ(v.width, 320u);
    EXPECT_EQ(v.height, 240u);
    EXPECT_EQ(v.codecName, "h264");
    // Non-empty CodecPrivate: FFmpeg reports the avcC-style extradata block.
    EXPECT_FALSE(v.extradata.empty());

    // Same geometry as the MP4 fixture it was transmuxed from.
    const double seconds = info.duration.toSeconds();
    EXPECT_GT(seconds, 0.9);
    EXPECT_LT(seconds, 1.2);
}

TEST(MediaProbeTest, MkvMuxedFixtureHasBothStreamsWithAudioExtradata) {
    auto result = MediaSource::probe(MediaLocator{mkvAvFixture()});
    ASSERT_TRUE(result.ok()) << result.message();
    const StreamInfo& info = *result;

    ASSERT_EQ(info.videoStreams.size(), 1u);
    ASSERT_EQ(info.audioStreams.size(), 1u);
    EXPECT_FALSE(info.videoStreams.front().extradata.empty());
    // AAC keeps its AudioSpecificConfig in CodecPrivate.
    EXPECT_FALSE(info.audioStreams.front().extradata.empty());
}

TEST(MediaProbeTest, MkvContainerNameIsReported) {
    auto result = MediaSource::probe(MediaLocator{mkvVideoFixture()});
    ASSERT_TRUE(result.ok()) << result.message();
    EXPECT_NE((*result).containerName.find("matroska"), std::string::npos);
}

TEST(DemuxerTest, MkvInfoCarriesExtradata) {
    auto opened = Demuxer::open(mkvVideoFixture());
    ASSERT_TRUE(opened.ok()) << opened.message();
    Demuxer demuxer = std::move(opened).value();

    ASSERT_EQ(demuxer.info().videoStreams.size(), 1u);
    // Demuxer::open() routes through MediaSource::probe(), so extradata must
    // survive into the demuxer's own StreamInfo.
    EXPECT_FALSE(demuxer.info().videoStreams.front().extradata.empty());
}

TEST(DemuxerTest, ReadsPacketsFromMkv) {
    auto opened = Demuxer::open(mkvAvFixture());
    ASSERT_TRUE(opened.ok()) << opened.message();
    Demuxer demuxer = std::move(opened).value();

    const std::vector<bl::VideoStreamInfo> videos = demuxer.info().videoStreams;
    ASSERT_FALSE(videos.empty());
    const int videoIndex = videos.front().index;

    int packets = 0;
    int videoKeyframes = 0;
    for (int guard = 0; guard < 500; ++guard) {
        auto next = demuxer.nextPacket();
        ASSERT_TRUE(next.ok()) << next.message();
        if (!next->has_value()) break;
        if ((*next)->streamIndex == videoIndex) {
            EXPECT_GT((*next)->data.size(), 0u);
            ++packets;
            if ((*next)->keyframe) ++videoKeyframes;
        }
    }
    EXPECT_GT(packets, 0) << "no video packets decoded from MKV";
    // A transmuxed H.264 MKV opens on a keyframe, which the first-frame
    // thumbnail path relies on.
    EXPECT_GT(videoKeyframes, 0);
}

TEST(DemuxerTest, OpenMissingFileFails) {
    auto result = Demuxer::open("/nonexistent/nope.mp4");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.code(), Err::FileNotFound);
}

TEST(DemuxerTest, IterateVideoPacketsToEof) {
    auto opened = Demuxer::open(videoFixture());
    ASSERT_TRUE(opened.ok()) << opened.message();
    Demuxer demuxer = std::move(*opened);

    int packets = 0;
    bool sawKeyframe = false;
    std::optional<int64_t> lastDts;

    for (;;) {
        auto next = demuxer.nextPacket();
        ASSERT_TRUE(next.ok()) << next.message();
        if (!*next) break;

        const Packet& pkt = **next;
        EXPECT_GE(pkt.streamIndex, 0);
        EXPECT_FALSE(pkt.data.empty());

        if (pkt.keyframe) sawKeyframe = true;

        if (pkt.hasDts && lastDts) {
            EXPECT_GE(pkt.dts.ticks, *lastDts)
                << "dts regressed at packet " << packets;
        }
        if (pkt.hasDts) {
            lastDts = pkt.dts.ticks;
        }
        ++packets;
    }

    EXPECT_GT(packets, 10);
    EXPECT_TRUE(sawKeyframe);
}

TEST(DemuxerTest, SeekNearMiddleContinuesMonotonic) {
    auto probe = MediaSource::probe(MediaLocator{avFixture()});
    ASSERT_TRUE(probe.ok());

    auto opened = Demuxer::open(avFixture());
    ASSERT_TRUE(opened.ok()) << opened.message();
    Demuxer demuxer = std::move(*opened);

    const double halfSeconds = (*probe).duration.toSeconds() / 2.0;
    Time target =
        Time::fromSeconds(halfSeconds, Rational{1'000'000, 1});

    ASSERT_TRUE(demuxer.seek(target).ok());

    const auto videoIndex = demuxer.info().videoStreams.front().index;
    std::optional<int64_t> firstVideoPtsAfterSeek;
    size_t readAfterSeek = 0;

    for (;;) {
        auto next = demuxer.nextPacket();
        ASSERT_TRUE(next.ok()) << next.message();
        if (!*next) break;

        const Packet& pkt = **next;
        if (pkt.streamIndex != videoIndex || !pkt.hasPts) continue;

        if (!firstVideoPtsAfterSeek) {
            firstVideoPtsAfterSeek = pkt.pts.ticks;
        } else {
            EXPECT_GE(pkt.pts.ticks, *firstVideoPtsAfterSeek);
        }
        ++readAfterSeek;
    }
    EXPECT_GT(readAfterSeek, 5u);
    EXPECT_TRUE(firstVideoPtsAfterSeek.has_value());
}

TEST(DemuxerTest, MoveSemanticsPreserveState) {
    auto opened = Demuxer::open(videoFixture());
    ASSERT_TRUE(opened.ok());
    Demuxer moved = std::move(*opened);
    EXPECT_EQ(moved.info().videoStreams.size(), 1u);

    auto next = moved.nextPacket();
    ASSERT_TRUE(next.ok());
    EXPECT_TRUE(*next);

    Demuxer reassigned = std::move(moved);
    auto again = reassigned.nextPacket();
    ASSERT_TRUE(again.ok());
    EXPECT_TRUE(*again);
}

TEST(JobManagerTest, NullJobRejected) {
    JobManager manager(1);
    EXPECT_EQ(manager.enqueue(nullptr), 0u);
    manager.shutdown();
}

TEST(JobManagerTest, ExecutesAndEmitsLifecycleEvents) {
    EventCollector events;
    JobManager manager(1);
    events.attach(manager);

    std::atomic<bool> ran{false};
    manager.enqueue(std::make_unique<LambdaJob>(
        "work", [&](const bl::ProgressFn&, const ICancelToken&) {
            ran.store(true);
            return Result<void>();
        }));

    EXPECT_TRUE(events.waitFor(
        [&](const std::vector<bl::JobEvent>& evts) {
            for (const auto& e : evts) {
                if (e.type == JobEventType::Done &&
                    e.jobName == "work") return true;
            }
            return false;
        },
        std::chrono::seconds(5)));

    EXPECT_TRUE(ran.load());
    const auto all = events.snapshot();
    std::vector<JobEventType> sequence;
    for (const auto& e : all) {
        if (e.jobName == "work") sequence.push_back(e.type);
    }
    ASSERT_GE(sequence.size(), 3u);
    EXPECT_EQ(sequence.front(), JobEventType::Queued);
    EXPECT_EQ(sequence[1], JobEventType::Started);
    EXPECT_EQ(sequence.back(), JobEventType::Done);

    manager.shutdown();
}

TEST(JobManagerTest, PriorityOrderSingleWorker) {
    EventCollector events;
    JobManager manager(1);
    events.attach(manager);

    std::atomic<bool> release{false};

    manager.enqueue(std::make_unique<LambdaJob>(
        "gate",
        [&](const bl::ProgressFn&, const ICancelToken& token) {
            while (!release.load() && !token.cancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return Result<void>();
        },
        JobPriority::High));

    std::vector<std::string> order = {"low_a", "normal_a", "high_a",
                                      "low_b", "high_b"};
    std::map<std::string, JobPriority> priorities{
        {"low_a", JobPriority::Low},   {"normal_a", JobPriority::Normal},
        {"high_a", JobPriority::High}, {"low_b", JobPriority::Low},
        {"high_b", JobPriority::High},
    };
    for (const auto& name : order) {
        manager.enqueue(std::make_unique<LambdaJob>(
            name,
            [](const bl::ProgressFn&, const ICancelToken&) {
                return Result<void>();
            },
            priorities[name]));
    }

    release.store(true);

    EXPECT_TRUE(events.waitFor(
        [&](const std::vector<bl::JobEvent>& evts) {
            std::set<std::string> done;
            for (const auto& e : evts) {
                if (e.type == JobEventType::Done ||
                    e.type == JobEventType::Cancelled) {
                    done.insert(e.jobName);
                }
            }
            return done.size() == order.size() + 1;
        },
        std::chrono::seconds(5)));

    std::vector<std::string> startedOrder;
    for (const auto& e : events.snapshot()) {
        if (e.type == JobEventType::Started &&
            e.jobName != "gate") {
            startedOrder.push_back(e.jobName);
        }
    }
    ASSERT_EQ(startedOrder.size(), order.size());
    EXPECT_EQ(startedOrder[0], "high_a");
    EXPECT_EQ(startedOrder[1], "high_b");
    EXPECT_EQ(startedOrder[2], "normal_a");
    EXPECT_EQ(startedOrder[3], "low_a");
    EXPECT_EQ(startedOrder[4], "low_b");

    manager.shutdown();
}

TEST(JobManagerTest, CancelQueuedPreventsExecution) {
    EventCollector events;
    JobManager manager(1);
    events.attach(manager);

    std::atomic<bool> release{false};
    manager.enqueue(std::make_unique<LambdaJob>(
        "blocker",
        [&](const bl::ProgressFn&, const ICancelToken& token) {
            while (!release.load() && !token.cancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return Result<void>();
        },
        JobPriority::High));

    std::atomic<bool> victimRan{false};
    bl::JobId victim = manager.enqueue(std::make_unique<LambdaJob>(
        "victim",
        [&](const bl::ProgressFn&, const ICancelToken&) {
            victimRan.store(true);
            return Result<void>();
        },
        JobPriority::Low));

    EXPECT_TRUE(manager.cancel(victim));

    std::atomic<bool> afterRan{false};
    manager.enqueue(std::make_unique<LambdaJob>(
        "after",
        [&](const bl::ProgressFn&, const ICancelToken&) {
            afterRan.store(true);
            return Result<void>();
        },
        JobPriority::Normal));

    release.store(true);

    EXPECT_TRUE(events.waitFor(
        [&](const std::vector<bl::JobEvent>& evts) {
            for (const auto& e : evts) {
                if (e.jobName == "after" && e.type == JobEventType::Done) {
                    return true;
                }
            }
            return false;
        },
        std::chrono::seconds(5)));

    EXPECT_FALSE(victimRan.load());
    EXPECT_TRUE(afterRan.load());

    bool cancelEventSeen = false;
    for (const auto& e : events.snapshot()) {
        if (e.jobName == "victim" && e.type == JobEventType::Cancelled) {
            cancelEventSeen = true;
        }
    }
    EXPECT_TRUE(cancelEventSeen);

    manager.shutdown();
}

TEST(JobManagerTest, CancelRunningIsCooperative) {
    EventCollector events;
    JobManager manager(1);
    events.attach(manager);

    std::atomic<bool> enteredLoop{false};
    bl::JobId runnerId = manager.enqueue(std::make_unique<LambdaJob>(
        "runner",
        [&](const bl::ProgressFn&, const ICancelToken& token) {
            enteredLoop.store(true);
            while (!token.cancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return Result<void>::err(Err::Cancelled, "cooperative exit");
        },
        JobPriority::High));

    EXPECT_TRUE(events.waitFor(
        [&](const std::vector<bl::JobEvent>&) {
            return enteredLoop.load();
        },
        std::chrono::seconds(5)));

    EXPECT_TRUE(manager.cancel(runnerId));

    EXPECT_TRUE(events.waitFor(
        [&](const std::vector<bl::JobEvent>& evts) {
            for (const auto& e : evts) {
                if (e.jobName == "runner" &&
                    e.type == JobEventType::Cancelled) {
                    return true;
                }
            }
            return false;
        },
        std::chrono::seconds(5)));

    manager.shutdown();
}

TEST(JobManagerTest, ProgressEventsForwarded) {
    EventCollector events;
    JobManager manager(1);
    events.attach(manager);

    manager.enqueue(std::make_unique<LambdaJob>(
        "reporter",
        [&](const bl::ProgressFn& report, const ICancelToken& token) {
            for (int step = 1; step <= 3; ++step) {
                if (token.cancelled()) {
                    return Result<void>::err(Err::Cancelled);
                }
                report(step / 3.0f, "step " + std::to_string(step));
            }
            return Result<void>();
        }));

    EXPECT_TRUE(events.waitFor(
        [&](const std::vector<bl::JobEvent>& evts) {
            for (const auto& e : evts) {
                if (e.jobName == "reporter" &&
                    e.type == JobEventType::Progress &&
                    e.ratio > 0.99f) {
                    return true;
                }
            }
            return false;
        },
        std::chrono::seconds(5)));

    manager.shutdown();
}

TEST(JobManagerTest, FailedJobReportsFailureMessage) {
    EventCollector events;
    JobManager manager(1);
    events.attach(manager);

    manager.enqueue(std::make_unique<LambdaJob>(
        "doomed",
        [](const bl::ProgressFn&, const ICancelToken&) {
            return Result<void>::err(Err::DecodeFailed, "bad stream");
        }));

    EXPECT_TRUE(events.waitFor(
        [&](const std::vector<bl::JobEvent>& evts) {
            for (const auto& e : evts) {
                if (e.jobName == "doomed" && e.type == JobEventType::Failed) {
                    EXPECT_EQ(e.message, "bad stream");
                    return true;
                }
            }
            return false;
        },
        std::chrono::seconds(5)));

    manager.shutdown();
}

TEST(JobManagerTest, ConcurrentProducersStress) {
    JobManager manager(4);

    constexpr int kProducerThreads = 4;
    constexpr int kJobsPerThread = 100;
    std::atomic<int> completed{0};

    std::mutex counterMutex;
    manager.onEvent([&](const bl::JobEvent& e) {
        if (e.type == JobEventType::Done) {
            std::lock_guard<std::mutex> lock(counterMutex);
            ++completed;
        }
    });

    std::vector<std::thread> producers;
    for (int t = 0; t < kProducerThreads; ++t) {
        producers.emplace_back([&manager, t] {
            for (int i = 0; i < kJobsPerThread; ++i) {
                manager.enqueue(std::make_unique<LambdaJob>(
                    "stress_" + std::to_string(t) + "_" + std::to_string(i),
                    [](const bl::ProgressFn&, const ICancelToken&) {
                        return Result<void>();
                    }));
            }
        });
    }
    for (auto& thread : producers) thread.join();

    manager.shutdown(JobManager::ShutdownMode::Drain);

    EXPECT_EQ(completed.load(),
              kProducerThreads * kJobsPerThread);
    EXPECT_EQ(manager.pendingCount(), 0u);
    EXPECT_EQ(manager.activeCount(), 0u);
}

TEST(JobManagerTest, ShutdownDrainProcessesRemainingJobs) {
    EventCollector events;
    JobManager manager(2);
    events.attach(manager);

    std::atomic<int> executed{0};
    for (int i = 0; i < 20; ++i) {
        manager.enqueue(std::make_unique<LambdaJob>(
            "queued",
            [&](const bl::ProgressFn&, const ICancelToken&) {
                executed.fetch_add(1);
                return Result<void>();
            }));
    }

    manager.shutdown(JobManager::ShutdownMode::Drain);
    EXPECT_EQ(executed.load(), 20);
}

TEST(JobManagerTest, ShutdownAbandonCancelsPending) {
    EventCollector events;
    {
        JobManager manager(1);
        events.attach(manager);

        std::atomic<bool> blockerRelease{false};
        manager.enqueue(std::make_unique<LambdaJob>(
            "long_blocker",
            [&](const bl::ProgressFn&, const ICancelToken& token) {
                while (!blockerRelease.load() && !token.cancelled()) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
                }
                return Result<void>();
            },
            JobPriority::High));

        for (int i = 0; i < 10; ++i) {
            manager.enqueue(std::make_unique<LambdaJob>(
                "pending_" + std::to_string(i),
                [](const bl::ProgressFn&, const ICancelToken&) {
                    return Result<void>();
                },
                JobPriority::Low));
        }

        manager.shutdown(JobManager::ShutdownMode::Abandon);
        blockerRelease.store(true);
    }

    const auto all = events.snapshot();
    int cancelledPending = 0;
    for (const auto& e : all) {
        if (e.type == JobEventType::Cancelled &&
            e.jobName.find("pending_") == 0) {
            ++cancelledPending;
        }
    }
    EXPECT_EQ(cancelledPending, 10);
}

} // namespace