// Copyright 2018 The Abseil Authors.
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

#include "absl/types/compare.h"

#include "gtest/gtest.h"
#include "absl/base/casts.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

// This is necessary to avoid a bunch of lint warnings suggesting that we use
// EXPECT_EQ/etc., which doesn't work in this case because they convert the `0`
// to an int, which can't be converted to the unspecified zero type.
bool Identity(bool b) { return b; }

TEST(Compare, PartialOrdering) {
  EXPECT_TRUE(Identity(partial_ordering::less < 0));
  EXPECT_TRUE(Identity(0 > partial_ordering::less));
  EXPECT_TRUE(Identity(partial_ordering::less <= 0));
  EXPECT_TRUE(Identity(0 >= partial_ordering::less));
  EXPECT_TRUE(Identity(partial_ordering::equivalent == 0));
  EXPECT_TRUE(Identity(0 == partial_ordering::equivalent));
  EXPECT_TRUE(Identity(partial_ordering::greater > 0));
  EXPECT_TRUE(Identity(0 < partial_ordering::greater));
  EXPECT_TRUE(Identity(partial_ordering::greater >= 0));
  EXPECT_TRUE(Identity(0 <= partial_ordering::greater));
  EXPECT_TRUE(Identity(partial_ordering::unordered != 0));
  EXPECT_TRUE(Identity(0 != partial_ordering::unordered));
  EXPECT_FALSE(Identity(partial_ordering::unordered < 0));
  EXPECT_FALSE(Identity(0 < partial_ordering::unordered));
  EXPECT_FALSE(Identity(partial_ordering::unordered <= 0));
  EXPECT_FALSE(Identity(0 <= partial_ordering::unordered));
  EXPECT_FALSE(Identity(partial_ordering::unordered > 0));
  EXPECT_FALSE(Identity(0 > partial_ordering::unordered));
  EXPECT_FALSE(Identity(partial_ordering::unordered >= 0));
  EXPECT_FALSE(Identity(0 >= partial_ordering::unordered));
  const partial_ordering values[] = {
      partial_ordering::less, partial_ordering::equivalent,
      partial_ordering::greater, partial_ordering::unordered};
  for (const auto& lhs : values) {
    for (const auto& rhs : values) {
      const bool are_equal = &lhs == &rhs;
      EXPECT_EQ(lhs == rhs, are_equal);
      EXPECT_EQ(lhs != rhs, !are_equal);
    }
  }
}

TEST(Compare, WeakOrdering) {
  EXPECT_TRUE(Identity(weak_ordering::less < 0));
  EXPECT_TRUE(Identity(0 > weak_ordering::less));
  EXPECT_TRUE(Identity(weak_ordering::less <= 0));
  EXPECT_TRUE(Identity(0 >= weak_ordering::less));
  EXPECT_TRUE(Identity(weak_ordering::equivalent == 0));
  EXPECT_TRUE(Identity(0 == weak_ordering::equivalent));
  EXPECT_TRUE(Identity(weak_ordering::greater > 0));
  EXPECT_TRUE(Identity(0 < weak_ordering::greater));
  EXPECT_TRUE(Identity(weak_ordering::greater >= 0));
  EXPECT_TRUE(Identity(0 <= weak_ordering::greater));
  const weak_ordering values[] = {
      weak_ordering::less, weak_ordering::equivalent, weak_ordering::greater};
  for (const auto& lhs : values) {
    for (const auto& rhs : values) {
      const bool are_equal = &lhs == &rhs;
      EXPECT_EQ(lhs == rhs, are_equal);
      EXPECT_EQ(lhs != rhs, !are_equal);
    }
  }
}

