//
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

// This file contains :int128 implementation details that depend on internal
// representation when ABSL_HAVE_INTRINSIC_INT128 is *not* defined. This file
// is included by int128.h and relies on ABSL_INTERNAL_WCHAR_T being defined.

constexpr uint64_t Int128Low64(int128 v) { return v.lo_; }

constexpr int64_t Int128High64(int128 v) { return v.hi_; }

#if defined(ABSL_IS_LITTLE_ENDIAN)

constexpr int128::int128(int64_t high, uint64_t low) : lo_(low), hi_(high) {}

constexpr int128::int128(int v)
    : lo_{static_cast<uint64_t>(v)}, hi_{v < 0 ? ~int64_t{0} : 0} {}
constexpr int128::int128(long v)  // NOLINT(runtime/int)
    : lo_{static_cast<uint64_t>(v)}, hi_{v < 0 ? ~int64_t{0} : 0} {}
constexpr int128::int128(long long v)  // NOLINT(runtime/int)
    : lo_{static_cast<uint64_t>(v)}, hi_{v < 0 ? ~int64_t{0} : 0} {}

constexpr int128::int128(unsigned int v) : lo_{v}, hi_{0} {}
// NOLINTNEXTLINE(runtime/int)
constexpr int128::int128(unsigned long v) : lo_{v}, hi_{0} {}
// NOLINTNEXTLINE(runtime/int)
constexpr int128::int128(unsigned long long v) : lo_{v}, hi_{0} {}

constexpr int128::int128(uint128 v)
    : lo_{Uint128Low64(v)}, hi_{static_cast<int64_t>(Uint128High64(v))} {}

#elif defined(ABSL_IS_BIG_ENDIAN)

constexpr int128::int128(int64_t high, uint64_t low) : hi_{high}, lo_{low} {}

constexpr int128::int128(int v)
    : hi_{v < 0 ? ~int64_t{0} : 0}, lo_{static_cast<uint64_t>(v)} {}
constexpr int128::int128(long v)  // NOLINT(runtime/int)
    : hi_{v < 0 ? ~int64_t{0} : 0}, lo_{static_cast<uint64_t>(v)} {}
constexpr int128::int128(long long v)  // NOLINT(runtime/int)
    : hi_{v < 0 ? ~int64_t{0} : 0}, lo_{static_cast<uint64_t>(v)} {}

constexpr int128::int128(unsigned int v) : hi_{0}, lo_{v} {}
// NOLINTNEXTLINE(runtime/int)
constexpr int128::int128(unsigned long v) : hi_{0}, lo_{v} {}
// NOLINTNEXTLINE(runtime/int)
constexpr int128::int128(unsigned long long v) : hi_{0}, lo_{v} {}

constexpr int128::int128(uint128 v)
    : hi_{static_cast<int64_t>(Uint128High64(v))}, lo_{Uint128Low64(v)} {}

#else  // byte order
#error "Unsupported byte order: must be little-endian or big-endian."
#endif  // byte order

constexpr int128::operator bool() const { return lo_ || hi_; }

constexpr int128::operator char() const {
  // NOLINTNEXTLINE(runtime/int)
  return static_cast<char>(static_cast<long long>(*this));
}

constexpr int128::operator signed char() const {
  // NOLINTNEXTLINE(runtime/int)
  return static_cast<signed char>(static_cast<long long>(*this));
}

constexpr int128::operator unsigned char() const {
  return static_cast<unsigned char>(lo_);
}

constexpr int128::operator char16_t() const {
  return static_cast<char16_t>(lo_);
}

constexpr int128::operator char32_t() const {
  return static_cast<char32_t>(lo_);
}

constexpr int128::operator ABSL_INTERNAL_WCHAR_T() const {
  // NOLINTNEXTLINE(runtime/int)
  return static_cast<ABSL_INTERNAL_WCHAR_T>(static_cast<long long>(*this));
}

constexpr int128::operator short() const {  // NOLINT(runtime/int)
  // NOLINTNEXTLINE(runtime/int)
  return static_cast<short>(static_cast<long long>(*this));
}

constexpr int128::operator unsigned short() const {  // NOLINT(runtime/int)
  return static_cast<unsigned short>(lo_);           // NOLINT(runtime/int)
}

