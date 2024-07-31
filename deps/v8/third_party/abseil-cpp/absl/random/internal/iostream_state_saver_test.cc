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

#include "absl/random/internal/iostream_state_saver.h"

#include <errno.h>
#include <stdio.h>

#include <sstream>
#include <string>

#include "gtest/gtest.h"

namespace {

using absl::random_internal::make_istream_state_saver;
using absl::random_internal::make_ostream_state_saver;
using absl::random_internal::stream_precision_helper;

template <typename T>
typename absl::enable_if_t<std::is_integral<T>::value, T>  //
StreamRoundTrip(T t) {
  std::stringstream ss;
  {
    auto saver = make_ostream_state_saver(ss);
    ss.precision(stream_precision_helper<T>::kPrecision);
    ss << t;
  }
  T result = 0;
  {
    auto saver = make_istream_state_saver(ss);
    ss >> result;
  }
  EXPECT_FALSE(ss.fail())            //
      << ss.str() << " "             //
      << (ss.good() ? "good " : "")  //
      << (ss.bad() ? "bad " : "")    //
      << (ss.eof() ? "eof " : "")    //
      << (ss.fail() ? "fail " : "");

  return result;
}

template <typename T>
typename absl::enable_if_t<std::is_floating_point<T>::value, T>  //
StreamRoundTrip(T t) {
  std::stringstream ss;
  {
    auto saver = make_ostream_state_saver(ss);
    ss.precision(stream_precision_helper<T>::kPrecision);
    ss << t;
  }
  T result = 0;
  {
    auto saver = make_istream_state_saver(ss);
    result = absl::random_internal::read_floating_point<T>(ss);
  }
  EXPECT_FALSE(ss.fail())            //
      << ss.str() << " "             //
      << (ss.good() ? "good " : "")  //
      << (ss.bad() ? "bad " : "")    //
      << (ss.eof() ? "eof " : "")    //
      << (ss.fail() ? "fail " : "");

  return result;
}

TEST(IOStreamStateSaver, BasicSaverState) {
  std::stringstream ss;
  ss.precision(2);
  ss.fill('x');
  ss.flags(std::ios_base::dec | std::ios_base::right);

  {
    auto saver = make_ostream_state_saver(ss);
    ss.precision(10);
    EXPECT_NE('x', ss.fill());
    EXPECT_EQ(10, ss.precision());
    EXPECT_NE(std::ios_base::dec | std::ios_base::right, ss.flags());

    ss << 1.23;
  }

  EXPECT_EQ('x', ss.fill());
  EXPECT_EQ(2, ss.precision());
  EXPECT_EQ(std::ios_base::dec | std::ios_base::right, ss.flags());
}

TEST(IOStreamStateSaver, RoundTripInts) {
  const uint64_t kUintValues[] = {
      0,
      1,
      static_cast<uint64_t>(-1),
      2,
      static_cast<uint64_t>(-2),

      1 << 7,
      1 << 8,
      1 << 16,
      1ull << 32,
      1ull << 50,
      1ull << 62,
      1ull << 63,

      (1 << 7) - 1,
      (1 << 8) - 1,
      (1 << 16) - 1,
      (1ull << 32) - 1,
      (1ull << 50) - 1,
      (1ull << 62) - 1,
      (1ull << 63) - 1,

      static_cast<uint64_t>(-(1 << 8)),
      static_cast<uint64_t>(-(1 << 16)),
      static_cast<uint64_t>(-(1ll << 32)),
      static_cast<uint64_t>(-(1ll << 50)),
      static_cast<uint64_t>(-(1ll << 62)),

      static_cast<uint64_t>(-(1 << 8) - 1),
      static_cast<uint64_t>(-(1 << 16) - 1),
      static_cast<uint64_t>(-(1ll << 32) - 1),
      static_cast<uint64_t>(-(1ll << 50) - 1),
      static_cast<uint64_t>(-(1ll << 62) - 1),
  };

  for (const uint64_t u : kUintValues) {
    EXPECT_EQ(u, StreamRoundTrip<uint64_t>(u));

    int64_t x = static_cast<int64_t>(u);
    EXPECT_EQ(x, StreamRoundTrip<int64_t>(x));

    double d = static_cast<double>(x);
    EXPECT_EQ(d, StreamRoundTrip<double>(d));

    float f = d;
    EXPECT_EQ(f, StreamRoundTrip<float>(f));
  }
}

TEST(IOStreamStateSaver, RoundTripFloats) {
  static_assert(
      stream_precision_helper<float>::kPrecision >= 9,
      "stream_precision_helper<float>::kPrecision should be at least 9");

  const float kValues[] = {
      1,
      std::nextafter(1.0f, 0.0f),  // 1 - epsilon
      std::nextafter(1.0f, 2.0f),  // 1 + epsilon

      1.0e+1f,
      1.0e-1f,
      1.0e+2f,
      1.0e-2f,
      1.0e+10f,
      1.0e-10f,

      0.00000051110000111311111111f,
      -0.00000051110000111211111111f,

      1.234678912345678912345e+6f,
      1.234678912345678912345e-6f,
      1.234678912345678912345e+30f,
      1.234678912345678912345e-30f,
      1.234678912345678912345e+38f,
      1.0234678912345678912345e-38f,

      // Boundary cases.
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::epsilon(),
      std::nextafter(std::numeric_limits<float>::min(),
                     1.0f),               // min + epsilon
      std::numeric_limits<float>::min(),  // smallest normal
      // There are some errors dealing with denorms on apple platforms.
      std::numeric_limits<float>::denorm_min(),  // smallest denorm
      std::numeric_limits<float>::min() / 2,
      std::nextafter(std::numeric_limits<float>::min(),
                     0.0f),  // denorm_max
      std::nextafter(std::numeric_limits<float>::denorm_min(), 1.0f),
  };

  for (const float f : kValues) {
    EXPECT_EQ(f, StreamRoundTrip<float>(f));
    EXPECT_EQ(-f, StreamRoundTrip<float>(-f));

    double d = f;
    EXPECT_EQ(d, StreamRoundTrip<double>(d));
    EXPECT_EQ(-d, StreamRoundTrip<double>(-d));

    // Avoid undefined behavior (overflow/underflow).
    if (f <= static_cast<float>(std::numeric_limits<int64_t>::max()) &&
        f >= static_cast<float>(std::numeric_limits<int64_t>::lowest())) {
      int64_t x = static_cast<int64_t>(f);
      EXPECT_EQ(x, StreamRoundTrip<int64_t>(x));
    }
  }
}

TEST(IOStreamStateSaver, RoundTripDoubles) {
  static_assert(
      stream_precision_helper<double>::kPrecision >= 17,
      "stream_precision_helper<double>::kPrecision should be at least 17");

  const double kValues[] = {
      1,
      std::nextafter(1.0, 0.0),  // 1 - epsilon
      std::nextafter(1.0, 2.0),  // 1 + epsilon

      1.0e+1,
      1.0e-1,
      1.0e+2,
      1.0e-2,
      1.0e+10,
      1.0e-10,

      0.00000051110000111311111111,
      -0.00000051110000111211111111,

      1.234678912345678912345e+6,
      1.234678912345678912345e-6,
      1.234678912345678912345e+30,
      1.234678912345678912345e-30,
      1.234678912345678912345e+38,
      1.0234678912345678912345e-38,

      1.0e+100,
      1.0e-100,
      1.234678912345678912345e+308,
      1.0234678912345678912345e-308,
      2.22507385850720138e-308,

      // Boundary cases.
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::lowest(),
      std::numeric_limits<double>::epsilon(),
      std::nextafter(std::numeric_limits<double>::min(),
                     1.0),                 // min + epsilon
      std::numeric_limits<double>::min(),  // smallest normal
      // There are some errors dealing with denorms on apple platforms.
      std::numeric_limits<double>::denorm_min(),  // smallest denorm
      std::numeric_limits<double>::min() / 2,
      std::nextafter(std::numeric_limits<double>::min(),
                     0.0),  // denorm_max
      std::nextafter(std::numeric_limits<double>::denorm_min(), 1.0f),
  };

  for (const double d : kValues) {
    EXPECT_EQ(d, StreamRoundTrip<double>(d));
    EXPECT_EQ(-d, StreamRoundTrip<double>(-d));

    // Avoid undefined behavior (overflow/underflow).
    if (d <= std::numeric_limits<float>::max() &&
        d >= std::numeric_limits<float>::lowest()) {
      float f = static_cast<float>(d);
      EXPECT_EQ(f, StreamRoundTrip<float>(f));
    }

    // Avoid undefined behavior (overflow/underflow).
    if (d <= static_cast<double>(std::numeric_limits<int64_t>::max()) &&
        d >= static_cast<double>(std::numeric_limits<int64_t>::lowest())) {
      int64_t x = static_cast<int64_t>(d);
      EXPECT_EQ(x, StreamRoundTrip<int64_t>(x));
    }
  }
}

TEST(IOStreamStateSaver, RoundTripLongDoubles) {
  // Technically, C++ only guarantees that long double is at least as large as a
  // double.  Practically it varies from 64-bits to 128-bits.
  //
  // So it is best to consider long double a best-effort extended precision
  // type.

  static_assert(
      stream_precision_helper<long double>::kPrecision >= 36,
      "stream_precision_helper<long double>::kPrecision should be at least 36");

  using real_type = long double;
  const real_type kValues[] = {
      1,
      std::nextafter(1.0, 0.0),  // 1 - epsilon
      std::nextafter(1.0, 2.0),  // 1 + epsilon

      1.0e+1,
      1.0e-1,
      1.0e+2,
      1.0e-2,
      1.0e+10,
      1.0e-10,

      0.00000051110000111311111111,
      -0.00000051110000111211111111,

      1.2346789123456789123456789123456789e+6,
      1.2346789123456789123456789123456789e-6,
      1.2346789123456789123456789123456789e+30,
      1.2346789123456789123456789123456789e-30,
      1.2346789123456789123456789123456789e+38,
      1.2346789123456789123456789123456789e-38,
      1.2346789123456789123456789123456789e+308,
      1.2346789123456789123456789123456789e-308,

      1.0e+100,
      1.0e-100,
      1.234678912345678912345e+308,
      1.0234678912345678912345e-308,

      // Boundary cases.
      std::numeric_limits<real_type>::max(),
      std::numeric_limits<real_type>::lowest(),
      std::numeric_limits<real_type>::epsilon(),
      std::nextafter(std::numeric_limits<real_type>::min(),
                     real_type(1)),           // min + epsilon
      std::numeric_limits<real_type>::min(),  // smallest normal
      // There are some errors dealing with denorms on apple platforms.
      std::numeric_limits<real_type>::denorm_min(),  // smallest denorm
      std::numeric_limits<real_type>::min() / 2,
      std::nextafter(std::numeric_limits<real_type>::min(),
                     0.0),  // denorm_max
      std::nextafter(std::numeric_limits<real_type>::denorm_min(), 1.0f),
  };

  int index = -1;
  for (const long double dd : kValues) {
    index++;
    EXPECT_EQ(dd, StreamRoundTrip<real_type>(dd)) << index;
    EXPECT_EQ(-dd, StreamRoundTrip<real_type>(-dd)) << index;

    // Avoid undefined behavior (overflow/underflow).
    if (dd <= std::numeric_limits<double>::max() &&
        dd >= std::numeric_limits<double>::lowest()) {
      double d = static_cast<double>(dd);
      EXPECT_EQ(d, StreamRoundTrip<double>(d));
    }

    // Avoid undefined behavior (overflow/underflow).
    if (dd <= static_cast<long double>(std::numeric_limits<int64_t>::max()) &&
        dd >=
            static_cast<long double>(std::numeric_limits<int64_t>::lowest())) {
      int64_t x = static_cast<int64_t>(dd);
      EXPECT_EQ(x, StreamRoundTrip<int64_t>(x));
    }
  }
}

TEST(StrToDTest, DoubleMin) {
  const char kV[] = "2.22507385850720138e-308";
  char* end;
  double x = std::strtod(kV, &end);
  EXPECT_EQ(std::numeric_limits<double>::min(), x);
  // errno may equal ERANGE.
}

TEST(StrToDTest, DoubleDenormMin) {
  const char kV[] = "4.94065645841246544e-324";
  char* end;
  double x = std::strtod(kV, &end);
  EXPECT_EQ(std::numeric_limits<double>::denorm_min(), x);
  // errno may equal ERANGE.
}

}  // namespace
