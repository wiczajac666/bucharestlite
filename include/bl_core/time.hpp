#pragma once

#include <cstdint>
#include <string>

namespace bl {

struct Rational {
    int64_t num{0};
    int64_t den{1};

    static Rational make(int64_t num, int64_t den);

    constexpr bool valid() const noexcept { return den > 0; }
    bool isZero() const noexcept { return num == 0; }
    double toDouble() const noexcept;
    std::string toString() const;
};

[[nodiscard]] bool exactEqual(const Rational& a, const Rational& b) noexcept;

constexpr bool operator==(const Rational& a, const Rational& b) noexcept {
    return a.num == b.num && a.den == b.den;
}

constexpr bool operator!=(const Rational& a, const Rational& b) noexcept {
    return !(a == b);
}

struct Duration;

struct Time {
    int64_t ticks{0};
    Rational rate{1'000'000, 1};

    static Time fromTicks(int64_t ticks, Rational rate);
    static Time fromFrame(int64_t frame, Rational rate);
    static Time fromFrameAt(int64_t frame, Rational frameRate, Rational targetRate);
    static Time fromSeconds(double seconds, Rational rate);

    Duration asDuration() const noexcept;

    int64_t floorFrame() const noexcept;
    int64_t ceilFrame() const noexcept;
    int64_t toFrame() const noexcept;
    int64_t floorFrameAt(Rational frameRate) const noexcept;
    int64_t ceilFrameAt(Rational frameRate) const noexcept;
    int64_t toFrameAt(Rational frameRate) const noexcept;
    double toSeconds() const noexcept;

    Time operator+(const Time& other) const;
    Duration operator-(const Time& other) const;
    Time operator+(const Duration& d) const;
    Time operator-(const Duration& d) const;
    Time& operator+=(const Time& other);
    Time& operator+=(const Duration& d);
    Time& operator-=(const Duration& d);
};

struct Duration {
    int64_t ticks{0};
    Rational rate{1'000'000, 1};

    static Duration fromTicks(int64_t ticks, Rational rate);
    static Duration fromFrames(int64_t frames, Rational frameRate);
    static Duration fromSeconds(double seconds, Rational rate);

    int64_t floorFramesAt(Rational frameRate) const noexcept;
    int64_t ceilFramesAt(Rational frameRate) const noexcept;
    int64_t toFramesAt(Rational frameRate) const noexcept;
    double toSeconds() const noexcept;

    Duration operator+(const Duration& other) const;
    Duration operator-(const Duration& other) const;
    Duration operator*(int64_t factor) const;
    Duration operator-() const;
    Duration& operator+=(const Duration& other);
    Duration& operator-=(const Duration& other);
};

Time operator+(const Duration& d, const Time& t);

bool operator==(const Time& a, const Time& b) noexcept;
bool operator!=(const Time& a, const Time& b) noexcept;
bool operator<(const Time& a, const Time& b) noexcept;
bool operator<=(const Time& a, const Time& b) noexcept;
bool operator>(const Time& a, const Time& b) noexcept;
bool operator>=(const Time& a, const Time& b) noexcept;

bool operator==(const Duration& a, const Duration& b) noexcept;
bool operator!=(const Duration& a, const Duration& b) noexcept;
bool operator<(const Duration& a, const Duration& b) noexcept;
bool operator<=(const Duration& a, const Duration& b) noexcept;
bool operator>(const Duration& a, const Duration& b) noexcept;
bool operator>=(const Duration& a, const Duration& b) noexcept;

struct TimeRange {
    Time start{};
    Duration duration{};

    Time end() const noexcept;
    bool isEmpty() const noexcept;
    bool overlaps(const TimeRange& other) const noexcept;
    bool contains(Time t) const noexcept;
    bool contains(const TimeRange& other) const noexcept;
};

} // namespace bl
