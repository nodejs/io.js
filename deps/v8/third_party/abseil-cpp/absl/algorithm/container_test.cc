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

#include "absl/algorithm/container.h"

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <ostream>
#include <random>
#include <set>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/memory/memory.h"
#include "absl/types/span.h"

namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Gt;
using ::testing::IsNull;
using ::testing::IsSubsetOf;
using ::testing::Lt;
using ::testing::Pointee;
using ::testing::SizeIs;
using ::testing::Truly;
using ::testing::UnorderedElementsAre;

// Most of these tests just check that the code compiles, not that it
// does the right thing. That's fine since the functions just forward
// to the STL implementation.
class NonMutatingTest : public testing::Test {
 protected:
  std::unordered_set<int> container_ = {1, 2, 3};
  std::list<int> sequence_ = {1, 2, 3};
  std::vector<int> vector_ = {1, 2, 3};
  int array_[3] = {1, 2, 3};
};

struct AccumulateCalls {
  void operator()(int value) { calls.push_back(value); }
  std::vector<int> calls;
};

bool Predicate(int value) { return value < 3; }
bool BinPredicate(int v1, int v2) { return v1 < v2; }
bool Equals(int v1, int v2) { return v1 == v2; }
bool IsOdd(int x) { return x % 2 != 0; }

TEST_F(NonMutatingTest, Distance) {
  EXPECT_EQ(container_.size(),
            static_cast<size_t>(absl::c_distance(container_)));
  EXPECT_EQ(sequence_.size(), static_cast<size_t>(absl::c_distance(sequence_)));
  EXPECT_EQ(vector_.size(), static_cast<size_t>(absl::c_distance(vector_)));
  EXPECT_EQ(ABSL_ARRAYSIZE(array_),
            static_cast<size_t>(absl::c_distance(array_)));

  // Works with a temporary argument.
  EXPECT_EQ(vector_.size(),
            static_cast<size_t>(absl::c_distance(std::vector<int>(vector_))));
}

TEST_F(NonMutatingTest, Distance_OverloadedBeginEnd) {
  // Works with classes which have custom ADL-selected overloads of std::begin
  // and std::end.
  std::initializer_list<int> a = {1, 2, 3};
  std::valarray<int> b = {1, 2, 3};
  EXPECT_EQ(3, absl::c_distance(a));
  EXPECT_EQ(3, absl::c_distance(b));

  // It is assumed that other c_* functions use the same mechanism for
  // ADL-selecting begin/end overloads.
}

TEST_F(NonMutatingTest, ForEach) {
  AccumulateCalls c = absl::c_for_each(container_, AccumulateCalls());
  // Don't rely on the unordered_set's order.
  std::sort(c.calls.begin(), c.calls.end());
  EXPECT_EQ(vector_, c.calls);

  // Works with temporary container, too.
  AccumulateCalls c2 =
      absl::c_for_each(std::unordered_set<int>(container_), AccumulateCalls());
  std::sort(c2.calls.begin(), c2.calls.end());
  EXPECT_EQ(vector_, c2.calls);
}

TEST_F(NonMutatingTest, FindReturnsCorrectType) {
  auto it = absl::c_find(container_, 3);
  EXPECT_EQ(3, *it);
  absl::c_find(absl::implicit_cast<const std::list<int>&>(sequence_), 3);
}

TEST_F(NonMutatingTest, Contains) {
  EXPECT_TRUE(absl::c_contains(container_, 3));
  EXPECT_FALSE(absl::c_contains(container_, 4));
}

TEST_F(NonMutatingTest, FindIf) { absl::c_find_if(container_, Predicate); }

TEST_F(NonMutatingTest, FindIfNot) {
  absl::c_find_if_not(container_, Predicate);
}

TEST_F(NonMutatingTest, FindEnd) {
  absl::c_find_end(sequence_, vector_);
  absl::c_find_end(vector_, sequence_);
}

TEST_F(NonMutatingTest, FindEndWithPredicate) {
  absl::c_find_end(sequence_, vector_, BinPredicate);
  absl::c_find_end(vector_, sequence_, BinPredicate);
}

TEST_F(NonMutatingTest, FindFirstOf) {
  absl::c_find_first_of(container_, sequence_);
  absl::c_find_first_of(sequence_, container_);
}

TEST_F(NonMutatingTest, FindFirstOfWithPredicate) {
  absl::c_find_first_of(container_, sequence_, BinPredicate);
  absl::c_find_first_of(sequence_, container_, BinPredicate);
}

TEST_F(NonMutatingTest, AdjacentFind) { absl::c_adjacent_find(sequence_); }

TEST_F(NonMutatingTest, AdjacentFindWithPredicate) {
  absl::c_adjacent_find(sequence_, BinPredicate);
}

TEST_F(NonMutatingTest, Count) { EXPECT_EQ(1, absl::c_count(container_, 3)); }

TEST_F(NonMutatingTest, CountIf) {
  EXPECT_EQ(2, absl::c_count_if(container_, Predicate));
  const std::unordered_set<int>& const_container = container_;
  EXPECT_EQ(2, absl::c_count_if(const_container, Predicate));
}

TEST_F(NonMutatingTest, Mismatch) {
  // Testing necessary as absl::c_mismatch executes logic.
  {
    auto result = absl::c_mismatch(vector_, sequence_);
    EXPECT_EQ(result.first, vector_.end());
    EXPECT_EQ(result.second, sequence_.end());
  }
  {
    auto result = absl::c_mismatch(sequence_, vector_);
    EXPECT_EQ(result.first, sequence_.end());
    EXPECT_EQ(result.second, vector_.end());
  }

  sequence_.back() = 5;
  {
    auto result = absl::c_mismatch(vector_, sequence_);
    EXPECT_EQ(result.first, std::prev(vector_.end()));
    EXPECT_EQ(result.second, std::prev(sequence_.end()));
  }
  {
    auto result = absl::c_mismatch(sequence_, vector_);
    EXPECT_EQ(result.first, std::prev(sequence_.end()));
    EXPECT_EQ(result.second, std::prev(vector_.end()));
  }

  sequence_.pop_back();
  {
    auto result = absl::c_mismatch(vector_, sequence_);
    EXPECT_EQ(result.first, std::prev(vector_.end()));
    EXPECT_EQ(result.second, sequence_.end());
  }
  {
    auto result = absl::c_mismatch(sequence_, vector_);
    EXPECT_EQ(result.first, sequence_.end());
    EXPECT_EQ(result.second, std::prev(vector_.end()));
  }
  {
    struct NoNotEquals {
      constexpr bool operator==(NoNotEquals) const { return true; }
      constexpr bool operator!=(NoNotEquals) const = delete;
    };
    std::vector<NoNotEquals> first;
    std::list<NoNotEquals> second;

    // Check this still compiles.
    absl::c_mismatch(first, second);
  }
}