constexpr int128::operator int() const {
  // NOLINTNEXTLINE(runtime/int)
  return static_cast<int>(static_cast<long long>(*this));
}

constexpr int128::operator unsigned int() const {
  return static_cast<unsigned int>(lo_);
}

constexpr int128::operator long() const {  // NOLINT(runtime/int)
  // NOLINTNEXTLINE(runtime/int)
  return static_cast<long>(static_cast<long long>(*this));
}

constexpr int128::operator unsigned long() const {  // NOLINT(runtime/int)
  return static_cast<unsigned long>(lo_);           // NOLINT(runtime/int)
}

constexpr int128::operator long long() const {  // NOLINT(runtime/int)
  // We don't bother checking the value of hi_. If *this < 0, lo_'s high bit
  // must be set in order for the value to fit into a long long. Conversely, if
  // lo_'s high bit is set, *this must be < 0 for the value to fit.
  return int128_internal::BitCastToSigned(lo_);
}

constexpr int128::operator unsigned long long() const {  // NOLINT(runtime/int)
  return static_cast<unsigned long long>(lo_);           // NOLINT(runtime/int)
}

inline int128::operator float() const {
  // We must convert the absolute value and then negate as needed, because
  // floating point types are typically sign-magnitude. Otherwise, the
  // difference between the high and low 64 bits when interpreted as two's
  // complement overwhelms the precision of the mantissa.
  //
  // Also check to make sure we don't negate Int128Min()
  return hi_ < 0 && *this != Int128Min()
             ? -static_cast<float>(-*this)
             : static_cast<float>(lo_) +
                   std::ldexp(static_cast<float>(hi_), 64);
}

inline int128::operator double() const {
  // See comment in int128::operator float() above.
  return hi_ < 0 && *this != Int128Min()
             ? -static_cast<double>(-*this)
             : static_cast<double>(lo_) +
                   std::ldexp(static_cast<double>(hi_), 64);
}

inline int128::operator long double() const {
  // See comment in int128::operator float() above.
  return hi_ < 0 && *this != Int128Min()
             ? -static_cast<long double>(-*this)
             : static_cast<long double>(lo_) +
                   std::ldexp(static_cast<long double>(hi_), 64);
}

// Comparison operators.

constexpr bool operator==(int128 lhs, int128 rhs) {
  return (Int128Low64(lhs) == Int128Low64(rhs) &&
          Int128High64(lhs) == Int128High64(rhs));
}

constexpr bool operator!=(int128 lhs, int128 rhs) { return !(lhs == rhs); }

constexpr bool operator<(int128 lhs, int128 rhs) {
  return (Int128High64(lhs) == Int128High64(rhs))
             ? (Int128Low64(lhs) < Int128Low64(rhs))
             : (Int128High64(lhs) < Int128High64(rhs));
}

constexpr bool operator>(int128 lhs, int128 rhs) {
  return (Int128High64(lhs) == Int128High64(rhs))
             ? (Int128Low64(lhs) > Int128Low64(rhs))
             : (Int128High64(lhs) > Int128High64(rhs));
}

constexpr bool operator<=(int128 lhs, int128 rhs) { return !(lhs > rhs); }

constexpr bool operator>=(int128 lhs, int128 rhs) { return !(lhs < rhs); }

// Unary operators.

constexpr int128 operator-(int128 v) {
  return MakeInt128(~Int128High64(v) + (Int128Low64(v) == 0),
                    ~Int128Low64(v) + 1);
}

constexpr bool operator!(int128 v) {
  return !Int128Low64(v) && !Int128High64(v);
}

constexpr int128 operator~(int128 val) {
  return MakeInt128(~Int128High64(val), ~Int128Low64(val));
}

// Arithmetic operators.

namespace int128_internal {
constexpr int128 SignedAddResult(int128 result, int128 lhs) {
  // check for carry
  return (Int128Low64(result) < Int128Low64(lhs))
             ? MakeInt128(Int128High64(result) + 1, Int128Low64(result))
             : result;
}
}  // namespace int128_internal
constexpr int128 operator+(int128 lhs, int128 rhs) {
  return int128_internal::SignedAddResult(
      MakeInt128(Int128High64(lhs) + Int128High64(rhs),
                 Int128Low64(lhs) + Int128Low64(rhs)),
      lhs);
}

