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

// This file contains a few select absl::Hash tests that, due to their reliance
// on INSTANTIATE_TYPED_TEST_SUITE_P, require a large amount of memory to
// compile. Put new tests in hash_test.cc, not this file.

#include "absl/hash/hash.h"

#include <stddef.h>

#include <algorithm>
#include <deque>
#include <forward_list>
#include <initializer_list>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/hash/hash_testing.h"
#include "absl/hash/internal/hash_test.h"

namespace {

using ::absl::hash_test_internal::is_hashable;
using ::absl::hash_test_internal::TypeErasedContainer;

// Dummy type with unordered equality and hashing semantics.  This preserves
// input order internally, and is used below to ensure we get test coverage
// for equal sequences with different iteraton orders.
template <typename T>
class UnorderedSequence {
 public:
  UnorderedSequence() = default;
  template <typename TT>
  UnorderedSequence(std::initializer_list<TT> l)
      : values_(l.begin(), l.end()) {}
  template <typename ForwardIterator,
            typename std::enable_if<!std::is_integral<ForwardIterator>::value,
                                    bool>::type = true>
  UnorderedSequence(ForwardIterator begin, ForwardIterator end)
      : values_(begin, end) {}
  // one-argument constructor of value type T, to appease older toolchains that
  // get confused by one-element initializer lists in some contexts
  explicit UnorderedSequence(const T& v) : values_(&v, &v + 1) {}

  using value_type = T;

  size_t size() const { return values_.size(); }
  typename std::vector<T>::const_iterator begin() const {
    return values_.begin();
  }
  typename std::vector<T>::const_iterator end() const { return values_.end(); }

  friend bool operator==(const UnorderedSequence& lhs,
                         const UnorderedSequence& rhs) {
    return lhs.size() == rhs.size() &&
           std::is_permutation(lhs.begin(), lhs.end(), rhs.begin());
  }
  friend bool operator!=(const UnorderedSequence& lhs,
                         const UnorderedSequence& rhs) {
    return !(lhs == rhs);
  }
  template <typename H>
  friend H AbslHashValue(H h, const UnorderedSequence& u) {
    return H::combine(H::combine_unordered(std::move(h), u.begin(), u.end()),
                      u.size());
  }

 private:
  std::vector<T> values_;
};

template <typename T>
class HashValueSequenceTest : public testing::Test {};
TYPED_TEST_SUITE_P(HashValueSequenceTest);

TYPED_TEST_P(HashValueSequenceTest, BasicUsage) {
  EXPECT_TRUE((is_hashable<TypeParam>::value));

  using IntType = typename TypeParam::value_type;
  auto a = static_cast<IntType>(0);
  auto b = static_cast<IntType>(23);
  auto c = static_cast<IntType>(42);

  std::vector<TypeParam> exemplars = {
      TypeParam(),        TypeParam(),        TypeParam{a, b, c},
      TypeParam{a, c, b}, TypeParam{c, a, b}, TypeParam{a},
      TypeParam{a, a},    TypeParam{a, a, a}, TypeParam{a, a, b},
      TypeParam{a, b, a}, TypeParam{b, a, a}, TypeParam{a, b},
      TypeParam{b, c}};
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(exemplars));
}

REGISTER_TYPED_TEST_SUITE_P(HashValueSequenceTest, BasicUsage);
using IntSequenceTypes = testing::Types<
    std::deque<int>, std::forward_list<int>, std::list<int>, std::vector<int>,
    std::vector<bool>, TypeErasedContainer<std::vector<int>>, std::set<int>,
    std::multiset<int>, UnorderedSequence<int>,
    TypeErasedContainer<UnorderedSequence<int>>, std::unordered_set<int>,
    std::unordered_multiset<int>, absl::flat_hash_set<int>,
    absl::node_hash_set<int>, absl::btree_set<int>>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, HashValueSequenceTest, IntSequenceTypes);

template <typename T>
class HashValueNestedSequenceTest : public testing::Test {};
TYPED_TEST_SUITE_P(HashValueNestedSequenceTest);

