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

// This file tests string processing functions related to numeric values.

#include "absl/strings/numbers.h"

#include <sys/types.h>

#include <cfenv>  // NOLINT(build/c++11)
#include <cfloat>
#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/internal/numbers_test_common.h"
#include "absl/strings/internal/ostringstream.h"
#include "absl/strings/internal/pow10_helper.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace {

using absl::SimpleAtoi;
using absl::SimpleHexAtoi;
using absl::numbers_internal::kSixDigitsToBufferSize;
using absl::numbers_internal::safe_strto32_base;
using absl::numbers_internal::safe_strto64_base;
using absl::numbers_internal::safe_strtou32_base;
using absl::numbers_internal::safe_strtou64_base;
using absl::numbers_internal::SixDigitsToBuffer;
using absl::strings_internal::Itoa;
using absl::strings_internal::strtouint32_test_cases;
using absl::strings_internal::strtouint64_test_cases;
using testing::Eq;
using testing::MatchesRegex;
using testing::Pointee;

// Number of floats to test with.
// 5,000,000 is a reasonable default for a test that only takes a few seconds.
// 1,000,000,000+ triggers checking for all possible mantissa values for
// double-precision tests. 2,000,000,000+ triggers checking for every possible
// single-precision float.
const int kFloatNumCases = 5000000;

// This is a slow, brute-force routine to compute the exact base-10
// representation of a double-precision floating-point number.  It
// is useful for debugging only.
std::string PerfectDtoa(double d) {
  if (d == 0) return "0";
  if (d < 0) return "-" + PerfectDtoa(-d);

  // Basic theory: decompose d into mantissa and exp, where
  // d = mantissa * 2^exp, and exp is as close to zero as possible.
  int64_t mantissa, exp = 0;
  while (d >= 1ULL << 63) ++exp, d *= 0.5;
  while ((mantissa = d) != d) --exp, d *= 2.0;

  // Then convert mantissa to ASCII, and either double it (if
  // exp > 0) or halve it (if exp < 0) repeatedly.  "halve it"
  // in this case means multiplying it by five and dividing by 10.
  constexpr int maxlen = 1100;  // worst case is actually 1030 or so.
  char buf[maxlen + 5];
  for (int64_t num = mantissa, pos = maxlen; --pos >= 0;) {
    buf[pos] = '0' + (num % 10);
    num /= 10;
  }
  char* begin = &buf[0];
  char* end = buf + maxlen;
  for (int i = 0; i != exp; i += (exp > 0) ? 1 : -1) {
    int carry = 0;
    for (char* p = end; --p != begin;) {
      int dig = *p - '0';
      dig = dig * (exp > 0 ? 2 : 5) + carry;
      carry = dig / 10;
      dig %= 10;
      *p = '0' + dig;
    }
  }
  if (exp < 0) {
    // "dividing by 10" above means we have to add the decimal point.
    memmove(end + 1 + exp, end + exp, 1 - exp);
    end[exp] = '.';
    ++end;
  }
  while (*begin == '0' && begin[1] != '.') ++begin;
  return {begin, end};
}

TEST(ToString, PerfectDtoa) {
  EXPECT_THAT(PerfectDtoa(1), Eq("1"));
  EXPECT_THAT(PerfectDtoa(0.1),
              Eq("0.1000000000000000055511151231257827021181583404541015625"));
  EXPECT_THAT(PerfectDtoa(1e24), Eq("999999999999999983222784"));
  EXPECT_THAT(PerfectDtoa(5e-324), MatchesRegex("0.0000.*625"));
  for (int i = 0; i < 100; ++i) {
    for (double multiplier :
         {1e-300, 1e-200, 1e-100, 0.1, 1.0, 10.0, 1e100, 1e300}) {
      double d = multiplier * i;
      std::string s = PerfectDtoa(d);
      EXPECT_DOUBLE_EQ(d, strtod(s.c_str(), nullptr));
    }
  }
}

template <typename integer>
struct MyInteger {
  integer i;
  explicit constexpr MyInteger(integer i) : i(i) {}
  constexpr operator integer() const { return i; }

  constexpr MyInteger operator+(MyInteger other) const { return i + other.i; }
  constexpr MyInteger operator-(MyInteger other) const { return i - other.i; }
  constexpr MyInteger operator*(MyInteger other) const { return i * other.i; }
  constexpr MyInteger operator/(MyInteger other) const { return i / other.i; }

  constexpr bool operator<(MyInteger other) const { return i < other.i; }
  constexpr bool operator<=(MyInteger other) const { return i <= other.i; }
  constexpr bool operator==(MyInteger other) const { return i == other.i; }
  constexpr bool operator>=(MyInteger other) const { return i >= other.i; }
  constexpr bool operator>(MyInteger other) const { return i > other.i; }
  constexpr bool operator!=(MyInteger other) const { return i != other.i; }

  integer as_integer() const { return i; }
};

typedef MyInteger<int64_t> MyInt64;
typedef MyInteger<uint64_t> MyUInt64;

void CheckInt32(int32_t x) {
  char buffer[absl::numbers_internal::kFastToBufferSize];
  char* actual = absl::numbers_internal::FastIntToBuffer(x, buffer);
  std::string expected = std::to_string(x);
  EXPECT_EQ(expected, std::string(buffer, actual)) << " Input " << x;

  char* generic_actual = absl::numbers_internal::FastIntToBuffer(x, buffer);
  EXPECT_EQ(expected, std::string(buffer, generic_actual)) << " Input " << x;
}

void CheckInt64(int64_t x) {
  char buffer[absl::numbers_internal::kFastToBufferSize + 3];
  buffer[0] = '*';
  buffer[23] = '*';
  buffer[24] = '*';
  char* actual = absl::numbers_internal::FastIntToBuffer(x, &buffer[1]);
  std::string expected = std::to_string(x);
  EXPECT_EQ(expected, std::string(&buffer[1], actual)) << " Input " << x;
  EXPECT_EQ(buffer[0], '*');
  EXPECT_EQ(buffer[23], '*');
  EXPECT_EQ(buffer[24], '*');

  char* my_actual =
      absl::numbers_internal::FastIntToBuffer(MyInt64(x), &buffer[1]);
  EXPECT_EQ(expected, std::string(&buffer[1], my_actual)) << " Input " << x;
}

void CheckUInt32(uint32_t x) {
  char buffer[absl::numbers_internal::kFastToBufferSize];
  char* actual = absl::numbers_internal::FastIntToBuffer(x, buffer);
  std::string expected = std::to_string(x);
  EXPECT_EQ(expected, std::string(buffer, actual)) << " Input " << x;

  char* generic_actual = absl::numbers_internal::FastIntToBuffer(x, buffer);
  EXPECT_EQ(expected, std::string(buffer, generic_actual)) << " Input " << x;
}

void CheckUInt64(uint64_t x) {
  char buffer[absl::numbers_internal::kFastToBufferSize + 1];
  char* actual = absl::numbers_internal::FastIntToBuffer(x, &buffer[1]);
  std::string expected = std::to_string(x);
  EXPECT_EQ(expected, std::string(&buffer[1], actual)) << " Input " << x;

  char* generic_actual = absl::numbers_internal::FastIntToBuffer(x, &buffer[1]);
  EXPECT_EQ(expected, std::string(&buffer[1], generic_actual))
      << " Input " << x;

  char* my_actual =
      absl::numbers_internal::FastIntToBuffer(MyUInt64(x), &buffer[1]);
  EXPECT_EQ(expected, std::string(&buffer[1], my_actual)) << " Input " << x;
}

void CheckHex64(uint64_t v) {
  char expected[16 + 1];
  std::string actual = absl::StrCat(absl::Hex(v, absl::kZeroPad16));
  snprintf(expected, sizeof(expected), "%016" PRIx64, static_cast<uint64_t>(v));
  EXPECT_EQ(expected, actual) << " Input " << v;
  actual = absl::StrCat(absl::Hex(v, absl::kSpacePad16));
  snprintf(expected, sizeof(expected), "%16" PRIx64, static_cast<uint64_t>(v));
  EXPECT_EQ(expected, actual) << " Input " << v;
}

