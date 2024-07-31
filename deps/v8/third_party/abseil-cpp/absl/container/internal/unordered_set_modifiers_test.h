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

#ifndef ABSL_CONTAINER_INTERNAL_UNORDERED_SET_MODIFIERS_TEST_H_
#define ABSL_CONTAINER_INTERNAL_UNORDERED_SET_MODIFIERS_TEST_H_

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/internal/hash_generator_testing.h"
#include "absl/container/internal/hash_policy_testing.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class UnordSet>
class ModifiersTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(ModifiersTest);

TYPED_TEST_P(ModifiersTest, Clear) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m(values.begin(), values.end());
  ASSERT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  m.clear();
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
  EXPECT_TRUE(m.empty());
}

TYPED_TEST_P(ModifiersTest, Insert) {
  using T = hash_internal::GeneratedType<TypeParam>;
  T val = hash_internal::Generator<T>()();
  TypeParam m;
  auto p = m.insert(val);
  EXPECT_TRUE(p.second);
  EXPECT_EQ(val, *p.first);
  p = m.insert(val);
  EXPECT_FALSE(p.second);
}

TYPED_TEST_P(ModifiersTest, InsertHint) {
  using T = hash_internal::GeneratedType<TypeParam>;
  T val = hash_internal::Generator<T>()();
  TypeParam m;
  auto it = m.insert(m.end(), val);
  EXPECT_TRUE(it != m.end());
  EXPECT_EQ(val, *it);
  it = m.insert(it, val);
  EXPECT_TRUE(it != m.end());
  EXPECT_EQ(val, *it);
}

TYPED_TEST_P(ModifiersTest, InsertRange) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m;
  m.insert(values.begin(), values.end());
  ASSERT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
}

TYPED_TEST_P(ModifiersTest, InsertWithinCapacity) {
  using T = hash_internal::GeneratedType<TypeParam>;
  T val = hash_internal::Generator<T>()();
  TypeParam m;
  m.reserve(10);
  const size_t original_capacity = m.bucket_count();
  m.insert(val);
  EXPECT_EQ(m.bucket_count(), original_capacity);
  m.insert(val);
  EXPECT_EQ(m.bucket_count(), original_capacity);
}

TYPED_TEST_P(ModifiersTest, InsertRangeWithinCapacity) {
#if !defined(__GLIBCXX__)
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> base_values;
  std::generate_n(std::back_inserter(base_values), 10,
                  hash_internal::Generator<T>());
  std::vector<T> values;
  while (values.size() != 100) {
    values.insert(values.end(), base_values.begin(), base_values.end());
  }
  TypeParam m;
  m.reserve(10);
  const size_t original_capacity = m.bucket_count();
  m.insert(values.begin(), values.end());
  EXPECT_EQ(m.bucket_count(), original_capacity);
#endif
}

TYPED_TEST_P(ModifiersTest, Emplace) {
  using T = hash_internal::GeneratedType<TypeParam>;
  T val = hash_internal::Generator<T>()();
  TypeParam m;
  // TODO(alkis): We need a way to run emplace in a more meaningful way. Perhaps
  // with test traits/policy.
  auto p = m.emplace(val);
  EXPECT_TRUE(p.second);
  EXPECT_EQ(val, *p.first);
  p = m.emplace(val);
  EXPECT_FALSE(p.second);
  EXPECT_EQ(val, *p.first);
}

TYPED_TEST_P(ModifiersTest, EmplaceHint) {
  using T = hash_internal::GeneratedType<TypeParam>;
  T val = hash_internal::Generator<T>()();
  TypeParam m;
  // TODO(alkis): We need a way to run emplace in a more meaningful way. Perhaps
  // with test traits/policy.
  auto it = m.emplace_hint(m.end(), val);
  EXPECT_EQ(val, *it);
  it = m.emplace_hint(it, val);
  EXPECT_EQ(val, *it);
}

template <class V>
using IfNotVoid = typename std::enable_if<!std::is_void<V>::value, V>::type;

// In openmap we chose not to return the iterator from erase because that's
// more expensive. As such we adapt erase to return an iterator here.
struct EraseFirst {
  template <class Map>
  auto operator()(Map* m, int) const
      -> IfNotVoid<decltype(m->erase(m->begin()))> {
    return m->erase(m->begin());
  }
  template <class Map>
  typename Map::iterator operator()(Map* m, ...) const {
    auto it = m->begin();
    m->erase(it++);
    return it;
  }
};

TYPED_TEST_P(ModifiersTest, Erase) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m(values.begin(), values.end());
  ASSERT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  std::vector<T> values2;
  for (const auto& val : values)
    if (val != *m.begin()) values2.push_back(val);
  auto it = EraseFirst()(&m, 0);
  ASSERT_TRUE(it != m.end());
  EXPECT_EQ(1, std::count(values2.begin(), values2.end(), *it));
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values2.begin(),
                                                            values2.end()));
}

TYPED_TEST_P(ModifiersTest, EraseRange) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m(values.begin(), values.end());
  ASSERT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  auto it = m.erase(m.begin(), m.end());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
  EXPECT_TRUE(it == m.end());
}

TYPED_TEST_P(ModifiersTest, EraseKey) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m(values.begin(), values.end());
  ASSERT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  EXPECT_EQ(1, m.erase(values[0]));
  EXPECT_EQ(0, std::count(m.begin(), m.end(), values[0]));
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values.begin() + 1,
                                                            values.end()));
}

TYPED_TEST_P(ModifiersTest, Swap) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> v1;
  std::vector<T> v2;
  std::generate_n(std::back_inserter(v1), 5, hash_internal::Generator<T>());
  std::generate_n(std::back_inserter(v2), 5, hash_internal::Generator<T>());
  TypeParam m1(v1.begin(), v1.end());
  TypeParam m2(v2.begin(), v2.end());
  EXPECT_THAT(keys(m1), ::testing::UnorderedElementsAreArray(v1));
  EXPECT_THAT(keys(m2), ::testing::UnorderedElementsAreArray(v2));
  m1.swap(m2);
  EXPECT_THAT(keys(m1), ::testing::UnorderedElementsAreArray(v2));
  EXPECT_THAT(keys(m2), ::testing::UnorderedElementsAreArray(v1));
}

// TODO(alkis): Write tests for extract.
// TODO(alkis): Write tests for merge.

REGISTER_TYPED_TEST_SUITE_P(ModifiersTest, Clear, Insert, InsertHint,
                            InsertRange, InsertWithinCapacity,
                            InsertRangeWithinCapacity, Emplace, EmplaceHint,
                            Erase, EraseRange, EraseKey, Swap);

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_UNORDERED_SET_MODIFIERS_TEST_H_
