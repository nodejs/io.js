// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: time.h
// -----------------------------------------------------------------------------
//
// This header file defines abstractions for computing with absolute points
// in time, durations of time, and formatting and parsing time within a given
// time zone. The following abstractions are defined:
//
//  * `absl::Time` defines an absolute, specific instance in time
//  * `absl::Duration` defines a signed, fixed-length span of time
//  * `absl::TimeZone` defines geopolitical time zone regions (as collected
//     within the IANA Time Zone database (https://www.iana.org/time-zones)).
//
// Note: Absolute times are distinct from civil times, which refer to the
// human-scale time commonly represented by `YYYY-MM-DD hh:mm:ss`. The mapping
// between absolute and civil times can be specified by use of time zones
// (`absl::TimeZone` within this API). That is:
//
//   Civil Time = F(Absolute Time, Time Zone)
//   Absolute Time = G(Civil Time, Time Zone)
//
// See civil_time.h for abstractions related to constructing and manipulating
// civil time.
//
// Example:
//
//   absl::TimeZone nyc;
//   // LoadTimeZone() may fail so it's always better to check for success.
//   if (!absl::LoadTimeZone("America/New_York", &nyc)) {
//      // handle error case
//   }
//
//   // My flight leaves NYC on Jan 2, 2017 at 03:04:05
//   absl::CivilSecond cs(2017, 1, 2, 3, 4, 5);
//   absl::Time takeoff = absl::FromCivil(cs, nyc);
//
//   absl::Duration flight_duration = absl::Hours(21) + absl::Minutes(35);
//   absl::Time landing = takeoff + flight_duration;
//
//   absl::TimeZone syd;
//   if (!absl::LoadTimeZone("Australia/Sydney", &syd)) {
//      // handle error case
//   }
//   std::string s = absl::FormatTime(
//       "My flight will land in Sydney on %Y-%m-%d at %H:%M:%S",
//       landing, syd);

#ifndef ABSL_TIME_TIME_H_
#define ABSL_TIME_TIME_H_

#if !defined(_MSC_VER)
#include <sys/time.h>
#else
// We don't include `winsock2.h` because it drags in `windows.h` and friends,
// and they define conflicting macros like OPAQUE, ERROR, and more. This has the
// potential to break Abseil users.
//
// Instead we only forward declare `timeval` and require Windows users include
// `winsock2.h` themselves. This is both inconsistent and troublesome, but so is
// including 'windows.h' so we are picking the lesser of two evils here.
struct timeval;
#endif
#include <chrono>  // NOLINT(build/c++11)
#include <cmath>
#include <cstdint>
#include <ctime>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

class Duration;  // Defined below
class Time;      // Defined below
class TimeZone;  // Defined below