TEST(Numbers, TestFastPrints) {
  for (int i = -100; i <= 100; i++) {
    CheckInt32(i);
    CheckInt64(i);
  }
  for (int i = 0; i <= 100; i++) {
    CheckUInt32(i);
    CheckUInt64(i);
  }
  // Test min int to make sure that works
  CheckInt32(INT_MIN);
  CheckInt32(INT_MAX);
  CheckInt64(LONG_MIN);
  CheckInt64(uint64_t{10000000});
  CheckInt64(uint64_t{100000000});
  CheckInt64(uint64_t{1000000000});
  CheckInt64(uint64_t{9999999999});
  CheckInt64(uint64_t{100000000000000});
  CheckInt64(uint64_t{999999999999999});
  CheckInt64(uint64_t{1000000000000000});
  CheckInt64(uint64_t{10000000000000000});
  CheckInt64(uint64_t{100000000000000000});
  CheckInt64(uint64_t{1000000000000000000});
  CheckInt64(uint64_t{1199999999999999999});
  CheckInt64(int64_t{-700000000000000000});
  CheckInt64(LONG_MAX);
  CheckUInt32(std::numeric_limits<uint32_t>::max());
  CheckUInt64(uint64_t{1000000000});
  CheckUInt64(uint64_t{9999999999});
  CheckUInt64(uint64_t{100000000000000});
  CheckUInt64(uint64_t{999999999999999});
  CheckUInt64(uint64_t{1000000000000000000});
  CheckUInt64(uint64_t{1199999999999999999});
  CheckUInt64(uint64_t{10000000000000000000u});
  CheckUInt64(uint64_t{10200300040000500006u});
  CheckUInt64(std::numeric_limits<uint64_t>::max());

  for (int i = 0; i < 10000; i++) {
    CheckHex64(i);
  }
  CheckHex64(uint64_t{0x123456789abcdef0});
}

template <typename int_type, typename in_val_type>
void VerifySimpleAtoiGood(in_val_type in_value, int_type exp_value) {
  std::string s;
  // (u)int128 can be streamed but not StrCat'd.
  absl::strings_internal::OStringStream(&s) << in_value;
  int_type x = static_cast<int_type>(~exp_value);
  EXPECT_TRUE(SimpleAtoi(s, &x))
      << "in_value=" << in_value << " s=" << s << " x=" << x;
  EXPECT_EQ(exp_value, x);
  x = static_cast<int_type>(~exp_value);
  EXPECT_TRUE(SimpleAtoi(s.c_str(), &x));
  EXPECT_EQ(exp_value, x);
}

template <typename int_type, typename in_val_type>
void VerifySimpleAtoiBad(in_val_type in_value) {
  std::string s;
  // (u)int128 can be streamed but not StrCat'd.
  absl::strings_internal::OStringStream(&s) << in_value;
  int_type x;
  EXPECT_FALSE(SimpleAtoi(s, &x));
  EXPECT_FALSE(SimpleAtoi(s.c_str(), &x));
}

TEST(NumbersTest, Atoi) {
  // SimpleAtoi(absl::string_view, int32_t)
  VerifySimpleAtoiGood<int32_t>(0, 0);
  VerifySimpleAtoiGood<int32_t>(42, 42);
  VerifySimpleAtoiGood<int32_t>(-42, -42);

  VerifySimpleAtoiGood<int32_t>(std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::min());
  VerifySimpleAtoiGood<int32_t>(std::numeric_limits<int32_t>::max(),
                                std::numeric_limits<int32_t>::max());

  // SimpleAtoi(absl::string_view, uint32_t)
  VerifySimpleAtoiGood<uint32_t>(0, 0);
  VerifySimpleAtoiGood<uint32_t>(42, 42);
  VerifySimpleAtoiBad<uint32_t>(-42);

  VerifySimpleAtoiBad<uint32_t>(std::numeric_limits<int32_t>::min());
  VerifySimpleAtoiGood<uint32_t>(std::numeric_limits<int32_t>::max(),
                                 std::numeric_limits<int32_t>::max());
  VerifySimpleAtoiGood<uint32_t>(std::numeric_limits<uint32_t>::max(),
                                 std::numeric_limits<uint32_t>::max());
  VerifySimpleAtoiBad<uint32_t>(std::numeric_limits<int64_t>::min());
  VerifySimpleAtoiBad<uint32_t>(std::numeric_limits<int64_t>::max());
  VerifySimpleAtoiBad<uint32_t>(std::numeric_limits<uint64_t>::max());

  // SimpleAtoi(absl::string_view, int64_t)
  VerifySimpleAtoiGood<int64_t>(0, 0);
  VerifySimpleAtoiGood<int64_t>(42, 42);
  VerifySimpleAtoiGood<int64_t>(-42, -42);

  VerifySimpleAtoiGood<int64_t>(std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::min());
  VerifySimpleAtoiGood<int64_t>(std::numeric_limits<int32_t>::max(),
                                std::numeric_limits<int32_t>::max());
  VerifySimpleAtoiGood<int64_t>(std::numeric_limits<uint32_t>::max(),
                                std::numeric_limits<uint32_t>::max());
  VerifySimpleAtoiGood<int64_t>(std::numeric_limits<int64_t>::min(),
                                std::numeric_limits<int64_t>::min());
  VerifySimpleAtoiGood<int64_t>(std::numeric_limits<int64_t>::max(),
                                std::numeric_limits<int64_t>::max());
  VerifySimpleAtoiBad<int64_t>(std::numeric_limits<uint64_t>::max());

  // SimpleAtoi(absl::string_view, uint64_t)
  VerifySimpleAtoiGood<uint64_t>(0, 0);
  VerifySimpleAtoiGood<uint64_t>(42, 42);
  VerifySimpleAtoiBad<uint64_t>(-42);

  VerifySimpleAtoiBad<uint64_t>(std::numeric_limits<int32_t>::min());
  VerifySimpleAtoiGood<uint64_t>(std::numeric_limits<int32_t>::max(),
                                 std::numeric_limits<int32_t>::max());
  VerifySimpleAtoiGood<uint64_t>(std::numeric_limits<uint32_t>::max(),
                                 std::numeric_limits<uint32_t>::max());
  VerifySimpleAtoiBad<uint64_t>(std::numeric_limits<int64_t>::min());
  VerifySimpleAtoiGood<uint64_t>(std::numeric_limits<int64_t>::max(),
                                 std::numeric_limits<int64_t>::max());
  VerifySimpleAtoiGood<uint64_t>(std::numeric_limits<uint64_t>::max(),
                                 std::numeric_limits<uint64_t>::max());

  // SimpleAtoi(absl::string_view, absl::uint128)
  VerifySimpleAtoiGood<absl::uint128>(0, 0);
  VerifySimpleAtoiGood<absl::uint128>(42, 42);
  VerifySimpleAtoiBad<absl::uint128>(-42);

  VerifySimpleAtoiBad<absl::uint128>(std::numeric_limits<int32_t>::min());
  VerifySimpleAtoiGood<absl::uint128>(std::numeric_limits<int32_t>::max(),
                                      std::numeric_limits<int32_t>::max());
  VerifySimpleAtoiGood<absl::uint128>(std::numeric_limits<uint32_t>::max(),
                                      std::numeric_limits<uint32_t>::max());
  VerifySimpleAtoiBad<absl::uint128>(std::numeric_limits<int64_t>::min());
  VerifySimpleAtoiGood<absl::uint128>(std::numeric_limits<int64_t>::max(),
                                      std::numeric_limits<int64_t>::max());
  VerifySimpleAtoiGood<absl::uint128>(std::numeric_limits<uint64_t>::max(),
                                      std::numeric_limits<uint64_t>::max());
  VerifySimpleAtoiGood<absl::uint128>(
      std::numeric_limits<absl::uint128>::max(),
      std::numeric_limits<absl::uint128>::max());

  // SimpleAtoi(absl::string_view, absl::int128)
  VerifySimpleAtoiGood<absl::int128>(0, 0);
  VerifySimpleAtoiGood<absl::int128>(42, 42);
  VerifySimpleAtoiGood<absl::int128>(-42, -42);

  VerifySimpleAtoiGood<absl::int128>(std::numeric_limits<int32_t>::min(),
                                      std::numeric_limits<int32_t>::min());
  VerifySimpleAtoiGood<absl::int128>(std::numeric_limits<int32_t>::max(),
                                      std::numeric_limits<int32_t>::max());
  VerifySimpleAtoiGood<absl::int128>(std::numeric_limits<uint32_t>::max(),
                                      std::numeric_limits<uint32_t>::max());
  VerifySimpleAtoiGood<absl::int128>(std::numeric_limits<int64_t>::min(),
                                      std::numeric_limits<int64_t>::min());
  VerifySimpleAtoiGood<absl::int128>(std::numeric_limits<int64_t>::max(),
                                      std::numeric_limits<int64_t>::max());
  VerifySimpleAtoiGood<absl::int128>(std::numeric_limits<uint64_t>::max(),
                                      std::numeric_limits<uint64_t>::max());
  VerifySimpleAtoiGood<absl::int128>(
      std::numeric_limits<absl::int128>::min(),
      std::numeric_limits<absl::int128>::min());
  VerifySimpleAtoiGood<absl::int128>(
      std::numeric_limits<absl::int128>::max(),
      std::numeric_limits<absl::int128>::max());
  VerifySimpleAtoiBad<absl::int128>(std::numeric_limits<absl::uint128>::max());

  // Some other types
  VerifySimpleAtoiGood<int>(-42, -42);
  VerifySimpleAtoiGood<int32_t>(-42, -42);
  VerifySimpleAtoiGood<uint32_t>(42, 42);
  VerifySimpleAtoiGood<unsigned int>(42, 42);
  VerifySimpleAtoiGood<int64_t>(-42, -42);
  VerifySimpleAtoiGood<long>(-42, -42);  // NOLINT: runtime-int
  VerifySimpleAtoiGood<uint64_t>(42, 42);
  VerifySimpleAtoiGood<size_t>(42, 42);
  VerifySimpleAtoiGood<std::string::size_type>(42, 42);
}