TEST_F(NonMutatingTest, MismatchWithPredicate) {
  // Testing necessary as absl::c_mismatch executes logic.
  {
    auto result = absl::c_mismatch(vector_, sequence_, BinPredicate);
    EXPECT_EQ(result.first, vector_.begin());
    EXPECT_EQ(result.second, sequence_.begin());
  }
  {
    auto result = absl::c_mismatch(sequence_, vector_, BinPredicate);
    EXPECT_EQ(result.first, sequence_.begin());
    EXPECT_EQ(result.second, vector_.begin());
  }

  sequence_.front() = 0;
  {
    auto result = absl::c_mismatch(vector_, sequence_, BinPredicate);
    EXPECT_EQ(result.first, vector_.begin());
    EXPECT_EQ(result.second, sequence_.begin());
  }
  {
    auto result = absl::c_mismatch(sequence_, vector_, BinPredicate);
    EXPECT_EQ(result.first, std::next(sequence_.begin()));
    EXPECT_EQ(result.second, std::next(vector_.begin()));
  }

  sequence_.clear();
  {
    auto result = absl::c_mismatch(vector_, sequence_, BinPredicate);
    EXPECT_EQ(result.first, vector_.begin());
    EXPECT_EQ(result.second, sequence_.end());
  }
  {
    auto result = absl::c_mismatch(sequence_, vector_, BinPredicate);
    EXPECT_EQ(result.first, sequence_.end());
    EXPECT_EQ(result.second, vector_.begin());
  }
}

TEST_F(NonMutatingTest, Equal) {
  EXPECT_TRUE(absl::c_equal(vector_, sequence_));
  EXPECT_TRUE(absl::c_equal(sequence_, vector_));
  EXPECT_TRUE(absl::c_equal(sequence_, array_));
  EXPECT_TRUE(absl::c_equal(array_, vector_));

  // Test that behavior appropriately differs from that of equal().
  std::vector<int> vector_plus = {1, 2, 3};
  vector_plus.push_back(4);
  EXPECT_FALSE(absl::c_equal(vector_plus, sequence_));
  EXPECT_FALSE(absl::c_equal(sequence_, vector_plus));
  EXPECT_FALSE(absl::c_equal(array_, vector_plus));
}

TEST_F(NonMutatingTest, EqualWithPredicate) {
  EXPECT_TRUE(absl::c_equal(vector_, sequence_, Equals));
  EXPECT_TRUE(absl::c_equal(sequence_, vector_, Equals));
  EXPECT_TRUE(absl::c_equal(array_, sequence_, Equals));
  EXPECT_TRUE(absl::c_equal(vector_, array_, Equals));

  // Test that behavior appropriately differs from that of equal().
  std::vector<int> vector_plus = {1, 2, 3};
  vector_plus.push_back(4);
  EXPECT_FALSE(absl::c_equal(vector_plus, sequence_, Equals));
  EXPECT_FALSE(absl::c_equal(sequence_, vector_plus, Equals));
  EXPECT_FALSE(absl::c_equal(vector_plus, array_, Equals));
}

TEST_F(NonMutatingTest, IsPermutation) {
  auto vector_permut_ = vector_;
  std::next_permutation(vector_permut_.begin(), vector_permut_.end());
  EXPECT_TRUE(absl::c_is_permutation(vector_permut_, sequence_));
  EXPECT_TRUE(absl::c_is_permutation(sequence_, vector_permut_));

  // Test that behavior appropriately differs from that of is_permutation().
  std::vector<int> vector_plus = {1, 2, 3};
  vector_plus.push_back(4);
  EXPECT_FALSE(absl::c_is_permutation(vector_plus, sequence_));
  EXPECT_FALSE(absl::c_is_permutation(sequence_, vector_plus));
}

TEST_F(NonMutatingTest, IsPermutationWithPredicate) {
  auto vector_permut_ = vector_;
  std::next_permutation(vector_permut_.begin(), vector_permut_.end());
  EXPECT_TRUE(absl::c_is_permutation(vector_permut_, sequence_, Equals));
  EXPECT_TRUE(absl::c_is_permutation(sequence_, vector_permut_, Equals));

  // Test that behavior appropriately differs from that of is_permutation().
  std::vector<int> vector_plus = {1, 2, 3};
  vector_plus.push_back(4);
  EXPECT_FALSE(absl::c_is_permutation(vector_plus, sequence_, Equals));
  EXPECT_FALSE(absl::c_is_permutation(sequence_, vector_plus, Equals));
}

TEST_F(NonMutatingTest, Search) {
  absl::c_search(sequence_, vector_);
  absl::c_search(vector_, sequence_);
  absl::c_search(array_, sequence_);
}

TEST_F(NonMutatingTest, SearchWithPredicate) {
  absl::c_search(sequence_, vector_, BinPredicate);
  absl::c_search(vector_, sequence_, BinPredicate);
}

TEST_F(NonMutatingTest, ContainsSubrange) {
  EXPECT_TRUE(absl::c_contains_subrange(sequence_, vector_));
  EXPECT_TRUE(absl::c_contains_subrange(vector_, sequence_));
  EXPECT_TRUE(absl::c_contains_subrange(array_, sequence_));
}

TEST_F(NonMutatingTest, ContainsSubrangeWithPredicate) {
  EXPECT_TRUE(absl::c_contains_subrange(sequence_, vector_, Equals));
  EXPECT_TRUE(absl::c_contains_subrange(vector_, sequence_, Equals));
}

TEST_F(NonMutatingTest, SearchN) { absl::c_search_n(sequence_, 3, 1); }

TEST_F(NonMutatingTest, SearchNWithPredicate) {
  absl::c_search_n(sequence_, 3, 1, BinPredicate);
}

TEST_F(NonMutatingTest, LowerBound) {
  std::list<int>::iterator i = absl::c_lower_bound(sequence_, 3);
  ASSERT_TRUE(i != sequence_.end());
  EXPECT_EQ(2, std::distance(sequence_.begin(), i));
  EXPECT_EQ(3, *i);
}

TEST_F(NonMutatingTest, LowerBoundWithPredicate) {
  std::vector<int> v(vector_);
  std::sort(v.begin(), v.end(), std::greater<int>());
  std::vector<int>::iterator i = absl::c_lower_bound(v, 3, std::greater<int>());
  EXPECT_TRUE(i == v.begin());
  EXPECT_EQ(3, *i);
}

TEST_F(NonMutatingTest, UpperBound) {
  std::list<int>::iterator i = absl::c_upper_bound(sequence_, 1);
  ASSERT_TRUE(i != sequence_.end());
  EXPECT_EQ(1, std::distance(sequence_.begin(), i));
  EXPECT_EQ(2, *i);
}

TEST_F(NonMutatingTest, UpperBoundWithPredicate) {
  std::vector<int> v(vector_);
  std::sort(v.begin(), v.end(), std::greater<int>());
  std::vector<int>::iterator i = absl::c_upper_bound(v, 1, std::greater<int>());
  EXPECT_EQ(3, i - v.begin());
  EXPECT_TRUE(i == v.end());
}

TEST_F(NonMutatingTest, EqualRange) {
  std::pair<std::list<int>::iterator, std::list<int>::iterator> p =
      absl::c_equal_range(sequence_, 2);
  EXPECT_EQ(1, std::distance(sequence_.begin(), p.first));
  EXPECT_EQ(2, std::distance(sequence_.begin(), p.second));
}

TEST_F(NonMutatingTest, EqualRangeArray) {
  auto p = absl::c_equal_range(array_, 2);
  EXPECT_EQ(1, std::distance(std::begin(array_), p.first));
  EXPECT_EQ(2, std::distance(std::begin(array_), p.second));
}

