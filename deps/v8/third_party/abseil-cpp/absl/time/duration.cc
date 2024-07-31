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

// The implementation of the absl::Duration class, which is declared in
// //absl/time.h.  This class behaves like a numeric type; it has no public
// methods and is used only through the operators defined here.
//
// Implementation notes:
//
// An absl::Duration is represented as
//
//   rep_hi_ : (int64_t)  Whole seconds
//   rep_lo_ : (uint32_t) Fractions of a second
//
// The seconds value (rep_hi_) may be positive or negative as appropriate.
// The fractional seconds (rep_lo_) is always a positive offset from rep_hi_.
// The API for Duration guarantees at least nanosecond resolution, which
// means rep_lo_ could have a max value of 1B - 1 if it stored nanoseconds.
// However, to utilize more of the available 32 bits of space in rep_lo_,
// we instead store quarters of a nanosecond in rep_lo_ resulting in a max
// value of 4B - 1.  This allows us to correctly handle calculations like
// 0.5 nanos + 0.5 nanos = 1 nano.  The following example shows the actual
// Duration rep using quarters of a nanosecond.
//
//    2.5 sec = {rep_hi_=2,  rep_lo_=2000000000}  // lo = 4 * 500000000
//   -2.5 sec = {rep_hi_=-3, rep_lo_=2000000000}
//
// Infinite durations are represented as Durations with the rep_lo_ field set
// to all 1s.
//
//   +InfiniteDuration:
//     rep_hi_ : kint64max
//     rep_lo_ : ~0U
//
//   -InfiniteDuration:
//     rep_hi_ : kint64min
//     rep_lo_ : ~0U
//
// Arithmetic overflows/underflows to +/- infinity and saturates.

#if defined(_MSC_VER)
#include <winsock2.h>  // for timeval
#endif

