// Copyright 2023 The Abseil Authors.
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

#include "absl/functional/overload.h"

#include <cstdint>
#include <string>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

#if defined(ABSL_INTERNAL_CPLUSPLUS_LANG) && \
    ABSL_INTERNAL_CPLUSPLUS_LANG >= 201703L

#include "gtest/gtest.h"

namespace {

TEST(OverloadTest, DispatchConsidersTypeWithAutoFallback) {
  auto overloaded = absl::Overload{
      [](int v) { return absl::StrCat("int ", v); },
      [](double v) { return absl::StrCat("double ", v); },
      [](const char* v) { return absl::StrCat("const char* ", v); },
      [](auto v) { return absl::StrCat("auto ", v); },
  };

  EXPECT_EQ("int 1", overloaded(1));
  EXPECT_EQ("double 2.5", overloaded(2.5));
  EXPECT_EQ("const char* hello", overloaded("hello"));
  EXPECT_EQ("auto 1.5", overloaded(1.5f));
}

TEST(OverloadTest, DispatchConsidersNumberOfArguments) {
  auto overloaded = absl::Overload{
      [](int a) { return a + 1; },
      [](int a, int b) { return a * b; },
      []() -> absl::string_view { return "none"; },
  };

  EXPECT_EQ(3, overloaded(2));
  EXPECT_EQ(21, overloaded(3, 7));
  EXPECT_EQ("none", overloaded());
}

TEST(OverloadTest, SupportsConstantEvaluation) {
  auto overloaded = absl::Overload{
      [](int a) { return a + 1; },
      [](int a, int b) { return a * b; },
      []() -> absl::string_view { return "none"; },
  };

  static_assert(overloaded() == "none");
  static_assert(overloaded(2) == 3);
  static_assert(overloaded(3, 7) == 21);
}

TEST(OverloadTest, PropogatesDefaults) {
  auto overloaded = absl::Overload{
      [](int a, int b = 5) { return a * b; },
      [](double c) { return c; },
  };

  EXPECT_EQ(21, overloaded(3, 7));
  EXPECT_EQ(35, overloaded(7));
  EXPECT_EQ(2.5, overloaded(2.5));
}

TEST(OverloadTest, AmbiguousWithDefaultsNotInvocable) {
  auto overloaded = absl::Overload{
      [](int a, int b = 5) { return a * b; },
      [](int c) { return c; },
  };

  static_assert(!std::is_invocable_v<decltype(overloaded), int>);
  static_assert(std::is_invocable_v<decltype(overloaded), int, int>);
}

TEST(OverloadTest, AmbiguousDuplicatesNotInvocable) {
  auto overloaded = absl::Overload{
      [](int a) { return a; },
      [](int c) { return c; },
  };

  static_assert(!std::is_invocable_v<decltype(overloaded), int>);
}

TEST(OverloadTest, AmbiguousConversionNotInvocable) {
  auto overloaded = absl::Overload{
      [](uint16_t a) { return a; },
      [](uint64_t c) { return c; },
  };

  static_assert(!std::is_invocable_v<decltype(overloaded), int>);
}

TEST(OverloadTest, AmbiguousConversionWithAutoNotInvocable) {
  auto overloaded = absl::Overload{
      [](auto a) { return a; },
      [](auto c) { return c; },
  };

  static_assert(!std::is_invocable_v<decltype(overloaded), int>);
}

#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L

TEST(OverloadTest, AmbiguousConversionWithAutoAndTemplateNotInvocable) {
  auto overloaded = absl::Overload{
      [](auto a) { return a; },
      []<class T>(T c) { return c; },
  };

  static_assert(!std::is_invocable_v<decltype(overloaded), int>);
}

TEST(OverloadTest, DispatchConsidersTypeWithTemplateFallback) {
  auto overloaded = absl::Overload{
      [](int a) { return a; },
      []<class T>(T c) { return c * 2; },
  };

  EXPECT_EQ(7, overloaded(7));
  EXPECT_EQ(14.0, overloaded(7.0));
}

#endif  // ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L

TEST(OverloadTest, DispatchConsidersSfinae) {
  auto overloaded = absl::Overload{
      [](auto a) -> decltype(a + 1) { return a + 1; },
  };

  static_assert(std::is_invocable_v<decltype(overloaded), int>);
  static_assert(!std::is_invocable_v<decltype(overloaded), std::string>);
}

TEST(OverloadTest, VariantVisitDispatchesCorrectly) {
  absl::variant<int, double, std::string> v(1);
  auto overloaded = absl::Overload{
      [](int) -> absl::string_view { return "int"; },
      [](double) -> absl::string_view { return "double"; },
      [](const std::string&) -> absl::string_view { return "string"; },
  };

  EXPECT_EQ("int", absl::visit(overloaded, v));
  v = 1.1;
  EXPECT_EQ("double", absl::visit(overloaded, v));
  v = "hello";
  EXPECT_EQ("string", absl::visit(overloaded, v));
}

TEST(OverloadTest, VariantVisitWithAutoFallbackDispatchesCorrectly) {
  absl::variant<std::string, int32_t, int64_t> v(int32_t{1});
  auto overloaded = absl::Overload{
      [](const std::string& s) { return s.size(); },
      [](const auto& s) { return sizeof(s); },
  };

  EXPECT_EQ(4, absl::visit(overloaded, v));
  v = int64_t{1};
  EXPECT_EQ(8, absl::visit(overloaded, v));
  v = std::string("hello");
  EXPECT_EQ(5, absl::visit(overloaded, v));
}

// This API used to be exported as a function, so it should also work fine to
// use parantheses when initializing it.
TEST(OverloadTest, UseWithParentheses) {
  const auto overloaded =
      absl::Overload([](const std::string& s) { return s.size(); },
                     [](const auto& s) { return sizeof(s); });

  absl::variant<std::string, int32_t, int64_t> v(int32_t{1});
  EXPECT_EQ(4, absl::visit(overloaded, v));

  v = int64_t{1};
  EXPECT_EQ(8, absl::visit(overloaded, v));

  v = std::string("hello");
  EXPECT_EQ(5, absl::visit(overloaded, v));
}

TEST(OverloadTest, HasConstexprConstructor) {
  constexpr auto overloaded = absl::Overload{
      [](int v) { return absl::StrCat("int ", v); },
      [](double v) { return absl::StrCat("double ", v); },
      [](const char* v) { return absl::StrCat("const char* ", v); },
      [](auto v) { return absl::StrCat("auto ", v); },
  };

  EXPECT_EQ("int 1", overloaded(1));
  EXPECT_EQ("double 2.5", overloaded(2.5));
  EXPECT_EQ("const char* hello", overloaded("hello"));
  EXPECT_EQ("auto 1.5", overloaded(1.5f));
}

}  // namespace

#endif