TEST(NumbersTest, Atod) {
  // DBL_TRUE_MIN and FLT_TRUE_MIN were not mandated in <cfloat> before C++17.
#if !defined(DBL_TRUE_MIN)
  static constexpr double DBL_TRUE_MIN =
      4.940656458412465441765687928682213723650598026143247644255856825e-324;
#endif
#if !defined(FLT_TRUE_MIN)
  static constexpr float FLT_TRUE_MIN =
      1.401298464324817070923729583289916131280261941876515771757068284e-45f;
#endif

  double d;
  float f;

  // NaN can be spelled in multiple ways.
  EXPECT_TRUE(absl::SimpleAtod("NaN", &d));
  EXPECT_TRUE(std::isnan(d));
  EXPECT_TRUE(absl::SimpleAtod("nAN", &d));
  EXPECT_TRUE(std::isnan(d));
  EXPECT_TRUE(absl::SimpleAtod("-nan", &d));
  EXPECT_TRUE(std::isnan(d));

  // Likewise for Infinity.
  EXPECT_TRUE(absl::SimpleAtod("inf", &d));
  EXPECT_TRUE(std::isinf(d) && (d > 0));
  EXPECT_TRUE(absl::SimpleAtod("+Infinity", &d));
  EXPECT_TRUE(std::isinf(d) && (d > 0));
  EXPECT_TRUE(absl::SimpleAtod("-INF", &d));
  EXPECT_TRUE(std::isinf(d) && (d < 0));

  // Parse DBL_MAX. Parsing something more than twice as big should also
  // produce infinity.
  EXPECT_TRUE(absl::SimpleAtod("1.7976931348623157e+308", &d));
  EXPECT_EQ(d, 1.7976931348623157e+308);
  EXPECT_TRUE(absl::SimpleAtod("5e308", &d));
  EXPECT_TRUE(std::isinf(d) && (d > 0));
  // Ditto, but for FLT_MAX.
  EXPECT_TRUE(absl::SimpleAtof("3.4028234663852886e+38", &f));
  EXPECT_EQ(f, 3.4028234663852886e+38f);
  EXPECT_TRUE(absl::SimpleAtof("7e38", &f));
  EXPECT_TRUE(std::isinf(f) && (f > 0));

  // Parse the largest N such that parsing 1eN produces a finite value and the
  // smallest M = N + 1 such that parsing 1eM produces infinity.
  //
  // The 309 exponent (and 39) confirms the "definition of
  // kEiselLemireMaxExclExp10" comment in charconv.cc.
  EXPECT_TRUE(absl::SimpleAtod("1e308", &d));
  EXPECT_EQ(d, 1e308);
  EXPECT_FALSE(std::isinf(d));
  EXPECT_TRUE(absl::SimpleAtod("1e309", &d));
  EXPECT_TRUE(std::isinf(d));
  // Ditto, but for Atof instead of Atod.
  EXPECT_TRUE(absl::SimpleAtof("1e38", &f));
  EXPECT_EQ(f, 1e38f);
  EXPECT_FALSE(std::isinf(f));
  EXPECT_TRUE(absl::SimpleAtof("1e39", &f));
  EXPECT_TRUE(std::isinf(f));

  // Parse the largest N such that parsing 9.999999999999999999eN, with 19
  // nines, produces a finite value.
  //
  // 9999999999999999999, with 19 nines but no decimal point, is the largest
  // "repeated nines" integer that fits in a uint64_t.
  EXPECT_TRUE(absl::SimpleAtod("9.999999999999999999e307", &d));
  EXPECT_EQ(d, 9.999999999999999999e307);
  EXPECT_FALSE(std::isinf(d));
  EXPECT_TRUE(absl::SimpleAtod("9.999999999999999999e308", &d));
  EXPECT_TRUE(std::isinf(d));
  // Ditto, but for Atof instead of Atod.
  EXPECT_TRUE(absl::SimpleAtof("9.999999999999999999e37", &f));
  EXPECT_EQ(f, 9.999999999999999999e37f);
  EXPECT_FALSE(std::isinf(f));
  EXPECT_TRUE(absl::SimpleAtof("9.999999999999999999e38", &f));
  EXPECT_TRUE(std::isinf(f));

  // Parse DBL_MIN (normal), DBL_TRUE_MIN (subnormal) and (DBL_TRUE_MIN / 10)
  // (effectively zero).
  EXPECT_TRUE(absl::SimpleAtod("2.2250738585072014e-308", &d));
  EXPECT_EQ(d, 2.2250738585072014e-308);
  EXPECT_TRUE(absl::SimpleAtod("4.9406564584124654e-324", &d));
  EXPECT_EQ(d, 4.9406564584124654e-324);
  EXPECT_TRUE(absl::SimpleAtod("4.9406564584124654e-325", &d));
  EXPECT_EQ(d, 0);
  // Ditto, but for FLT_MIN, FLT_TRUE_MIN and (FLT_TRUE_MIN / 10).
  EXPECT_TRUE(absl::SimpleAtof("1.1754943508222875e-38", &f));
  EXPECT_EQ(f, 1.1754943508222875e-38f);
  EXPECT_TRUE(absl::SimpleAtof("1.4012984643248171e-45", &f));
  EXPECT_EQ(f, 1.4012984643248171e-45f);
  EXPECT_TRUE(absl::SimpleAtof("1.4012984643248171e-46", &f));
  EXPECT_EQ(f, 0);

  // Parse the largest N (the most negative -N) such that parsing 1e-N produces
  // a normal or subnormal (but still positive) or zero value.
  EXPECT_TRUE(absl::SimpleAtod("1e-307", &d));
  EXPECT_EQ(d, 1e-307);
  EXPECT_GE(d, DBL_MIN);
  EXPECT_LT(d, DBL_MIN * 10);
  EXPECT_TRUE(absl::SimpleAtod("1e-323", &d));
  EXPECT_EQ(d, 1e-323);
  EXPECT_GE(d, DBL_TRUE_MIN);
  EXPECT_LT(d, DBL_TRUE_MIN * 10);
  EXPECT_TRUE(absl::SimpleAtod("1e-324", &d));
  EXPECT_EQ(d, 0);
  // Ditto, but for Atof instead of Atod.
  EXPECT_TRUE(absl::SimpleAtof("1e-37", &f));
  EXPECT_EQ(f, 1e-37f);
  EXPECT_GE(f, FLT_MIN);
  EXPECT_LT(f, FLT_MIN * 10);
  EXPECT_TRUE(absl::SimpleAtof("1e-45", &f));
  EXPECT_EQ(f, 1e-45f);
  EXPECT_GE(f, FLT_TRUE_MIN);
  EXPECT_LT(f, FLT_TRUE_MIN * 10);
  EXPECT_TRUE(absl::SimpleAtof("1e-46", &f));
  EXPECT_EQ(f, 0);

  // Parse the largest N (the most negative -N) such that parsing
  // 9.999999999999999999e-N, with 19 nines, produces a normal or subnormal
  // (but still positive) or zero value.
  //
  // 9999999999999999999, with 19 nines but no decimal point, is the largest
  // "repeated nines" integer that fits in a uint64_t.
  //
  // The -324/-325 exponents (and -46/-47) confirms the "definition of
  // kEiselLemireMinInclExp10" comment in charconv.cc.
  EXPECT_TRUE(absl::SimpleAtod("9.999999999999999999e-308", &d));
  EXPECT_EQ(d, 9.999999999999999999e-308);
  EXPECT_GE(d, DBL_MIN);
  EXPECT_LT(d, DBL_MIN * 10);
  EXPECT_TRUE(absl::SimpleAtod("9.999999999999999999e-324", &d));
  EXPECT_EQ(d, 9.999999999999999999e-324);
  EXPECT_GE(d, DBL_TRUE_MIN);
  EXPECT_LT(d, DBL_TRUE_MIN * 10);
  EXPECT_TRUE(absl::SimpleAtod("9.999999999999999999e-325", &d));
  EXPECT_EQ(d, 0);
  // Ditto, but for Atof instead of Atod.
  EXPECT_TRUE(absl::SimpleAtof("9.999999999999999999e-38", &f));
  EXPECT_EQ(f, 9.999999999999999999e-38f);
  EXPECT_GE(f, FLT_MIN);
  EXPECT_LT(f, FLT_MIN * 10);
  EXPECT_TRUE(absl::SimpleAtof("9.999999999999999999e-46", &f));
  EXPECT_EQ(f, 9.999999999999999999e-46f);
  EXPECT_GE(f, FLT_TRUE_MIN);
  EXPECT_LT(f, FLT_TRUE_MIN * 10);
  EXPECT_TRUE(absl::SimpleAtof("9.999999999999999999e-47", &f));
  EXPECT_EQ(f, 0);

  // Leading and/or trailing whitespace is OK.
  EXPECT_TRUE(absl::SimpleAtod("  \t\r\n  2.718", &d));
  EXPECT_EQ(d, 2.718);
  EXPECT_TRUE(absl::SimpleAtod("  3.141  ", &d));
  EXPECT_EQ(d, 3.141);

  // Leading or trailing not-whitespace is not OK.
  EXPECT_FALSE(absl::SimpleAtod("n 0", &d));
  EXPECT_FALSE(absl::SimpleAtod("0n ", &d));

  // Multiple leading 0s are OK.
  EXPECT_TRUE(absl::SimpleAtod("000123", &d));
  EXPECT_EQ(d, 123);
  EXPECT_TRUE(absl::SimpleAtod("000.456", &d));
  EXPECT_EQ(d, 0.456);

  // An absent leading 0 (for a fraction < 1) is OK.
  EXPECT_TRUE(absl::SimpleAtod(".5", &d));
  EXPECT_EQ(d, 0.5);
  EXPECT_TRUE(absl::SimpleAtod("-.707", &d));
  EXPECT_EQ(d, -0.707);

  // Unary + is OK.
  EXPECT_TRUE(absl::SimpleAtod("+6.0221408e+23", &d));
  EXPECT_EQ(d, 6.0221408e+23);

  // Underscores are not OK.
  EXPECT_FALSE(absl::SimpleAtod("123_456", &d));

  // The decimal separator must be '.' and is never ','.
  EXPECT_TRUE(absl::SimpleAtod("8.9", &d));
  EXPECT_FALSE(absl::SimpleAtod("8,9", &d));

  // These examples are called out in the EiselLemire function's comments.
  EXPECT_TRUE(absl::SimpleAtod("4503599627370497.5", &d));
  EXPECT_EQ(d, 4503599627370497.5);
  EXPECT_TRUE(absl::SimpleAtod("1e+23", &d));
  EXPECT_EQ(d, 1e+23);
  EXPECT_TRUE(absl::SimpleAtod("9223372036854775807", &d));
  EXPECT_EQ(d, 9223372036854775807);
  // Ditto, but for Atof instead of Atod.
  EXPECT_TRUE(absl::SimpleAtof("0.0625", &f));
  EXPECT_EQ(f, 0.0625f);
  EXPECT_TRUE(absl::SimpleAtof("20040229.0", &f));
  EXPECT_EQ(f, 20040229.0f);
  EXPECT_TRUE(absl::SimpleAtof("2147483647.0", &f));
  EXPECT_EQ(f, 2147483647.0f);

  // Some parsing algorithms don't always round correctly (but absl::SimpleAtod
  // should). This test case comes from
  // https://github.com/serde-rs/json/issues/707
  //
  // See also atod_manual_test.cc for running many more test cases.
  EXPECT_TRUE(absl::SimpleAtod("122.416294033786585", &d));
  EXPECT_EQ(d, 122.416294033786585);
  EXPECT_TRUE(absl::SimpleAtof("122.416294033786585", &f));
  EXPECT_EQ(f, 122.416294033786585f);
}