#include <algorithm>
#include <cassert>
#include <chrono>  // NOLINT(build/c++11)
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace {

using time_internal::kTicksPerNanosecond;
using time_internal::kTicksPerSecond;

constexpr int64_t kint64max = std::numeric_limits<int64_t>::max();
constexpr int64_t kint64min = std::numeric_limits<int64_t>::min();

// Can't use std::isinfinite() because it doesn't exist on windows.
inline bool IsFinite(double d) {
  if (std::isnan(d)) return false;
  return d != std::numeric_limits<double>::infinity() &&
         d != -std::numeric_limits<double>::infinity();
}

inline bool IsValidDivisor(double d) {
  if (std::isnan(d)) return false;
  return d != 0.0;
}

// *sec may be positive or negative.  *ticks must be in the range
// -kTicksPerSecond < *ticks < kTicksPerSecond.  If *ticks is negative it
// will be normalized to a positive value by adjusting *sec accordingly.
inline void NormalizeTicks(int64_t* sec, int64_t* ticks) {
  if (*ticks < 0) {
    --*sec;
    *ticks += kTicksPerSecond;
  }
}

// Makes a uint128 from the absolute value of the given scalar.
inline uint128 MakeU128(int64_t a) {
  uint128 u128 = 0;
  if (a < 0) {
    ++u128;
    ++a;  // Makes it safe to negate 'a'
    a = -a;
  }
  u128 += static_cast<uint64_t>(a);
  return u128;
}

// Makes a uint128 count of ticks out of the absolute value of the Duration.
inline uint128 MakeU128Ticks(Duration d) {
  int64_t rep_hi = time_internal::GetRepHi(d);
  uint32_t rep_lo = time_internal::GetRepLo(d);
  if (rep_hi < 0) {
    ++rep_hi;
    rep_hi = -rep_hi;
    rep_lo = kTicksPerSecond - rep_lo;
  }
  uint128 u128 = static_cast<uint64_t>(rep_hi);
  u128 *= static_cast<uint64_t>(kTicksPerSecond);
  u128 += rep_lo;
  return u128;
}

// Breaks a uint128 of ticks into a Duration.
inline Duration MakeDurationFromU128(uint128 u128, bool is_neg) {
  int64_t rep_hi;
  uint32_t rep_lo;
  const uint64_t h64 = Uint128High64(u128);
  const uint64_t l64 = Uint128Low64(u128);
  if (h64 == 0) {  // fastpath
    const uint64_t hi = l64 / kTicksPerSecond;
    rep_hi = static_cast<int64_t>(hi);
    rep_lo = static_cast<uint32_t>(l64 - hi * kTicksPerSecond);
  } else {
    // kMaxRepHi64 is the high 64 bits of (2^63 * kTicksPerSecond).
    // Any positive tick count whose high 64 bits are >= kMaxRepHi64
    // is not representable as a Duration.  A negative tick count can
    // have its high 64 bits == kMaxRepHi64 but only when the low 64
    // bits are all zero, otherwise it is not representable either.
    const uint64_t kMaxRepHi64 = 0x77359400UL;
    if (h64 >= kMaxRepHi64) {
      if (is_neg && h64 == kMaxRepHi64 && l64 == 0) {
        // Avoid trying to represent -kint64min below.
        return time_internal::MakeDuration(kint64min);
      }
      return is_neg ? -InfiniteDuration() : InfiniteDuration();
    }
    const uint128 kTicksPerSecond128 = static_cast<uint64_t>(kTicksPerSecond);
    const uint128 hi = u128 / kTicksPerSecond128;
    rep_hi = static_cast<int64_t>(Uint128Low64(hi));
    rep_lo =
        static_cast<uint32_t>(Uint128Low64(u128 - hi * kTicksPerSecond128));
  }
  if (is_neg) {
    rep_hi = -rep_hi;
    if (rep_lo != 0) {
      --rep_hi;
      rep_lo = kTicksPerSecond - rep_lo;
    }
  }
  return time_internal::MakeDuration(rep_hi, rep_lo);
}

// Convert between int64_t and uint64_t, preserving representation. This
// allows us to do arithmetic in the unsigned domain, where overflow has
// well-defined behavior. See operator+=() and operator-=().
//
// C99 7.20.1.1.1, as referenced by C++11 18.4.1.2, says, "The typedef
// name intN_t designates a signed integer type with width N, no padding
// bits, and a two's complement representation." So, we can convert to
// and from the corresponding uint64_t value using a bit cast.
inline uint64_t EncodeTwosComp(int64_t v) {
  return absl::bit_cast<uint64_t>(v);
}
inline int64_t DecodeTwosComp(uint64_t v) { return absl::bit_cast<int64_t>(v); }

// Note: The overflow detection in this function is done using greater/less *or
// equal* because kint64max/min is too large to be represented exactly in a
// double (which only has 53 bits of precision). In order to avoid assigning to
// rep->hi a double value that is too large for an int64_t (and therefore is
// undefined), we must consider computations that equal kint64max/min as a
// double as overflow cases.
inline bool SafeAddRepHi(double a_hi, double b_hi, Duration* d) {
  double c = a_hi + b_hi;
  if (c >= static_cast<double>(kint64max)) {
    *d = InfiniteDuration();
    return false;
  }
  if (c <= static_cast<double>(kint64min)) {
    *d = -InfiniteDuration();
    return false;
  }
  *d = time_internal::MakeDuration(c, time_internal::GetRepLo(*d));
  return true;
}

// A functor that's similar to std::multiplies<T>, except this returns the max
// T value instead of overflowing. This is only defined for uint128.
template <typename Ignored>
struct SafeMultiply {
  uint128 operator()(uint128 a, uint128 b) const {
    // b hi is always zero because it originated as an int64_t.
    assert(Uint128High64(b) == 0);
    // Fastpath to avoid the expensive overflow check with division.
    if (Uint128High64(a) == 0) {
      return (((Uint128Low64(a) | Uint128Low64(b)) >> 32) == 0)
                 ? static_cast<uint128>(Uint128Low64(a) * Uint128Low64(b))
                 : a * b;
    }
    return b == 0 ? b : (a > Uint128Max() / b) ? Uint128Max() : a * b;
  }
};

// Scales (i.e., multiplies or divides, depending on the Operation template)
// the Duration d by the int64_t r.
template <template <typename> class Operation>
inline Duration ScaleFixed(Duration d, int64_t r) {
  const uint128 a = MakeU128Ticks(d);
  const uint128 b = MakeU128(r);
  const uint128 q = Operation<uint128>()(a, b);
  const bool is_neg = (time_internal::GetRepHi(d) < 0) != (r < 0);
  return MakeDurationFromU128(q, is_neg);
}

// Scales (i.e., multiplies or divides, depending on the Operation template)
// the Duration d by the double r.
template <template <typename> class Operation>
inline Duration ScaleDouble(Duration d, double r) {
  Operation<double> op;
  double hi_doub = op(time_internal::GetRepHi(d), r);
  double lo_doub = op(time_internal::GetRepLo(d), r);

  double hi_int = 0;
  double hi_frac = std::modf(hi_doub, &hi_int);

  // Moves hi's fractional bits to lo.
  lo_doub /= kTicksPerSecond;
  lo_doub += hi_frac;

  double lo_int = 0;
  double lo_frac = std::modf(lo_doub, &lo_int);

  // Rolls lo into hi if necessary.
  int64_t lo64 = std::round(lo_frac * kTicksPerSecond);

  Duration ans;
  if (!SafeAddRepHi(hi_int, lo_int, &ans)) return ans;
  int64_t hi64 = time_internal::GetRepHi(ans);
  if (!SafeAddRepHi(hi64, lo64 / kTicksPerSecond, &ans)) return ans;
  hi64 = time_internal::GetRepHi(ans);
  lo64 %= kTicksPerSecond;
  NormalizeTicks(&hi64, &lo64);
  return time_internal::MakeDuration(hi64, lo64);
}

// Tries to divide num by den as fast as possible by looking for common, easy
// cases. If the division was done, the quotient is in *q and the remainder is
// in *rem and true will be returned.
inline bool IDivFastPath(const Duration num, const Duration den, int64_t* q,
                         Duration* rem) {
  // Bail if num or den is an infinity.
  if (time_internal::IsInfiniteDuration(num) ||
      time_internal::IsInfiniteDuration(den))
    return false;

  int64_t num_hi = time_internal::GetRepHi(num);
  uint32_t num_lo = time_internal::GetRepLo(num);
  int64_t den_hi = time_internal::GetRepHi(den);
  uint32_t den_lo = time_internal::GetRepLo(den);

  if (den_hi == 0) {
    if (den_lo == kTicksPerNanosecond) {
      // Dividing by 1ns
      if (num_hi >= 0 && num_hi < (kint64max - kTicksPerSecond) / 1000000000) {
        *q = num_hi * 1000000000 + num_lo / kTicksPerNanosecond;
        *rem = time_internal::MakeDuration(0, num_lo % den_lo);
        return true;
      }
    } else if (den_lo == 100 * kTicksPerNanosecond) {
      // Dividing by 100ns (common when converting to Universal time)
      if (num_hi >= 0 && num_hi < (kint64max - kTicksPerSecond) / 10000000) {
        *q = num_hi * 10000000 + num_lo / (100 * kTicksPerNanosecond);
        *rem = time_internal::MakeDuration(0, num_lo % den_lo);
        return true;
      }
    } else if (den_lo == 1000 * kTicksPerNanosecond) {
      // Dividing by 1us
      if (num_hi >= 0 && num_hi < (kint64max - kTicksPerSecond) / 1000000) {
        *q = num_hi * 1000000 + num_lo / (1000 * kTicksPerNanosecond);
        *rem = time_internal::MakeDuration(0, num_lo % den_lo);
        return true;
      }
    } else if (den_lo == 1000000 * kTicksPerNanosecond) {
      // Dividing by 1ms
      if (num_hi >= 0 && num_hi < (kint64max - kTicksPerSecond) / 1000) {
        *q = num_hi * 1000 + num_lo / (1000000 * kTicksPerNanosecond);
        *rem = time_internal::MakeDuration(0, num_lo % den_lo);
        return true;
      }
    }
  } else if (den_hi > 0 && den_lo == 0) {
    // Dividing by positive multiple of 1s
    if (num_hi >= 0) {
      if (den_hi == 1) {
        *q = num_hi;
        *rem = time_internal::MakeDuration(0, num_lo);
        return true;
      }
      *q = num_hi / den_hi;
      *rem = time_internal::MakeDuration(num_hi % den_hi, num_lo);
      return true;
    }
    if (num_lo != 0) {
      num_hi += 1;
    }
    int64_t quotient = num_hi / den_hi;
    int64_t rem_sec = num_hi % den_hi;
    if (rem_sec > 0) {
      rem_sec -= den_hi;
      quotient += 1;
    }
    if (num_lo != 0) {
      rem_sec -= 1;
    }
    *q = quotient;
    *rem = time_internal::MakeDuration(rem_sec, num_lo);
    return true;
  }

  return false;
}

}  // namespace