namespace time_internal {
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixDuration(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration ToUnixDuration(Time t);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr int64_t GetRepHi(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr uint32_t GetRepLo(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration MakeDuration(int64_t hi,
                                                              uint32_t lo);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration MakeDuration(int64_t hi,
                                                              int64_t lo);
ABSL_ATTRIBUTE_CONST_FUNCTION inline Duration MakePosDoubleDuration(double n);
constexpr int64_t kTicksPerNanosecond = 4;
constexpr int64_t kTicksPerSecond = 1000 * 1000 * 1000 * kTicksPerNanosecond;
template <std::intmax_t N>
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration FromInt64(int64_t v,
                                                           std::ratio<1, N>);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration FromInt64(int64_t v,
                                                           std::ratio<60>);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration FromInt64(int64_t v,
                                                           std::ratio<3600>);
template <typename T>
using EnableIfIntegral = typename std::enable_if<
    std::is_integral<T>::value || std::is_enum<T>::value, int>::type;
template <typename T>
using EnableIfFloat =
    typename std::enable_if<std::is_floating_point<T>::value, int>::type;
}  // namespace time_internal

// Duration
//
// The `absl::Duration` class represents a signed, fixed-length amount of time.
// A `Duration` is generated using a unit-specific factory function, or is
// the result of subtracting one `absl::Time` from another. Durations behave
// like unit-safe integers and they support all the natural integer-like
// arithmetic operations. Arithmetic overflows and saturates at +/- infinity.
// `Duration` should be passed by value rather than const reference.
//
// Factory functions `Nanoseconds()`, `Microseconds()`, `Milliseconds()`,
// `Seconds()`, `Minutes()`, `Hours()` and `InfiniteDuration()` allow for
// creation of constexpr `Duration` values
//
// Examples:
//
//   constexpr absl::Duration ten_ns = absl::Nanoseconds(10);
//   constexpr absl::Duration min = absl::Minutes(1);
//   constexpr absl::Duration hour = absl::Hours(1);
//   absl::Duration dur = 60 * min;  // dur == hour
//   absl::Duration half_sec = absl::Milliseconds(500);
//   absl::Duration quarter_sec = 0.25 * absl::Seconds(1);
//
// `Duration` values can be easily converted to an integral number of units
// using the division operator.
//
// Example:
//
//   constexpr absl::Duration dur = absl::Milliseconds(1500);
//   int64_t ns = dur / absl::Nanoseconds(1);   // ns == 1500000000
//   int64_t ms = dur / absl::Milliseconds(1);  // ms == 1500
//   int64_t sec = dur / absl::Seconds(1);    // sec == 1 (subseconds truncated)
//   int64_t min = dur / absl::Minutes(1);    // min == 0
//
// See the `IDivDuration()` and `FDivDuration()` functions below for details on
// how to access the fractional parts of the quotient.
//
// Alternatively, conversions can be performed using helpers such as
// `ToInt64Microseconds()` and `ToDoubleSeconds()`.
class Duration {
 public:
  // Value semantics.
  constexpr Duration() : rep_hi_(0), rep_lo_(0) {}  // zero-length duration

  // Copyable.
#if !defined(__clang__) && defined(_MSC_VER) && _MSC_VER < 1930
  // Explicitly defining the constexpr copy constructor avoids an MSVC bug.
  constexpr Duration(const Duration& d)
      : rep_hi_(d.rep_hi_), rep_lo_(d.rep_lo_) {}
#else
  constexpr Duration(const Duration& d) = default;
#endif
  Duration& operator=(const Duration& d) = default;

  // Compound assignment operators.
  Duration& operator+=(Duration d);
  Duration& operator-=(Duration d);
  Duration& operator*=(int64_t r);
  Duration& operator*=(double r);
  Duration& operator/=(int64_t r);
  Duration& operator/=(double r);
  Duration& operator%=(Duration rhs);

  // Overloads that forward to either the int64_t or double overloads above.
  // Integer operands must be representable as int64_t. Integer division is
  // truncating, so values less than the resolution will be returned as zero.
  // Floating-point multiplication and division is rounding (halfway cases
  // rounding away from zero), so values less than the resolution may be
  // returned as either the resolution or zero.  In particular, `d / 2.0`
  // can produce `d` when it is the resolution and "even".
  template <typename T, time_internal::EnableIfIntegral<T> = 0>
  Duration& operator*=(T r) {
    int64_t x = r;
    return *this *= x;
  }

  template <typename T, time_internal::EnableIfIntegral<T> = 0>
  Duration& operator/=(T r) {
    int64_t x = r;
    return *this /= x;
  }

  template <typename T, time_internal::EnableIfFloat<T> = 0>
  Duration& operator*=(T r) {
    double x = r;
    return *this *= x;
  }

  template <typename T, time_internal::EnableIfFloat<T> = 0>
  Duration& operator/=(T r) {
    double x = r;
    return *this /= x;
  }

  template <typename H>
  friend H AbslHashValue(H h, Duration d) {
    return H::combine(std::move(h), d.rep_hi_.Get(), d.rep_lo_);
  }

 private:
  friend constexpr int64_t time_internal::GetRepHi(Duration d);
  friend constexpr uint32_t time_internal::GetRepLo(Duration d);
  friend constexpr Duration time_internal::MakeDuration(int64_t hi,
                                                        uint32_t lo);
  constexpr Duration(int64_t hi, uint32_t lo) : rep_hi_(hi), rep_lo_(lo) {}

  // We store `rep_hi_` 4-byte rather than 8-byte aligned to avoid 4 bytes of
  // tail padding.
  class HiRep {
   public:
    // Default constructor default-initializes `hi_`, which has the same
    // semantics as default-initializing an `int64_t` (undetermined value).
    HiRep() = default;

    HiRep(const HiRep&) = default;
    HiRep& operator=(const HiRep&) = default;

    explicit constexpr HiRep(const int64_t value)
        :  // C++17 forbids default-initialization in constexpr contexts. We can
           // remove this in C++20.
#if defined(ABSL_IS_BIG_ENDIAN) && ABSL_IS_BIG_ENDIAN
          hi_(0),
          lo_(0)
#else
          lo_(0),
          hi_(0)
#endif
    {
      *this = value;
    }

    constexpr int64_t Get() const {
      const uint64_t unsigned_value =
          (static_cast<uint64_t>(hi_) << 32) | static_cast<uint64_t>(lo_);
      // `static_cast<int64_t>(unsigned_value)` is implementation-defined
      // before c++20. On all supported platforms the behaviour is that mandated
      // by c++20, i.e. "If the destination type is signed, [...] the result is
      // the unique value of the destination type equal to the source value
      // modulo 2^n, where n is the number of bits used to represent the
      // destination type."
      static_assert(
          (static_cast<int64_t>((std::numeric_limits<uint64_t>::max)()) ==
           int64_t{-1}) &&
              (static_cast<int64_t>(static_cast<uint64_t>(
                                        (std::numeric_limits<int64_t>::max)()) +
                                    1) ==
               (std::numeric_limits<int64_t>::min)()),
          "static_cast<int64_t>(uint64_t) does not have c++20 semantics");
      return static_cast<int64_t>(unsigned_value);
    }

    constexpr HiRep& operator=(const int64_t value) {
      // "If the destination type is unsigned, the resulting value is the
      // smallest unsigned value equal to the source value modulo 2^n
      // where `n` is the number of bits used to represent the destination
      // type".
      const auto unsigned_value = static_cast<uint64_t>(value);
      hi_ = static_cast<uint32_t>(unsigned_value >> 32);
      lo_ = static_cast<uint32_t>(unsigned_value);
      return *this;
    }

   private:
    // Notes:
    //  - Ideally we would use a `char[]` and `std::bitcast`, but the latter
    //    does not exist (and is not constexpr in `absl`) before c++20.
    //  - Order is optimized depending on endianness so that the compiler can
    //    turn `Get()` (resp. `operator=()`) into a single 8-byte load (resp.
    //    store).
#if defined(ABSL_IS_BIG_ENDIAN) && ABSL_IS_BIG_ENDIAN
    uint32_t hi_;
    uint32_t lo_;
#else
    uint32_t lo_;
    uint32_t hi_;
#endif
  };
  HiRep rep_hi_;
  uint32_t rep_lo_;
};

// Relational Operators
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator<(Duration lhs,
                                                       Duration rhs);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator>(Duration lhs,
                                                       Duration rhs) {
  return rhs < lhs;
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator>=(Duration lhs,
                                                        Duration rhs) {
  return !(lhs < rhs);
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator<=(Duration lhs,
                                                        Duration rhs) {
  return !(rhs < lhs);
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator==(Duration lhs,
                                                        Duration rhs);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator!=(Duration lhs,
                                                        Duration rhs) {
  return !(lhs == rhs);
}

// Additive Operators
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration operator-(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION inline Duration operator+(Duration lhs,
                                                        Duration rhs) {
  return lhs += rhs;
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline Duration operator-(Duration lhs,
                                                        Duration rhs) {
  return lhs -= rhs;
}

// IDivDuration()
//
// Divides a numerator `Duration` by a denominator `Duration`, returning the
// quotient and remainder. The remainder always has the same sign as the
// numerator. The returned quotient and remainder respect the identity:
//
//   numerator = denominator * quotient + remainder
//
// Returned quotients are capped to the range of `int64_t`, with the difference
// spilling into the remainder to uphold the above identity. This means that the
// remainder returned could differ from the remainder returned by
// `Duration::operator%` for huge quotients.
//
// See also the notes on `InfiniteDuration()` below regarding the behavior of
// division involving zero and infinite durations.
//
// Example:
//
//   constexpr absl::Duration a =
//       absl::Seconds(std::numeric_limits<int64_t>::max());  // big
//   constexpr absl::Duration b = absl::Nanoseconds(1);       // small
//
//   absl::Duration rem = a % b;
//   // rem == absl::ZeroDuration()
//
//   // Here, q would overflow int64_t, so rem accounts for the difference.
//   int64_t q = absl::IDivDuration(a, b, &rem);
//   // q == std::numeric_limits<int64_t>::max(), rem == a - b * q
int64_t IDivDuration(Duration num, Duration den, Duration* rem);

// FDivDuration()
//
// Divides a `Duration` numerator into a fractional number of units of a
// `Duration` denominator.
//
// See also the notes on `InfiniteDuration()` below regarding the behavior of
// division involving zero and infinite durations.
//
// Example:
//
//   double d = absl::FDivDuration(absl::Milliseconds(1500), absl::Seconds(1));
//   // d == 1.5
ABSL_ATTRIBUTE_CONST_FUNCTION double FDivDuration(Duration num, Duration den);

// Multiplicative Operators
// Integer operands must be representable as int64_t.
template <typename T>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration operator*(Duration lhs, T rhs) {
  return lhs *= rhs;
}
template <typename T>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration operator*(T lhs, Duration rhs) {
  return rhs *= lhs;
}
template <typename T>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration operator/(Duration lhs, T rhs) {
  return lhs /= rhs;
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline int64_t operator/(Duration lhs,
                                                       Duration rhs) {
  return IDivDuration(lhs, rhs,
                      &lhs);  // trunc towards zero
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline Duration operator%(Duration lhs,
                                                        Duration rhs) {
  return lhs %= rhs;
}

// ZeroDuration()
//
// Returns a zero-length duration. This function behaves just like the default
// constructor, but the name helps make the semantics clear at call sites.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration ZeroDuration() {
  return Duration();
}

// AbsDuration()
//
// Returns the absolute value of a duration.
ABSL_ATTRIBUTE_CONST_FUNCTION inline Duration AbsDuration(Duration d) {
  return (d < ZeroDuration()) ? -d : d;
}

// Trunc()
//
// Truncates a duration (toward zero) to a multiple of a non-zero unit.
//
// Example:
//
//   absl::Duration d = absl::Nanoseconds(123456789);
//   absl::Duration a = absl::Trunc(d, absl::Microseconds(1));  // 123456us
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Trunc(Duration d, Duration unit);

// Floor()
//
// Floors a duration using the passed duration unit to its largest value not
// greater than the duration.
//
// Example:
//
//   absl::Duration d = absl::Nanoseconds(123456789);
//   absl::Duration b = absl::Floor(d, absl::Microseconds(1));  // 123456us
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Floor(Duration d, Duration unit);

// Ceil()
//
// Returns the ceiling of a duration using the passed duration unit to its
// smallest value not less than the duration.
//
// Example:
//
//   absl::Duration d = absl::Nanoseconds(123456789);
//   absl::Duration c = absl::Ceil(d, absl::Microseconds(1));   // 123457us
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Ceil(Duration d, Duration unit);

// InfiniteDuration()
//
// Returns an infinite `Duration`.  To get a `Duration` representing negative
// infinity, use `-InfiniteDuration()`.
//
// Duration arithmetic overflows to +/- infinity and saturates. In general,
// arithmetic with `Duration` infinities is similar to IEEE 754 infinities
// except where IEEE 754 NaN would be involved, in which case +/-
// `InfiniteDuration()` is used in place of a "nan" Duration.
//
// Examples:
//
//   constexpr absl::Duration inf = absl::InfiniteDuration();
//   const absl::Duration d = ... any finite duration ...
//
//   inf == inf + inf
//   inf == inf + d
//   inf == inf - inf
//   -inf == d - inf
//
//   inf == d * 1e100
//   inf == inf / 2
//   0 == d / inf
//   INT64_MAX == inf / d
//
//   d < inf
//   -inf < d
//
//   // Division by zero returns infinity, or INT64_MIN/MAX where appropriate.
//   inf == d / 0
//   INT64_MAX == d / absl::ZeroDuration()
//
// The examples involving the `/` operator above also apply to `IDivDuration()`
// and `FDivDuration()`.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration InfiniteDuration();

// Nanoseconds()
// Microseconds()
// Milliseconds()
// Seconds()
// Minutes()
// Hours()
//
// Factory functions for constructing `Duration` values from an integral number
// of the unit indicated by the factory function's name. The number must be
// representable as int64_t.
//
// NOTE: no "Days()" factory function exists because "a day" is ambiguous.
// Civil days are not always 24 hours long, and a 24-hour duration often does
// not correspond with a civil day. If a 24-hour duration is needed, use
// `absl::Hours(24)`. If you actually want a civil day, use absl::CivilDay
// from civil_time.h.
//
// Example:
//
//   absl::Duration a = absl::Seconds(60);
//   absl::Duration b = absl::Minutes(1);  // b == a
template <typename T, time_internal::EnableIfIntegral<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration Nanoseconds(T n) {
  return time_internal::FromInt64(n, std::nano{});
}
template <typename T, time_internal::EnableIfIntegral<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration Microseconds(T n) {
  return time_internal::FromInt64(n, std::micro{});
}
template <typename T, time_internal::EnableIfIntegral<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration Milliseconds(T n) {
  return time_internal::FromInt64(n, std::milli{});
}
template <typename T, time_internal::EnableIfIntegral<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration Seconds(T n) {
  return time_internal::FromInt64(n, std::ratio<1>{});
}
template <typename T, time_internal::EnableIfIntegral<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration Minutes(T n) {
  return time_internal::FromInt64(n, std::ratio<60>{});
}
template <typename T, time_internal::EnableIfIntegral<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration Hours(T n) {
  return time_internal::FromInt64(n, std::ratio<3600>{});
}

// Factory overloads for constructing `Duration` values from a floating-point
// number of the unit indicated by the factory function's name. These functions
// exist for convenience, but they are not as efficient as the integral
// factories, which should be preferred.
//
// Example:
//
//   auto a = absl::Seconds(1.5);        // OK
//   auto b = absl::Milliseconds(1500);  // BETTER
template <typename T, time_internal::EnableIfFloat<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Nanoseconds(T n) {
  return n * Nanoseconds(1);
}
template <typename T, time_internal::EnableIfFloat<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Microseconds(T n) {
  return n * Microseconds(1);
}
template <typename T, time_internal::EnableIfFloat<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Milliseconds(T n) {
  return n * Milliseconds(1);
}
template <typename T, time_internal::EnableIfFloat<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Seconds(T n) {
  if (n >= 0) {  // Note: `NaN >= 0` is false.
    if (n >= static_cast<T>((std::numeric_limits<int64_t>::max)())) {
      return InfiniteDuration();
    }
    return time_internal::MakePosDoubleDuration(n);
  } else {
    if (std::isnan(n))
      return std::signbit(n) ? -InfiniteDuration() : InfiniteDuration();
    if (n <= (std::numeric_limits<int64_t>::min)()) return -InfiniteDuration();
    return -time_internal::MakePosDoubleDuration(-n);
  }
}
template <typename T, time_internal::EnableIfFloat<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Minutes(T n) {
  return n * Minutes(1);
}
template <typename T, time_internal::EnableIfFloat<T> = 0>
ABSL_ATTRIBUTE_CONST_FUNCTION Duration Hours(T n) {
  return n * Hours(1);
}

// ToInt64Nanoseconds()
// ToInt64Microseconds()
// ToInt64Milliseconds()
// ToInt64Seconds()
// ToInt64Minutes()
// ToInt64Hours()
//
// Helper functions that convert a Duration to an integral count of the
// indicated unit. These return the same results as the `IDivDuration()`
// function, though they usually do so more efficiently; see the
// documentation of `IDivDuration()` for details about overflow, etc.
//
// Example:
//
//   absl::Duration d = absl::Milliseconds(1500);
//   int64_t isec = absl::ToInt64Seconds(d);  // isec == 1
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToInt64Nanoseconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToInt64Microseconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToInt64Milliseconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToInt64Seconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToInt64Minutes(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToInt64Hours(Duration d);

// ToDoubleNanoseconds()
// ToDoubleMicroseconds()
// ToDoubleMilliseconds()
// ToDoubleSeconds()
// ToDoubleMinutes()
// ToDoubleHours()
//
// Helper functions that convert a Duration to a floating point count of the
// indicated unit. These functions are shorthand for the `FDivDuration()`
// function above; see its documentation for details about overflow, etc.
//
// Example:
//
//   absl::Duration d = absl::Milliseconds(1500);
//   double dsec = absl::ToDoubleSeconds(d);  // dsec == 1.5
ABSL_ATTRIBUTE_CONST_FUNCTION double ToDoubleNanoseconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION double ToDoubleMicroseconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION double ToDoubleMilliseconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION double ToDoubleSeconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION double ToDoubleMinutes(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION double ToDoubleHours(Duration d);

// FromChrono()
//
// Converts any of the pre-defined std::chrono durations to an absl::Duration.
//
// Example:
//
//   std::chrono::milliseconds ms(123);
//   absl::Duration d = absl::FromChrono(ms);
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::nanoseconds& d);
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::microseconds& d);
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::milliseconds& d);
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::seconds& d);
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::minutes& d);
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::hours& d);

// ToChronoNanoseconds()
// ToChronoMicroseconds()
// ToChronoMilliseconds()
// ToChronoSeconds()
// ToChronoMinutes()
// ToChronoHours()
//
// Converts an absl::Duration to any of the pre-defined std::chrono durations.
// If overflow would occur, the returned value will saturate at the min/max
// chrono duration value instead.
//
// Example:
//
//   absl::Duration d = absl::Microseconds(123);
//   auto x = absl::ToChronoMicroseconds(d);
//   auto y = absl::ToChronoNanoseconds(d);  // x == y
//   auto z = absl::ToChronoSeconds(absl::InfiniteDuration());
//   // z == std::chrono::seconds::max()
ABSL_ATTRIBUTE_CONST_FUNCTION std::chrono::nanoseconds ToChronoNanoseconds(
    Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION std::chrono::microseconds ToChronoMicroseconds(
    Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION std::chrono::milliseconds ToChronoMilliseconds(
    Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION std::chrono::seconds ToChronoSeconds(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION std::chrono::minutes ToChronoMinutes(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION std::chrono::hours ToChronoHours(Duration d);

// FormatDuration()
//
// Returns a string representing the duration in the form "72h3m0.5s".
// Returns "inf" or "-inf" for +/- `InfiniteDuration()`.
ABSL_ATTRIBUTE_CONST_FUNCTION std::string FormatDuration(Duration d);

// Output stream operator.
inline std::ostream& operator<<(std::ostream& os, Duration d) {
  return os << FormatDuration(d);
}

// Support for StrFormat(), StrCat() etc.
template <typename Sink>
void AbslStringify(Sink& sink, Duration d) {
  sink.Append(FormatDuration(d));
}

// ParseDuration()
//
// Parses a duration string consisting of a possibly signed sequence of
// decimal numbers, each with an optional fractional part and a unit
// suffix.  The valid suffixes are "ns", "us" "ms", "s", "m", and "h".
// Simple examples include "300ms", "-1.5h", and "2h45m".  Parses "0" as
// `ZeroDuration()`. Parses "inf" and "-inf" as +/- `InfiniteDuration()`.
bool ParseDuration(absl::string_view dur_string, Duration* d);

// AbslParseFlag()
//
// Parses a command-line flag string representation `text` into a Duration
// value. Duration flags must be specified in a format that is valid input for
// `absl::ParseDuration()`.
bool AbslParseFlag(absl::string_view text, Duration* dst, std::string* error);


// AbslUnparseFlag()
//
// Unparses a Duration value into a command-line string representation using
// the format specified by `absl::ParseDuration()`.
std::string AbslUnparseFlag(Duration d);

ABSL_DEPRECATED("Use AbslParseFlag() instead.")
bool ParseFlag(const std::string& text, Duration* dst, std::string* error);
ABSL_DEPRECATED("Use AbslUnparseFlag() instead.")
std::string UnparseFlag(Duration d);

// Time
//
// An `absl::Time` represents a specific instant in time. Arithmetic operators
// are provided for naturally expressing time calculations. Instances are
// created using `absl::Now()` and the `absl::From*()` factory functions that
// accept the gamut of other time representations. Formatting and parsing
// functions are provided for conversion to and from strings.  `absl::Time`
// should be passed by value rather than const reference.
//
// `absl::Time` assumes there are 60 seconds in a minute, which means the
// underlying time scales must be "smeared" to eliminate leap seconds.
// See https://developers.google.com/time/smear.
//
// Even though `absl::Time` supports a wide range of timestamps, exercise
// caution when using values in the distant past. `absl::Time` uses the
// Proleptic Gregorian calendar, which extends the Gregorian calendar backward
// to dates before its introduction in 1582.
// See https://en.wikipedia.org/wiki/Proleptic_Gregorian_calendar
// for more information. Use the ICU calendar classes to convert a date in
// some other calendar (http://userguide.icu-project.org/datetime/calendar).
//
// Similarly, standardized time zones are a reasonably recent innovation, with
// the Greenwich prime meridian being established in 1884. The TZ database
// itself does not profess accurate offsets for timestamps prior to 1970. The
// breakdown of future timestamps is subject to the whim of regional
// governments.
//
// The `absl::Time` class represents an instant in time as a count of clock
// ticks of some granularity (resolution) from some starting point (epoch).
//
// `absl::Time` uses a resolution that is high enough to avoid loss in
// precision, and a range that is wide enough to avoid overflow, when
// converting between tick counts in most Google time scales (i.e., resolution
// of at least one nanosecond, and range +/-100 billion years).  Conversions
// between the time scales are performed by truncating (towards negative
// infinity) to the nearest representable point.
//
// Examples:
//
//   absl::Time t1 = ...;
//   absl::Time t2 = t1 + absl::Minutes(2);
//   absl::Duration d = t2 - t1;  // == absl::Minutes(2)
//
class Time {
 public:
  // Value semantics.

  // Returns the Unix epoch.  However, those reading your code may not know
  // or expect the Unix epoch as the default value, so make your code more
  // readable by explicitly initializing all instances before use.
  //
  // Example:
  //   absl::Time t = absl::UnixEpoch();
  //   absl::Time t = absl::Now();
  //   absl::Time t = absl::TimeFromTimeval(tv);
  //   absl::Time t = absl::InfinitePast();
  constexpr Time() = default;

  // Copyable.
  constexpr Time(const Time& t) = default;
  Time& operator=(const Time& t) = default;

  // Assignment operators.
  Time& operator+=(Duration d) {
    rep_ += d;
    return *this;
  }
  Time& operator-=(Duration d) {
    rep_ -= d;
    return *this;
  }

  // Time::Breakdown
  //
  // The calendar and wall-clock (aka "civil time") components of an
  // `absl::Time` in a certain `absl::TimeZone`. This struct is not
  // intended to represent an instant in time. So, rather than passing
  // a `Time::Breakdown` to a function, pass an `absl::Time` and an
  // `absl::TimeZone`.
  //
  // Deprecated. Use `absl::TimeZone::CivilInfo`.
  struct ABSL_DEPRECATED("Use `absl::TimeZone::CivilInfo`.") Breakdown {
    int64_t year;        // year (e.g., 2013)
    int month;           // month of year [1:12]
    int day;             // day of month [1:31]
    int hour;            // hour of day [0:23]
    int minute;          // minute of hour [0:59]
    int second;          // second of minute [0:59]
    Duration subsecond;  // [Seconds(0):Seconds(1)) if finite
    int weekday;         // 1==Mon, ..., 7=Sun
    int yearday;         // day of year [1:366]

    // Note: The following fields exist for backward compatibility
    // with older APIs.  Accessing these fields directly is a sign of
    // imprudent logic in the calling code.  Modern time-related code
    // should only access this data indirectly by way of FormatTime().
    // These fields are undefined for InfiniteFuture() and InfinitePast().
    int offset;             // seconds east of UTC
    bool is_dst;            // is offset non-standard?
    const char* zone_abbr;  // time-zone abbreviation (e.g., "PST")
  };

  // Time::In()
  //
  // Returns the breakdown of this instant in the given TimeZone.
  //
  // Deprecated. Use `absl::TimeZone::At(Time)`.
  ABSL_INTERNAL_DISABLE_DEPRECATED_DECLARATION_WARNING
  ABSL_DEPRECATED("Use `absl::TimeZone::At(Time)`.")
  Breakdown In(TimeZone tz) const;
  ABSL_INTERNAL_RESTORE_DEPRECATED_DECLARATION_WARNING

  template <typename H>
  friend H AbslHashValue(H h, Time t) {
    return H::combine(std::move(h), t.rep_);
  }

 private:
  friend constexpr Time time_internal::FromUnixDuration(Duration d);
  friend constexpr Duration time_internal::ToUnixDuration(Time t);
  friend constexpr bool operator<(Time lhs, Time rhs);
  friend constexpr bool operator==(Time lhs, Time rhs);
  friend Duration operator-(Time lhs, Time rhs);
  friend constexpr Time UniversalEpoch();
  friend constexpr Time InfiniteFuture();
  friend constexpr Time InfinitePast();
  constexpr explicit Time(Duration rep) : rep_(rep) {}
  Duration rep_;
};

// Relational Operators
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator<(Time lhs, Time rhs) {
  return lhs.rep_ < rhs.rep_;
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator>(Time lhs, Time rhs) {
  return rhs < lhs;
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator>=(Time lhs, Time rhs) {
  return !(lhs < rhs);
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator<=(Time lhs, Time rhs) {
  return !(rhs < lhs);
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator==(Time lhs, Time rhs) {
  return lhs.rep_ == rhs.rep_;
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator!=(Time lhs, Time rhs) {
  return !(lhs == rhs);
}

// Additive Operators
ABSL_ATTRIBUTE_CONST_FUNCTION inline Time operator+(Time lhs, Duration rhs) {
  return lhs += rhs;
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline Time operator+(Duration lhs, Time rhs) {
  return rhs += lhs;
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline Time operator-(Time lhs, Duration rhs) {
  return lhs -= rhs;
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline Duration operator-(Time lhs, Time rhs) {
  return lhs.rep_ - rhs.rep_;
}

// UnixEpoch()
//
// Returns the `absl::Time` representing "1970-01-01 00:00:00.0 +0000".
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time UnixEpoch() { return Time(); }

// UniversalEpoch()
//
// Returns the `absl::Time` representing "0001-01-01 00:00:00.0 +0000", the
// epoch of the ICU Universal Time Scale.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time UniversalEpoch() {
  // 719162 is the number of days from 0001-01-01 to 1970-01-01,
  // assuming the Gregorian calendar.
  return Time(
      time_internal::MakeDuration(-24 * 719162 * int64_t{3600}, uint32_t{0}));
}

// InfiniteFuture()
//
// Returns an `absl::Time` that is infinitely far in the future.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time InfiniteFuture() {
  return Time(time_internal::MakeDuration((std::numeric_limits<int64_t>::max)(),
                                          ~uint32_t{0}));
}

// InfinitePast()
//
// Returns an `absl::Time` that is infinitely far in the past.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time InfinitePast() {
  return Time(time_internal::MakeDuration((std::numeric_limits<int64_t>::min)(),
                                          ~uint32_t{0}));
}

// FromUnixNanos()
// FromUnixMicros()
// FromUnixMillis()
// FromUnixSeconds()
// FromTimeT()
// FromUDate()
// FromUniversal()
//
// Creates an `absl::Time` from a variety of other representations.  See
// https://unicode-org.github.io/icu/userguide/datetime/universaltimescale.html
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixNanos(int64_t ns);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixMicros(int64_t us);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixMillis(int64_t ms);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixSeconds(int64_t s);
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromTimeT(time_t t);
ABSL_ATTRIBUTE_CONST_FUNCTION Time FromUDate(double udate);
ABSL_ATTRIBUTE_CONST_FUNCTION Time FromUniversal(int64_t universal);

// ToUnixNanos()
// ToUnixMicros()
// ToUnixMillis()
// ToUnixSeconds()
// ToTimeT()
// ToUDate()
// ToUniversal()
//
// Converts an `absl::Time` to a variety of other representations.  See
// https://unicode-org.github.io/icu/userguide/datetime/universaltimescale.html
//
// Note that these operations round down toward negative infinity where
// necessary to adjust to the resolution of the result type.  Beware of
// possible time_t over/underflow in ToTime{T,val,spec}() on 32-bit platforms.
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToUnixNanos(Time t);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToUnixMicros(Time t);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToUnixMillis(Time t);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToUnixSeconds(Time t);
ABSL_ATTRIBUTE_CONST_FUNCTION time_t ToTimeT(Time t);
ABSL_ATTRIBUTE_CONST_FUNCTION double ToUDate(Time t);
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToUniversal(Time t);

// DurationFromTimespec()
// DurationFromTimeval()
// ToTimespec()
// ToTimeval()
// TimeFromTimespec()
// TimeFromTimeval()
// ToTimespec()
// ToTimeval()
//
// Some APIs use a timespec or a timeval as a Duration (e.g., nanosleep(2)
// and select(2)), while others use them as a Time (e.g. clock_gettime(2)
// and gettimeofday(2)), so conversion functions are provided for both cases.
// The "to timespec/val" direction is easily handled via overloading, but
// for "from timespec/val" the desired type is part of the function name.
ABSL_ATTRIBUTE_CONST_FUNCTION Duration DurationFromTimespec(timespec ts);
ABSL_ATTRIBUTE_CONST_FUNCTION Duration DurationFromTimeval(timeval tv);
ABSL_ATTRIBUTE_CONST_FUNCTION timespec ToTimespec(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION timeval ToTimeval(Duration d);
ABSL_ATTRIBUTE_CONST_FUNCTION Time TimeFromTimespec(timespec ts);
ABSL_ATTRIBUTE_CONST_FUNCTION Time TimeFromTimeval(timeval tv);
ABSL_ATTRIBUTE_CONST_FUNCTION timespec ToTimespec(Time t);
ABSL_ATTRIBUTE_CONST_FUNCTION timeval ToTimeval(Time t);

// FromChrono()
//
// Converts a std::chrono::system_clock::time_point to an absl::Time.
//
// Example:
//
//   auto tp = std::chrono::system_clock::from_time_t(123);
//   absl::Time t = absl::FromChrono(tp);
//   // t == absl::FromTimeT(123)
ABSL_ATTRIBUTE_PURE_FUNCTION Time
FromChrono(const std::chrono::system_clock::time_point& tp);

// ToChronoTime()
//
// Converts an absl::Time to a std::chrono::system_clock::time_point. If
// overflow would occur, the returned value will saturate at the min/max time
// point value instead.
//
// Example:
//
//   absl::Time t = absl::FromTimeT(123);
//   auto tp = absl::ToChronoTime(t);
//   // tp == std::chrono::system_clock::from_time_t(123);
ABSL_ATTRIBUTE_CONST_FUNCTION std::chrono::system_clock::time_point
    ToChronoTime(Time);

// AbslParseFlag()
//
// Parses the command-line flag string representation `text` into a Time value.
// Time flags must be specified in a format that matches absl::RFC3339_full.
//
// For example:
//
//   --start_time=2016-01-02T03:04:05.678+08:00
//
// Note: A UTC offset (or 'Z' indicating a zero-offset from UTC) is required.
//
// Additionally, if you'd like to specify a time as a count of
// seconds/milliseconds/etc from the Unix epoch, use an absl::Duration flag
// and add that duration to absl::UnixEpoch() to get an absl::Time.
bool AbslParseFlag(absl::string_view text, Time* t, std::string* error);

// AbslUnparseFlag()
//
// Unparses a Time value into a command-line string representation using
// the format specified by `absl::ParseTime()`.
std::string AbslUnparseFlag(Time t);

ABSL_DEPRECATED("Use AbslParseFlag() instead.")
bool ParseFlag(const std::string& text, Time* t, std::string* error);
ABSL_DEPRECATED("Use AbslUnparseFlag() instead.")
std::string UnparseFlag(Time t);

// TimeZone
//
// The `absl::TimeZone` is an opaque, small, value-type class representing a
// geo-political region within which particular rules are used for converting
// between absolute and civil times (see https://git.io/v59Ly). `absl::TimeZone`
// values are named using the TZ identifiers from the IANA Time Zone Database,
// such as "America/Los_Angeles" or "Australia/Sydney". `absl::TimeZone` values
// are created from factory functions such as `absl::LoadTimeZone()`. Note:
// strings like "PST" and "EDT" are not valid TZ identifiers. Prefer to pass by
// value rather than const reference.
//
// For more on the fundamental concepts of time zones, absolute times, and civil
// times, see https://github.com/google/cctz#fundamental-concepts
//
// Examples:
//
//   absl::TimeZone utc = absl::UTCTimeZone();
//   absl::TimeZone pst = absl::FixedTimeZone(-8 * 60 * 60);
//   absl::TimeZone loc = absl::LocalTimeZone();
//   absl::TimeZone lax;
//   if (!absl::LoadTimeZone("America/Los_Angeles", &lax)) {
//     // handle error case
//   }
//
// See also:
// - https://github.com/google/cctz
// - https://www.iana.org/time-zones
// - https://en.wikipedia.org/wiki/Zoneinfo
class TimeZone {
 public:
  explicit TimeZone(time_internal::cctz::time_zone tz) : cz_(tz) {}
  TimeZone() = default;  // UTC, but prefer UTCTimeZone() to be explicit.

  // Copyable.
  TimeZone(const TimeZone&) = default;
  TimeZone& operator=(const TimeZone&) = default;

  explicit operator time_internal::cctz::time_zone() const { return cz_; }

  std::string name() const { return cz_.name(); }

  // TimeZone::CivilInfo
  //
  // Information about the civil time corresponding to an absolute time.
  // This struct is not intended to represent an instant in time. So, rather
  // than passing a `TimeZone::CivilInfo` to a function, pass an `absl::Time`
  // and an `absl::TimeZone`.
  struct CivilInfo {
    CivilSecond cs;
    Duration subsecond;

    // Note: The following fields exist for backward compatibility
    // with older APIs.  Accessing these fields directly is a sign of
    // imprudent logic in the calling code.  Modern time-related code
    // should only access this data indirectly by way of FormatTime().
    // These fields are undefined for InfiniteFuture() and InfinitePast().
    int offset;             // seconds east of UTC
    bool is_dst;            // is offset non-standard?
    const char* zone_abbr;  // time-zone abbreviation (e.g., "PST")
  };

  // TimeZone::At(Time)
  //
  // Returns the civil time for this TimeZone at a certain `absl::Time`.
  // If the input time is infinite, the output civil second will be set to
  // CivilSecond::max() or min(), and the subsecond will be infinite.
  //
  // Example:
  //
  //   const auto epoch = lax.At(absl::UnixEpoch());
  //   // epoch.cs == 1969-12-31 16:00:00
  //   // epoch.subsecond == absl::ZeroDuration()
  //   // epoch.offset == -28800
  //   // epoch.is_dst == false
  //   // epoch.abbr == "PST"
  CivilInfo At(Time t) const;

  // TimeZone::TimeInfo
  //
  // Information about the absolute times corresponding to a civil time.
  // (Subseconds must be handled separately.)
  //
  // It is possible for a caller to pass a civil-time value that does
  // not represent an actual or unique instant in time (due to a shift
  // in UTC offset in the TimeZone, which results in a discontinuity in
  // the civil-time components). For example, a daylight-saving-time
  // transition skips or repeats civil times---in the United States,
  // March 13, 2011 02:15 never occurred, while November 6, 2011 01:15
  // occurred twice---so requests for such times are not well-defined.
  // To account for these possibilities, `absl::TimeZone::TimeInfo` is
  // richer than just a single `absl::Time`.
  struct TimeInfo {
    enum CivilKind {
      UNIQUE,    // the civil time was singular (pre == trans == post)
      SKIPPED,   // the civil time did not exist (pre >= trans > post)
      REPEATED,  // the civil time was ambiguous (pre < trans <= post)
    } kind;
    Time pre;    // time calculated using the pre-transition offset
    Time trans;  // when the civil-time discontinuity occurred
    Time post;   // time calculated using the post-transition offset
  };

  // TimeZone::At(CivilSecond)
  //
  // Returns an `absl::TimeInfo` containing the absolute time(s) for this
  // TimeZone at an `absl::CivilSecond`. When the civil time is skipped or
  // repeated, returns times calculated using the pre-transition and post-
  // transition UTC offsets, plus the transition time itself.
  //
  // Examples:
  //
  //   // A unique civil time
  //   const auto jan01 = lax.At(absl::CivilSecond(2011, 1, 1, 0, 0, 0));
  //   // jan01.kind == TimeZone::TimeInfo::UNIQUE
  //   // jan01.pre    is 2011-01-01 00:00:00 -0800
  //   // jan01.trans  is 2011-01-01 00:00:00 -0800
  //   // jan01.post   is 2011-01-01 00:00:00 -0800
  //
  //   // A Spring DST transition, when there is a gap in civil time
  //   const auto mar13 = lax.At(absl::CivilSecond(2011, 3, 13, 2, 15, 0));
  //   // mar13.kind == TimeZone::TimeInfo::SKIPPED
  //   // mar13.pre   is 2011-03-13 03:15:00 -0700
  //   // mar13.trans is 2011-03-13 03:00:00 -0700
  //   // mar13.post  is 2011-03-13 01:15:00 -0800
  //
  //   // A Fall DST transition, when civil times are repeated
  //   const auto nov06 = lax.At(absl::CivilSecond(2011, 11, 6, 1, 15, 0));
  //   // nov06.kind == TimeZone::TimeInfo::REPEATED
  //   // nov06.pre   is 2011-11-06 01:15:00 -0700
  //   // nov06.trans is 2011-11-06 01:00:00 -0800
  //   // nov06.post  is 2011-11-06 01:15:00 -0800
  TimeInfo At(CivilSecond ct) const;

  // TimeZone::NextTransition()
  // TimeZone::PrevTransition()
  //
  // Finds the time of the next/previous offset change in this time zone.
  //
  // By definition, `NextTransition(t, &trans)` returns false when `t` is
  // `InfiniteFuture()`, and `PrevTransition(t, &trans)` returns false
  // when `t` is `InfinitePast()`. If the zone has no transitions, the
  // result will also be false no matter what the argument.
  //
  // Otherwise, when `t` is `InfinitePast()`, `NextTransition(t, &trans)`
  // returns true and sets `trans` to the first recorded transition. Chains
  // of calls to `NextTransition()/PrevTransition()` will eventually return
  // false, but it is unspecified exactly when `NextTransition(t, &trans)`
  // jumps to false, or what time is set by `PrevTransition(t, &trans)` for
  // a very distant `t`.
  //
  // Note: Enumeration of time-zone transitions is for informational purposes
  // only. Modern time-related code should not care about when offset changes
  // occur.
  //
  // Example:
  //   absl::TimeZone nyc;
  //   if (!absl::LoadTimeZone("America/New_York", &nyc)) { ... }
  //   const auto now = absl::Now();
  //   auto t = absl::InfinitePast();
  //   absl::TimeZone::CivilTransition trans;
  //   while (t <= now && nyc.NextTransition(t, &trans)) {
  //     // transition: trans.from -> trans.to
  //     t = nyc.At(trans.to).trans;
  //   }
  struct CivilTransition {
    CivilSecond from;  // the civil time we jump from
    CivilSecond to;    // the civil time we jump to
  };
  bool NextTransition(Time t, CivilTransition* trans) const;
  bool PrevTransition(Time t, CivilTransition* trans) const;

  template <typename H>
  friend H AbslHashValue(H h, TimeZone tz) {
    return H::combine(std::move(h), tz.cz_);
  }

 private:
  friend bool operator==(TimeZone a, TimeZone b) { return a.cz_ == b.cz_; }
  friend bool operator!=(TimeZone a, TimeZone b) { return a.cz_ != b.cz_; }
  friend std::ostream& operator<<(std::ostream& os, TimeZone tz) {
    return os << tz.name();
  }

  time_internal::cctz::time_zone cz_;
};

// LoadTimeZone()
//
// Loads the named zone. May perform I/O on the initial load of the named
// zone. If the name is invalid, or some other kind of error occurs, returns
// `false` and `*tz` is set to the UTC time zone.
inline bool LoadTimeZone(absl::string_view name, TimeZone* tz) {
  if (name == "localtime") {
    *tz = TimeZone(time_internal::cctz::local_time_zone());
    return true;
  }
  time_internal::cctz::time_zone cz;
  const bool b = time_internal::cctz::load_time_zone(std::string(name), &cz);
  *tz = TimeZone(cz);
  return b;
}

// FixedTimeZone()
//
// Returns a TimeZone that is a fixed offset (seconds east) from UTC.
// Note: If the absolute value of the offset is greater than 24 hours
// you'll get UTC (i.e., no offset) instead.
inline TimeZone FixedTimeZone(int seconds) {
  return TimeZone(
      time_internal::cctz::fixed_time_zone(std::chrono::seconds(seconds)));
}

// UTCTimeZone()
//
// Convenience method returning the UTC time zone.
inline TimeZone UTCTimeZone() {
  return TimeZone(time_internal::cctz::utc_time_zone());
}

// LocalTimeZone()
//
// Convenience method returning the local time zone, or UTC if there is
// no configured local zone.  Warning: Be wary of using LocalTimeZone(),
// and particularly so in a server process, as the zone configured for the
// local machine should be irrelevant.  Prefer an explicit zone name.
inline TimeZone LocalTimeZone() {
  return TimeZone(time_internal::cctz::local_time_zone());
}

// ToCivilSecond()
// ToCivilMinute()
// ToCivilHour()
// ToCivilDay()
// ToCivilMonth()
// ToCivilYear()
//
// Helpers for TimeZone::At(Time) to return particularly aligned civil times.
//
// Example:
//
//   absl::Time t = ...;
//   absl::TimeZone tz = ...;
//   const auto cd = absl::ToCivilDay(t, tz);
ABSL_ATTRIBUTE_PURE_FUNCTION inline CivilSecond ToCivilSecond(Time t,
                                                              TimeZone tz) {
  return tz.At(t).cs;  // already a CivilSecond
}
ABSL_ATTRIBUTE_PURE_FUNCTION inline CivilMinute ToCivilMinute(Time t,
                                                              TimeZone tz) {
  return CivilMinute(tz.At(t).cs);
}
ABSL_ATTRIBUTE_PURE_FUNCTION inline CivilHour ToCivilHour(Time t, TimeZone tz) {
  return CivilHour(tz.At(t).cs);
}
ABSL_ATTRIBUTE_PURE_FUNCTION inline CivilDay ToCivilDay(Time t, TimeZone tz) {
  return CivilDay(tz.At(t).cs);
}
ABSL_ATTRIBUTE_PURE_FUNCTION inline CivilMonth ToCivilMonth(Time t,
                                                            TimeZone tz) {
  return CivilMonth(tz.At(t).cs);
}
ABSL_ATTRIBUTE_PURE_FUNCTION inline CivilYear ToCivilYear(Time t, TimeZone tz) {
  return CivilYear(tz.At(t).cs);
}

// FromCivil()
//
// Helper for TimeZone::At(CivilSecond) that provides "order-preserving
// semantics." If the civil time maps to a unique time, that time is
// returned. If the civil time is repeated in the given time zone, the
// time using the pre-transition offset is returned. Otherwise, the
// civil time is skipped in the given time zone, and the transition time
// is returned. This means that for any two civil times, ct1 and ct2,
// (ct1 < ct2) => (FromCivil(ct1) <= FromCivil(ct2)), the equal case
// being when two non-existent civil times map to the same transition time.
//
// Note: Accepts civil times of any alignment.
ABSL_ATTRIBUTE_PURE_FUNCTION inline Time FromCivil(CivilSecond ct,
                                                   TimeZone tz) {
  const auto ti = tz.At(ct);
  if (ti.kind == TimeZone::TimeInfo::SKIPPED) return ti.trans;
  return ti.pre;
}

// TimeConversion
//
// An `absl::TimeConversion` represents the conversion of year, month, day,
// hour, minute, and second values (i.e., a civil time), in a particular
// `absl::TimeZone`, to a time instant (an absolute time), as returned by
// `absl::ConvertDateTime()`. Legacy version of `absl::TimeZone::TimeInfo`.
//
// Deprecated. Use `absl::TimeZone::TimeInfo`.
struct ABSL_DEPRECATED("Use `absl::TimeZone::TimeInfo`.") TimeConversion {
  Time pre;    // time calculated using the pre-transition offset
  Time trans;  // when the civil-time discontinuity occurred
  Time post;   // time calculated using the post-transition offset

  enum Kind {
    UNIQUE,    // the civil time was singular (pre == trans == post)
    SKIPPED,   // the civil time did not exist
    REPEATED,  // the civil time was ambiguous
  };
  Kind kind;

  bool normalized;  // input values were outside their valid ranges
};

// ConvertDateTime()
//
// Legacy version of `absl::TimeZone::At(absl::CivilSecond)` that takes
// the civil time as six, separate values (YMDHMS).
//
// The input month, day, hour, minute, and second values can be outside
// of their valid ranges, in which case they will be "normalized" during
// the conversion.
//
// Example:
//
//   // "October 32" normalizes to "November 1".
//   absl::TimeConversion tc =
//       absl::ConvertDateTime(2013, 10, 32, 8, 30, 0, lax);
//   // tc.kind == TimeConversion::UNIQUE && tc.normalized == true
//   // absl::ToCivilDay(tc.pre, tz).month() == 11
//   // absl::ToCivilDay(tc.pre, tz).day() == 1
//
// Deprecated. Use `absl::TimeZone::At(CivilSecond)`.
ABSL_INTERNAL_DISABLE_DEPRECATED_DECLARATION_WARNING
ABSL_DEPRECATED("Use `absl::TimeZone::At(CivilSecond)`.")
TimeConversion ConvertDateTime(int64_t year, int mon, int day, int hour,
                               int min, int sec, TimeZone tz);
ABSL_INTERNAL_RESTORE_DEPRECATED_DECLARATION_WARNING

// FromDateTime()
//
// A convenience wrapper for `absl::ConvertDateTime()` that simply returns
// the "pre" `absl::Time`.  That is, the unique result, or the instant that
// is correct using the pre-transition offset (as if the transition never
// happened).
//
// Example:
//
//   absl::Time t = absl::FromDateTime(2017, 9, 26, 9, 30, 0, lax);
//   // t = 2017-09-26 09:30:00 -0700
//
// Deprecated. Use `absl::FromCivil(CivilSecond, TimeZone)`. Note that the
// behavior of `FromCivil()` differs from `FromDateTime()` for skipped civil
// times. If you care about that see `absl::TimeZone::At(absl::CivilSecond)`.
ABSL_DEPRECATED("Use `absl::FromCivil(CivilSecond, TimeZone)`.")
inline Time FromDateTime(int64_t year, int mon, int day, int hour, int min,
                         int sec, TimeZone tz) {
  ABSL_INTERNAL_DISABLE_DEPRECATED_DECLARATION_WARNING
  return ConvertDateTime(year, mon, day, hour, min, sec, tz).pre;
  ABSL_INTERNAL_RESTORE_DEPRECATED_DECLARATION_WARNING
}

// FromTM()
//
// Converts the `tm_year`, `tm_mon`, `tm_mday`, `tm_hour`, `tm_min`, and
// `tm_sec` fields to an `absl::Time` using the given time zone. See ctime(3)
// for a description of the expected values of the tm fields. If the civil time
// is unique (see `absl::TimeZone::At(absl::CivilSecond)` above), the matching
// time instant is returned.  Otherwise, the `tm_isdst` field is consulted to
// choose between the possible results.  For a repeated civil time, `tm_isdst !=
// 0` returns the matching DST instant, while `tm_isdst == 0` returns the
// matching non-DST instant.  For a skipped civil time there is no matching
// instant, so `tm_isdst != 0` returns the DST instant, and `tm_isdst == 0`
// returns the non-DST instant, that would have matched if the transition never
// happened.
ABSL_ATTRIBUTE_PURE_FUNCTION Time FromTM(const struct tm& tm, TimeZone tz);

// ToTM()
//
// Converts the given `absl::Time` to a struct tm using the given time zone.
// See ctime(3) for a description of the values of the tm fields.
ABSL_ATTRIBUTE_PURE_FUNCTION struct tm ToTM(Time t, TimeZone tz);

// RFC3339_full
// RFC3339_sec
//
// FormatTime()/ParseTime() format specifiers for RFC3339 date/time strings,
// with trailing zeros trimmed or with fractional seconds omitted altogether.
//
// Note that RFC3339_sec[] matches an ISO 8601 extended format for date and
// time with UTC offset.  Also note the use of "%Y": RFC3339 mandates that
// years have exactly four digits, but we allow them to take their natural
// width.
ABSL_DLL extern const char RFC3339_full[];  // %Y-%m-%d%ET%H:%M:%E*S%Ez
ABSL_DLL extern const char RFC3339_sec[];   // %Y-%m-%d%ET%H:%M:%S%Ez

// RFC1123_full
// RFC1123_no_wday
//
// FormatTime()/ParseTime() format specifiers for RFC1123 date/time strings.
ABSL_DLL extern const char RFC1123_full[];     // %a, %d %b %E4Y %H:%M:%S %z
ABSL_DLL extern const char RFC1123_no_wday[];  // %d %b %E4Y %H:%M:%S %z

// FormatTime()
//
// Formats the given `absl::Time` in the `absl::TimeZone` according to the
// provided format string. Uses strftime()-like formatting options, with
// the following extensions:
//
//   - %Ez  - RFC3339-compatible numeric UTC offset (+hh:mm or -hh:mm)
//   - %E*z - Full-resolution numeric UTC offset (+hh:mm:ss or -hh:mm:ss)
//   - %E#S - Seconds with # digits of fractional precision
//   - %E*S - Seconds with full fractional precision (a literal '*')
//   - %E#f - Fractional seconds with # digits of precision
//   - %E*f - Fractional seconds with full precision (a literal '*')
//   - %E4Y - Four-character years (-999 ... -001, 0000, 0001 ... 9999)
//   - %ET  - The RFC3339 "date-time" separator "T"
//
// Note that %E0S behaves like %S, and %E0f produces no characters.  In
// contrast %E*f always produces at least one digit, which may be '0'.
//
// Note that %Y produces as many characters as it takes to fully render the
// year.  A year outside of [-999:9999] when formatted with %E4Y will produce
// more than four characters, just like %Y.
//
// We recommend that format strings include the UTC offset (%z, %Ez, or %E*z)
// so that the result uniquely identifies a time instant.
//
// Example:
//
//   absl::CivilSecond cs(2013, 1, 2, 3, 4, 5);
//   absl::Time t = absl::FromCivil(cs, lax);
//   std::string f = absl::FormatTime("%H:%M:%S", t, lax);  // "03:04:05"
//   f = absl::FormatTime("%H:%M:%E3S", t, lax);  // "03:04:05.000"
//
// Note: If the given `absl::Time` is `absl::InfiniteFuture()`, the returned
// string will be exactly "infinite-future". If the given `absl::Time` is
// `absl::InfinitePast()`, the returned string will be exactly "infinite-past".
// In both cases the given format string and `absl::TimeZone` are ignored.
//
ABSL_ATTRIBUTE_PURE_FUNCTION std::string FormatTime(absl::string_view format,
                                                    Time t, TimeZone tz);

// Convenience functions that format the given time using the RFC3339_full
// format.  The first overload uses the provided TimeZone, while the second
// uses LocalTimeZone().
ABSL_ATTRIBUTE_PURE_FUNCTION std::string FormatTime(Time t, TimeZone tz);
ABSL_ATTRIBUTE_PURE_FUNCTION std::string FormatTime(Time t);

// Output stream operator.
inline std::ostream& operator<<(std::ostream& os, Time t) {
  return os << FormatTime(t);
}

// Support for StrFormat(), StrCat() etc.
template <typename Sink>
void AbslStringify(Sink& sink, Time t) {
  sink.Append(FormatTime(t));
}

// ParseTime()
//
// Parses an input string according to the provided format string and
// returns the corresponding `absl::Time`. Uses strftime()-like formatting
// options, with the same extensions as FormatTime(), but with the
// exceptions that %E#S is interpreted as %E*S, and %E#f as %E*f.  %Ez
// and %E*z also accept the same inputs, which (along with %z) includes
// 'z' and 'Z' as synonyms for +00:00.  %ET accepts either 'T' or 't'.
//
// %Y consumes as many numeric characters as it can, so the matching data
// should always be terminated with a non-numeric.  %E4Y always consumes
// exactly four characters, including any sign.
//
// Unspecified fields are taken from the default date and time of ...
//
//   "1970-01-01 00:00:00.0 +0000"
//
// For example, parsing a string of "15:45" (%H:%M) will return an absl::Time
// that represents "1970-01-01 15:45:00.0 +0000".
//
// Note that since ParseTime() returns time instants, it makes the most sense
// to parse fully-specified date/time strings that include a UTC offset (%z,
// %Ez, or %E*z).
//
// Note also that `absl::ParseTime()` only heeds the fields year, month, day,
// hour, minute, (fractional) second, and UTC offset.  Other fields, like
// weekday (%a or %A), while parsed for syntactic validity, are ignored
// in the conversion.
//
// Date and time fields that are out-of-range will be treated as errors
// rather than normalizing them like `absl::CivilSecond` does.  For example,
// it is an error to parse the date "Oct 32, 2013" because 32 is out of range.
//
// A leap second of ":60" is normalized to ":00" of the following minute
// with fractional seconds discarded.  The following table shows how the
// given seconds and subseconds will be parsed:
//
//   "59.x" -> 59.x  // exact
//   "60.x" -> 00.0  // normalized
//   "00.x" -> 00.x  // exact
//
// Errors are indicated by returning false and assigning an error message
// to the "err" out param if it is non-null.
//
// Note: If the input string is exactly "infinite-future", the returned
// `absl::Time` will be `absl::InfiniteFuture()` and `true` will be returned.
// If the input string is "infinite-past", the returned `absl::Time` will be
// `absl::InfinitePast()` and `true` will be returned.
//
bool ParseTime(absl::string_view format, absl::string_view input, Time* time,
               std::string* err);

// Like ParseTime() above, but if the format string does not contain a UTC
// offset specification (%z/%Ez/%E*z) then the input is interpreted in the
// given TimeZone.  This means that the input, by itself, does not identify a
// unique instant.  Being time-zone dependent, it also admits the possibility
// of ambiguity or non-existence, in which case the "pre" time (as defined
// by TimeZone::TimeInfo) is returned.  For these reasons we recommend that
// all date/time strings include a UTC offset so they're context independent.
bool ParseTime(absl::string_view format, absl::string_view input, TimeZone tz,
               Time* time, std::string* err);

// ============================================================================
// Implementation Details Follow
// ============================================================================

namespace time_internal {

// Creates a Duration with a given representation.
// REQUIRES: hi,lo is a valid representation of a Duration as specified
// in time/duration.cc.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration MakeDuration(int64_t hi,
                                                              uint32_t lo = 0) {
  return Duration(hi, lo);
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration MakeDuration(int64_t hi,
                                                              int64_t lo) {
  return MakeDuration(hi, static_cast<uint32_t>(lo));
}

// Make a Duration value from a floating-point number, as long as that number
// is in the range [ 0 .. numeric_limits<int64_t>::max ), that is, as long as
// it's positive and can be converted to int64_t without risk of UB.
ABSL_ATTRIBUTE_CONST_FUNCTION inline Duration MakePosDoubleDuration(double n) {
  const int64_t int_secs = static_cast<int64_t>(n);
  const uint32_t ticks = static_cast<uint32_t>(
      std::round((n - static_cast<double>(int_secs)) * kTicksPerSecond));
  return ticks < kTicksPerSecond
             ? MakeDuration(int_secs, ticks)
             : MakeDuration(int_secs + 1, ticks - kTicksPerSecond);
}

// Creates a normalized Duration from an almost-normalized (sec,ticks)
// pair. sec may be positive or negative.  ticks must be in the range
// -kTicksPerSecond < *ticks < kTicksPerSecond.  If ticks is negative it
// will be normalized to a positive value in the resulting Duration.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration MakeNormalizedDuration(
    int64_t sec, int64_t ticks) {
  return (ticks < 0) ? MakeDuration(sec - 1, ticks + kTicksPerSecond)
                     : MakeDuration(sec, ticks);
}

// Provide access to the Duration representation.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr int64_t GetRepHi(Duration d) {
  return d.rep_hi_.Get();
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr uint32_t GetRepLo(Duration d) {
  return d.rep_lo_;
}

// Returns true iff d is positive or negative infinity.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool IsInfiniteDuration(Duration d) {
  return GetRepLo(d) == ~uint32_t{0};
}

// Returns an infinite Duration with the opposite sign.
// REQUIRES: IsInfiniteDuration(d)
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration OppositeInfinity(Duration d) {
  return GetRepHi(d) < 0
             ? MakeDuration((std::numeric_limits<int64_t>::max)(), ~uint32_t{0})
             : MakeDuration((std::numeric_limits<int64_t>::min)(),
                            ~uint32_t{0});
}

// Returns (-n)-1 (equivalently -(n+1)) without avoidable overflow.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr int64_t NegateAndSubtractOne(
    int64_t n) {
  // Note: Good compilers will optimize this expression to ~n when using
  // a two's-complement representation (which is required for int64_t).
  return (n < 0) ? -(n + 1) : (-n) - 1;
}

// Map between a Time and a Duration since the Unix epoch.  Note that these
// functions depend on the above mentioned choice of the Unix epoch for the
// Time representation (and both need to be Time friends).  Without this
// knowledge, we would need to add-in/subtract-out UnixEpoch() respectively.
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixDuration(Duration d) {
  return Time(d);
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration ToUnixDuration(Time t) {
  return t.rep_;
}

template <std::intmax_t N>
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration FromInt64(int64_t v,
                                                           std::ratio<1, N>) {
  static_assert(0 < N && N <= 1000 * 1000 * 1000, "Unsupported ratio");
  // Subsecond ratios cannot overflow.
  return MakeNormalizedDuration(
      v / N, v % N * kTicksPerNanosecond * 1000 * 1000 * 1000 / N);
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration FromInt64(int64_t v,
                                                           std::ratio<60>) {
  return (v <= (std::numeric_limits<int64_t>::max)() / 60 &&
          v >= (std::numeric_limits<int64_t>::min)() / 60)
             ? MakeDuration(v * 60)
             : v > 0 ? InfiniteDuration() : -InfiniteDuration();
}
ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration FromInt64(int64_t v,
                                                           std::ratio<3600>) {
  return (v <= (std::numeric_limits<int64_t>::max)() / 3600 &&
          v >= (std::numeric_limits<int64_t>::min)() / 3600)
             ? MakeDuration(v * 3600)
             : v > 0 ? InfiniteDuration() : -InfiniteDuration();
}

// IsValidRep64<T>(0) is true if the expression `int64_t{std::declval<T>()}` is
// valid. That is, if a T can be assigned to an int64_t without narrowing.
template <typename T>
constexpr auto IsValidRep64(int) -> decltype(int64_t{std::declval<T>()} == 0) {
  return true;
}
template <typename T>
constexpr auto IsValidRep64(char) -> bool {
  return false;
}

// Converts a std::chrono::duration to an absl::Duration.
template <typename Rep, typename Period>
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::duration<Rep, Period>& d) {
  static_assert(IsValidRep64<Rep>(0), "duration::rep is invalid");
  return FromInt64(int64_t{d.count()}, Period{});
}

template <typename Ratio>
ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToInt64(Duration d, Ratio) {
  // Note: This may be used on MSVC, which may have a system_clock period of
  // std::ratio<1, 10 * 1000 * 1000>
  return ToInt64Seconds(d * Ratio::den / Ratio::num);
}
// Fastpath implementations for the 6 common duration units.
ABSL_ATTRIBUTE_CONST_FUNCTION inline int64_t ToInt64(Duration d, std::nano) {
  return ToInt64Nanoseconds(d);
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline int64_t ToInt64(Duration d, std::micro) {
  return ToInt64Microseconds(d);
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline int64_t ToInt64(Duration d, std::milli) {
  return ToInt64Milliseconds(d);
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline int64_t ToInt64(Duration d,
                                                     std::ratio<1>) {
  return ToInt64Seconds(d);
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline int64_t ToInt64(Duration d,
                                                     std::ratio<60>) {
  return ToInt64Minutes(d);
}
ABSL_ATTRIBUTE_CONST_FUNCTION inline int64_t ToInt64(Duration d,
                                                     std::ratio<3600>) {
  return ToInt64Hours(d);
}

// Converts an absl::Duration to a chrono duration of type T.
template <typename T>
ABSL_ATTRIBUTE_CONST_FUNCTION T ToChronoDuration(Duration d) {
  using Rep = typename T::rep;
  using Period = typename T::period;
  static_assert(IsValidRep64<Rep>(0), "duration::rep is invalid");
  if (time_internal::IsInfiniteDuration(d))
    return d < ZeroDuration() ? (T::min)() : (T::max)();
  const auto v = ToInt64(d, Period{});
  if (v > (std::numeric_limits<Rep>::max)()) return (T::max)();
  if (v < (std::numeric_limits<Rep>::min)()) return (T::min)();
  return T{v};
}

}  // namespace time_internal

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator<(Duration lhs,
                                                       Duration rhs) {
  return time_internal::GetRepHi(lhs) != time_internal::GetRepHi(rhs)
             ? time_internal::GetRepHi(lhs) < time_internal::GetRepHi(rhs)
         : time_internal::GetRepHi(lhs) == (std::numeric_limits<int64_t>::min)()
             ? time_internal::GetRepLo(lhs) + 1 <
                   time_internal::GetRepLo(rhs) + 1
             : time_internal::GetRepLo(lhs) < time_internal::GetRepLo(rhs);
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr bool operator==(Duration lhs,
                                                        Duration rhs) {
  return time_internal::GetRepHi(lhs) == time_internal::GetRepHi(rhs) &&
         time_internal::GetRepLo(lhs) == time_internal::GetRepLo(rhs);
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration operator-(Duration d) {
  // This is a little interesting because of the special cases.
  //
  // If rep_lo_ is zero, we have it easy; it's safe to negate rep_hi_, we're
  // dealing with an integral number of seconds, and the only special case is
  // the maximum negative finite duration, which can't be negated.
  //
  // Infinities stay infinite, and just change direction.
  //
  // Finally we're in the case where rep_lo_ is non-zero, and we can borrow
  // a second's worth of ticks and avoid overflow (as negating int64_t-min + 1
  // is safe).
  return time_internal::GetRepLo(d) == 0
             ? time_internal::GetRepHi(d) ==
                       (std::numeric_limits<int64_t>::min)()
                   ? InfiniteDuration()
                   : time_internal::MakeDuration(-time_internal::GetRepHi(d))
             : time_internal::IsInfiniteDuration(d)
                   ? time_internal::OppositeInfinity(d)
                   : time_internal::MakeDuration(
                         time_internal::NegateAndSubtractOne(
                             time_internal::GetRepHi(d)),
                         time_internal::kTicksPerSecond -
                             time_internal::GetRepLo(d));
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Duration InfiniteDuration() {
  return time_internal::MakeDuration((std::numeric_limits<int64_t>::max)(),
                                     ~uint32_t{0});
}

ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::nanoseconds& d) {
  return time_internal::FromChrono(d);
}
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::microseconds& d) {
  return time_internal::FromChrono(d);
}
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::milliseconds& d) {
  return time_internal::FromChrono(d);
}
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::seconds& d) {
  return time_internal::FromChrono(d);
}
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::minutes& d) {
  return time_internal::FromChrono(d);
}
ABSL_ATTRIBUTE_PURE_FUNCTION constexpr Duration FromChrono(
    const std::chrono::hours& d) {
  return time_internal::FromChrono(d);
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixNanos(int64_t ns) {
  return time_internal::FromUnixDuration(Nanoseconds(ns));
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixMicros(int64_t us) {
  return time_internal::FromUnixDuration(Microseconds(us));
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixMillis(int64_t ms) {
  return time_internal::FromUnixDuration(Milliseconds(ms));
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromUnixSeconds(int64_t s) {
  return time_internal::FromUnixDuration(Seconds(s));
}

ABSL_ATTRIBUTE_CONST_FUNCTION constexpr Time FromTimeT(time_t t) {
  return time_internal::FromUnixDuration(Seconds(t));
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_TIME_H_