TEST_F(NonMutatingTest, EqualRangeWithPredicate) {
  std::vector<int> v(vector_);
  std::sort(v.begin(), v.end(), std::greater<int>());
  std::pair<std::vector<int>::iterator, std::vector<int>::iterator> p =
      absl::c_equal_range(v, 2, std::greater<int>());
  EXPECT_EQ(1, std::distance(v.begin(), p.first));
  EXPECT_EQ(2, std::distance(v.begin(), p.second));
}

TEST_F(NonMutatingTest, BinarySearch) {
  EXPECT_TRUE(absl::c_binary_search(vector_, 2));
  EXPECT_TRUE(absl::c_binary_search(std::vector<int>(vector_), 2));
}

TEST_F(NonMutatingTest, BinarySearchWithPredicate) {
  std::vector<int> v(vector_);
  std::sort(v.begin(), v.end(), std::greater<int>());
  EXPECT_TRUE(absl::c_binary_search(v, 2, std::greater<int>()));
  EXPECT_TRUE(
      absl::c_binary_search(std::vector<int>(v), 2, std::greater<int>()));
}

TEST_F(NonMutatingTest, MinElement) {
  std::list<int>::iterator i = absl::c_min_element(sequence_);
  ASSERT_TRUE(i != sequence_.end());
  EXPECT_EQ(*i, 1);
}

TEST_F(NonMutatingTest, MinElementWithPredicate) {
  std::list<int>::iterator i =
      absl::c_min_element(sequence_, std::greater<int>());
  ASSERT_TRUE(i != sequence_.end());
  EXPECT_EQ(*i, 3);
}

TEST_F(NonMutatingTest, MaxElement) {
  std::list<int>::iterator i = absl::c_max_element(sequence_);
  ASSERT_TRUE(i != sequence_.end());
  EXPECT_EQ(*i, 3);
}

TEST_F(NonMutatingTest, MaxElementWithPredicate) {
  std::list<int>::iterator i =
      absl::c_max_element(sequence_, std::greater<int>());
  ASSERT_TRUE(i != sequence_.end());
  EXPECT_EQ(*i, 1);
}

TEST_F(NonMutatingTest, LexicographicalCompare) {
  EXPECT_FALSE(absl::c_lexicographical_compare(sequence_, sequence_));

  std::vector<int> v;
  v.push_back(1);
  v.push_back(2);
  v.push_back(4);

  EXPECT_TRUE(absl::c_lexicographical_compare(sequence_, v));
  EXPECT_TRUE(absl::c_lexicographical_compare(std::list<int>(sequence_), v));
}

TEST_F(NonMutatingTest, LexicographicalCopmareWithPredicate) {
  EXPECT_FALSE(absl::c_lexicographical_compare(sequence_, sequence_,
                                               std::greater<int>()));

  std::vector<int> v;
  v.push_back(1);
  v.push_back(2);
  v.push_back(4);

  EXPECT_TRUE(
      absl::c_lexicographical_compare(v, sequence_, std::greater<int>()));
  EXPECT_TRUE(absl::c_lexicographical_compare(
      std::vector<int>(v), std::list<int>(sequence_), std::greater<int>()));
}

TEST_F(NonMutatingTest, Includes) {
  std::set<int> s(vector_.begin(), vector_.end());
  s.insert(4);
  EXPECT_TRUE(absl::c_includes(s, vector_));
}

TEST_F(NonMutatingTest, IncludesWithPredicate) {
  std::vector<int> v = {3, 2, 1};
  std::set<int, std::greater<int>> s(v.begin(), v.end());
  s.insert(4);
  EXPECT_TRUE(absl::c_includes(s, v, std::greater<int>()));
}

class NumericMutatingTest : public testing::Test {
 protected:
  std::list<int> list_ = {1, 2, 3};
  std::vector<int> output_;
};

TEST_F(NumericMutatingTest, Iota) {
  absl::c_iota(list_, 5);
  std::list<int> expected{5, 6, 7};
  EXPECT_EQ(list_, expected);
}

TEST_F(NonMutatingTest, Accumulate) {
  EXPECT_EQ(absl::c_accumulate(sequence_, 4), 1 + 2 + 3 + 4);
}

TEST_F(NonMutatingTest, AccumulateWithBinaryOp) {
  EXPECT_EQ(absl::c_accumulate(sequence_, 4, std::multiplies<int>()),
            1 * 2 * 3 * 4);
}

TEST_F(NonMutatingTest, AccumulateLvalueInit) {
  int lvalue = 4;
  EXPECT_EQ(absl::c_accumulate(sequence_, lvalue), 1 + 2 + 3 + 4);
}

TEST_F(NonMutatingTest, AccumulateWithBinaryOpLvalueInit) {
  int lvalue = 4;
  EXPECT_EQ(absl::c_accumulate(sequence_, lvalue, std::multiplies<int>()),
            1 * 2 * 3 * 4);
}

TEST_F(NonMutatingTest, InnerProduct) {
  EXPECT_EQ(absl::c_inner_product(sequence_, vector_, 1000),
            1000 + 1 * 1 + 2 * 2 + 3 * 3);
}

TEST_F(NonMutatingTest, InnerProductWithBinaryOps) {
  EXPECT_EQ(absl::c_inner_product(sequence_, vector_, 10,
                                  std::multiplies<int>(), std::plus<int>()),
            10 * (1 + 1) * (2 + 2) * (3 + 3));
}

TEST_F(NonMutatingTest, InnerProductLvalueInit) {
  int lvalue = 1000;
  EXPECT_EQ(absl::c_inner_product(sequence_, vector_, lvalue),
            1000 + 1 * 1 + 2 * 2 + 3 * 3);
}

TEST_F(NonMutatingTest, InnerProductWithBinaryOpsLvalueInit) {
  int lvalue = 10;
  EXPECT_EQ(absl::c_inner_product(sequence_, vector_, lvalue,
                                  std::multiplies<int>(), std::plus<int>()),
            10 * (1 + 1) * (2 + 2) * (3 + 3));
}

TEST_F(NumericMutatingTest, AdjacentDifference) {
  auto last = absl::c_adjacent_difference(list_, std::back_inserter(output_));
  *last = 1000;
  std::vector<int> expected{1, 2 - 1, 3 - 2, 1000};
  EXPECT_EQ(output_, expected);
}

TEST_F(NumericMutatingTest, AdjacentDifferenceWithBinaryOp) {
  auto last = absl::c_adjacent_difference(list_, std::back_inserter(output_),
                                          std::multiplies<int>());
  *last = 1000;
  std::vector<int> expected{1, 2 * 1, 3 * 2, 1000};
  EXPECT_EQ(output_, expected);
}

TEST_F(NumericMutatingTest, PartialSum) {
  auto last = absl::c_partial_sum(list_, std::back_inserter(output_));
  *last = 1000;
  std::vector<int> expected{1, 1 + 2, 1 + 2 + 3, 1000};
  EXPECT_EQ(output_, expected);
}

TEST_F(NumericMutatingTest, PartialSumWithBinaryOp) {
  auto last = absl::c_partial_sum(list_, std::back_inserter(output_),
                                  std::multiplies<int>());
  *last = 1000;
  std::vector<int> expected{1, 1 * 2, 1 * 2 * 3, 1000};
  EXPECT_EQ(output_, expected);
}