namespace {

int64_t IDivSlowPath(bool satq, const Duration num, const Duration den,
                     Duration* rem) {
  const bool num_neg = num < ZeroDuration();
  const bool den_neg = den < ZeroDuration();
  const bool quotient_neg = num_neg != den_neg;

  if (time_internal::IsInfiniteDuration(num) || den == ZeroDuration()) {
    *rem = num_neg ? -InfiniteDuration() : InfiniteDuration();
    return quotient_neg ? kint64min : kint64max;
  }
  if (time_internal::IsInfiniteDuration(den)) {
    *rem = num;
    return 0;
  }

  const uint128 a = MakeU128Ticks(num);
  const uint128 b = MakeU128Ticks(den);
  uint128 quotient128 = a / b;

  if (satq) {
    // Limits the quotient to the range of int64_t.
    if (quotient128 > uint128(static_cast<uint64_t>(kint64max))) {
      quotient128 = quotient_neg ? uint128(static_cast<uint64_t>(kint64min))
                                 : uint128(static_cast<uint64_t>(kint64max));
    }
  }

  const uint128 remainder128 = a - quotient128 * b;
  *rem = MakeDurationFromU128(remainder128, num_neg);

  if (!quotient_neg || quotient128 == 0) {
    return Uint128Low64(quotient128) & kint64max;
  }
  // The quotient needs to be negated, but we need to carefully handle
  // quotient128s with the top bit on.
  return -static_cast<int64_t>(Uint128Low64(quotient128 - 1) & kint64max) - 1;
}

// The 'satq' argument indicates whether the quotient should saturate at the
// bounds of int64_t.  If it does saturate, the difference will spill over to
// the remainder.  If it does not saturate, the remainder remain accurate,
// but the returned quotient will over/underflow int64_t and should not be used.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline int64_t IDivDurationImpl(bool satq,
                                                             const Duration num,
                                                             const Duration den,
                                                             Duration* rem) {
  int64_t q = 0;
  if (IDivFastPath(num, den, &q, rem)) {
    return q;
  }
  return IDivSlowPath(satq, num, den, rem);
}

}  // namespace

