// Copyright 2020 The Abseil Authors.
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

#include "absl/strings/internal/string_constant.h"

#include "absl/meta/type_traits.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using absl::strings_internal::MakeStringConstant;

struct Callable {
  constexpr absl::string_view operator()() const {
    return absl::string_view("Callable", 8);
  }
};

TEST(StringConstant, Traits) {
  constexpr auto str = MakeStringConstant(Callable{});
  using T = decltype(str);

  EXPECT_TRUE(std::is_empty<T>::value);
  EXPECT_TRUE(std::is_trivial<T>::value);
  EXPECT_TRUE(absl::is_trivially_default_constructible<T>::value);
  EXPECT_TRUE(absl::is_trivially_copy_constructible<T>::value);
  EXPECT_TRUE(absl::is_trivially_move_constructible<T>::value);
  EXPECT_TRUE(absl::is_trivially_destructible<T>::value);
}

TEST(StringConstant, MakeFromCallable) {
  constexpr auto str = MakeStringConstant(Callable{});
  using T = decltype(str);
  EXPECT_EQ(Callable{}(), T::value);
  EXPECT_EQ(Callable{}(), str());
}

TEST(StringConstant, MakeFromStringConstant) {
  // We want to make sure the StringConstant itself is a valid input to the
  // factory function.
  constexpr auto str = MakeStringConstant(Callable{});
  constexpr auto str2 = MakeStringConstant(str);
  using T = decltype(str2);
  EXPECT_EQ(Callable{}(), T::value);
  EXPECT_EQ(Callable{}(), str2());
}

}  // namespace