TEST_F(NonMutatingTest, LinearSearch) {
  EXPECT_TRUE(absl::c_linear_search(container_, 3));
  EXPECT_FALSE(absl::c_linear_search(container_, 4));
}

TEST_F(NonMutatingTest, AllOf) {
  const std::vector<int>& v = vector_;
  EXPECT_FALSE(absl::c_all_of(v, [](int x) { return x > 1; }));
  EXPECT_TRUE(absl::c_all_of(v, [](int x) { return x > 0; }));
}

TEST_F(NonMutatingTest, AnyOf) {
  const std::vector<int>& v = vector_;
  EXPECT_TRUE(absl::c_any_of(v, [](int x) { return x > 2; }));
  EXPECT_FALSE(absl::c_any_of(v, [](int x) { return x > 5; }));
}

TEST_F(NonMutatingTest, NoneOf) {
  const std::vector<int>& v = vector_;
  EXPECT_FALSE(absl::c_none_of(v, [](int x) { return x > 2; }));
  EXPECT_TRUE(absl::c_none_of(v, [](int x) { return x > 5; }));
}

TEST_F(NonMutatingTest, MinMaxElementLess) {
  std::pair<std::vector<int>::const_iterator, std::vector<int>::const_iterator>
      p = absl::c_minmax_element(vector_, std::less<int>());
  EXPECT_TRUE(p.first == vector_.begin());
  EXPECT_TRUE(p.second == vector_.begin() + 2);
}

TEST_F(NonMutatingTest, MinMaxElementGreater) {
  std::pair<std::vector<int>::const_iterator, std::vector<int>::const_iterator>
      p = absl::c_minmax_element(vector_, std::greater<int>());
  EXPECT_TRUE(p.first == vector_.begin() + 2);
  EXPECT_TRUE(p.second == vector_.begin());
}

TEST_F(NonMutatingTest, MinMaxElementNoPredicate) {
  std::pair<std::vector<int>::const_iterator, std::vector<int>::const_iterator>
      p = absl::c_minmax_element(vector_);
  EXPECT_TRUE(p.first == vector_.begin());
  EXPECT_TRUE(p.second == vector_.begin() + 2);
}

class SortingTest : public testing::Test {
 protected:
  std::list<int> sorted_ = {1, 2, 3, 4};
  std::list<int> unsorted_ = {2, 4, 1, 3};
  std::list<int> reversed_ = {4, 3, 2, 1};
};

TEST_F(SortingTest, IsSorted) {
  EXPECT_TRUE(absl::c_is_sorted(sorted_));
  EXPECT_FALSE(absl::c_is_sorted(unsorted_));
  EXPECT_FALSE(absl::c_is_sorted(reversed_));
}

TEST_F(SortingTest, IsSortedWithPredicate) {
  EXPECT_FALSE(absl::c_is_sorted(sorted_, std::greater<int>()));
  EXPECT_FALSE(absl::c_is_sorted(unsorted_, std::greater<int>()));
  EXPECT_TRUE(absl::c_is_sorted(reversed_, std::greater<int>()));
}

TEST_F(SortingTest, IsSortedUntil) {
  EXPECT_EQ(1, *absl::c_is_sorted_until(unsorted_));
  EXPECT_EQ(4, *absl::c_is_sorted_until(unsorted_, std::greater<int>()));
}

TEST_F(SortingTest, NthElement) {
  std::vector<int> unsorted = {2, 4, 1, 3};
  absl::c_nth_element(unsorted, unsorted.begin() + 2);
  EXPECT_THAT(unsorted, ElementsAre(Lt(3), Lt(3), 3, Gt(3)));
  absl::c_nth_element(unsorted, unsorted.begin() + 2, std::greater<int>());
  EXPECT_THAT(unsorted, ElementsAre(Gt(2), Gt(2), 2, Lt(2)));
}

TEST(MutatingTest, IsPartitioned) {
  EXPECT_TRUE(
      absl::c_is_partitioned(std::vector<int>{1, 3, 5, 2, 4, 6}, IsOdd));
  EXPECT_FALSE(
      absl::c_is_partitioned(std::vector<int>{1, 2, 3, 4, 5, 6}, IsOdd));
  EXPECT_FALSE(
      absl::c_is_partitioned(std::vector<int>{2, 4, 6, 1, 3, 5}, IsOdd));
}

TEST(MutatingTest, Partition) {
  std::vector<int> actual = {1, 2, 3, 4, 5};
  absl::c_partition(actual, IsOdd);
  EXPECT_THAT(actual, Truly([](const std::vector<int>& c) {
                return absl::c_is_partitioned(c, IsOdd);
              }));
}

TEST(MutatingTest, StablePartition) {
  std::vector<int> actual = {1, 2, 3, 4, 5};
  absl::c_stable_partition(actual, IsOdd);
  EXPECT_THAT(actual, ElementsAre(1, 3, 5, 2, 4));
}

TEST(MutatingTest, PartitionCopy) {
  const std::vector<int> initial = {1, 2, 3, 4, 5};
  std::vector<int> odds, evens;
  auto ends = absl::c_partition_copy(initial, back_inserter(odds),
                                     back_inserter(evens), IsOdd);
  *ends.first = 7;
  *ends.second = 6;
  EXPECT_THAT(odds, ElementsAre(1, 3, 5, 7));
  EXPECT_THAT(evens, ElementsAre(2, 4, 6));
}

TEST(MutatingTest, PartitionPoint) {
  const std::vector<int> initial = {1, 3, 5, 2, 4};
  auto middle = absl::c_partition_point(initial, IsOdd);
  EXPECT_EQ(2, *middle);
}

TEST(MutatingTest, CopyMiddle) {
  const std::vector<int> initial = {4, -1, -2, -3, 5};
  const std::list<int> input = {1, 2, 3};
  const std::vector<int> expected = {4, 1, 2, 3, 5};

  std::list<int> test_list(initial.begin(), initial.end());
  absl::c_copy(input, ++test_list.begin());
  EXPECT_EQ(std::list<int>(expected.begin(), expected.end()), test_list);

  std::vector<int> test_vector = initial;
  absl::c_copy(input, test_vector.begin() + 1);
  EXPECT_EQ(expected, test_vector);
}

TEST(MutatingTest, CopyFrontInserter) {
  const std::list<int> initial = {4, 5};
  const std::list<int> input = {1, 2, 3};
  const std::list<int> expected = {3, 2, 1, 4, 5};

  std::list<int> test_list = initial;
  absl::c_copy(input, std::front_inserter(test_list));
  EXPECT_EQ(expected, test_list);
}

TEST(MutatingTest, CopyBackInserter) {
  const std::vector<int> initial = {4, 5};
  const std::list<int> input = {1, 2, 3};
  const std::vector<int> expected = {4, 5, 1, 2, 3};

  std::list<int> test_list(initial.begin(), initial.end());
  absl::c_copy(input, std::back_inserter(test_list));
  EXPECT_EQ(std::list<int>(expected.begin(), expected.end()), test_list);

  std::vector<int> test_vector = initial;
  absl::c_copy(input, std::back_inserter(test_vector));
  EXPECT_EQ(expected, test_vector);
}