int64_t IDivDuration(Duration num, Duration den, Duration* rem) {
  return IDivDurationImpl(true, num, den,
                          rem);  // trunc towards zero
}

//
// Additive operators.
//

Duration& Duration::operator+=(Duration rhs) {
  if (time_internal::IsInfiniteDuration(*this)) return *this;
  if (time_internal::IsInfiniteDuration(rhs)) return *this = rhs;
  const int64_t orig_rep_hi = rep_hi_.Get();
  rep_hi_ = DecodeTwosComp(EncodeTwosComp(rep_hi_.Get()) +
                           EncodeTwosComp(rhs.rep_hi_.Get()));
  if (rep_lo_ >= kTicksPerSecond - rhs.rep_lo_) {
    rep_hi_ = DecodeTwosComp(EncodeTwosComp(rep_hi_.Get()) + 1);
    rep_lo_ -= kTicksPerSecond;
  }
  rep_lo_ += rhs.rep_lo_;
  if (rhs.rep_hi_.Get() < 0 ? rep_hi_.Get() > orig_rep_hi
                            : rep_hi_.Get() < orig_rep_hi) {
    return *this =
               rhs.rep_hi_.Get() < 0 ? -InfiniteDuration() : InfiniteDuration();
  }
  return *this;
}

Duration& Duration::operator-=(Duration rhs) {
  if (time_internal::IsInfiniteDuration(*this)) return *this;
  if (time_internal::IsInfiniteDuration(rhs)) {
    return *this = rhs.rep_hi_.Get() >= 0 ? -InfiniteDuration()
                                          : InfiniteDuration();
  }
  const int64_t orig_rep_hi = rep_hi_.Get();
  rep_hi_ = DecodeTwosComp(EncodeTwosComp(rep_hi_.Get()) -
                           EncodeTwosComp(rhs.rep_hi_.Get()));
  if (rep_lo_ < rhs.rep_lo_) {
    rep_hi_ = DecodeTwosComp(EncodeTwosComp(rep_hi_.Get()) - 1);
    rep_lo_ += kTicksPerSecond;
  }
  rep_lo_ -= rhs.rep_lo_;
  if (rhs.rep_hi_.Get() < 0 ? rep_hi_.Get() < orig_rep_hi
                            : rep_hi_.Get() > orig_rep_hi) {
    return *this = rhs.rep_hi_.Get() >= 0 ? -InfiniteDuration()
                                          : InfiniteDuration();
  }
  return *this;
}

//
// Multiplicative operators.
//

Duration& Duration::operator*=(int64_t r) {
  if (time_internal::IsInfiniteDuration(*this)) {
    const bool is_neg = (r < 0) != (rep_hi_.Get() < 0);
    return *this = is_neg ? -InfiniteDuration() : InfiniteDuration();
  }
  return *this = ScaleFixed<SafeMultiply>(*this, r);
}

Duration& Duration::operator*=(double r) {
  if (time_internal::IsInfiniteDuration(*this) || !IsFinite(r)) {
    const bool is_neg = std::signbit(r) != (rep_hi_.Get() < 0);
    return *this = is_neg ? -InfiniteDuration() : InfiniteDuration();
  }
  return *this = ScaleDouble<std::multiplies>(*this, r);
}