TEST(NumbersTest, Prefixes) {
  double d;
  EXPECT_FALSE(absl::SimpleAtod("++1", &d));
  EXPECT_FALSE(absl::SimpleAtod("+-1", &d));
  EXPECT_FALSE(absl::SimpleAtod("-+1", &d));
  EXPECT_FALSE(absl::SimpleAtod("--1", &d));
  EXPECT_TRUE(absl::SimpleAtod("-1", &d));
  EXPECT_EQ(d, -1.);
  EXPECT_TRUE(absl::SimpleAtod("+1", &d));
  EXPECT_EQ(d, +1.);

  float f;
  EXPECT_FALSE(absl::SimpleAtof("++1", &f));
  EXPECT_FALSE(absl::SimpleAtof("+-1", &f));
  EXPECT_FALSE(absl::SimpleAtof("-+1", &f));
  EXPECT_FALSE(absl::SimpleAtof("--1", &f));
  EXPECT_TRUE(absl::SimpleAtof("-1", &f));
  EXPECT_EQ(f, -1.f);
  EXPECT_TRUE(absl::SimpleAtof("+1", &f));
  EXPECT_EQ(f, +1.f);
}

TEST(NumbersTest, Atoenum) {
  enum E01 {
    E01_zero = 0,
    E01_one = 1,
  };

  VerifySimpleAtoiGood<E01>(E01_zero, E01_zero);
  VerifySimpleAtoiGood<E01>(E01_one, E01_one);

  enum E_101 {
    E_101_minusone = -1,
    E_101_zero = 0,
    E_101_one = 1,
  };

  VerifySimpleAtoiGood<E_101>(E_101_minusone, E_101_minusone);
  VerifySimpleAtoiGood<E_101>(E_101_zero, E_101_zero);
  VerifySimpleAtoiGood<E_101>(E_101_one, E_101_one);

  enum E_bigint {
    E_bigint_zero = 0,
    E_bigint_one = 1,
    E_bigint_max31 = static_cast<int32_t>(0x7FFFFFFF),
  };

  VerifySimpleAtoiGood<E_bigint>(E_bigint_zero, E_bigint_zero);
  VerifySimpleAtoiGood<E_bigint>(E_bigint_one, E_bigint_one);
  VerifySimpleAtoiGood<E_bigint>(E_bigint_max31, E_bigint_max31);

  enum E_fullint {
    E_fullint_zero = 0,
    E_fullint_one = 1,
    E_fullint_max31 = static_cast<int32_t>(0x7FFFFFFF),
    E_fullint_min32 = INT32_MIN,
  };

  VerifySimpleAtoiGood<E_fullint>(E_fullint_zero, E_fullint_zero);
  VerifySimpleAtoiGood<E_fullint>(E_fullint_one, E_fullint_one);
  VerifySimpleAtoiGood<E_fullint>(E_fullint_max31, E_fullint_max31);
  VerifySimpleAtoiGood<E_fullint>(E_fullint_min32, E_fullint_min32);

  enum E_biguint {
    E_biguint_zero = 0,
    E_biguint_one = 1,
    E_biguint_max31 = static_cast<uint32_t>(0x7FFFFFFF),
    E_biguint_max32 = static_cast<uint32_t>(0xFFFFFFFF),
  };

  VerifySimpleAtoiGood<E_biguint>(E_biguint_zero, E_biguint_zero);
  VerifySimpleAtoiGood<E_biguint>(E_biguint_one, E_biguint_one);
  VerifySimpleAtoiGood<E_biguint>(E_biguint_max31, E_biguint_max31);
  VerifySimpleAtoiGood<E_biguint>(E_biguint_max32, E_biguint_max32);
}

template <typename int_type, typename in_val_type>
void VerifySimpleHexAtoiGood(in_val_type in_value, int_type exp_value) {
  std::string s;
  // uint128 can be streamed but not StrCat'd
  absl::strings_internal::OStringStream strm(&s);
  if (in_value >= 0) {
    strm << std::hex << in_value;
  } else {
    // Inefficient for small integers, but works with all integral types.
    strm << "-" << std::hex << -absl::uint128(in_value);
  }
  int_type x = static_cast<int_type>(~exp_value);
  EXPECT_TRUE(SimpleHexAtoi(s, &x))
      << "in_value=" << std::hex << in_value << " s=" << s << " x=" << x;
  EXPECT_EQ(exp_value, x);
  x = static_cast<int_type>(~exp_value);
  EXPECT_TRUE(SimpleHexAtoi(
      s.c_str(), &x));  // NOLINT: readability-redundant-string-conversions
  EXPECT_EQ(exp_value, x);
}

template <typename int_type, typename in_val_type>
void VerifySimpleHexAtoiBad(in_val_type in_value) {
  std::string s;
  // uint128 can be streamed but not StrCat'd
  absl::strings_internal::OStringStream strm(&s);
  if (in_value >= 0) {
    strm << std::hex << in_value;
  } else {
    // Inefficient for small integers, but works with all integral types.
    strm << "-" << std::hex << -absl::uint128(in_value);
  }
  int_type x;
  EXPECT_FALSE(SimpleHexAtoi(s, &x));
  EXPECT_FALSE(SimpleHexAtoi(
      s.c_str(), &x));  // NOLINT: readability-redundant-string-conversions
}

