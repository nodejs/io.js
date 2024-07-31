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

#include <type_traits>

#include "absl/base/internal/inline_variable.h"
#include "absl/base/internal/inline_variable_testing.h"

#include "gtest/gtest.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace inline_variable_testing_internal {
namespace {

TEST(InlineVariableTest, Constexpr) {
  static_assert(inline_variable_foo.value == 5, "");
  static_assert(other_inline_variable_foo.value == 5, "");
  static_assert(inline_variable_int == 5, "");
  static_assert(other_inline_variable_int == 5, "");
}

TEST(InlineVariableTest, DefaultConstructedIdentityEquality) {
  EXPECT_EQ(get_foo_a().value, 5);
  EXPECT_EQ(get_foo_b().value, 5);
  EXPECT_EQ(&get_foo_a(), &get_foo_b());
}

TEST(InlineVariableTest, DefaultConstructedIdentityInequality) {
  EXPECT_NE(&inline_variable_foo, &other_inline_variable_foo);
}

TEST(InlineVariableTest, InitializedIdentityEquality) {
  EXPECT_EQ(get_int_a(), 5);
  EXPECT_EQ(get_int_b(), 5);
  EXPECT_EQ(&get_int_a(), &get_int_b());
}

TEST(InlineVariableTest, InitializedIdentityInequality) {
  EXPECT_NE(&inline_variable_int, &other_inline_variable_int);
}

TEST(InlineVariableTest, FunPtrType) {
  static_assert(
      std::is_same<void(*)(),
                   std::decay<decltype(inline_variable_fun_ptr)>::type>::value,
      "");
}

}  // namespace
}  // namespace inline_variable_testing_internal
ABSL_NAMESPACE_END
}  // namespace absl
