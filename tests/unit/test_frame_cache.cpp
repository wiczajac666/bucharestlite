#include <bl_render/frame_cache.hpp>
#include <bl_core/time.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

using bl::Duration;
using bl::Frame;
using bl::FrameCache;
using bl::FrameKey;
using bl::Rational;
using bl::Time;

static std::shared_ptr<Frame> makeTestFrame(uint32_t w, uint32_t h) {
    auto f = std::make_shared<Frame>();
    f->type = Frame::Type::Video;
    f->width = w;
    f->height = h;
    f->linesize = w * 4;
    f->dataSize = w * h * 4;
    f->data = new uint8_t[f->dataSize];
    std::memset(f->data, 0xAB, f->dataSize);
    // Set a custom free that deletes[] instead of host->free
    f->hostApi = nullptr;
    return f;
}

struct FrameCacheTest : ::testing::Test {};

TEST_F(FrameCacheTest, PutAndGet) {
    FrameCache cache;
    FrameKey key{"video.mp4", Time::fromSeconds(1.0, Rational{1,1}), 1920, 1080};
    auto frame = makeTestFrame(1920, 1080);

    cache.put(key, frame);
    EXPECT_EQ(cache.count(), 1u);

    auto got = cache.get(key);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->width, 1920u);
}

TEST_F(FrameCacheTest, MissReturnsNull) {
    FrameCache cache;
    FrameKey key{"missing.mp4", Time::fromSeconds(0, Rational{1,1}), 100, 100};
    EXPECT_EQ(cache.get(key), nullptr);
}

TEST_F(FrameCacheTest, EvictsOldestWhenOverBudget) {
    // 100 byte budget, each frame is 4 bytes (1x1 BGRA)
    FrameCache cache(100);

    for (int i = 0; i < 30; ++i) {
        FrameKey key{std::to_string(i), Time::fromSeconds(i, Rational{1,1}), 1, 1};
        cache.put(key, makeTestFrame(1, 1));
    }

    // Should have evicted some frames to stay at or under 100 bytes
    EXPECT_LE(cache.bytesUsed(), 100u);
    EXPECT_LT(cache.count(), 30u);
}

TEST_F(FrameCacheTest, ClearEmptiesCache) {
    FrameCache cache;
    FrameKey key{"a.mp4", Time::fromSeconds(0, Rational{1,1}), 10, 10};
    cache.put(key, makeTestFrame(10, 10));
    EXPECT_EQ(cache.count(), 1u);

    cache.clear();
    EXPECT_EQ(cache.count(), 0u);
    EXPECT_EQ(cache.bytesUsed(), 0u);
    EXPECT_EQ(cache.get(key), nullptr);
}

TEST_F(FrameCacheTest, UpdateExistingKey) {
    FrameCache cache;
    FrameKey key{"a.mp4", Time::fromSeconds(0, Rational{1,1}), 10, 10};

    cache.put(key, makeTestFrame(10, 10));
    EXPECT_EQ(cache.count(), 1u);

    auto newFrame = makeTestFrame(10, 10);
    cache.put(key, newFrame);
    EXPECT_EQ(cache.count(), 1u);

    auto got = cache.get(key);
    ASSERT_NE(got, nullptr);
}

TEST_F(FrameCacheTest, GetPromotesToMRU) {
    FrameCache cache(60);  // tight budget: only room for 15 frames at 4 bytes each

    FrameKey k1{"a.mp4", Time::fromSeconds(0, Rational{1,1}), 1, 1};
    FrameKey k2{"b.mp4", Time::fromSeconds(0, Rational{1,1}), 1, 1};
    FrameKey k3{"c.mp4", Time::fromSeconds(0, Rational{1,1}), 1, 1};

    cache.put(k1, makeTestFrame(1, 1));
    cache.put(k2, makeTestFrame(1, 1));
    cache.put(k3, makeTestFrame(1, 1));

    // Access k1 to promote it to MRU
    cache.get(k1);

    // Add enough frames to trigger eviction of LRU items
    for (int i = 4; i <= 16; ++i) {
        FrameKey ki{std::to_string(i), Time::fromSeconds(i, Rational{1,1}), 1, 1};
        cache.put(ki, makeTestFrame(1, 1));
    }

    EXPECT_NE(cache.get(k1), nullptr);  // promoted, should survive
    EXPECT_EQ(cache.get(k2), nullptr);  // was LRU after k1 promotion, should be evicted
}

} // namespace