TEST(NumbersTest, HexAtoi) {
  // SimpleHexAtoi(absl::string_view, int32_t)
  VerifySimpleHexAtoiGood<int32_t>(0, 0);
  VerifySimpleHexAtoiGood<int32_t>(0x42, 0x42);
  VerifySimpleHexAtoiGood<int32_t>(-0x42, -0x42);

  VerifySimpleHexAtoiGood<int32_t>(std::numeric_limits<int32_t>::min(),
                                   std::numeric_limits<int32_t>::min());
  VerifySimpleHexAtoiGood<int32_t>(std::numeric_limits<int32_t>::max(),
                                   std::numeric_limits<int32_t>::max());

  // SimpleHexAtoi(absl::string_view, uint32_t)
  VerifySimpleHexAtoiGood<uint32_t>(0, 0);
  VerifySimpleHexAtoiGood<uint32_t>(0x42, 0x42);
  VerifySimpleHexAtoiBad<uint32_t>(-0x42);

  VerifySimpleHexAtoiBad<uint32_t>(std::numeric_limits<int32_t>::min());
  VerifySimpleHexAtoiGood<uint32_t>(std::numeric_limits<int32_t>::max(),
                                    std::numeric_limits<int32_t>::max());
  VerifySimpleHexAtoiGood<uint32_t>(std::numeric_limits<uint32_t>::max(),
                                    std::numeric_limits<uint32_t>::max());
  VerifySimpleHexAtoiBad<uint32_t>(std::numeric_limits<int64_t>::min());
  VerifySimpleHexAtoiBad<uint32_t>(std::numeric_limits<int64_t>::max());
  VerifySimpleHexAtoiBad<uint32_t>(std::numeric_limits<uint64_t>::max());

  // SimpleHexAtoi(absl::string_view, int64_t)
  VerifySimpleHexAtoiGood<int64_t>(0, 0);
  VerifySimpleHexAtoiGood<int64_t>(0x42, 0x42);
  VerifySimpleHexAtoiGood<int64_t>(-0x42, -0x42);

  VerifySimpleHexAtoiGood<int64_t>(std::numeric_limits<int32_t>::min(),
                                   std::numeric_limits<int32_t>::min());
  VerifySimpleHexAtoiGood<int64_t>(std::numeric_limits<int32_t>::max(),
                                   std::numeric_limits<int32_t>::max());
  VerifySimpleHexAtoiGood<int64_t>(std::numeric_limits<uint32_t>::max(),
                                   std::numeric_limits<uint32_t>::max());
  VerifySimpleHexAtoiGood<int64_t>(std::numeric_limits<int64_t>::min(),
                                   std::numeric_limits<int64_t>::min());
  VerifySimpleHexAtoiGood<int64_t>(std::numeric_limits<int64_t>::max(),
                                   std::numeric_limits<int64_t>::max());
  VerifySimpleHexAtoiBad<int64_t>(std::numeric_limits<uint64_t>::max());

  // SimpleHexAtoi(absl::string_view, uint64_t)
  VerifySimpleHexAtoiGood<uint64_t>(0, 0);
  VerifySimpleHexAtoiGood<uint64_t>(0x42, 0x42);
  VerifySimpleHexAtoiBad<uint64_t>(-0x42);

  VerifySimpleHexAtoiBad<uint64_t>(std::numeric_limits<int32_t>::min());
  VerifySimpleHexAtoiGood<uint64_t>(std::numeric_limits<int32_t>::max(),
                                    std::numeric_limits<int32_t>::max());
  VerifySimpleHexAtoiGood<uint64_t>(std::numeric_limits<uint32_t>::max(),
                                    std::numeric_limits<uint32_t>::max());
  VerifySimpleHexAtoiBad<uint64_t>(std::numeric_limits<int64_t>::min());
  VerifySimpleHexAtoiGood<uint64_t>(std::numeric_limits<int64_t>::max(),
                                    std::numeric_limits<int64_t>::max());
  VerifySimpleHexAtoiGood<uint64_t>(std::numeric_limits<uint64_t>::max(),
                                    std::numeric_limits<uint64_t>::max());

  // SimpleHexAtoi(absl::string_view, absl::uint128)
  VerifySimpleHexAtoiGood<absl::uint128>(0, 0);
  VerifySimpleHexAtoiGood<absl::uint128>(0x42, 0x42);
  VerifySimpleHexAtoiBad<absl::uint128>(-0x42);

  VerifySimpleHexAtoiBad<absl::uint128>(std::numeric_limits<int32_t>::min());
  VerifySimpleHexAtoiGood<absl::uint128>(std::numeric_limits<int32_t>::max(),
                                         std::numeric_limits<int32_t>::max());
  VerifySimpleHexAtoiGood<absl::uint128>(std::numeric_limits<uint32_t>::max(),
                                         std::numeric_limits<uint32_t>::max());
  VerifySimpleHexAtoiBad<absl::uint128>(std::numeric_limits<int64_t>::min());
  VerifySimpleHexAtoiGood<absl::uint128>(std::numeric_limits<int64_t>::max(),
                                         std::numeric_limits<int64_t>::max());
  VerifySimpleHexAtoiGood<absl::uint128>(std::numeric_limits<uint64_t>::max(),
                                         std::numeric_limits<uint64_t>::max());
  VerifySimpleHexAtoiGood<absl::uint128>(
      std::numeric_limits<absl::uint128>::max(),
      std::numeric_limits<absl::uint128>::max());

  // Some other types
  VerifySimpleHexAtoiGood<int>(-0x42, -0x42);
  VerifySimpleHexAtoiGood<int32_t>(-0x42, -0x42);
  VerifySimpleHexAtoiGood<uint32_t>(0x42, 0x42);
  VerifySimpleHexAtoiGood<unsigned int>(0x42, 0x42);
  VerifySimpleHexAtoiGood<int64_t>(-0x42, -0x42);
  VerifySimpleHexAtoiGood<long>(-0x42, -0x42);  // NOLINT: runtime-int
  VerifySimpleHexAtoiGood<uint64_t>(0x42, 0x42);
  VerifySimpleHexAtoiGood<size_t>(0x42, 0x42);
  VerifySimpleHexAtoiGood<std::string::size_type>(0x42, 0x42);

  // Number prefix
  int32_t value;
  EXPECT_TRUE(safe_strto32_base("0x34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(safe_strto32_base("0X34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  // ASCII whitespace
  EXPECT_TRUE(safe_strto32_base(" \t\n 34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(safe_strto32_base("34234324 \t\n ", &value, 16));
  EXPECT_EQ(0x34234324, value);
}

TEST(stringtest, safe_strto32_base) {
  int32_t value;
  EXPECT_TRUE(safe_strto32_base("0x34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(safe_strto32_base("0X34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(safe_strto32_base("34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(safe_strto32_base("0", &value, 16));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto32_base(" \t\n -0x34234324", &value, 16));
  EXPECT_EQ(-0x34234324, value);

  EXPECT_TRUE(safe_strto32_base(" \t\n -34234324", &value, 16));
  EXPECT_EQ(-0x34234324, value);

  EXPECT_TRUE(safe_strto32_base("7654321", &value, 8));
  EXPECT_EQ(07654321, value);

  EXPECT_TRUE(safe_strto32_base("-01234", &value, 8));
  EXPECT_EQ(-01234, value);

  EXPECT_FALSE(safe_strto32_base("1834", &value, 8));

  // Autodetect base.
  EXPECT_TRUE(safe_strto32_base("0", &value, 0));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto32_base("077", &value, 0));
  EXPECT_EQ(077, value);  // Octal interpretation

  // Leading zero indicates octal, but then followed by invalid digit.
  EXPECT_FALSE(safe_strto32_base("088", &value, 0));

  // Leading 0x indicated hex, but then followed by invalid digit.
  EXPECT_FALSE(safe_strto32_base("0xG", &value, 0));

  // Base-10 version.
  EXPECT_TRUE(safe_strto32_base("34234324", &value, 10));
  EXPECT_EQ(34234324, value);

  EXPECT_TRUE(safe_strto32_base("0", &value, 10));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto32_base(" \t\n -34234324", &value, 10));
  EXPECT_EQ(-34234324, value);

  EXPECT_TRUE(safe_strto32_base("34234324 \n\t ", &value, 10));
  EXPECT_EQ(34234324, value);

  // Invalid ints.
  EXPECT_FALSE(safe_strto32_base("", &value, 10));
  EXPECT_FALSE(safe_strto32_base("  ", &value, 10));
  EXPECT_FALSE(safe_strto32_base("abc", &value, 10));
  EXPECT_FALSE(safe_strto32_base("34234324a", &value, 10));
  EXPECT_FALSE(safe_strto32_base("34234.3", &value, 10));

  // Out of bounds.
  EXPECT_FALSE(safe_strto32_base("2147483648", &value, 10));
  EXPECT_FALSE(safe_strto32_base("-2147483649", &value, 10));

  // String version.
  EXPECT_TRUE(safe_strto32_base(std::string("0x1234"), &value, 16));
  EXPECT_EQ(0x1234, value);

  // Base-10 string version.
  EXPECT_TRUE(safe_strto32_base("1234", &value, 10));
  EXPECT_EQ(1234, value);
}

TEST(stringtest, safe_strto32_range) {
  // These tests verify underflow/overflow behaviour.
  int32_t value;
  EXPECT_FALSE(safe_strto32_base("2147483648", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), value);

  EXPECT_TRUE(safe_strto32_base("-2147483648", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), value);

  EXPECT_FALSE(safe_strto32_base("-2147483649", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), value);
}

TEST(stringtest, safe_strto64_range) {
  // These tests verify underflow/overflow behaviour.
  int64_t value;
  EXPECT_FALSE(safe_strto64_base("9223372036854775808", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), value);

  EXPECT_TRUE(safe_strto64_base("-9223372036854775808", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), value);

  EXPECT_FALSE(safe_strto64_base("-9223372036854775809", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), value);
}

TEST(stringtest, safe_strto32_leading_substring) {
  // These tests verify this comment in numbers.h:
  // On error, returns false, and sets *value to: [...]
  //   conversion of leading substring if available ("123@@@" -> 123)
  //   0 if no leading substring available
  int32_t value;
  EXPECT_FALSE(safe_strto32_base("04069@@@", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(safe_strto32_base("04069@@@", &value, 8));
  EXPECT_EQ(0406, value);

  EXPECT_FALSE(safe_strto32_base("04069balloons", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(safe_strto32_base("04069balloons", &value, 16));
  EXPECT_EQ(0x4069ba, value);

  EXPECT_FALSE(safe_strto32_base("@@@", &value, 10));
  EXPECT_EQ(0, value);  // there was no leading substring
}

TEST(stringtest, safe_strto64_leading_substring) {
  // These tests verify this comment in numbers.h:
  // On error, returns false, and sets *value to: [...]
  //   conversion of leading substring if available ("123@@@" -> 123)
  //   0 if no leading substring available
  int64_t value;
  EXPECT_FALSE(safe_strto64_base("04069@@@", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(safe_strto64_base("04069@@@", &value, 8));
  EXPECT_EQ(0406, value);

  EXPECT_FALSE(safe_strto64_base("04069balloons", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(safe_strto64_base("04069balloons", &value, 16));
  EXPECT_EQ(0x4069ba, value);

  EXPECT_FALSE(safe_strto64_base("@@@", &value, 10));
  EXPECT_EQ(0, value);  // there was no leading substring
}

TEST(stringtest, safe_strto64_base) {
  int64_t value;
  EXPECT_TRUE(safe_strto64_base("0x3423432448783446", &value, 16));
  EXPECT_EQ(int64_t{0x3423432448783446}, value);

  EXPECT_TRUE(safe_strto64_base("3423432448783446", &value, 16));
  EXPECT_EQ(int64_t{0x3423432448783446}, value);

  EXPECT_TRUE(safe_strto64_base("0", &value, 16));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto64_base(" \t\n -0x3423432448783446", &value, 16));
  EXPECT_EQ(int64_t{-0x3423432448783446}, value);

  EXPECT_TRUE(safe_strto64_base(" \t\n -3423432448783446", &value, 16));
  EXPECT_EQ(int64_t{-0x3423432448783446}, value);

  EXPECT_TRUE(safe_strto64_base("123456701234567012", &value, 8));
  EXPECT_EQ(int64_t{0123456701234567012}, value);

  EXPECT_TRUE(safe_strto64_base("-017777777777777", &value, 8));
  EXPECT_EQ(int64_t{-017777777777777}, value);

  EXPECT_FALSE(safe_strto64_base("19777777777777", &value, 8));

  // Autodetect base.
  EXPECT_TRUE(safe_strto64_base("0", &value, 0));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto64_base("077", &value, 0));
  EXPECT_EQ(077, value);  // Octal interpretation

  // Leading zero indicates octal, but then followed by invalid digit.
  EXPECT_FALSE(safe_strto64_base("088", &value, 0));

  // Leading 0x indicated hex, but then followed by invalid digit.
  EXPECT_FALSE(safe_strto64_base("0xG", &value, 0));

  // Base-10 version.
  EXPECT_TRUE(safe_strto64_base("34234324487834466", &value, 10));
  EXPECT_EQ(int64_t{34234324487834466}, value);

  EXPECT_TRUE(safe_strto64_base("0", &value, 10));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto64_base(" \t\n -34234324487834466", &value, 10));
  EXPECT_EQ(int64_t{-34234324487834466}, value);

  EXPECT_TRUE(safe_strto64_base("34234324487834466 \n\t ", &value, 10));
  EXPECT_EQ(int64_t{34234324487834466}, value);

  // Invalid ints.
  EXPECT_FALSE(safe_strto64_base("", &value, 10));
  EXPECT_FALSE(safe_strto64_base("  ", &value, 10));
  EXPECT_FALSE(safe_strto64_base("abc", &value, 10));
  EXPECT_FALSE(safe_strto64_base("34234324487834466a", &value, 10));
  EXPECT_FALSE(safe_strto64_base("34234487834466.3", &value, 10));

  // Out of bounds.
  EXPECT_FALSE(safe_strto64_base("9223372036854775808", &value, 10));
  EXPECT_FALSE(safe_strto64_base("-9223372036854775809", &value, 10));

  // String version.
  EXPECT_TRUE(safe_strto64_base(std::string("0x1234"), &value, 16));
  EXPECT_EQ(0x1234, value);

  // Base-10 string version.
  EXPECT_TRUE(safe_strto64_base("1234", &value, 10));
  EXPECT_EQ(1234, value);
}

const size_t kNumRandomTests = 10000;

template <typename IntType>
void test_random_integer_parse_base(bool (*parse_func)(absl::string_view,
                                                       IntType* value,
                                                       int base)) {
  using RandomEngine = std::minstd_rand0;
  std::random_device rd;
  RandomEngine rng(rd());
  std::uniform_int_distribution<IntType> random_int(
      std::numeric_limits<IntType>::min());
  std::uniform_int_distribution<int> random_base(2, 35);
  for (size_t i = 0; i < kNumRandomTests; i++) {
    IntType value = random_int(rng);
    int base = random_base(rng);
    std::string str_value;
    EXPECT_TRUE(Itoa<IntType>(value, base, &str_value));
    IntType parsed_value;

    // Test successful parse
    EXPECT_TRUE(parse_func(str_value, &parsed_value, base));
    EXPECT_EQ(parsed_value, value);

    // Test overflow
    EXPECT_FALSE(
        parse_func(absl::StrCat(std::numeric_limits<IntType>::max(), value),
                   &parsed_value, base));

    // Test underflow
    if (std::numeric_limits<IntType>::min() < 0) {
      EXPECT_FALSE(
          parse_func(absl::StrCat(std::numeric_limits<IntType>::min(), value),
                     &parsed_value, base));
    } else {
      EXPECT_FALSE(parse_func(absl::StrCat("-", value), &parsed_value, base));
    }
  }
}

TEST(stringtest, safe_strto32_random) {
  test_random_integer_parse_base<int32_t>(&safe_strto32_base);
}
TEST(stringtest, safe_strto64_random) {
  test_random_integer_parse_base<int64_t>(&safe_strto64_base);
}
TEST(stringtest, safe_strtou32_random) {
  test_random_integer_parse_base<uint32_t>(&safe_strtou32_base);
}
TEST(stringtest, safe_strtou64_random) {
  test_random_integer_parse_base<uint64_t>(&safe_strtou64_base);
}
TEST(stringtest, safe_strtou128_random) {
  // random number generators don't work for uint128, and
  // uint128 can be streamed but not StrCat'd, so this code must be custom
  // implemented for uint128, but is generally the same as what's above.
  // test_random_integer_parse_base<absl::uint128>(
  //     &absl::numbers_internal::safe_strtou128_base);
  using RandomEngine = std::minstd_rand0;
  using IntType = absl::uint128;
  constexpr auto parse_func = &absl::numbers_internal::safe_strtou128_base;

  std::random_device rd;
  RandomEngine rng(rd());
  std::uniform_int_distribution<uint64_t> random_uint64(
      std::numeric_limits<uint64_t>::min());
  std::uniform_int_distribution<int> random_base(2, 35);

  for (size_t i = 0; i < kNumRandomTests; i++) {
    IntType value = random_uint64(rng);
    value = (value << 64) + random_uint64(rng);
    int base = random_base(rng);
    std::string str_value;
    EXPECT_TRUE(Itoa<IntType>(value, base, &str_value));
    IntType parsed_value;

    // Test successful parse
    EXPECT_TRUE(parse_func(str_value, &parsed_value, base));
    EXPECT_EQ(parsed_value, value);

    // Test overflow
    std::string s;
    absl::strings_internal::OStringStream(&s)
        << std::numeric_limits<IntType>::max() << value;
    EXPECT_FALSE(parse_func(s, &parsed_value, base));

    // Test underflow
    s.clear();
    absl::strings_internal::OStringStream(&s) << "-" << value;
    EXPECT_FALSE(parse_func(s, &parsed_value, base));
  }
}
TEST(stringtest, safe_strto128_random) {
  // random number generators don't work for int128, and
  // int128 can be streamed but not StrCat'd, so this code must be custom
  // implemented for int128, but is generally the same as what's above.
  // test_random_integer_parse_base<absl::int128>(
  //     &absl::numbers_internal::safe_strto128_base);
  using RandomEngine = std::minstd_rand0;
  using IntType = absl::int128;
  constexpr auto parse_func = &absl::numbers_internal::safe_strto128_base;

  std::random_device rd;
  RandomEngine rng(rd());
  std::uniform_int_distribution<int64_t> random_int64(
      std::numeric_limits<int64_t>::min());
  std::uniform_int_distribution<uint64_t> random_uint64(
      std::numeric_limits<uint64_t>::min());
  std::uniform_int_distribution<int> random_base(2, 35);

  for (size_t i = 0; i < kNumRandomTests; ++i) {
    int64_t high = random_int64(rng);
    uint64_t low = random_uint64(rng);
    IntType value = absl::MakeInt128(high, low);

    int base = random_base(rng);
    std::string str_value;
    EXPECT_TRUE(Itoa<IntType>(value, base, &str_value));
    IntType parsed_value;

    // Test successful parse
    EXPECT_TRUE(parse_func(str_value, &parsed_value, base));
    EXPECT_EQ(parsed_value, value);

    // Test overflow
    std::string s;
    absl::strings_internal::OStringStream(&s)
        << std::numeric_limits<IntType>::max() << value;
    EXPECT_FALSE(parse_func(s, &parsed_value, base));

    // Test underflow
    s.clear();
    absl::strings_internal::OStringStream(&s)
        << std::numeric_limits<IntType>::min() << value;
    EXPECT_FALSE(parse_func(s, &parsed_value, base));
  }
}

TEST(stringtest, safe_strtou32_base) {
  for (int i = 0; strtouint32_test_cases()[i].str != nullptr; ++i) {
    const auto& e = strtouint32_test_cases()[i];
    uint32_t value;
    EXPECT_EQ(e.expect_ok, safe_strtou32_base(e.str, &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "i=" << i << " str=\"" << e.str
                                   << "\" base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtou32_base_length_delimited) {
  for (int i = 0; strtouint32_test_cases()[i].str != nullptr; ++i) {
    const auto& e = strtouint32_test_cases()[i];
    std::string tmp(e.str);
    tmp.append("12");  // Adds garbage at the end.

    uint32_t value;
    EXPECT_EQ(e.expect_ok,
              safe_strtou32_base(absl::string_view(tmp.data(), strlen(e.str)),
                                 &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "i=" << i << " str=" << e.str
                                   << " base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtou64_base) {
  for (int i = 0; strtouint64_test_cases()[i].str != nullptr; ++i) {
    const auto& e = strtouint64_test_cases()[i];
    uint64_t value;
    EXPECT_EQ(e.expect_ok, safe_strtou64_base(e.str, &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=" << e.str << " base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtou64_base_length_delimited) {
  for (int i = 0; strtouint64_test_cases()[i].str != nullptr; ++i) {
    const auto& e = strtouint64_test_cases()[i];
    std::string tmp(e.str);
    tmp.append("12");  // Adds garbage at the end.

    uint64_t value;
    EXPECT_EQ(e.expect_ok,
              safe_strtou64_base(absl::string_view(tmp.data(), strlen(e.str)),
                                 &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=\"" << e.str << "\" base=" << e.base;
    }
  }
}

// feenableexcept() and fedisableexcept() are extensions supported by some libc
// implementations.
#if defined(__GLIBC__) || defined(__BIONIC__)
#define ABSL_HAVE_FEENABLEEXCEPT 1
#define ABSL_HAVE_FEDISABLEEXCEPT 1
#endif

class SimpleDtoaTest : public testing::Test {
 protected:
  void SetUp() override {
    // Store the current floating point env & clear away any pending exceptions.
    feholdexcept(&fp_env_);
#ifdef ABSL_HAVE_FEENABLEEXCEPT
    // Turn on floating point exceptions.
    feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
  }

  void TearDown() override {
    // Restore the floating point environment to the original state.
    // In theory fedisableexcept is unnecessary; fesetenv will also do it.
    // In practice, our toolchains have subtle bugs.
#ifdef ABSL_HAVE_FEDISABLEEXCEPT
    fedisableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
    fesetenv(&fp_env_);
  }

  std::string ToNineDigits(double value) {
    char buffer[16];  // more than enough for %.9g
    snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
  }

  fenv_t fp_env_;
};

// Run the given runnable functor for "cases" test cases, chosen over the
// available range of float.  pi and e and 1/e are seeded, and then all
// available integer powers of 2 and 10 are multiplied against them.  In
// addition to trying all those values, we try the next higher and next lower
// float, and then we add additional test cases evenly distributed between them.
// Each test case is passed to runnable as both a positive and negative value.
template <typename R>
void ExhaustiveFloat(uint32_t cases, R&& runnable) {
  runnable(0.0f);
  runnable(-0.0f);
  if (cases >= 2e9) {  // more than 2 billion?  Might as well run them all.
    for (float f = 0; f < std::numeric_limits<float>::max(); ) {
      f = nextafterf(f, std::numeric_limits<float>::max());
      runnable(-f);
      runnable(f);
    }
    return;
  }
  std::set<float> floats = {3.4028234e38f};
  for (float f : {1.0, 3.14159265, 2.718281828, 1 / 2.718281828}) {
    for (float testf = f; testf != 0; testf *= 0.1f) floats.insert(testf);
    for (float testf = f; testf != 0; testf *= 0.5f) floats.insert(testf);
    for (float testf = f; testf < 3e38f / 2; testf *= 2.0f)
      floats.insert(testf);
    for (float testf = f; testf < 3e38f / 10; testf *= 10) floats.insert(testf);
  }

  float last = *floats.begin();

  runnable(last);
  runnable(-last);
  int iters_per_float = cases / floats.size();
  if (iters_per_float == 0) iters_per_float = 1;
  for (float f : floats) {
    if (f == last) continue;
    float testf = std::nextafter(last, std::numeric_limits<float>::max());
    runnable(testf);
    runnable(-testf);
    last = testf;
    if (f == last) continue;
    double step = (double{f} - last) / iters_per_float;
    for (double d = last + step; d < f; d += step) {
      testf = d;
      if (testf != last) {
        runnable(testf);
        runnable(-testf);
        last = testf;
      }
    }
    testf = std::nextafter(f, 0.0f);
    if (testf > last) {
      runnable(testf);
      runnable(-testf);
      last = testf;
    }
    if (f != last) {
      runnable(f);
      runnable(-f);
      last = f;
    }
  }
}

TEST_F(SimpleDtoaTest, ExhaustiveDoubleToSixDigits) {
  uint64_t test_count = 0;
  std::vector<double> mismatches;
  auto checker = [&](double d) {
    if (d != d) return;  // rule out NaNs
    ++test_count;
    char sixdigitsbuf[kSixDigitsToBufferSize] = {0};
    SixDigitsToBuffer(d, sixdigitsbuf);
    char snprintfbuf[kSixDigitsToBufferSize] = {0};
    snprintf(snprintfbuf, kSixDigitsToBufferSize, "%g", d);
    if (strcmp(sixdigitsbuf, snprintfbuf) != 0) {
      mismatches.push_back(d);
      if (mismatches.size() < 10) {
        LOG(ERROR) << "Six-digit failure with double.  d=" << d
                   << " sixdigits=" << sixdigitsbuf
                   << " printf(%g)=" << snprintfbuf;
      }
    }
  };
  // Some quick sanity checks...
  checker(5e-324);
  checker(1e-308);
  checker(1.0);
  checker(1.000005);
  checker(1.7976931348623157e308);
  checker(0.00390625);
#ifndef _MSC_VER
  // on MSVC, snprintf() rounds it to 0.00195313. SixDigitsToBuffer() rounds it
  // to 0.00195312 (round half to even).
  checker(0.001953125);
#endif
  checker(0.005859375);
  // Some cases where the rounding is very very close
  checker(1.089095e-15);
  checker(3.274195e-55);
  checker(6.534355e-146);
  checker(2.920845e+234);

  if (mismatches.empty()) {
    test_count = 0;
    ExhaustiveFloat(kFloatNumCases, checker);

    test_count = 0;
    std::vector<int> digit_testcases{
        100000, 100001, 100002, 100005, 100010, 100020, 100050, 100100,  // misc
        195312, 195313,  // 1.953125 is a case where we round down, just barely.
        200000, 500000, 800000,  // misc mid-range cases
        585937, 585938,  // 5.859375 is a case where we round up, just barely.
        900000, 990000, 999000, 999900, 999990, 999996, 999997, 999998, 999999};
    if (kFloatNumCases >= 1e9) {
      // If at least 1 billion test cases were requested, user wants an
      // exhaustive test. So let's test all mantissas, too.
      constexpr int min_mantissa = 100000, max_mantissa = 999999;
      digit_testcases.resize(max_mantissa - min_mantissa + 1);
      std::iota(digit_testcases.begin(), digit_testcases.end(), min_mantissa);
    }

    for (int exponent = -324; exponent <= 308; ++exponent) {
      double powten = absl::strings_internal::Pow10(exponent);
      if (powten == 0) powten = 5e-324;
      if (kFloatNumCases >= 1e9) {
        // The exhaustive test takes a very long time, so log progress.
        char buf[kSixDigitsToBufferSize];
        LOG(INFO) << "Exp " << exponent << " powten=" << powten << "(" << powten
                  << ") ("
                  << absl::string_view(buf, SixDigitsToBuffer(powten, buf))
                  << ")";
      }
      for (int digits : digit_testcases) {
        if (exponent == 308 && digits >= 179769) break;  // don't overflow!
        double digiform = (digits + 0.5) * 0.00001;
        double testval = digiform * powten;
        double pretestval = nextafter(testval, 0);
        double posttestval = nextafter(testval, 1.7976931348623157e308);
        checker(testval);
        checker(pretestval);
        checker(posttestval);
      }
    }
  } else {
    EXPECT_EQ(mismatches.size(), 0);
    for (size_t i = 0; i < mismatches.size(); ++i) {
      if (i > 100) i = mismatches.size() - 1;
      double d = mismatches[i];
      char sixdigitsbuf[kSixDigitsToBufferSize] = {0};
      SixDigitsToBuffer(d, sixdigitsbuf);
      char snprintfbuf[kSixDigitsToBufferSize] = {0};
      snprintf(snprintfbuf, kSixDigitsToBufferSize, "%g", d);
      double before = nextafter(d, 0.0);
      double after = nextafter(d, 1.7976931348623157e308);
      char b1[32], b2[kSixDigitsToBufferSize];
      LOG(ERROR) << "Mismatch #" << i << "  d=" << d << " (" << ToNineDigits(d)
                 << ") sixdigits='" << sixdigitsbuf << "' snprintf='"
                 << snprintfbuf << "' Before.=" << PerfectDtoa(before) << " "
                 << (SixDigitsToBuffer(before, b2), b2) << " vs snprintf="
                 << (snprintf(b1, sizeof(b1), "%g", before), b1)
                 << " Perfect=" << PerfectDtoa(d) << " "
                 << (SixDigitsToBuffer(d, b2), b2)
                 << " vs snprintf=" << (snprintf(b1, sizeof(b1), "%g", d), b1)
                 << " After.=." << PerfectDtoa(after) << " "
                 << (SixDigitsToBuffer(after, b2), b2) << " vs snprintf="
                 << (snprintf(b1, sizeof(b1), "%g", after), b1);
    }
  }
}

TEST(StrToInt32, Partial) {
  struct Int32TestLine {
    std::string input;
    bool status;
    int32_t value;
  };
  const int32_t int32_min = std::numeric_limits<int32_t>::min();
  const int32_t int32_max = std::numeric_limits<int32_t>::max();
  Int32TestLine int32_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123@@@", false, 123},
      {absl::StrCat(int32_min, int32_max), false, int32_min},
      {absl::StrCat(int32_max, int32_max), false, int32_max},
  };

  for (const Int32TestLine& test_line : int32_test_line) {
    int32_t value = -2;
    bool status = safe_strto32_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = safe_strto32_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = safe_strto32_base(absl::string_view(test_line.input), &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToUint32, Partial) {
  struct Uint32TestLine {
    std::string input;
    bool status;
    uint32_t value;
  };
  const uint32_t uint32_max = std::numeric_limits<uint32_t>::max();
  Uint32TestLine uint32_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123@@@", false, 123},
      {absl::StrCat(uint32_max, uint32_max), false, uint32_max},
  };

  for (const Uint32TestLine& test_line : uint32_test_line) {
    uint32_t value = 2;
    bool status = safe_strtou32_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = safe_strtou32_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = safe_strtou32_base(absl::string_view(test_line.input), &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToInt64, Partial) {
  struct Int64TestLine {
    std::string input;
    bool status;
    int64_t value;
  };
  const int64_t int64_min = std::numeric_limits<int64_t>::min();
  const int64_t int64_max = std::numeric_limits<int64_t>::max();
  Int64TestLine int64_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123@@@", false, 123},
      {absl::StrCat(int64_min, int64_max), false, int64_min},
      {absl::StrCat(int64_max, int64_max), false, int64_max},
  };

  for (const Int64TestLine& test_line : int64_test_line) {
    int64_t value = -2;
    bool status = safe_strto64_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = safe_strto64_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = safe_strto64_base(absl::string_view(test_line.input), &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToUint64, Partial) {
  struct Uint64TestLine {
    std::string input;
    bool status;
    uint64_t value;
  };
  const uint64_t uint64_max = std::numeric_limits<uint64_t>::max();
  Uint64TestLine uint64_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123@@@", false, 123},
      {absl::StrCat(uint64_max, uint64_max), false, uint64_max},
  };

  for (const Uint64TestLine& test_line : uint64_test_line) {
    uint64_t value = 2;
    bool status = safe_strtou64_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = safe_strtou64_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = safe_strtou64_base(absl::string_view(test_line.input), &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToInt32Base, PrefixOnly) {
  struct Int32TestLine {
    std::string input;
    bool status;
    int32_t value;
  };
  Int32TestLine int32_test_line[] = {
    { "", false, 0 },
    { "-", false, 0 },
    { "-0", true, 0 },
    { "0", true, 0 },
    { "0x", false, 0 },
    { "-0x", false, 0 },
  };
  const int base_array[] = { 0, 2, 8, 10, 16 };

  for (const Int32TestLine& line : int32_test_line) {
    for (const int base : base_array) {
      int32_t value = 2;
      bool status = safe_strto32_base(line.input.c_str(), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strto32_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strto32_base(absl::string_view(line.input), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToUint32Base, PrefixOnly) {
  struct Uint32TestLine {
    std::string input;
    bool status;
    uint32_t value;
  };
  Uint32TestLine uint32_test_line[] = {
    { "", false, 0 },
    { "0", true, 0 },
    { "0x", false, 0 },
  };
  const int base_array[] = { 0, 2, 8, 10, 16 };

  for (const Uint32TestLine& line : uint32_test_line) {
    for (const int base : base_array) {
      uint32_t value = 2;
      bool status = safe_strtou32_base(line.input.c_str(), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strtou32_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strtou32_base(absl::string_view(line.input), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToInt64Base, PrefixOnly) {
  struct Int64TestLine {
    std::string input;
    bool status;
    int64_t value;
  };
  Int64TestLine int64_test_line[] = {
    { "", false, 0 },
    { "-", false, 0 },
    { "-0", true, 0 },
    { "0", true, 0 },
    { "0x", false, 0 },
    { "-0x", false, 0 },
  };
  const int base_array[] = { 0, 2, 8, 10, 16 };

  for (const Int64TestLine& line : int64_test_line) {
    for (const int base : base_array) {
      int64_t value = 2;
      bool status = safe_strto64_base(line.input.c_str(), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strto64_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strto64_base(absl::string_view(line.input), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToUint64Base, PrefixOnly) {
  struct Uint64TestLine {
    std::string input;
    bool status;
    uint64_t value;
  };
  Uint64TestLine uint64_test_line[] = {
    { "", false, 0 },
    { "0", true, 0 },
    { "0x", false, 0 },
  };
  const int base_array[] = { 0, 2, 8, 10, 16 };

  for (const Uint64TestLine& line : uint64_test_line) {
    for (const int base : base_array) {
      uint64_t value = 2;
      bool status = safe_strtou64_base(line.input.c_str(), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strtou64_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strtou64_base(absl::string_view(line.input), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

void TestFastHexToBufferZeroPad16(uint64_t v) {
  char buf[16];
  auto digits = absl::numbers_internal::FastHexToBufferZeroPad16(v, buf);
  absl::string_view res(buf, 16);
  char buf2[17];
  snprintf(buf2, sizeof(buf2), "%016" PRIx64, v);
  EXPECT_EQ(res, buf2) << v;
  size_t expected_digits = snprintf(buf2, sizeof(buf2), "%" PRIx64, v);
  EXPECT_EQ(digits, expected_digits) << v;
}

TEST(FastHexToBufferZeroPad16, Smoke) {
  TestFastHexToBufferZeroPad16(std::numeric_limits<uint64_t>::min());
  TestFastHexToBufferZeroPad16(std::numeric_limits<uint64_t>::max());
  TestFastHexToBufferZeroPad16(std::numeric_limits<int64_t>::min());
  TestFastHexToBufferZeroPad16(std::numeric_limits<int64_t>::max());
  absl::BitGen rng;
  for (int i = 0; i < 100000; ++i) {
    TestFastHexToBufferZeroPad16(
        absl::LogUniform(rng, std::numeric_limits<uint64_t>::min(),
                         std::numeric_limits<uint64_t>::max()));
  }
}

template <typename Int>
void ExpectWritesNull() {
  {
    char buf[absl::numbers_internal::kFastToBufferSize];
    Int x = std::numeric_limits<Int>::min();
    EXPECT_THAT(absl::numbers_internal::FastIntToBuffer(x, buf), Pointee('\0'));
  }
  {
    char buf[absl::numbers_internal::kFastToBufferSize];
    Int x = std::numeric_limits<Int>::max();
    EXPECT_THAT(absl::numbers_internal::FastIntToBuffer(x, buf), Pointee('\0'));
  }
}

TEST(FastIntToBuffer, WritesNull) {
  ExpectWritesNull<int32_t>();
  ExpectWritesNull<uint32_t>();
  ExpectWritesNull<int64_t>();
  ExpectWritesNull<uint32_t>();
}

}  // namespace
