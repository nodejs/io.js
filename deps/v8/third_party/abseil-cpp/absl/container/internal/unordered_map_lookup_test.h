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

#ifndef ABSL_CONTAINER_INTERNAL_UNORDERED_MAP_LOOKUP_TEST_H_
#define ABSL_CONTAINER_INTERNAL_UNORDERED_MAP_LOOKUP_TEST_H_

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/internal/hash_generator_testing.h"
#include "absl/container/internal/hash_policy_testing.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class UnordMap>
class LookupTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(LookupTest);

TYPED_TEST_P(LookupTest, At) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m(values.begin(), values.end());
  for (const auto& p : values) {
    const auto& val = m.at(p.first);
    EXPECT_EQ(p.second, val) << ::testing::PrintToString(p.first);
  }
}

TYPED_TEST_P(LookupTest, OperatorBracket) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using V = typename TypeParam::mapped_type;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m;
  for (const auto& p : values) {
    auto& val = m[p.first];
    EXPECT_EQ(V(), val) << ::testing::PrintToString(p.first);
    val = p.second;
  }
  for (const auto& p : values)
    EXPECT_EQ(p.second, m[p.first]) << ::testing::PrintToString(p.first);
}

TYPED_TEST_P(LookupTest, Count) {
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m;
  for (const auto& p : values)
    EXPECT_EQ(0, m.count(p.first)) << ::testing::PrintToString(p.first);
  m.insert(values.begin(), values.end());
  for (const auto& p : values)
    EXPECT_EQ(1, m.count(p.first)) << ::testing::PrintToString(p.first);
}

TYPED_TEST_P(LookupTest, Find) {
  using std::get;
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m;
  for (const auto& p : values)
    EXPECT_TRUE(m.end() == m.find(p.first))
        << ::testing::PrintToString(p.first);
  m.insert(values.begin(), values.end());
  for (const auto& p : values) {
    auto it = m.find(p.first);
    EXPECT_TRUE(m.end() != it) << ::testing::PrintToString(p.first);
    EXPECT_EQ(p.second, get<1>(*it)) << ::testing::PrintToString(p.first);
  }
}

TYPED_TEST_P(LookupTest, EqualRange) {
  using std::get;
  using T = hash_internal::GeneratedType<TypeParam>;
  std::vector<T> values;
  std::generate_n(std::back_inserter(values), 10,
                  hash_internal::Generator<T>());
  TypeParam m;
  for (const auto& p : values) {
    auto r = m.equal_range(p.first);
    ASSERT_EQ(0, std::distance(r.first, r.second));
  }
  m.insert(values.begin(), values.end());
  for (const auto& p : values) {
    auto r = m.equal_range(p.first);
    ASSERT_EQ(1, std::distance(r.first, r.second));
    EXPECT_EQ(p.second, get<1>(*r.first)) << ::testing::PrintToString(p.first);
  }
}

REGISTER_TYPED_TEST_SUITE_P(LookupTest, At, OperatorBracket, Count, Find,
                            EqualRange);

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_UNORDERED_MAP_LOOKUP_TEST_H_