Duration& Duration::operator/=(int64_t r) {
  if (time_internal::IsInfiniteDuration(*this) || r == 0) {
    const bool is_neg = (r < 0) != (rep_hi_.Get() < 0);
    return *this = is_neg ? -InfiniteDuration() : InfiniteDuration();
  }
  return *this = ScaleFixed<std::divides>(*this, r);
}

Duration& Duration::operator/=(double r) {
  if (time_internal::IsInfiniteDuration(*this) || !IsValidDivisor(r)) {
    const bool is_neg = std::signbit(r) != (rep_hi_.Get() < 0);
    return *this = is_neg ? -InfiniteDuration() : InfiniteDuration();
  }
  return *this = ScaleDouble<std::divides>(*this, r);
}

Duration& Duration::operator%=(Duration rhs) {
  IDivDurationImpl(false, *this, rhs, this);
  return *this;
}

double FDivDuration(Duration num, Duration den) {
  // Arithmetic with infinity is sticky.
  if (time_internal::IsInfiniteDuration(num) || den == ZeroDuration()) {
    return (num < ZeroDuration()) == (den < ZeroDuration())
               ? std::numeric_limits<double>::infinity()
               : -std::numeric_limits<double>::infinity();
  }
  if (time_internal::IsInfiniteDuration(den)) return 0.0;

  double a =
      static_cast<double>(time_internal::GetRepHi(num)) * kTicksPerSecond +
      time_internal::GetRepLo(num);
  double b =
      static_cast<double>(time_internal::GetRepHi(den)) * kTicksPerSecond +
      time_internal::GetRepLo(den);
  return a / b;
}

//
// Trunc/Floor/Ceil.
//

Duration Trunc(Duration d, Duration unit) { return d - (d % unit); }

Duration Floor(const Duration d, const Duration unit) {
  const absl::Duration td = Trunc(d, unit);
  return td <= d ? td : td - AbsDuration(unit);
}

Duration Ceil(const Duration d, const Duration unit) {
  const absl::Duration td = Trunc(d, unit);
  return td >= d ? td : td + AbsDuration(unit);
}

//
// Factory functions.
//

Duration DurationFromTimespec(timespec ts) {
  if (static_cast<uint64_t>(ts.tv_nsec) < 1000 * 1000 * 1000) {
    int64_t ticks = ts.tv_nsec * kTicksPerNanosecond;
    return time_internal::MakeDuration(ts.tv_sec, ticks);
  }
  return Seconds(ts.tv_sec) + Nanoseconds(ts.tv_nsec);
}

Duration DurationFromTimeval(timeval tv) {
  if (static_cast<uint64_t>(tv.tv_usec) < 1000 * 1000) {
    int64_t ticks = tv.tv_usec * 1000 * kTicksPerNanosecond;
    return time_internal::MakeDuration(tv.tv_sec, ticks);
  }
  return Seconds(tv.tv_sec) + Microseconds(tv.tv_usec);
}

//
// Conversion to other duration types.
//

int64_t ToInt64Nanoseconds(Duration d) {
  if (time_internal::GetRepHi(d) >= 0 &&
      time_internal::GetRepHi(d) >> 33 == 0) {
    return (time_internal::GetRepHi(d) * 1000 * 1000 * 1000) +
           (time_internal::GetRepLo(d) / kTicksPerNanosecond);
  }
  return d / Nanoseconds(1);
}
int64_t ToInt64Microseconds(Duration d) {
  if (time_internal::GetRepHi(d) >= 0 &&
      time_internal::GetRepHi(d) >> 43 == 0) {
    return (time_internal::GetRepHi(d) * 1000 * 1000) +
           (time_internal::GetRepLo(d) / (kTicksPerNanosecond * 1000));
  }
  return d / Microseconds(1);
}
int64_t ToInt64Milliseconds(Duration d) {
  if (time_internal::GetRepHi(d) >= 0 &&
      time_internal::GetRepHi(d) >> 53 == 0) {
    return (time_internal::GetRepHi(d) * 1000) +
           (time_internal::GetRepLo(d) / (kTicksPerNanosecond * 1000 * 1000));
  }
  return d / Milliseconds(1);
}
int64_t ToInt64Seconds(Duration d) {
  int64_t hi = time_internal::GetRepHi(d);
  if (time_internal::IsInfiniteDuration(d)) return hi;
  if (hi < 0 && time_internal::GetRepLo(d) != 0) ++hi;
  return hi;
}
int64_t ToInt64Minutes(Duration d) {
  int64_t hi = time_internal::GetRepHi(d);
  if (time_internal::IsInfiniteDuration(d)) return hi;
  if (hi < 0 && time_internal::GetRepLo(d) != 0) ++hi;
  return hi / 60;
}
int64_t ToInt64Hours(Duration d) {
  int64_t hi = time_internal::GetRepHi(d);
  if (time_internal::IsInfiniteDuration(d)) return hi;
  if (hi < 0 && time_internal::GetRepLo(d) != 0) ++hi;
  return hi / (60 * 60);
}

