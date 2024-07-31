// Copyright 2019 The Abseil Authors.
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

#ifndef ABSL_CONTAINER_INTERNAL_UNORDERED_SET_MEMBERS_TEST_H_
#define ABSL_CONTAINER_INTERNAL_UNORDERED_SET_MEMBERS_TEST_H_

#include <type_traits>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class UnordSet>
class MembersTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(MembersTest);

template <typename T>
void UseType() {}

TYPED_TEST_P(MembersTest, Typedefs) {
  EXPECT_TRUE((std::is_same<typename TypeParam::key_type,
                            typename TypeParam::value_type>()));
  EXPECT_TRUE((absl::conjunction<
               absl::negation<std::is_signed<typename TypeParam::size_type>>,
               std::is_integral<typename TypeParam::size_type>>()));
  EXPECT_TRUE((absl::conjunction<
               std::is_signed<typename TypeParam::difference_type>,
               std::is_integral<typename TypeParam::difference_type>>()));
  EXPECT_TRUE((std::is_convertible<
               decltype(std::declval<const typename TypeParam::hasher&>()(
                   std::declval<const typename TypeParam::key_type&>())),
               size_t>()));
  EXPECT_TRUE((std::is_convertible<
               decltype(std::declval<const typename TypeParam::key_equal&>()(
                   std::declval<const typename TypeParam::key_type&>(),
                   std::declval<const typename TypeParam::key_type&>())),
               bool>()));
  EXPECT_TRUE((std::is_same<typename TypeParam::allocator_type::value_type,
                            typename TypeParam::value_type>()));
  EXPECT_TRUE((std::is_same<typename TypeParam::value_type&,
                            typename TypeParam::reference>()));
  EXPECT_TRUE((std::is_same<const typename TypeParam::value_type&,
                            typename TypeParam::const_reference>()));
  EXPECT_TRUE((std::is_same<typename std::allocator_traits<
                                typename TypeParam::allocator_type>::pointer,
                            typename TypeParam::pointer>()));
  EXPECT_TRUE(
      (std::is_same<typename std::allocator_traits<
                        typename TypeParam::allocator_type>::const_pointer,
                    typename TypeParam::const_pointer>()));
}

TYPED_TEST_P(MembersTest, SimpleFunctions) {
  EXPECT_GT(TypeParam().max_size(), 0);
}

TYPED_TEST_P(MembersTest, BeginEnd) {
  TypeParam t = {typename TypeParam::value_type{}};
  EXPECT_EQ(t.begin(), t.cbegin());
  EXPECT_EQ(t.end(), t.cend());
  EXPECT_NE(t.begin(), t.end());
  EXPECT_NE(t.cbegin(), t.cend());
}

REGISTER_TYPED_TEST_SUITE_P(MembersTest, Typedefs, SimpleFunctions, BeginEnd);

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_UNORDERED_SET_MEMBERS_TEST_H_
