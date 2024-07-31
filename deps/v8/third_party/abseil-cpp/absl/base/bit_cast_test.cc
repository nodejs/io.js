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

// Unit test for bit_cast template.

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/base/macros.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

template <int N>
struct marshall { char buf[N]; };

template <typename T>
void TestMarshall(const T values[], int num_values) {
  for (int i = 0; i < num_values; ++i) {
    T t0 = values[i];
    marshall<sizeof(T)> m0 = absl::bit_cast<marshall<sizeof(T)> >(t0);
    T t1 = absl::bit_cast<T>(m0);
    marshall<sizeof(T)> m1 = absl::bit_cast<marshall<sizeof(T)> >(t1);
    ASSERT_EQ(0, memcmp(&t0, &t1, sizeof(T)));
    ASSERT_EQ(0, memcmp(&m0, &m1, sizeof(T)));
  }
}

// Convert back and forth to an integral type.  The C++ standard does
// not guarantee this will work, but we test that this works on all the
// platforms we support.
//
// Likewise, we below make assumptions about sizeof(float) and
// sizeof(double) which the standard does not guarantee, but which hold on the
// platforms we support.

template <typename T, typename I>
void TestIntegral(const T values[], int num_values) {
  for (int i = 0; i < num_values; ++i) {
    T t0 = values[i];
    I i0 = absl::bit_cast<I>(t0);
    T t1 = absl::bit_cast<T>(i0);
    I i1 = absl::bit_cast<I>(t1);
    ASSERT_EQ(0, memcmp(&t0, &t1, sizeof(T)));
    ASSERT_EQ(i0, i1);
  }
}

TEST(BitCast, Bool) {
  static const bool bool_list[] = { false, true };
  TestMarshall<bool>(bool_list, ABSL_ARRAYSIZE(bool_list));
}

TEST(BitCast, Int32) {
  static const int32_t int_list[] =
    { 0, 1, 100, 2147483647, -1, -100, -2147483647, -2147483647-1 };
  TestMarshall<int32_t>(int_list, ABSL_ARRAYSIZE(int_list));
}

TEST(BitCast, Int64) {
  static const int64_t int64_list[] =
    { 0, 1, 1LL << 40, -1, -(1LL<<40) };
  TestMarshall<int64_t>(int64_list, ABSL_ARRAYSIZE(int64_list));
}

TEST(BitCast, Uint64) {
  static const uint64_t uint64_list[] =
    { 0, 1, 1LLU << 40, 1LLU << 63 };
  TestMarshall<uint64_t>(uint64_list, ABSL_ARRAYSIZE(uint64_list));
}

TEST(BitCast, Float) {
  static const float float_list[] =
    { 0.0f, 1.0f, -1.0f, 10.0f, -10.0f,
      1e10f, 1e20f, 1e-10f, 1e-20f,
      2.71828f, 3.14159f };
  TestMarshall<float>(float_list, ABSL_ARRAYSIZE(float_list));
  TestIntegral<float, int>(float_list, ABSL_ARRAYSIZE(float_list));
  TestIntegral<float, unsigned>(float_list, ABSL_ARRAYSIZE(float_list));
}

TEST(BitCast, Double) {
  static const double double_list[] =
    { 0.0, 1.0, -1.0, 10.0, -10.0,
      1e10, 1e100, 1e-10, 1e-100,
      2.718281828459045,
      3.141592653589793238462643383279502884197169399375105820974944 };
  TestMarshall<double>(double_list, ABSL_ARRAYSIZE(double_list));
  TestIntegral<double, int64_t>(double_list, ABSL_ARRAYSIZE(double_list));
  TestIntegral<double, uint64_t>(double_list, ABSL_ARRAYSIZE(double_list));
}

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