double ToDoubleNanoseconds(Duration d) {
  return FDivDuration(d, Nanoseconds(1));
}
double ToDoubleMicroseconds(Duration d) {
  return FDivDuration(d, Microseconds(1));
}
double ToDoubleMilliseconds(Duration d) {
  return FDivDuration(d, Milliseconds(1));
}
double ToDoubleSeconds(Duration d) { return FDivDuration(d, Seconds(1)); }
double ToDoubleMinutes(Duration d) { return FDivDuration(d, Minutes(1)); }
double ToDoubleHours(Duration d) { return FDivDuration(d, Hours(1)); }

timespec ToTimespec(Duration d) {
  timespec ts;
  if (!time_internal::IsInfiniteDuration(d)) {
    int64_t rep_hi = time_internal::GetRepHi(d);
    uint32_t rep_lo = time_internal::GetRepLo(d);
    if (rep_hi < 0) {
      // Tweak the fields so that unsigned division of rep_lo
      // maps to truncation (towards zero) for the timespec.
      rep_lo += kTicksPerNanosecond - 1;
      if (rep_lo >= kTicksPerSecond) {
        rep_hi += 1;
        rep_lo -= kTicksPerSecond;
      }
    }
    ts.tv_sec = static_cast<decltype(ts.tv_sec)>(rep_hi);
    if (ts.tv_sec == rep_hi) {  // no time_t narrowing
      ts.tv_nsec = rep_lo / kTicksPerNanosecond;
      return ts;
    }
  }
  if (d >= ZeroDuration()) {
    ts.tv_sec = std::numeric_limits<time_t>::max();
    ts.tv_nsec = 1000 * 1000 * 1000 - 1;
  } else {
    ts.tv_sec = std::numeric_limits<time_t>::min();
    ts.tv_nsec = 0;
  }
  return ts;
}

timeval ToTimeval(Duration d) {
  timeval tv;
  timespec ts = ToTimespec(d);
  if (ts.tv_sec < 0) {
    // Tweak the fields so that positive division of tv_nsec
    // maps to truncation (towards zero) for the timeval.
    ts.tv_nsec += 1000 - 1;
    if (ts.tv_nsec >= 1000 * 1000 * 1000) {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000 * 1000 * 1000;
    }
  }
  tv.tv_sec = static_cast<decltype(tv.tv_sec)>(ts.tv_sec);
  if (tv.tv_sec != ts.tv_sec) {  // narrowing
    if (ts.tv_sec < 0) {
      tv.tv_sec = std::numeric_limits<decltype(tv.tv_sec)>::min();
      tv.tv_usec = 0;
    } else {
      tv.tv_sec = std::numeric_limits<decltype(tv.tv_sec)>::max();
      tv.tv_usec = 1000 * 1000 - 1;
    }
    return tv;
  }
  tv.tv_usec = static_cast<int>(ts.tv_nsec / 1000);  // suseconds_t
  return tv;
}

std::chrono::nanoseconds ToChronoNanoseconds(Duration d) {
  return time_internal::ToChronoDuration<std::chrono::nanoseconds>(d);
}
std::chrono::microseconds ToChronoMicroseconds(Duration d) {
  return time_internal::ToChronoDuration<std::chrono::microseconds>(d);
}
std::chrono::milliseconds ToChronoMilliseconds(Duration d) {
  return time_internal::ToChronoDuration<std::chrono::milliseconds>(d);
}
std::chrono::seconds ToChronoSeconds(Duration d) {
  return time_internal::ToChronoDuration<std::chrono::seconds>(d);
}
std::chrono::minutes ToChronoMinutes(Duration d) {
  return time_internal::ToChronoDuration<std::chrono::minutes>(d);
}
std::chrono::hours ToChronoHours(Duration d) {
  return time_internal::ToChronoDuration<std::chrono::hours>(d);
}

//
// To/From string formatting.
//