namespace int128_internal {
constexpr int128 SignedSubstructResult(int128 result, int128 lhs, int128 rhs) {
  // check for carry
  return (Int128Low64(lhs) < Int128Low64(rhs))
             ? MakeInt128(Int128High64(result) - 1, Int128Low64(result))
             : result;
}
}  // namespace int128_internal
constexpr int128 operator-(int128 lhs, int128 rhs) {
  return int128_internal::SignedSubstructResult(
      MakeInt128(Int128High64(lhs) - Int128High64(rhs),
                 Int128Low64(lhs) - Int128Low64(rhs)),
      lhs, rhs);
}

inline int128 operator*(int128 lhs, int128 rhs) {
  return MakeInt128(
      int128_internal::BitCastToSigned(Uint128High64(uint128(lhs) * rhs)),
      Uint128Low64(uint128(lhs) * rhs));
}

inline int128 int128::operator++(int) {
  int128 tmp(*this);
  *this += 1;
  return tmp;
}

inline int128 int128::operator--(int) {
  int128 tmp(*this);
  *this -= 1;
  return tmp;
}

inline int128& int128::operator++() {
  *this += 1;
  return *this;
}

inline int128& int128::operator--() {
  *this -= 1;
  return *this;
}

constexpr int128 operator|(int128 lhs, int128 rhs) {
  return MakeInt128(Int128High64(lhs) | Int128High64(rhs),
                    Int128Low64(lhs) | Int128Low64(rhs));
}

constexpr int128 operator&(int128 lhs, int128 rhs) {
  return MakeInt128(Int128High64(lhs) & Int128High64(rhs),
                    Int128Low64(lhs) & Int128Low64(rhs));
}

constexpr int128 operator^(int128 lhs, int128 rhs) {
  return MakeInt128(Int128High64(lhs) ^ Int128High64(rhs),
                    Int128Low64(lhs) ^ Int128Low64(rhs));
}

constexpr int128 operator<<(int128 lhs, int amount) {
  // int64_t shifts of >= 63 are undefined, so we need some special-casing.
  assert(amount >= 0 && amount < 127);
  if (amount <= 0) {
    return lhs;
  } else if (amount < 63) {
    return MakeInt128(
        (Int128High64(lhs) << amount) |
            static_cast<int64_t>(Int128Low64(lhs) >> (64 - amount)),
        Int128Low64(lhs) << amount);
  } else if (amount == 63) {
    return MakeInt128(((Int128High64(lhs) << 32) << 31) |
                          static_cast<int64_t>(Int128Low64(lhs) >> 1),
                      (Int128Low64(lhs) << 32) << 31);
  } else if (amount == 127) {
    return MakeInt128(static_cast<int64_t>(Int128Low64(lhs) << 63), 0);
  } else if (amount > 127) {
    return MakeInt128(0, 0);
  } else {
    // amount >= 64 && amount < 127
    return MakeInt128(static_cast<int64_t>(Int128Low64(lhs) << (amount - 64)),
                      0);
  }
}

constexpr int128 operator>>(int128 lhs, int amount) {
  // int64_t shifts of >= 63 are undefined, so we need some special-casing.
  assert(amount >= 0 && amount < 127);
  if (amount <= 0) {
    return lhs;
  } else if (amount < 63) {
    return MakeInt128(
        Int128High64(lhs) >> amount,
        Int128Low64(lhs) >> amount | static_cast<uint64_t>(Int128High64(lhs))
                                         << (64 - amount));
  } else if (amount == 63) {
    return MakeInt128((Int128High64(lhs) >> 32) >> 31,
                      static_cast<uint64_t>(Int128High64(lhs) << 1) |
                          (Int128Low64(lhs) >> 32) >> 31);

  } else if (amount >= 127) {
    return MakeInt128((Int128High64(lhs) >> 32) >> 31,
                      static_cast<uint64_t>((Int128High64(lhs) >> 32) >> 31));
  } else {
    // amount >= 64 && amount < 127
    return MakeInt128(
        (Int128High64(lhs) >> 32) >> 31,
        static_cast<uint64_t>(Int128High64(lhs) >> (amount - 64)));
  }
}
