#pragma once

#include <bl_core/decoder_bridge.hpp>
#include <bl_core/result.hpp>
#include <bl_core/time.hpp>

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bl {

struct FrameKey {
    std::string mediaItemId;
    Time sourceTime{};
    uint32_t width{0};
    uint32_t height{0};

    bool operator==(const FrameKey& other) const noexcept;
};

struct FrameKeyHash {
    size_t operator()(const FrameKey& key) const noexcept;
};

class FrameCache {
public:
    explicit FrameCache(size_t maxBytes = 256 * 1024 * 1024);

    std::shared_ptr<Frame> get(const FrameKey& key);
    void put(const FrameKey& key, std::shared_ptr<Frame> frame);

    size_t bytesUsed() const noexcept;
    size_t count() const noexcept;
    void clear() noexcept;

private:
    struct Entry {
        FrameKey key;
        std::shared_ptr<Frame> frame;
        size_t bytes{0};
    };

    mutable std::mutex mutex_;
    size_t maxBytes_;
    size_t bytesUsed_{0};
    std::list<Entry> lru_;  // front = most recently used
    std::unordered_map<FrameKey, std::list<Entry>::iterator, FrameKeyHash> map_;

    void evictUntilUnderBudget();
};

} // namespace bl