TEST(MutatingTest, CopyN) {
  const std::vector<int> initial = {1, 2, 3, 4, 5};
  const std::vector<int> expected = {1, 2};
  std::vector<int> actual;
  absl::c_copy_n(initial, 2, back_inserter(actual));
  EXPECT_EQ(expected, actual);
}

TEST(MutatingTest, CopyIf) {
  const std::list<int> input = {1, 2, 3};
  std::vector<int> output;
  absl::c_copy_if(input, std::back_inserter(output),
                  [](int i) { return i != 2; });
  EXPECT_THAT(output, ElementsAre(1, 3));
}

TEST(MutatingTest, CopyBackward) {
  std::vector<int> actual = {1, 2, 3, 4, 5};
  std::vector<int> expected = {1, 2, 1, 2, 3};
  absl::c_copy_backward(absl::MakeSpan(actual.data(), 3), actual.end());
  EXPECT_EQ(expected, actual);
}

TEST(MutatingTest, Move) {
  std::vector<std::unique_ptr<int>> src;
  src.emplace_back(absl::make_unique<int>(1));
  src.emplace_back(absl::make_unique<int>(2));
  src.emplace_back(absl::make_unique<int>(3));
  src.emplace_back(absl::make_unique<int>(4));
  src.emplace_back(absl::make_unique<int>(5));

  std::vector<std::unique_ptr<int>> dest = {};
  absl::c_move(src, std::back_inserter(dest));
  EXPECT_THAT(src, Each(IsNull()));
  EXPECT_THAT(dest, ElementsAre(Pointee(1), Pointee(2), Pointee(3), Pointee(4),
                                Pointee(5)));
}

TEST(MutatingTest, MoveBackward) {
  std::vector<std::unique_ptr<int>> actual;
  actual.emplace_back(absl::make_unique<int>(1));
  actual.emplace_back(absl::make_unique<int>(2));
  actual.emplace_back(absl::make_unique<int>(3));
  actual.emplace_back(absl::make_unique<int>(4));
  actual.emplace_back(absl::make_unique<int>(5));
  auto subrange = absl::MakeSpan(actual.data(), 3);
  absl::c_move_backward(subrange, actual.end());
  EXPECT_THAT(actual, ElementsAre(IsNull(), IsNull(), Pointee(1), Pointee(2),
                                  Pointee(3)));
}

TEST(MutatingTest, MoveWithRvalue) {
  auto MakeRValueSrc = [] {
    std::vector<std::unique_ptr<int>> src;
    src.emplace_back(absl::make_unique<int>(1));
    src.emplace_back(absl::make_unique<int>(2));
    src.emplace_back(absl::make_unique<int>(3));
    return src;
  };

  std::vector<std::unique_ptr<int>> dest = MakeRValueSrc();
  absl::c_move(MakeRValueSrc(), std::back_inserter(dest));
  EXPECT_THAT(dest, ElementsAre(Pointee(1), Pointee(2), Pointee(3), Pointee(1),
                                Pointee(2), Pointee(3)));
}

TEST(MutatingTest, SwapRanges) {
  std::vector<int> odds = {2, 4, 6};
  std::vector<int> evens = {1, 3, 5};
  absl::c_swap_ranges(odds, evens);
  EXPECT_THAT(odds, ElementsAre(1, 3, 5));
  EXPECT_THAT(evens, ElementsAre(2, 4, 6));

  odds.pop_back();
  absl::c_swap_ranges(odds, evens);
  EXPECT_THAT(odds, ElementsAre(2, 4));
  EXPECT_THAT(evens, ElementsAre(1, 3, 6));

  absl::c_swap_ranges(evens, odds);
  EXPECT_THAT(odds, ElementsAre(1, 3));
  EXPECT_THAT(evens, ElementsAre(2, 4, 6));
}

TEST_F(NonMutatingTest, Transform) {
  std::vector<int> x{0, 2, 4}, y, z;
  auto end = absl::c_transform(x, back_inserter(y), std::negate<int>());
  EXPECT_EQ(std::vector<int>({0, -2, -4}), y);
  *end = 7;
  EXPECT_EQ(std::vector<int>({0, -2, -4, 7}), y);

  y = {1, 3, 0};
  end = absl::c_transform(x, y, back_inserter(z), std::plus<int>());
  EXPECT_EQ(std::vector<int>({1, 5, 4}), z);
  *end = 7;
  EXPECT_EQ(std::vector<int>({1, 5, 4, 7}), z);

  z.clear();
  y.pop_back();
  end = absl::c_transform(x, y, std::back_inserter(z), std::plus<int>());
  EXPECT_EQ(std::vector<int>({1, 5}), z);
  *end = 7;
  EXPECT_EQ(std::vector<int>({1, 5, 7}), z);

  z.clear();
  std::swap(x, y);
  end = absl::c_transform(x, y, std::back_inserter(z), std::plus<int>());
  EXPECT_EQ(std::vector<int>({1, 5}), z);
  *end = 7;
  EXPECT_EQ(std::vector<int>({1, 5, 7}), z);
}

TEST(MutatingTest, Replace) {
  const std::vector<int> initial = {1, 2, 3, 1, 4, 5};
  const std::vector<int> expected = {4, 2, 3, 4, 4, 5};

  std::vector<int> test_vector = initial;
  absl::c_replace(test_vector, 1, 4);
  EXPECT_EQ(expected, test_vector);

  std::list<int> test_list(initial.begin(), initial.end());
  absl::c_replace(test_list, 1, 4);
  EXPECT_EQ(std::list<int>(expected.begin(), expected.end()), test_list);
}

TEST(MutatingTest, ReplaceIf) {
  std::vector<int> actual = {1, 2, 3, 4, 5};
  const std::vector<int> expected = {0, 2, 0, 4, 0};

  absl::c_replace_if(actual, IsOdd, 0);
  EXPECT_EQ(expected, actual);
}

TEST(MutatingTest, ReplaceCopy) {
  const std::vector<int> initial = {1, 2, 3, 1, 4, 5};
  const std::vector<int> expected = {4, 2, 3, 4, 4, 5};

  std::vector<int> actual;
  absl::c_replace_copy(initial, back_inserter(actual), 1, 4);
  EXPECT_EQ(expected, actual);
}

TEST(MutatingTest, Sort) {
  std::vector<int> test_vector = {2, 3, 1, 4};
  absl::c_sort(test_vector);
  EXPECT_THAT(test_vector, ElementsAre(1, 2, 3, 4));
}

TEST(MutatingTest, SortWithPredicate) {
  std::vector<int> test_vector = {2, 3, 1, 4};
  absl::c_sort(test_vector, std::greater<int>());
  EXPECT_THAT(test_vector, ElementsAre(4, 3, 2, 1));
}

// For absl::c_stable_sort tests. Needs an operator< that does not cover all
// fields so that the test can check the sort preserves order of equal elements.
struct Element {
  int key;
  int value;
  friend bool operator<(const Element& e1, const Element& e2) {
    return e1.key < e2.key;
  }
  // Make gmock print useful diagnostics.
  friend std::ostream& operator<<(std::ostream& o, const Element& e) {
    return o << "{" << e.key << ", " << e.value << "}";
  }
};