TEST(Compare, StrongOrdering) {
  EXPECT_TRUE(Identity(strong_ordering::less < 0));
  EXPECT_TRUE(Identity(0 > strong_ordering::less));
  EXPECT_TRUE(Identity(strong_ordering::less <= 0));
  EXPECT_TRUE(Identity(0 >= strong_ordering::less));
  EXPECT_TRUE(Identity(strong_ordering::equal == 0));
  EXPECT_TRUE(Identity(0 == strong_ordering::equal));
  EXPECT_TRUE(Identity(strong_ordering::equivalent == 0));
  EXPECT_TRUE(Identity(0 == strong_ordering::equivalent));
  EXPECT_TRUE(Identity(strong_ordering::greater > 0));
  EXPECT_TRUE(Identity(0 < strong_ordering::greater));
  EXPECT_TRUE(Identity(strong_ordering::greater >= 0));
  EXPECT_TRUE(Identity(0 <= strong_ordering::greater));
  const strong_ordering values[] = {
      strong_ordering::less, strong_ordering::equal, strong_ordering::greater};
  for (const auto& lhs : values) {
    for (const auto& rhs : values) {
      const bool are_equal = &lhs == &rhs;
      EXPECT_EQ(lhs == rhs, are_equal);
      EXPECT_EQ(lhs != rhs, !are_equal);
    }
  }
  EXPECT_TRUE(Identity(strong_ordering::equivalent == strong_ordering::equal));
}

TEST(Compare, Conversions) {
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(weak_ordering::less) != 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(weak_ordering::less) < 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(weak_ordering::less) <= 0));
  EXPECT_TRUE(Identity(
      implicit_cast<partial_ordering>(weak_ordering::equivalent) == 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(weak_ordering::greater) != 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(weak_ordering::greater) > 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(weak_ordering::greater) >= 0));

  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(strong_ordering::less) != 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(strong_ordering::less) < 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(strong_ordering::less) <= 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(strong_ordering::equal) == 0));
  EXPECT_TRUE(Identity(
      implicit_cast<partial_ordering>(strong_ordering::equivalent) == 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(strong_ordering::greater) != 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(strong_ordering::greater) > 0));
  EXPECT_TRUE(
      Identity(implicit_cast<partial_ordering>(strong_ordering::greater) >= 0));

  EXPECT_TRUE(
      Identity(implicit_cast<weak_ordering>(strong_ordering::less) != 0));
  EXPECT_TRUE(
      Identity(implicit_cast<weak_ordering>(strong_ordering::less) < 0));
  EXPECT_TRUE(
      Identity(implicit_cast<weak_ordering>(strong_ordering::less) <= 0));
  EXPECT_TRUE(
      Identity(implicit_cast<weak_ordering>(strong_ordering::equal) == 0));
  EXPECT_TRUE(
      Identity(implicit_cast<weak_ordering>(strong_ordering::equivalent) == 0));
  EXPECT_TRUE(
      Identity(implicit_cast<weak_ordering>(strong_ordering::greater) != 0));
  EXPECT_TRUE(
      Identity(implicit_cast<weak_ordering>(strong_ordering::greater) > 0));
  EXPECT_TRUE(
      Identity(implicit_cast<weak_ordering>(strong_ordering::greater) >= 0));
}

struct WeakOrderingLess {
  template <typename T>
  absl::weak_ordering operator()(const T& a, const T& b) const {
    return a < b ? absl::weak_ordering::less
                 : a == b ? absl::weak_ordering::equivalent
                          : absl::weak_ordering::greater;
  }
};

TEST(CompareResultAsLessThan, SanityTest) {
  EXPECT_FALSE(absl::compare_internal::compare_result_as_less_than(false));
  EXPECT_TRUE(absl::compare_internal::compare_result_as_less_than(true));

  EXPECT_TRUE(
      absl::compare_internal::compare_result_as_less_than(weak_ordering::less));
  EXPECT_FALSE(absl::compare_internal::compare_result_as_less_than(
      weak_ordering::equivalent));
  EXPECT_FALSE(absl::compare_internal::compare_result_as_less_than(
      weak_ordering::greater));
}

TEST(DoLessThanComparison, SanityTest) {
  std::less<int> less;
  WeakOrderingLess weak;

  EXPECT_TRUE(absl::compare_internal::do_less_than_comparison(less, -1, 0));
  EXPECT_TRUE(absl::compare_internal::do_less_than_comparison(weak, -1, 0));

  EXPECT_FALSE(absl::compare_internal::do_less_than_comparison(less, 10, 10));
  EXPECT_FALSE(absl::compare_internal::do_less_than_comparison(weak, 10, 10));

  EXPECT_FALSE(absl::compare_internal::do_less_than_comparison(less, 10, 5));
  EXPECT_FALSE(absl::compare_internal::do_less_than_comparison(weak, 10, 5));
}

