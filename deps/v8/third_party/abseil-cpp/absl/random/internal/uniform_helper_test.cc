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

#include "absl/random/internal/uniform_helper.h"

#include <cmath>
#include <cstdint>
#include <random>

#include "gtest/gtest.h"

namespace {

using absl::IntervalClosedClosedTag;
using absl::IntervalClosedOpenTag;
using absl::IntervalOpenClosedTag;
using absl::IntervalOpenOpenTag;
using absl::random_internal::uniform_inferred_return_t;
using absl::random_internal::uniform_lower_bound;
using absl::random_internal::uniform_upper_bound;

class UniformHelperTest : public testing::Test {};

TEST_F(UniformHelperTest, UniformBoundFunctionsGeneral) {
  constexpr IntervalClosedClosedTag IntervalClosedClosed;
  constexpr IntervalClosedOpenTag IntervalClosedOpen;
  constexpr IntervalOpenClosedTag IntervalOpenClosed;
  constexpr IntervalOpenOpenTag IntervalOpenOpen;

  // absl::uniform_int_distribution natively assumes IntervalClosedClosed
  // absl::uniform_real_distribution natively assumes IntervalClosedOpen

  EXPECT_EQ(uniform_lower_bound(IntervalOpenClosed, 0, 100), 1);
  EXPECT_EQ(uniform_lower_bound(IntervalOpenOpen, 0, 100), 1);
  EXPECT_GT(uniform_lower_bound<float>(IntervalOpenClosed, 0, 1.0), 0);
  EXPECT_GT(uniform_lower_bound<float>(IntervalOpenOpen, 0, 1.0), 0);
  EXPECT_GT(uniform_lower_bound<double>(IntervalOpenClosed, 0, 1.0), 0);
  EXPECT_GT(uniform_lower_bound<double>(IntervalOpenOpen, 0, 1.0), 0);

  EXPECT_EQ(uniform_lower_bound(IntervalClosedClosed, 0, 100), 0);
  EXPECT_EQ(uniform_lower_bound(IntervalClosedOpen, 0, 100), 0);
  EXPECT_EQ(uniform_lower_bound<float>(IntervalClosedClosed, 0, 1.0), 0);
  EXPECT_EQ(uniform_lower_bound<float>(IntervalClosedOpen, 0, 1.0), 0);
  EXPECT_EQ(uniform_lower_bound<double>(IntervalClosedClosed, 0, 1.0), 0);
  EXPECT_EQ(uniform_lower_bound<double>(IntervalClosedOpen, 0, 1.0), 0);

  EXPECT_EQ(uniform_upper_bound(IntervalOpenOpen, 0, 100), 99);
  EXPECT_EQ(uniform_upper_bound(IntervalClosedOpen, 0, 100), 99);
  EXPECT_EQ(uniform_upper_bound<float>(IntervalOpenOpen, 0, 1.0), 1.0);
  EXPECT_EQ(uniform_upper_bound<float>(IntervalClosedOpen, 0, 1.0), 1.0);
  EXPECT_EQ(uniform_upper_bound<double>(IntervalOpenOpen, 0, 1.0), 1.0);
  EXPECT_EQ(uniform_upper_bound<double>(IntervalClosedOpen, 0, 1.0), 1.0);

  EXPECT_EQ(uniform_upper_bound(IntervalOpenClosed, 0, 100), 100);
  EXPECT_EQ(uniform_upper_bound(IntervalClosedClosed, 0, 100), 100);
  EXPECT_GT(uniform_upper_bound<float>(IntervalOpenClosed, 0, 1.0), 1.0);
  EXPECT_GT(uniform_upper_bound<float>(IntervalClosedClosed, 0, 1.0), 1.0);
  EXPECT_GT(uniform_upper_bound<double>(IntervalOpenClosed, 0, 1.0), 1.0);
  EXPECT_GT(uniform_upper_bound<double>(IntervalClosedClosed, 0, 1.0), 1.0);

  // Negative value tests
  EXPECT_EQ(uniform_lower_bound(IntervalOpenClosed, -100, -1), -99);
  EXPECT_EQ(uniform_lower_bound(IntervalOpenOpen, -100, -1), -99);
  EXPECT_GT(uniform_lower_bound<float>(IntervalOpenClosed, -2.0, -1.0), -2.0);
  EXPECT_GT(uniform_lower_bound<float>(IntervalOpenOpen, -2.0, -1.0), -2.0);
  EXPECT_GT(uniform_lower_bound<double>(IntervalOpenClosed, -2.0, -1.0), -2.0);
  EXPECT_GT(uniform_lower_bound<double>(IntervalOpenOpen, -2.0, -1.0), -2.0);

  EXPECT_EQ(uniform_lower_bound(IntervalClosedClosed, -100, -1), -100);
  EXPECT_EQ(uniform_lower_bound(IntervalClosedOpen, -100, -1), -100);
  EXPECT_EQ(uniform_lower_bound<float>(IntervalClosedClosed, -2.0, -1.0), -2.0);
  EXPECT_EQ(uniform_lower_bound<float>(IntervalClosedOpen, -2.0, -1.0), -2.0);
  EXPECT_EQ(uniform_lower_bound<double>(IntervalClosedClosed, -2.0, -1.0),
            -2.0);
  EXPECT_EQ(uniform_lower_bound<double>(IntervalClosedOpen, -2.0, -1.0), -2.0);

  EXPECT_EQ(uniform_upper_bound(IntervalOpenOpen, -100, -1), -2);
  EXPECT_EQ(uniform_upper_bound(IntervalClosedOpen, -100, -1), -2);
  EXPECT_EQ(uniform_upper_bound<float>(IntervalOpenOpen, -2.0, -1.0), -1.0);
  EXPECT_EQ(uniform_upper_bound<float>(IntervalClosedOpen, -2.0, -1.0), -1.0);
  EXPECT_EQ(uniform_upper_bound<double>(IntervalOpenOpen, -2.0, -1.0), -1.0);
  EXPECT_EQ(uniform_upper_bound<double>(IntervalClosedOpen, -2.0, -1.0), -1.0);

  EXPECT_EQ(uniform_upper_bound(IntervalOpenClosed, -100, -1), -1);
  EXPECT_EQ(uniform_upper_bound(IntervalClosedClosed, -100, -1), -1);
  EXPECT_GT(uniform_upper_bound<float>(IntervalOpenClosed, -2.0, -1.0), -1.0);
  EXPECT_GT(uniform_upper_bound<float>(IntervalClosedClosed, -2.0, -1.0), -1.0);
  EXPECT_GT(uniform_upper_bound<double>(IntervalOpenClosed, -2.0, -1.0), -1.0);
  EXPECT_GT(uniform_upper_bound<double>(IntervalClosedClosed, -2.0, -1.0),
            -1.0);

  EXPECT_GT(uniform_lower_bound(IntervalOpenClosed, 1.0, 2.0), 1.0);
  EXPECT_LT(uniform_lower_bound(IntervalOpenClosed, 1.0, +0.0), 1.0);
  EXPECT_LT(uniform_lower_bound(IntervalOpenClosed, 1.0, -0.0), 1.0);
  EXPECT_LT(uniform_lower_bound(IntervalOpenClosed, 1.0, -1.0), 1.0);
}

TEST_F(UniformHelperTest, UniformBoundFunctionsIntBounds) {
  // Verifies the saturating nature of uniform_lower_bound and
  // uniform_upper_bound
  constexpr IntervalOpenOpenTag IntervalOpenOpen;

  // uint max.
  constexpr auto m = (std::numeric_limits<uint64_t>::max)();

  EXPECT_EQ(1, uniform_lower_bound(IntervalOpenOpen, 0u, 0u));
  EXPECT_EQ(m, uniform_lower_bound(IntervalOpenOpen, m, m));
  EXPECT_EQ(m, uniform_lower_bound(IntervalOpenOpen, m - 1, m - 1));
  EXPECT_EQ(0, uniform_upper_bound(IntervalOpenOpen, 0u, 0u));
  EXPECT_EQ(m - 1, uniform_upper_bound(IntervalOpenOpen, m, m));

  // int min/max
  constexpr auto l = (std::numeric_limits<int64_t>::min)();
  constexpr auto r = (std::numeric_limits<int64_t>::max)();
  EXPECT_EQ(1, uniform_lower_bound(IntervalOpenOpen, 0, 0));
  EXPECT_EQ(l + 1, uniform_lower_bound(IntervalOpenOpen, l, l));
  EXPECT_EQ(r, uniform_lower_bound(IntervalOpenOpen, r - 1, r - 1));
  EXPECT_EQ(r, uniform_lower_bound(IntervalOpenOpen, r, r));
  EXPECT_EQ(-1, uniform_upper_bound(IntervalOpenOpen, 0, 0));
  EXPECT_EQ(l, uniform_upper_bound(IntervalOpenOpen, l, l));
  EXPECT_EQ(r - 1, uniform_upper_bound(IntervalOpenOpen, r, r));
}

TEST_F(UniformHelperTest, UniformBoundFunctionsRealBounds) {
  // absl::uniform_real_distribution natively assumes IntervalClosedOpen;
  // use the inverse here so each bound has to change.
  constexpr IntervalOpenClosedTag IntervalOpenClosed;

  // Edge cases: the next value toward itself is itself.
  EXPECT_EQ(1.0, uniform_lower_bound(IntervalOpenClosed, 1.0, 1.0));
  EXPECT_EQ(1.0f, uniform_lower_bound(IntervalOpenClosed, 1.0f, 1.0f));

  // rightmost and leftmost finite values.
  constexpr auto r = (std::numeric_limits<double>::max)();
  const auto re = std::nexttoward(r, 0.0);
  constexpr auto l = -r;
  const auto le = std::nexttoward(l, 0.0);

  EXPECT_EQ(l, uniform_lower_bound(IntervalOpenClosed, l, l));     // (l,l)
  EXPECT_EQ(r, uniform_lower_bound(IntervalOpenClosed, r, r));     // (r,r)
  EXPECT_EQ(le, uniform_lower_bound(IntervalOpenClosed, l, r));    // (l,r)
  EXPECT_EQ(le, uniform_lower_bound(IntervalOpenClosed, l, 0.0));  // (l, 0)
  EXPECT_EQ(le, uniform_lower_bound(IntervalOpenClosed, l, le));   // (l, le)
  EXPECT_EQ(r, uniform_lower_bound(IntervalOpenClosed, re, r));    // (re, r)

  EXPECT_EQ(le, uniform_upper_bound(IntervalOpenClosed, l, l));   // (l,l)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, r, r));    // (r,r)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, l, r));    // (l,r)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, l, re));   // (l,re)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, 0.0, r));  // (0, r)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, re, r));   // (re, r)
  EXPECT_EQ(r, uniform_upper_bound(IntervalOpenClosed, le, re));  // (le, re)

  const double e = std::nextafter(1.0, 2.0);  // 1 + epsilon
  const double f = std::nextafter(1.0, 0.0);  // 1 - epsilon

  // (1.0, 1.0 + epsilon)
  EXPECT_EQ(e, uniform_lower_bound(IntervalOpenClosed, 1.0, e));
  EXPECT_EQ(std::nextafter(e, 2.0),
            uniform_upper_bound(IntervalOpenClosed, 1.0, e));

  // (1.0-epsilon, 1.0)
  EXPECT_EQ(1.0, uniform_lower_bound(IntervalOpenClosed, f, 1.0));
  EXPECT_EQ(e, uniform_upper_bound(IntervalOpenClosed, f, 1.0));

  // denorm cases.
  const double g = std::numeric_limits<double>::denorm_min();
  const double h = std::nextafter(g, 1.0);

  // (0, denorm_min)
  EXPECT_EQ(g, uniform_lower_bound(IntervalOpenClosed, 0.0, g));
  EXPECT_EQ(h, uniform_upper_bound(IntervalOpenClosed, 0.0, g));

  // (denorm_min, 1.0)
  EXPECT_EQ(h, uniform_lower_bound(IntervalOpenClosed, g, 1.0));
  EXPECT_EQ(e, uniform_upper_bound(IntervalOpenClosed, g, 1.0));

  // Edge cases: invalid bounds.
  EXPECT_EQ(f, uniform_lower_bound(IntervalOpenClosed, 1.0, -1.0));
}

struct Invalid {};

template <typename A, typename B>
auto InferredUniformReturnT(int) -> uniform_inferred_return_t<A, B>;

template <typename, typename>
Invalid InferredUniformReturnT(...);

// Given types <A, B, Expect>, CheckArgsInferType() verifies that
//
//   uniform_inferred_return_t<A, B> and
//   uniform_inferred_return_t<B, A>
//
// returns the type "Expect".
//
// This interface can also be used to assert that a given inferred return types
// are invalid. Writing:
//
//   CheckArgsInferType<float, int, Invalid>()
//
// will assert that this overload does not exist.
template <typename A, typename B, typename Expect>
void CheckArgsInferType() {
  static_assert(
      absl::conjunction<
          std::is_same<Expect, decltype(InferredUniformReturnT<A, B>(0))>,
          std::is_same<Expect,
                       decltype(InferredUniformReturnT<B, A>(0))>>::value,
      "");
}

TEST_F(UniformHelperTest, UniformTypeInference) {
  // Infers common types.
  CheckArgsInferType<uint16_t, uint16_t, uint16_t>();
  CheckArgsInferType<uint32_t, uint32_t, uint32_t>();
  CheckArgsInferType<uint64_t, uint64_t, uint64_t>();
  CheckArgsInferType<int16_t, int16_t, int16_t>();
  CheckArgsInferType<int32_t, int32_t, int32_t>();
  CheckArgsInferType<int64_t, int64_t, int64_t>();
  CheckArgsInferType<float, float, float>();
  CheckArgsInferType<double, double, double>();

  // Properly promotes uint16_t.
  CheckArgsInferType<uint16_t, uint32_t, uint32_t>();
  CheckArgsInferType<uint16_t, uint64_t, uint64_t>();
  CheckArgsInferType<uint16_t, int32_t, int32_t>();
  CheckArgsInferType<uint16_t, int64_t, int64_t>();
  CheckArgsInferType<uint16_t, float, float>();
  CheckArgsInferType<uint16_t, double, double>();

  // Properly promotes int16_t.
  CheckArgsInferType<int16_t, int32_t, int32_t>();
  CheckArgsInferType<int16_t, int64_t, int64_t>();
  CheckArgsInferType<int16_t, float, float>();
  CheckArgsInferType<int16_t, double, double>();

  // Invalid (u)int16_t-pairings do not compile.
  // See "CheckArgsInferType" comments above, for how this is achieved.
  CheckArgsInferType<uint16_t, int16_t, Invalid>();
  CheckArgsInferType<int16_t, uint32_t, Invalid>();
  CheckArgsInferType<int16_t, uint64_t, Invalid>();

  // Properly promotes uint32_t.
  CheckArgsInferType<uint32_t, uint64_t, uint64_t>();
  CheckArgsInferType<uint32_t, int64_t, int64_t>();
  CheckArgsInferType<uint32_t, double, double>();

  // Properly promotes int32_t.
  CheckArgsInferType<int32_t, int64_t, int64_t>();
  CheckArgsInferType<int32_t, double, double>();

  // Invalid (u)int32_t-pairings do not compile.
  CheckArgsInferType<uint32_t, int32_t, Invalid>();
  CheckArgsInferType<int32_t, uint64_t, Invalid>();
  CheckArgsInferType<int32_t, float, Invalid>();
  CheckArgsInferType<uint32_t, float, Invalid>();

  // Invalid (u)int64_t-pairings do not compile.
  CheckArgsInferType<uint64_t, int64_t, Invalid>();
  CheckArgsInferType<int64_t, float, Invalid>();
  CheckArgsInferType<int64_t, double, Invalid>();

  // Properly promotes float.
  CheckArgsInferType<float, double, double>();
}

}  // namespace