MATCHER_P2(IsElement, key, value, "") {
  return arg.key == key && arg.value == value;
}

TEST(MutatingTest, StableSort) {
  std::vector<Element> test_vector = {{1, 1}, {2, 1}, {2, 0}, {1, 0}, {2, 2}};
  absl::c_stable_sort(test_vector);
  EXPECT_THAT(test_vector,
              ElementsAre(IsElement(1, 1), IsElement(1, 0), IsElement(2, 1),
                          IsElement(2, 0), IsElement(2, 2)));
}

TEST(MutatingTest, StableSortWithPredicate) {
  std::vector<Element> test_vector = {{1, 1}, {2, 1}, {2, 0}, {1, 0}, {2, 2}};
  absl::c_stable_sort(test_vector, [](const Element& e1, const Element& e2) {
    return e2 < e1;
  });
  EXPECT_THAT(test_vector,
              ElementsAre(IsElement(2, 1), IsElement(2, 0), IsElement(2, 2),
                          IsElement(1, 1), IsElement(1, 0)));
}

TEST(MutatingTest, ReplaceCopyIf) {
  const std::vector<int> initial = {1, 2, 3, 4, 5};
  const std::vector<int> expected = {0, 2, 0, 4, 0};

  std::vector<int> actual;
  absl::c_replace_copy_if(initial, back_inserter(actual), IsOdd, 0);
  EXPECT_EQ(expected, actual);
}

TEST(MutatingTest, Fill) {
  std::vector<int> actual(5);
  absl::c_fill(actual, 1);
  EXPECT_THAT(actual, ElementsAre(1, 1, 1, 1, 1));
}

TEST(MutatingTest, FillN) {
  std::vector<int> actual(5, 0);
  absl::c_fill_n(actual, 2, 1);
  EXPECT_THAT(actual, ElementsAre(1, 1, 0, 0, 0));
}

TEST(MutatingTest, Generate) {
  std::vector<int> actual(5);
  int x = 0;
  absl::c_generate(actual, [&x]() { return ++x; });
  EXPECT_THAT(actual, ElementsAre(1, 2, 3, 4, 5));
}

TEST(MutatingTest, GenerateN) {
  std::vector<int> actual(5, 0);
  int x = 0;
  absl::c_generate_n(actual, 3, [&x]() { return ++x; });
  EXPECT_THAT(actual, ElementsAre(1, 2, 3, 0, 0));
}

TEST(MutatingTest, RemoveCopy) {
  std::vector<int> actual;
  absl::c_remove_copy(std::vector<int>{1, 2, 3}, back_inserter(actual), 2);
  EXPECT_THAT(actual, ElementsAre(1, 3));
}

TEST(MutatingTest, RemoveCopyIf) {
  std::vector<int> actual;
  absl::c_remove_copy_if(std::vector<int>{1, 2, 3}, back_inserter(actual),
                         IsOdd);
  EXPECT_THAT(actual, ElementsAre(2));
}

TEST(MutatingTest, UniqueCopy) {
  std::vector<int> actual;
  absl::c_unique_copy(std::vector<int>{1, 2, 2, 2, 3, 3, 2},
                      back_inserter(actual));
  EXPECT_THAT(actual, ElementsAre(1, 2, 3, 2));
}

TEST(MutatingTest, UniqueCopyWithPredicate) {
  std::vector<int> actual;
  absl::c_unique_copy(std::vector<int>{1, 2, 3, -1, -2, -3, 1},
                      back_inserter(actual),
                      [](int x, int y) { return (x < 0) == (y < 0); });
  EXPECT_THAT(actual, ElementsAre(1, -1, 1));
}

TEST(MutatingTest, Reverse) {
  std::vector<int> test_vector = {1, 2, 3, 4};
  absl::c_reverse(test_vector);
  EXPECT_THAT(test_vector, ElementsAre(4, 3, 2, 1));

  std::list<int> test_list = {1, 2, 3, 4};
  absl::c_reverse(test_list);
  EXPECT_THAT(test_list, ElementsAre(4, 3, 2, 1));
}

TEST(MutatingTest, ReverseCopy) {
  std::vector<int> actual;
  absl::c_reverse_copy(std::vector<int>{1, 2, 3, 4}, back_inserter(actual));
  EXPECT_THAT(actual, ElementsAre(4, 3, 2, 1));
}

TEST(MutatingTest, Rotate) {
  std::vector<int> actual = {1, 2, 3, 4};
  auto it = absl::c_rotate(actual, actual.begin() + 2);
  EXPECT_THAT(actual, testing::ElementsAreArray({3, 4, 1, 2}));
  EXPECT_EQ(*it, 1);
}

TEST(MutatingTest, RotateCopy) {
  std::vector<int> initial = {1, 2, 3, 4};
  std::vector<int> actual;
  auto end =
      absl::c_rotate_copy(initial, initial.begin() + 2, back_inserter(actual));
  *end = 5;
  EXPECT_THAT(actual, ElementsAre(3, 4, 1, 2, 5));
}

template <typename T>
T RandomlySeededPrng() {
  std::random_device rdev;
  std::seed_seq::result_type data[T::state_size];
  std::generate_n(data, T::state_size, std::ref(rdev));
  std::seed_seq prng_seed(data, data + T::state_size);
  return T(prng_seed);
}

TEST(MutatingTest, Shuffle) {
  std::vector<int> actual = {1, 2, 3, 4, 5};
  absl::c_shuffle(actual, RandomlySeededPrng<std::mt19937_64>());
  EXPECT_THAT(actual, UnorderedElementsAre(1, 2, 3, 4, 5));
}

TEST(MutatingTest, Sample) {
  std::vector<int> actual;
  absl::c_sample(std::vector<int>{1, 2, 3, 4, 5}, std::back_inserter(actual), 3,
                 RandomlySeededPrng<std::mt19937_64>());
  EXPECT_THAT(actual, IsSubsetOf({1, 2, 3, 4, 5}));
  EXPECT_THAT(actual, SizeIs(3));
}

TEST(MutatingTest, PartialSort) {
  std::vector<int> sequence{5, 3, 42, 0};
  absl::c_partial_sort(sequence, sequence.begin() + 2);
  EXPECT_THAT(absl::MakeSpan(sequence.data(), 2), ElementsAre(0, 3));
  absl::c_partial_sort(sequence, sequence.begin() + 2, std::greater<int>());
  EXPECT_THAT(absl::MakeSpan(sequence.data(), 2), ElementsAre(42, 5));
}

TEST(MutatingTest, PartialSortCopy) {
  const std::vector<int> initial = {5, 3, 42, 0};
  std::vector<int> actual(2);
  absl::c_partial_sort_copy(initial, actual);
  EXPECT_THAT(actual, ElementsAre(0, 3));
  absl::c_partial_sort_copy(initial, actual, std::greater<int>());
  EXPECT_THAT(actual, ElementsAre(42, 5));
}

TEST(MutatingTest, Merge) {
  std::vector<int> actual;
  absl::c_merge(std::vector<int>{1, 3, 5}, std::vector<int>{2, 4},
                back_inserter(actual));
  EXPECT_THAT(actual, ElementsAre(1, 2, 3, 4, 5));
}