namespace {

// Formats a positive 64-bit integer in the given field width.  Note that
// it is up to the caller of Format64() to ensure that there is sufficient
// space before ep to hold the conversion.
char* Format64(char* ep, int width, int64_t v) {
  do {
    --width;
    *--ep = static_cast<char>('0' + (v % 10));  // contiguous digits
  } while (v /= 10);
  while (--width >= 0) *--ep = '0';  // zero pad
  return ep;
}

// Helpers for FormatDuration() that format 'n' and append it to 'out'
// followed by the given 'unit'.  If 'n' formats to "0", nothing is
// appended (not even the unit).

// A type that encapsulates how to display a value of a particular unit. For
// values that are displayed with fractional parts, the precision indicates
// where to round the value. The precision varies with the display unit because
// a Duration can hold only quarters of a nanosecond, so displaying information
// beyond that is just noise.
//
// For example, a microsecond value of 42.00025xxxxx should not display beyond 5
// fractional digits, because it is in the noise of what a Duration can
// represent.
struct DisplayUnit {
  absl::string_view abbr;
  int prec;
  double pow10;
};
ABSL_CONST_INIT const DisplayUnit kDisplayNano = {"ns", 2, 1e2};
ABSL_CONST_INIT const DisplayUnit kDisplayMicro = {"us", 5, 1e5};
ABSL_CONST_INIT const DisplayUnit kDisplayMilli = {"ms", 8, 1e8};
ABSL_CONST_INIT const DisplayUnit kDisplaySec = {"s", 11, 1e11};
ABSL_CONST_INIT const DisplayUnit kDisplayMin = {"m", -1, 0.0};  // prec ignored
ABSL_CONST_INIT const DisplayUnit kDisplayHour = {"h", -1,
                                                  0.0};  // prec ignored

void AppendNumberUnit(std::string* out, int64_t n, DisplayUnit unit) {
  char buf[sizeof("2562047788015216")];  // hours in max duration
  char* const ep = buf + sizeof(buf);
  char* bp = Format64(ep, 0, n);
  if (*bp != '0' || bp + 1 != ep) {
    out->append(bp, static_cast<size_t>(ep - bp));
    out->append(unit.abbr.data(), unit.abbr.size());
  }
}

// Note: unit.prec is limited to double's digits10 value (typically 15) so it
// always fits in buf[].
void AppendNumberUnit(std::string* out, double n, DisplayUnit unit) {
  constexpr int kBufferSize = std::numeric_limits<double>::digits10;
  const int prec = std::min(kBufferSize, unit.prec);
  char buf[kBufferSize];  // also large enough to hold integer part
  char* ep = buf + sizeof(buf);
  double d = 0;
  int64_t frac_part = std::round(std::modf(n, &d) * unit.pow10);
  int64_t int_part = d;
  if (int_part != 0 || frac_part != 0) {
    char* bp = Format64(ep, 0, int_part);  // always < 1000
    out->append(bp, static_cast<size_t>(ep - bp));
    if (frac_part != 0) {
      out->push_back('.');
      bp = Format64(ep, prec, frac_part);
      while (ep[-1] == '0') --ep;
      out->append(bp, static_cast<size_t>(ep - bp));
    }
    out->append(unit.abbr.data(), unit.abbr.size());
  }
}

}  // namespace

// From Go's doc at https://golang.org/pkg/time/#Duration.String
//   [FormatDuration] returns a string representing the duration in the
//   form "72h3m0.5s". Leading zero units are omitted.  As a special
//   case, durations less than one second format use a smaller unit
//   (milli-, micro-, or nanoseconds) to ensure that the leading digit
//   is non-zero.
// Unlike Go, we format the zero duration as 0, with no unit.
std::string FormatDuration(Duration d) {
  constexpr Duration kMinDuration = Seconds(kint64min);
  std::string s;
  if (d == kMinDuration) {
    // Avoid needing to negate kint64min by directly returning what the
    // following code should produce in that case.
    s = "-2562047788015215h30m8s";
    return s;
  }
  if (d < ZeroDuration()) {
    s.append("-");
    d = -d;
  }
  if (d == InfiniteDuration()) {
    s.append("inf");
  } else if (d < Seconds(1)) {
    // Special case for durations with a magnitude < 1 second.  The duration
    // is printed as a fraction of a single unit, e.g., "1.2ms".
    if (d < Microseconds(1)) {
      AppendNumberUnit(&s, FDivDuration(d, Nanoseconds(1)), kDisplayNano);
    } else if (d < Milliseconds(1)) {
      AppendNumberUnit(&s, FDivDuration(d, Microseconds(1)), kDisplayMicro);
    } else {
      AppendNumberUnit(&s, FDivDuration(d, Milliseconds(1)), kDisplayMilli);
    }
  } else {
    AppendNumberUnit(&s, IDivDuration(d, Hours(1), &d), kDisplayHour);
    AppendNumberUnit(&s, IDivDuration(d, Minutes(1), &d), kDisplayMin);
    AppendNumberUnit(&s, FDivDuration(d, Seconds(1)), kDisplaySec);
  }
  if (s.empty() || s == "-") {
    s = "0";
  }
  return s;
}

