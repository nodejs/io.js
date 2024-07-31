// Copyright 2023 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/utility/internal/if_constexpr.h"

#include <utility>

#include "gtest/gtest.h"

namespace {

struct Empty {};
struct HasFoo {
  int foo() const { return 1; }
};

TEST(IfConstexpr, Basic) {
  int i = 0;
  absl::utility_internal::IfConstexpr<false>(
      [&](const auto& t) { i = t.foo(); }, Empty{});
  EXPECT_EQ(i, 0);

  absl::utility_internal::IfConstexpr<false>(
      [&](const auto& t) { i = t.foo(); }, HasFoo{});
  EXPECT_EQ(i, 0);

  absl::utility_internal::IfConstexpr<true>(
      [&](const auto& t) { i = t.foo(); }, HasFoo{});
  EXPECT_EQ(i, 1);
}

TEST(IfConstexprElse, Basic) {
  EXPECT_EQ(absl::utility_internal::IfConstexprElse<false>(
      [&](const auto& t) { return t.foo(); }, [&](const auto&) { return 2; },
      Empty{}), 2);

  EXPECT_EQ(absl::utility_internal::IfConstexprElse<false>(
      [&](const auto& t) { return t.foo(); }, [&](const auto&) { return 2; },
      HasFoo{}), 2);

  EXPECT_EQ(absl::utility_internal::IfConstexprElse<true>(
      [&](const auto& t) { return t.foo(); }, [&](const auto&) { return 2; },
      HasFoo{}), 1);
}

struct HasFooRValue {
  int foo() && { return 1; }
};
struct RValueFunc {
  void operator()(HasFooRValue&& t) && { *i = std::move(t).foo(); }

  int* i = nullptr;
};

TEST(IfConstexpr, RValues) {
  int i = 0;
  RValueFunc func = {&i};
  absl::utility_internal::IfConstexpr<false>(
      std::move(func), HasFooRValue{});
  EXPECT_EQ(i, 0);

  func = RValueFunc{&i};
  absl::utility_internal::IfConstexpr<true>(
      std::move(func), HasFooRValue{});
  EXPECT_EQ(i, 1);
}

}  // namespace