TEST(MutatingTest, MergeWithComparator) {
  std::vector<int> actual;
  absl::c_merge(std::vector<int>{5, 3, 1}, std::vector<int>{4, 2},
                back_inserter(actual), std::greater<int>());
  EXPECT_THAT(actual, ElementsAre(5, 4, 3, 2, 1));
}

TEST(MutatingTest, InplaceMerge) {
  std::vector<int> actual = {1, 3, 5, 2, 4};
  absl::c_inplace_merge(actual, actual.begin() + 3);
  EXPECT_THAT(actual, ElementsAre(1, 2, 3, 4, 5));
}

TEST(MutatingTest, InplaceMergeWithComparator) {
  std::vector<int> actual = {5, 3, 1, 4, 2};
  absl::c_inplace_merge(actual, actual.begin() + 3, std::greater<int>());
  EXPECT_THAT(actual, ElementsAre(5, 4, 3, 2, 1));
}

class SetOperationsTest : public testing::Test {
 protected:
  std::vector<int> a_ = {1, 2, 3};
  std::vector<int> b_ = {1, 3, 5};

  std::vector<int> a_reversed_ = {3, 2, 1};
  std::vector<int> b_reversed_ = {5, 3, 1};
};

TEST_F(SetOperationsTest, SetUnion) {
  std::vector<int> actual;
  absl::c_set_union(a_, b_, back_inserter(actual));
  EXPECT_THAT(actual, ElementsAre(1, 2, 3, 5));
}

TEST_F(SetOperationsTest, SetUnionWithComparator) {
  std::vector<int> actual;
  absl::c_set_union(a_reversed_, b_reversed_, back_inserter(actual),
                    std::greater<int>());
  EXPECT_THAT(actual, ElementsAre(5, 3, 2, 1));
}

TEST_F(SetOperationsTest, SetIntersection) {
  std::vector<int> actual;
  absl::c_set_intersection(a_, b_, back_inserter(actual));
  EXPECT_THAT(actual, ElementsAre(1, 3));
}

TEST_F(SetOperationsTest, SetIntersectionWithComparator) {
  std::vector<int> actual;
  absl::c_set_intersection(a_reversed_, b_reversed_, back_inserter(actual),
                           std::greater<int>());
  EXPECT_THAT(actual, ElementsAre(3, 1));
}

TEST_F(SetOperationsTest, SetDifference) {
  std::vector<int> actual;
  absl::c_set_difference(a_, b_, back_inserter(actual));
  EXPECT_THAT(actual, ElementsAre(2));
}

TEST_F(SetOperationsTest, SetDifferenceWithComparator) {
  std::vector<int> actual;
  absl::c_set_difference(a_reversed_, b_reversed_, back_inserter(actual),
                         std::greater<int>());
  EXPECT_THAT(actual, ElementsAre(2));
}

TEST_F(SetOperationsTest, SetSymmetricDifference) {
  std::vector<int> actual;
  absl::c_set_symmetric_difference(a_, b_, back_inserter(actual));
  EXPECT_THAT(actual, ElementsAre(2, 5));
}

TEST_F(SetOperationsTest, SetSymmetricDifferenceWithComparator) {
  std::vector<int> actual;
  absl::c_set_symmetric_difference(a_reversed_, b_reversed_,
                                   back_inserter(actual), std::greater<int>());
  EXPECT_THAT(actual, ElementsAre(5, 2));
}

TEST(HeapOperationsTest, WithoutComparator) {
  std::vector<int> heap = {1, 2, 3};
  EXPECT_FALSE(absl::c_is_heap(heap));
  absl::c_make_heap(heap);
  EXPECT_TRUE(absl::c_is_heap(heap));
  heap.push_back(4);
  EXPECT_EQ(3, absl::c_is_heap_until(heap) - heap.begin());
  absl::c_push_heap(heap);
  EXPECT_EQ(4, heap[0]);
  absl::c_pop_heap(heap);
  EXPECT_EQ(4, heap[3]);
  absl::c_make_heap(heap);
  absl::c_sort_heap(heap);
  EXPECT_THAT(heap, ElementsAre(1, 2, 3, 4));
  EXPECT_FALSE(absl::c_is_heap(heap));
}

TEST(HeapOperationsTest, WithComparator) {
  using greater = std::greater<int>;
  std::vector<int> heap = {3, 2, 1};
  EXPECT_FALSE(absl::c_is_heap(heap, greater()));
  absl::c_make_heap(heap, greater());
  EXPECT_TRUE(absl::c_is_heap(heap, greater()));
  heap.push_back(0);
  EXPECT_EQ(3, absl::c_is_heap_until(heap, greater()) - heap.begin());
  absl::c_push_heap(heap, greater());
  EXPECT_EQ(0, heap[0]);
  absl::c_pop_heap(heap, greater());
  EXPECT_EQ(0, heap[3]);
  absl::c_make_heap(heap, greater());
  absl::c_sort_heap(heap, greater());
  EXPECT_THAT(heap, ElementsAre(3, 2, 1, 0));
  EXPECT_FALSE(absl::c_is_heap(heap, greater()));
}

TEST(MutatingTest, PermutationOperations) {
  std::vector<int> initial = {1, 2, 3, 4};
  std::vector<int> permuted = initial;

  absl::c_next_permutation(permuted);
  EXPECT_TRUE(absl::c_is_permutation(initial, permuted));
  EXPECT_TRUE(absl::c_is_permutation(initial, permuted, std::equal_to<int>()));

  std::vector<int> permuted2 = initial;
  absl::c_prev_permutation(permuted2, std::greater<int>());
  EXPECT_EQ(permuted, permuted2);

  absl::c_prev_permutation(permuted);
  EXPECT_EQ(initial, permuted);
}

#if defined(ABSL_INTERNAL_CPLUSPLUS_LANG) && \
    ABSL_INTERNAL_CPLUSPLUS_LANG >= 201703L

TEST(ConstexprTest, Distance) {
  // Works at compile time with constexpr containers.
  static_assert(absl::c_distance(std::array<int, 3>()) == 3);
}

TEST(ConstexprTest, MinElement) {
  constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(*absl::c_min_element(kArray) == 1);
}

TEST(ConstexprTest, MinElementWithPredicate) {
  constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(*absl::c_min_element(kArray, std::greater<int>()) == 3);
}

TEST(ConstexprTest, MaxElement) {
  constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(*absl::c_max_element(kArray) == 3);
}

TEST(ConstexprTest, MaxElementWithPredicate) {
  constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(*absl::c_max_element(kArray, std::greater<int>()) == 1);
}

TEST(ConstexprTest, MinMaxElement) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  constexpr auto kMinMaxPair = absl::c_minmax_element(kArray);
  static_assert(*kMinMaxPair.first == 1);
  static_assert(*kMinMaxPair.second == 3);
}

TEST(ConstexprTest, MinMaxElementWithPredicate) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  constexpr auto kMinMaxPair =
      absl::c_minmax_element(kArray, std::greater<int>());
  static_assert(*kMinMaxPair.first == 3);
  static_assert(*kMinMaxPair.second == 1);
}
#endif  // defined(ABSL_INTERNAL_CPLUSPLUS_LANG) &&
        //  ABSL_INTERNAL_CPLUSPLUS_LANG >= 201703L