TEST(CompareResultAsOrdering, SanityTest) {
  EXPECT_TRUE(
      Identity(absl::compare_internal::compare_result_as_ordering(-1) < 0));
  EXPECT_FALSE(
      Identity(absl::compare_internal::compare_result_as_ordering(-1) == 0));
  EXPECT_FALSE(
      Identity(absl::compare_internal::compare_result_as_ordering(-1) > 0));
  EXPECT_TRUE(Identity(absl::compare_internal::compare_result_as_ordering(
                           weak_ordering::less) < 0));
  EXPECT_FALSE(Identity(absl::compare_internal::compare_result_as_ordering(
                            weak_ordering::less) == 0));
  EXPECT_FALSE(Identity(absl::compare_internal::compare_result_as_ordering(
                            weak_ordering::less) > 0));

  EXPECT_FALSE(
      Identity(absl::compare_internal::compare_result_as_ordering(0) < 0));
  EXPECT_TRUE(
      Identity(absl::compare_internal::compare_result_as_ordering(0) == 0));
  EXPECT_FALSE(
      Identity(absl::compare_internal::compare_result_as_ordering(0) > 0));
  EXPECT_FALSE(Identity(absl::compare_internal::compare_result_as_ordering(
                            weak_ordering::equivalent) < 0));
  EXPECT_TRUE(Identity(absl::compare_internal::compare_result_as_ordering(
                           weak_ordering::equivalent) == 0));
  EXPECT_FALSE(Identity(absl::compare_internal::compare_result_as_ordering(
                            weak_ordering::equivalent) > 0));

  EXPECT_FALSE(
      Identity(absl::compare_internal::compare_result_as_ordering(1) < 0));
  EXPECT_FALSE(
      Identity(absl::compare_internal::compare_result_as_ordering(1) == 0));
  EXPECT_TRUE(
      Identity(absl::compare_internal::compare_result_as_ordering(1) > 0));
  EXPECT_FALSE(Identity(absl::compare_internal::compare_result_as_ordering(
                            weak_ordering::greater) < 0));
  EXPECT_FALSE(Identity(absl::compare_internal::compare_result_as_ordering(
                            weak_ordering::greater) == 0));
  EXPECT_TRUE(Identity(absl::compare_internal::compare_result_as_ordering(
                           weak_ordering::greater) > 0));
}

TEST(DoThreeWayComparison, SanityTest) {
  std::less<int> less;
  WeakOrderingLess weak;

  EXPECT_TRUE(Identity(
      absl::compare_internal::do_three_way_comparison(less, -1, 0) < 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(less, -1, 0) == 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(less, -1, 0) > 0));
  EXPECT_TRUE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, -1, 0) < 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, -1, 0) == 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, -1, 0) > 0));

  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(less, 10, 10) < 0));
  EXPECT_TRUE(Identity(
      absl::compare_internal::do_three_way_comparison(less, 10, 10) == 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(less, 10, 10) > 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, 10, 10) < 0));
  EXPECT_TRUE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, 10, 10) == 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, 10, 10) > 0));

  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(less, 10, 5) < 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(less, 10, 5) == 0));
  EXPECT_TRUE(Identity(
      absl::compare_internal::do_three_way_comparison(less, 10, 5) > 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, 10, 5) < 0));
  EXPECT_FALSE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, 10, 5) == 0));
  EXPECT_TRUE(Identity(
      absl::compare_internal::do_three_way_comparison(weak, 10, 5) > 0));
}

#ifdef __cpp_inline_variables
TEST(Compare, StaticAsserts) {
  static_assert(partial_ordering::less < 0, "");
  static_assert(partial_ordering::equivalent == 0, "");
  static_assert(partial_ordering::greater > 0, "");
  static_assert(partial_ordering::unordered != 0, "");

  static_assert(weak_ordering::less < 0, "");
  static_assert(weak_ordering::equivalent == 0, "");
  static_assert(weak_ordering::greater > 0, "");

  static_assert(strong_ordering::less < 0, "");
  static_assert(strong_ordering::equal == 0, "");
  static_assert(strong_ordering::equivalent == 0, "");
  static_assert(strong_ordering::greater > 0, "");
}
#endif  // __cpp_inline_variables

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
