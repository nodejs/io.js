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

#ifndef ABSL_CONTAINER_INTERNAL_UNORDERED_SET_LOOKUP_TEST_H_
#define ABSL_CONTAINER_INTERNAL_UNORDERED_SET_LOOKUP_TEST_H_

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/internal/hash_generator_testing.h"
#include "absl/container/internal/hash_policy_testing.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class UnordSet>
class LookupTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(LookupTest);

TYPED_TEST_P(LookupTest, Count) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m;
  for (const auto& v : values)
    EXPECT_EQ(0, m.count(v)) << ::testing::PrintToString(v);
  m.insert(values.begin(), values.end());
  for (const auto& v : values)
    EXPECT_EQ(1, m.count(v)) << ::testing::PrintToString(v);
}

TYPED_TEST_P(LookupTest, Find) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m;
  for (const auto& v : values)
    EXPECT_TRUE(m.end() == m.find(v)) << ::testing::PrintToString(v);
  m.insert(values.begin(), values.end());
  for (const auto& v : values) {
    typename TypeParam::iterator it = m.find(v);
    static_assert(std::is_same<const typename TypeParam::value_type&,
                               decltype(*it)>::value,
                  "");
    static_assert(std::is_same<const typename TypeParam::value_type*,
                               decltype(it.operator->())>::value,
                  "");
    EXPECT_TRUE(m.end() != it) << ::testing::PrintToString(v);
    EXPECT_EQ(v, *it) << ::testing::PrintToString(v);
  }
}

TYPED_TEST_P(LookupTest, EqualRange) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m;
  for (const auto& v : values) {
    auto r = m.equal_range(v);
    ASSERT_EQ(0, std::distance(r.first, r.second));
  }
  m.insert(values.begin(), values.end());
  for (const auto& v : values) {
    auto r = m.equal_range(v);
    ASSERT_EQ(1, std::distance(r.first, r.second));
    EXPECT_EQ(v, *r.first);
  }
}

REGISTER_TYPED_TEST_SUITE_P(LookupTest, Count, Find, EqualRange);

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_UNORDERED_SET_LOOKUP_TEST_H_
