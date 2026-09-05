#include <bl_render/frame_cache.hpp>

#include <algorithm>

namespace bl {

bool FrameKey::operator==(const FrameKey& other) const noexcept {
    return mediaItemId == other.mediaItemId && sourceTime == other.sourceTime &&
           width == other.width && height == other.height;
}

size_t FrameKeyHash::operator()(const FrameKey& key) const noexcept {
    size_t h = std::hash<std::string>{}(key.mediaItemId);
    h ^= std::hash<int64_t>{}(key.sourceTime.ticks) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(key.width) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(key.height) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

FrameCache::FrameCache(size_t maxBytes) : maxBytes_(maxBytes) {}

std::shared_ptr<Frame> FrameCache::get(const FrameKey& key) {
    std::lock_guard lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) return nullptr;

    lru_.splice(lru_.begin(), lru_, it->second);
    return it->second->frame;
}

void FrameCache::put(const FrameKey& key, std::shared_ptr<Frame> frame) {
    std::lock_guard lock(mutex_);

    auto it = map_.find(key);
    if (it != map_.end()) {
        bytesUsed_ -= it->second->bytes;
        lru_.erase(it->second);
        map_.erase(it);
    }

    size_t frameBytes = frame ? frame->dataSize : 0;
    lru_.push_front(Entry{key, std::move(frame), frameBytes});
    map_[key] = lru_.begin();
    bytesUsed_ += frameBytes;

    evictUntilUnderBudget();
}

size_t FrameCache::bytesUsed() const noexcept {
    std::lock_guard lock(mutex_);
    return bytesUsed_;
}

size_t FrameCache::count() const noexcept {
    std::lock_guard lock(mutex_);
    return map_.size();
}

void FrameCache::clear() noexcept {
    std::lock_guard lock(mutex_);
    lru_.clear();
    map_.clear();
    bytesUsed_ = 0;
}

void FrameCache::evictUntilUnderBudget() {
    while (bytesUsed_ > maxBytes_ && !lru_.empty()) {
        auto& back = lru_.back();
        bytesUsed_ -= back.bytes;
        map_.erase(back.key);
        lru_.pop_back();
    }
}

} // namespace bl