#if defined(ABSL_INTERNAL_CPLUSPLUS_LANG) && \
    ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L

TEST(ConstexprTest, LinearSearch) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_linear_search(kArray, 3));
  static_assert(!absl::c_linear_search(kArray, 4));
}

TEST(ConstexprTest, AllOf) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(!absl::c_all_of(kArray, [](int x) { return x > 1; }));
  static_assert(absl::c_all_of(kArray, [](int x) { return x > 0; }));
}

TEST(ConstexprTest, AnyOf) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_any_of(kArray, [](int x) { return x > 2; }));
  static_assert(!absl::c_any_of(kArray, [](int x) { return x > 5; }));
}

TEST(ConstexprTest, NoneOf) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(!absl::c_none_of(kArray, [](int x) { return x > 2; }));
  static_assert(absl::c_none_of(kArray, [](int x) { return x > 5; }));
}

TEST(ConstexprTest, ForEach) {
  static constexpr std::array<int, 3> kArray = [] {
    std::array<int, 3> array = {1, 2, 3};
    absl::c_for_each(array, [](int& x) { x += 1; });
    return array;
  }();
  static_assert(kArray == std::array{2, 3, 4});
}

TEST(ConstexprTest, Find) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_find(kArray, 1) == kArray.begin());
  static_assert(absl::c_find(kArray, 4) == kArray.end());
}

TEST(ConstexprTest, Contains) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_contains(kArray, 1));
  static_assert(!absl::c_contains(kArray, 4));
}

TEST(ConstexprTest, FindIf) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_find_if(kArray, [](int x) { return x > 2; }) ==
                kArray.begin() + 2);
  static_assert(absl::c_find_if(kArray, [](int x) { return x > 5; }) ==
                kArray.end());
}

TEST(ConstexprTest, FindIfNot) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_find_if_not(kArray, [](int x) { return x > 1; }) ==
                kArray.begin());
  static_assert(absl::c_find_if_not(kArray, [](int x) { return x > 0; }) ==
                kArray.end());
}

TEST(ConstexprTest, FindEnd) {
  static constexpr std::array<int, 5> kHaystack = {1, 2, 3, 2, 3};
  static constexpr std::array<int, 2> kNeedle = {2, 3};
  static_assert(absl::c_find_end(kHaystack, kNeedle) == kHaystack.begin() + 3);
}

TEST(ConstexprTest, FindFirstOf) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_find_first_of(kArray, kArray) == kArray.begin());
}

TEST(ConstexprTest, AdjacentFind) {
  static constexpr std::array<int, 4> kArray = {1, 2, 2, 3};
  static_assert(absl::c_adjacent_find(kArray) == kArray.begin() + 1);
}

TEST(ConstexprTest, AdjacentFindWithPredicate) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_adjacent_find(kArray, std::less<int>()) ==
                kArray.begin());
}

TEST(ConstexprTest, Count) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_count(kArray, 1) == 1);
  static_assert(absl::c_count(kArray, 2) == 1);
  static_assert(absl::c_count(kArray, 3) == 1);
  static_assert(absl::c_count(kArray, 4) == 0);
}

TEST(ConstexprTest, CountIf) {
  static constexpr std::array<int, 3> kArray = {1, 2, 3};
  static_assert(absl::c_count_if(kArray, [](int x) { return x > 0; }) == 3);
  static_assert(absl::c_count_if(kArray, [](int x) { return x > 1; }) == 2);
}

TEST(ConstexprTest, Mismatch) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(absl::c_mismatch(kArray1, kArray2) ==
                std::pair{kArray1.end(), kArray2.end()});
  static_assert(absl::c_mismatch(kArray1, kArray3) ==
                std::pair{kArray1.begin(), kArray3.begin()});
}

TEST(ConstexprTest, MismatchWithPredicate) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(absl::c_mismatch(kArray1, kArray2, std::not_equal_to<int>()) ==
                std::pair{kArray1.begin(), kArray2.begin()});
  static_assert(absl::c_mismatch(kArray1, kArray3, std::not_equal_to<int>()) ==
                std::pair{kArray1.end(), kArray3.end()});
}

TEST(ConstexprTest, Equal) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(absl::c_equal(kArray1, kArray2));
  static_assert(!absl::c_equal(kArray1, kArray3));
}

TEST(ConstexprTest, EqualWithPredicate) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(!absl::c_equal(kArray1, kArray2, std::not_equal_to<int>()));
  static_assert(absl::c_equal(kArray1, kArray3, std::not_equal_to<int>()));
}

TEST(ConstexprTest, IsPermutation) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {3, 2, 1};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(absl::c_is_permutation(kArray1, kArray2));
  static_assert(!absl::c_is_permutation(kArray1, kArray3));
}

TEST(ConstexprTest, IsPermutationWithPredicate) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {3, 2, 1};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(absl::c_is_permutation(kArray1, kArray2, std::equal_to<int>()));
  static_assert(
      !absl::c_is_permutation(kArray1, kArray3, std::equal_to<int>()));
}

TEST(ConstexprTest, Search) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(absl::c_search(kArray1, kArray2) == kArray1.begin());
  static_assert(absl::c_search(kArray1, kArray3) == kArray1.end());
}

TEST(ConstexprTest, SearchWithPredicate) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(absl::c_search(kArray1, kArray2, std::not_equal_to<int>()) ==
                kArray1.end());
  static_assert(absl::c_search(kArray1, kArray3, std::not_equal_to<int>()) ==
                kArray1.begin());
}

TEST(ConstexprTest, ContainsSubrange) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(absl::c_contains_subrange(kArray1, kArray2));
  static_assert(!absl::c_contains_subrange(kArray1, kArray3));
}

TEST(ConstexprTest, ContainsSubrangeWithPredicate) {
  static constexpr std::array<int, 3> kArray1 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray2 = {1, 2, 3};
  static constexpr std::array<int, 3> kArray3 = {2, 3, 4};
  static_assert(
      !absl::c_contains_subrange(kArray1, kArray2, std::not_equal_to<>()));
  static_assert(
      absl::c_contains_subrange(kArray1, kArray3, std::not_equal_to<>()));
}

TEST(ConstexprTest, SearchN) {
  static constexpr std::array<int, 4> kArray = {1, 2, 2, 3};
  static_assert(absl::c_search_n(kArray, 1, 1) == kArray.begin());
  static_assert(absl::c_search_n(kArray, 2, 2) == kArray.begin() + 1);
  static_assert(absl::c_search_n(kArray, 1, 4) == kArray.end());
}

TEST(ConstexprTest, SearchNWithPredicate) {
  static constexpr std::array<int, 4> kArray = {1, 2, 2, 3};
  static_assert(absl::c_search_n(kArray, 1, 1, std::not_equal_to<int>()) ==
                kArray.begin() + 1);
  static_assert(absl::c_search_n(kArray, 2, 2, std::not_equal_to<int>()) ==
                kArray.end());
  static_assert(absl::c_search_n(kArray, 1, 4, std::not_equal_to<int>()) ==
                kArray.begin());
}

#endif  // defined(ABSL_INTERNAL_CPLUSPLUS_LANG) &&
        //  ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L

}  // namespace