TYPED_TEST_P(HashValueNestedSequenceTest, BasicUsage) {
  using T = TypeParam;
  using V = typename T::value_type;
  std::vector<T> exemplars = {
      // empty case
      T{},
      // sets of empty sets
      T{V{}}, T{V{}, V{}}, T{V{}, V{}, V{}},
      // multisets of different values
      T{V{1}}, T{V{1, 1}, V{1, 1}}, T{V{1, 1, 1}, V{1, 1, 1}, V{1, 1, 1}},
      // various orderings of same nested sets
      T{V{}, V{1, 2}}, T{V{}, V{2, 1}}, T{V{1, 2}, V{}}, T{V{2, 1}, V{}},
      // various orderings of various nested sets, case 2
      T{V{1, 2}, V{3, 4}}, T{V{1, 2}, V{4, 3}}, T{V{1, 3}, V{2, 4}},
      T{V{1, 3}, V{4, 2}}, T{V{1, 4}, V{2, 3}}, T{V{1, 4}, V{3, 2}},
      T{V{2, 3}, V{1, 4}}, T{V{2, 3}, V{4, 1}}, T{V{2, 4}, V{1, 3}},
      T{V{2, 4}, V{3, 1}}, T{V{3, 4}, V{1, 2}}, T{V{3, 4}, V{2, 1}}};
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(exemplars));
}

REGISTER_TYPED_TEST_SUITE_P(HashValueNestedSequenceTest, BasicUsage);
template <typename T>
using TypeErasedSet = TypeErasedContainer<UnorderedSequence<T>>;

using NestedIntSequenceTypes = testing::Types<
    std::vector<std::vector<int>>, std::vector<UnorderedSequence<int>>,
    std::vector<TypeErasedSet<int>>, UnorderedSequence<std::vector<int>>,
    UnorderedSequence<UnorderedSequence<int>>,
    UnorderedSequence<TypeErasedSet<int>>, TypeErasedSet<std::vector<int>>,
    TypeErasedSet<UnorderedSequence<int>>, TypeErasedSet<TypeErasedSet<int>>>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, HashValueNestedSequenceTest,
                               NestedIntSequenceTypes);

template <typename T>
class HashValueAssociativeMapTest : public testing::Test {};
TYPED_TEST_SUITE_P(HashValueAssociativeMapTest);

TYPED_TEST_P(HashValueAssociativeMapTest, BasicUsage) {
  using M = TypeParam;
  using V = typename M::value_type;
  std::vector<M> exemplars{M{},
                           M{V{0, "foo"}},
                           M{V{1, "foo"}},
                           M{V{0, "bar"}},
                           M{V{1, "bar"}},
                           M{V{0, "foo"}, V{42, "bar"}},
                           M{V{42, "bar"}, V{0, "foo"}},
                           M{V{1, "foo"}, V{42, "bar"}},
                           M{V{1, "foo"}, V{43, "bar"}},
                           M{V{1, "foo"}, V{43, "baz"}}};
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(exemplars));
}

REGISTER_TYPED_TEST_SUITE_P(HashValueAssociativeMapTest, BasicUsage);
using AssociativeMapTypes = testing::Types<
    std::map<int, std::string>, std::unordered_map<int, std::string>,
    absl::flat_hash_map<int, std::string>,
    absl::node_hash_map<int, std::string>, absl::btree_map<int, std::string>,
    UnorderedSequence<std::pair<const int, std::string>>>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, HashValueAssociativeMapTest,
                               AssociativeMapTypes);

template <typename T>
class HashValueAssociativeMultimapTest : public testing::Test {};
TYPED_TEST_SUITE_P(HashValueAssociativeMultimapTest);

TYPED_TEST_P(HashValueAssociativeMultimapTest, BasicUsage) {
  using MM = TypeParam;
  using V = typename MM::value_type;
  std::vector<MM> exemplars{MM{},
                            MM{V{0, "foo"}},
                            MM{V{1, "foo"}},
                            MM{V{0, "bar"}},
                            MM{V{1, "bar"}},
                            MM{V{0, "foo"}, V{0, "bar"}},
                            MM{V{0, "bar"}, V{0, "foo"}},
                            MM{V{0, "foo"}, V{42, "bar"}},
                            MM{V{1, "foo"}, V{42, "bar"}},
                            MM{V{1, "foo"}, V{1, "foo"}, V{43, "bar"}},
                            MM{V{1, "foo"}, V{43, "bar"}, V{1, "foo"}},
                            MM{V{1, "foo"}, V{43, "baz"}}};
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(exemplars));
}

REGISTER_TYPED_TEST_SUITE_P(HashValueAssociativeMultimapTest, BasicUsage);
using AssociativeMultimapTypes =
    testing::Types<std::multimap<int, std::string>,
                   std::unordered_multimap<int, std::string>>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, HashValueAssociativeMultimapTest,
                               AssociativeMultimapTypes);

}  // namespace
