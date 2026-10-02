#include "bl_core/time.hpp"

#include <cassert>
#include <cmath>
#include <string>

#if defined(__SIZEOF_INT128__)
#define BL_HAS_INT128 1
#else
#define BL_HAS_INT128 0
#endif

namespace bl {

namespace {

struct Rep {
    int64_t ticks;
    Rational rate;
};

#if BL_HAS_INT128

__extension__ typedef __int128 Wide;

Wide wideMul(int64_t a, int64_t b) {
    return static_cast<Wide>(a) * static_cast<Wide>(b);
}

int64_t roundHalfEvenWide(Wide n, Wide d) {
    assert(d != 0);
    if (d < 0) {
        n = -n;
        d = -d;
    }
    Wide q = n / d;
    Wide r = n % d;
    if (r == 0) return static_cast<int64_t>(q);
    Wide twice = r * 2;
    if (twice > d || twice < -d) return static_cast<int64_t>(q + (r > 0 ? 1 : -1));
    if (q % 2 != 0) return static_cast<int64_t>(q + (r > 0 ? 1 : -1));
    return static_cast<int64_t>(q);
}

int64_t floorDivWide(Wide n, Wide d) {
    assert(d > 0);
    Wide q = n / d;
    if ((n % d) != 0 && n < 0) --q;
    return static_cast<int64_t>(q);
}

int64_t ceilDivWide(Wide n, Wide d) {
    assert(d > 0);
    Wide q = n / d;
    if ((n % d) != 0 && n > 0) ++q;
    return static_cast<int64_t>(q);
}

struct SecondsFrac {
    Wide num;
    Wide den;
};

SecondsFrac repToSeconds(const Rep& r) {
    return SecondsFrac{wideMul(r.ticks, static_cast<int64_t>(r.rate.den)),
                       static_cast<Wide>(r.rate.num)};
}

SecondsFrac secondsAdd(const SecondsFrac& a, const SecondsFrac& b) {
    return SecondsFrac{a.num * b.den + b.num * a.den, a.den * b.den};
}

SecondsFrac secondsSub(const SecondsFrac& a, const SecondsFrac& b) {
    return SecondsFrac{a.num * b.den - b.num * a.den, a.den * b.den};
}

bool secondsLess(const SecondsFrac& a, const SecondsFrac& b) {
    return a.num * b.den < b.num * a.den;
}

bool secondsEqual(const SecondsFrac& a, const SecondsFrac& b) {
    return a.num * b.den == b.num * a.den;
}

Rep secondsToRate(const SecondsFrac& s, Rational target) {
    Wide num = s.num * static_cast<Wide>(target.num);
    Wide den = s.den * static_cast<Wide>(target.den);
    return Rep{roundHalfEvenWide(num, den), target};
}

int64_t framesFromSecondsFloor(const SecondsFrac& s, Rational fps) {
    Wide num = s.num * static_cast<Wide>(fps.num);
    Wide den = s.den * static_cast<Wide>(fps.den);
    if (den < 0) {
        num = -num;
        den = -den;
    }
    return floorDivWide(num, den);
}

int64_t framesFromSecondsCeil(const SecondsFrac& s, Rational fps) {
    Wide num = s.num * static_cast<Wide>(fps.num);
    Wide den = s.den * static_cast<Wide>(fps.den);
    if (den < 0) {
        num = -num;
        den = -den;
    }
    return ceilDivWide(num, den);
}

int64_t framesFromSecondsRound(const SecondsFrac& s, Rational fps) {
    Wide num = s.num * static_cast<Wide>(fps.num);
    Wide den = s.den * static_cast<Wide>(fps.den);
    return roundHalfEvenWide(num, den);
}

#else

long double secondsValue(const Rep& r) {
    return static_cast<long double>(r.ticks) * static_cast<long double>(r.rate.den) /
           static_cast<long double>(r.rate.num);
}

long double roundHalfEvenLd(long double v) {
    long double fl = std::floorl(v);
    long double frac = v - fl;
    if (frac > 0.5L) return fl + 1.0L;
    if (frac < 0.5L) return fl;
    return ((static_cast<int64_t>(fl) % 2) == 0) ? fl : fl + 1.0L;
}

struct SecondsFrac {
    long double value;
};

SecondsFrac repToSeconds(const Rep& r) { return SecondsFrac{secondsValue(r)}; }
SecondsFrac secondsAdd(const SecondsFrac& a, const SecondsFrac& b) { return {a.value + b.value}; }
SecondsFrac secondsSub(const SecondsFrac& a, const SecondsFrac& b) { return {a.value - b.value}; }
bool secondsLess(const SecondsFrac& a, const SecondsFrac& b) { return a.value < b.value; }
bool secondsEqual(const SecondsFrac& a, const SecondsFrac& b) { return a.value == b.value; }

Rep secondsToRate(const SecondsFrac& s, Rational target) {
    long double v = s.value * static_cast<long double>(target.num) /
                    static_cast<long double>(target.den);
    return Rep{static_cast<int64_t>(roundHalfEvenLd(v)), target};
}

int64_t framesFromSecondsFloor(const SecondsFrac& s, Rational fps) {
    long double v = s.value * static_cast<long double>(fps.num) /
                    static_cast<long double>(fps.den);
    return static_cast<int64_t>(std::floorl(v));
}

int64_t framesFromSecondsCeil(const SecondsFrac& s, Rational fps) {
    long double v = s.value * static_cast<long double>(fps.num) /
                    static_cast<long double>(fps.den);
    return static_cast<int64_t>(std::ceill(v));
}

int64_t framesFromSecondsRound(const SecondsFrac& s, Rational fps) {
    long double v = s.value * static_cast<long double>(fps.num) /
                    static_cast<long double>(fps.den);
    return static_cast<int64_t>(roundHalfEvenLd(v));
}

#endif

Rep repAdd(const Rep& a, const Rep& b) {
    if (a.rate == b.rate) return Rep{a.ticks + b.ticks, a.rate};
    return secondsToRate(secondsAdd(repToSeconds(a), repToSeconds(b)), a.rate);
}

Rep repSub(const Rep& a, const Rep& b) {
    if (a.rate == b.rate) return Rep{a.ticks - b.ticks, a.rate};
    return secondsToRate(secondsSub(repToSeconds(a), repToSeconds(b)), a.rate);
}

bool repLess(const Rep& a, const Rep& b) {
    if (a.rate == b.rate) return a.ticks < b.ticks;
    return secondsLess(repToSeconds(a), repToSeconds(b));
}

bool repEqual(const Rep& a, const Rep& b) {
    if (a.rate == b.rate) return a.ticks == b.ticks;
    return secondsEqual(repToSeconds(a), repToSeconds(b));
}

} // namespace

bool exactEqual(const Rational& a, const Rational& b) noexcept {
    if (a.num == 0 || b.num == 0) return a.num == b.num;
#if BL_HAS_INT128
    __extension__ __int128 lhs = static_cast<__int128>(a.num) * b.den;
    __extension__ __int128 rhs = static_cast<__int128>(b.num) * a.den;
    return lhs == rhs;
#else
    return static_cast<long double>(a.num) * b.den ==
           static_cast<long double>(b.num) * a.den;
#endif
}

Rational Rational::make(int64_t numIn, int64_t denIn) {
    assert(denIn != 0);
    if (denIn == 0) return Rational{0, 1};
    if (denIn < 0) {
        numIn = -numIn;
        denIn = -denIn;
    }
    if (numIn == 0) return Rational{0, 1};
    int64_t a = numIn > 0 ? numIn : -numIn;
    int64_t g = a;
    int64_t b = denIn;
    while (b != 0) {
        int64_t t = g % b;
        g = b;
        b = t;
    }
    return Rational{numIn / g, denIn / g};
}

double Rational::toDouble() const noexcept {
    return static_cast<double>(num) / static_cast<double>(den);
}

std::string Rational::toString() const {
    return std::to_string(num) + "/" + std::to_string(den);
}

Time Time::fromTicks(int64_t ticksIn, Rational rateIn) {
    assert(rateIn.valid() && rateIn.num > 0);
    Time t;
    t.ticks = ticksIn;
    t.rate = rateIn;
    return t;
}

Time Time::fromFrame(int64_t frame, Rational frameRate) {
    return fromTicks(frame, frameRate);
}

Time Time::fromFrameAt(int64_t frame, Rational frameRate, Rational targetRate) {
    assert(frameRate.valid() && frameRate.num > 0);
    Rep src{frame, frameRate};
    return Time{secondsToRate(repToSeconds(src), targetRate).ticks, targetRate};
}

Time Time::fromSeconds(double seconds, Rational rate) {
    long double scaled =
        (static_cast<long double>(seconds) * static_cast<long double>(rate.num)) /
        static_cast<long double>(rate.den);
    // std::floor, not std::floorl: the long-double overload returns long
    // double, and it is portable. std::floorl is only pulled into namespace
    // std conditionally by libstdc++, and it did not compile on the older
    // toolchain the CI runner used before it was moved to ubuntu-26.04.
    long double fl = std::floor(scaled);
    long double frac = scaled - fl;
    int64_t base = static_cast<int64_t>(fl);
    int64_t result;
    if (frac > 0.5L) result = base + 1;
    else if (frac < 0.5L) result = base;
    else result = (base % 2 == 0) ? base : base + 1;
    return fromTicks(result, rate);
}

Duration Time::asDuration() const noexcept { return Duration{ticks, rate}; }

int64_t Time::floorFrame() const noexcept { return ticks; }
int64_t Time::ceilFrame() const noexcept { return ticks; }

int64_t Time::toFrame() const noexcept {
    return ticks;
}

double Time::toSeconds() const noexcept {
    return static_cast<double>(ticks) * static_cast<double>(rate.den) /
           static_cast<double>(rate.num);
}

int64_t Time::floorFrameAt(Rational fps) const noexcept {
    assert(fps.valid() && fps.num > 0);
    return framesFromSecondsFloor(repToSeconds(Rep{ticks, rate}), fps);
}

int64_t Time::ceilFrameAt(Rational fps) const noexcept {
    assert(fps.valid() && fps.num > 0);
    return framesFromSecondsCeil(repToSeconds(Rep{ticks, rate}), fps);
}

int64_t Time::toFrameAt(Rational fps) const noexcept {
    assert(fps.valid() && fps.num > 0);
    return framesFromSecondsRound(repToSeconds(Rep{ticks, rate}), fps);
}

Time Time::operator+(const Time& other) const {
    Rep r = repAdd(Rep{ticks, rate}, Rep{other.ticks, other.rate});
    return Time{r.ticks, r.rate};
}

Duration Time::operator-(const Time& other) const {
    Rep r = repSub(Rep{ticks, rate}, Rep{other.ticks, other.rate});
    return Duration{r.ticks, r.rate};
}

Time Time::operator+(const Duration& d) const {
    Rep r = repAdd(Rep{ticks, rate}, Rep{d.ticks, d.rate});
    return Time{r.ticks, r.rate};
}

Time Time::operator-(const Duration& d) const {
    Rep r = repSub(Rep{ticks, rate}, Rep{d.ticks, d.rate});
    return Time{r.ticks, r.rate};
}

Time& Time::operator+=(const Time& other) { *this = *this + other; return *this; }
Time& Time::operator+=(const Duration& d) { *this = *this + d; return *this; }
Time& Time::operator-=(const Duration& d) { *this = *this - d; return *this; }

Duration Duration::fromTicks(int64_t ticksIn, Rational rateIn) {
    assert(rateIn.valid() && rateIn.num > 0);
    return Duration{ticksIn, rateIn};
}

Duration Duration::fromFrames(int64_t frames, Rational frameRate) {
    return Duration{frames, frameRate};
}

Duration Duration::fromSeconds(double seconds, Rational rate) {
    return Time::fromSeconds(seconds, rate).asDuration();
}

double Duration::toSeconds() const noexcept {
    return static_cast<double>(ticks) * static_cast<double>(rate.den) /
           static_cast<double>(rate.num);
}

int64_t Duration::floorFramesAt(Rational fps) const noexcept {
    assert(fps.valid() && fps.num > 0);
    return framesFromSecondsFloor(repToSeconds(Rep{ticks, rate}), fps);
}

int64_t Duration::ceilFramesAt(Rational fps) const noexcept {
    assert(fps.valid() && fps.num > 0);
    return framesFromSecondsCeil(repToSeconds(Rep{ticks, rate}), fps);
}

int64_t Duration::toFramesAt(Rational fps) const noexcept {
    assert(fps.valid() && fps.num > 0);
    return framesFromSecondsRound(repToSeconds(Rep{ticks, rate}), fps);
}

Duration Duration::operator+(const Duration& other) const {
    Rep r = repAdd(Rep{ticks, rate}, Rep{other.ticks, other.rate});
    return Duration{r.ticks, r.rate};
}

Duration Duration::operator-(const Duration& other) const {
    Rep r = repSub(Rep{ticks, rate}, Rep{other.ticks, other.rate});
    return Duration{r.ticks, r.rate};
}

Duration Duration::operator*(int64_t factor) const {
    return Duration{ticks * factor, rate};
}

Duration Duration::operator-() const { return Duration{-ticks, rate}; }

Duration& Duration::operator+=(const Duration& other) { *this = *this + other; return *this; }
Duration& Duration::operator-=(const Duration& other) { *this = *this - other; return *this; }

Time operator+(const Duration& d, const Time& t) { return t + d; }

bool operator==(const Time& a, const Time& b) noexcept {
    return repEqual(Rep{a.ticks, a.rate}, Rep{b.ticks, b.rate});
}
bool operator!=(const Time& a, const Time& b) noexcept { return !(a == b); }
bool operator<(const Time& a, const Time& b) noexcept {
    return repLess(Rep{a.ticks, a.rate}, Rep{b.ticks, b.rate});
}
bool operator<=(const Time& a, const Time& b) noexcept { return !(b < a); }
bool operator>(const Time& a, const Time& b) noexcept { return b < a; }
bool operator>=(const Time& a, const Time& b) noexcept { return !(a < b); }

bool operator==(const Duration& a, const Duration& b) noexcept {
    return repEqual(Rep{a.ticks, a.rate}, Rep{b.ticks, b.rate});
}
bool operator!=(const Duration& a, const Duration& b) noexcept { return !(a == b); }
bool operator<(const Duration& a, const Duration& b) noexcept {
    return repLess(Rep{a.ticks, a.rate}, Rep{b.ticks, b.rate});
}
bool operator<=(const Duration& a, const Duration& b) noexcept { return !(b < a); }
bool operator>(const Duration& a, const Duration& b) noexcept { return b < a; }
bool operator>=(const Duration& a, const Duration& b) noexcept { return !(a < b); }

Time TimeRange::end() const noexcept { return start + duration; }

bool TimeRange::isEmpty() const noexcept { return duration.ticks == 0; }

bool TimeRange::overlaps(const TimeRange& other) const noexcept {
    return start < other.end() && other.start < end();
}

bool TimeRange::contains(Time t) const noexcept {
    return start <= t && t < end();
}

bool TimeRange::contains(const TimeRange& other) const noexcept {
    return other.start >= start && other.end() <= end();
}

} // namespace bl