namespace {

// A helper for ParseDuration() that parses a leading number from the given
// string and stores the result in *int_part/*frac_part/*frac_scale.  The
// given string pointer is modified to point to the first unconsumed char.
bool ConsumeDurationNumber(const char** dpp, const char* ep, int64_t* int_part,
                           int64_t* frac_part, int64_t* frac_scale) {
  *int_part = 0;
  *frac_part = 0;
  *frac_scale = 1;  // invariant: *frac_part < *frac_scale
  const char* start = *dpp;
  for (; *dpp != ep; *dpp += 1) {
    const int d = **dpp - '0';  // contiguous digits
    if (d < 0 || 10 <= d) break;

    if (*int_part > kint64max / 10) return false;
    *int_part *= 10;
    if (*int_part > kint64max - d) return false;
    *int_part += d;
  }
  const bool int_part_empty = (*dpp == start);
  if (*dpp == ep || **dpp != '.') return !int_part_empty;

  for (*dpp += 1; *dpp != ep; *dpp += 1) {
    const int d = **dpp - '0';  // contiguous digits
    if (d < 0 || 10 <= d) break;
    if (*frac_scale <= kint64max / 10) {
      *frac_part *= 10;
      *frac_part += d;
      *frac_scale *= 10;
    }
  }
  return !int_part_empty || *frac_scale != 1;
}

// A helper for ParseDuration() that parses a leading unit designator (e.g.,
// ns, us, ms, s, m, h) from the given string and stores the resulting unit
// in "*unit".  The given string pointer is modified to point to the first
// unconsumed char.
bool ConsumeDurationUnit(const char** start, const char* end, Duration* unit) {
  size_t size = static_cast<size_t>(end - *start);
  switch (size) {
    case 0:
      return false;
    default:
      switch (**start) {
        case 'n':
          if (*(*start + 1) == 's') {
            *start += 2;
            *unit = Nanoseconds(1);
            return true;
          }
          break;
        case 'u':
          if (*(*start + 1) == 's') {
            *start += 2;
            *unit = Microseconds(1);
            return true;
          }
          break;
        case 'm':
          if (*(*start + 1) == 's') {
            *start += 2;
            *unit = Milliseconds(1);
            return true;
          }
          break;
        default:
          break;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case 1:
      switch (**start) {
        case 's':
          *unit = Seconds(1);
          *start += 1;
          return true;
        case 'm':
          *unit = Minutes(1);
          *start += 1;
          return true;
        case 'h':
          *unit = Hours(1);
          *start += 1;
          return true;
        default:
          return false;
      }
  }
}

}  // namespace

// From Go's doc at https://golang.org/pkg/time/#ParseDuration
//   [ParseDuration] parses a duration string. A duration string is
//   a possibly signed sequence of decimal numbers, each with optional
//   fraction and a unit suffix, such as "300ms", "-1.5h" or "2h45m".
//   Valid time units are "ns", "us" "ms", "s", "m", "h".
bool ParseDuration(absl::string_view dur_sv, Duration* d) {
  int sign = 1;
  if (absl::ConsumePrefix(&dur_sv, "-")) {
    sign = -1;
  } else {
    absl::ConsumePrefix(&dur_sv, "+");
  }
  if (dur_sv.empty()) return false;

  // Special case for a string of "0".
  if (dur_sv == "0") {
    *d = ZeroDuration();
    return true;
  }

  if (dur_sv == "inf") {
    *d = sign * InfiniteDuration();
    return true;
  }

  const char* start = dur_sv.data();
  const char* end = start + dur_sv.size();

  Duration dur;
  while (start != end) {
    int64_t int_part;
    int64_t frac_part;
    int64_t frac_scale;
    Duration unit;
    if (!ConsumeDurationNumber(&start, end, &int_part, &frac_part,
                               &frac_scale) ||
        !ConsumeDurationUnit(&start, end, &unit)) {
      return false;
    }
    if (int_part != 0) dur += sign * int_part * unit;
    if (frac_part != 0) dur += sign * frac_part * unit / frac_scale;
  }
  *d = dur;
  return true;
}

bool AbslParseFlag(absl::string_view text, Duration* dst, std::string*) {
  return ParseDuration(text, dst);
}

std::string AbslUnparseFlag(Duration d) { return FormatDuration(d); }
bool ParseFlag(const std::string& text, Duration* dst, std::string* ) {
  return ParseDuration(text, dst);
}

std::string UnparseFlag(Duration d) { return FormatDuration(d); }

ABSL_NAMESPACE_END
}  // namespace absl
