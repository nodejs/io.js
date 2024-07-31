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

#include "absl/debugging/internal/demangle.h"

#include <cstdlib>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/debugging/internal/stack_consumption.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
namespace {

using ::testing::ContainsRegex;

TEST(Demangle, FunctionTemplate) {
  char tmp[100];

  // template <typename T>
  // int foo(T);
  //
  // foo<int>(5);
  ASSERT_TRUE(Demangle("_Z3fooIiEiT_", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FunctionTemplateWithNesting) {
  char tmp[100];

  // template <typename T>
  // int foo(T);
  //
  // foo<Wrapper<int>>({ .value = 5 });
  ASSERT_TRUE(Demangle("_Z3fooI7WrapperIiEEiT_", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FunctionTemplateWithNonTypeParamConstraint) {
  char tmp[100];

  // template <std::integral T>
  // int foo(T);
  //
  // foo<int>(5);
  ASSERT_TRUE(Demangle("_Z3fooITkSt8integraliEiT_", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FunctionTemplateWithFunctionRequiresClause) {
  char tmp[100];

  // template <typename T>
  // int foo() requires std::integral<T>;
  //
  // foo<int>();
  ASSERT_TRUE(Demangle("_Z3fooIiEivQsr3stdE8integralIT_E", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FunctionWithTemplateParamRequiresClause) {
  char tmp[100];

  // template <typename T>
  //     requires std::integral<T>
  // int foo();
  //
  // foo<int>();
  ASSERT_TRUE(Demangle("_Z3fooIiQsr3stdE8integralIT_EEiv", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FunctionWithTemplateParamAndFunctionRequiresClauses) {
  char tmp[100];

  // template <typename T>
  //     requires std::integral<T>
  // int foo() requires std::integral<T>;
  //
  // foo<int>();
  ASSERT_TRUE(Demangle("_Z3fooIiQsr3stdE8integralIT_EEivQsr3stdE8integralIS0_E",
                       tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FunctionTemplateBacktracksOnMalformedRequiresClause) {
  char tmp[100];

  // template <typename T>
  // int foo(T);
  //
  // foo<int>(5);
  // Except there's an extra `Q` where the mangled requires clause would be.
  ASSERT_FALSE(Demangle("_Z3fooIiQEiT_", tmp, sizeof(tmp)));
}

TEST(Demangle, FunctionTemplateWithAutoParam) {
  char tmp[100];

  // template <auto>
  // void foo();
  //
  // foo<1>();
  ASSERT_TRUE(Demangle("_Z3fooITnDaLi1EEvv", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FunctionTemplateWithNonTypeParamPack) {
  char tmp[100];

  // template <int&..., typename T>
  // void foo(T);
  //
  // foo(2);
  ASSERT_TRUE(Demangle("_Z3fooITpTnRiJEiEvT0_", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FunctionTemplateTemplateParamWithConstrainedArg) {
  char tmp[100];

  // template <typename T>
  // concept True = true;
  //
  // template <typename T> requires True<T>
  // struct Fooer {};
  //
  // template <template <typename T> typename>
  // void foo() {}
  //
  // foo<Fooer>();
  ASSERT_TRUE(Demangle("_Z3fooITtTyE5FooerEvv", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, NonTemplateBuiltinType) {
  char tmp[100];

  // void foo(__my_builtin_type t);
  //
  // foo({});
  ASSERT_TRUE(Demangle("_Z3foou17__my_builtin_type", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo()");
}

TEST(Demangle, SingleArgTemplateBuiltinType) {
  char tmp[100];

  // template <typename T>
  // __my_builtin_type<T> foo();
  //
  // foo<int>();
  ASSERT_TRUE(Demangle("_Z3fooIiEu17__my_builtin_typeIT_Ev", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, FailsOnTwoArgTemplateBuiltinType) {
  char tmp[100];

  // template <typename T, typename U>
  // __my_builtin_type<T, U> foo();
  //
  // foo<int, char>();
  ASSERT_FALSE(
      Demangle("_Z3fooIicEu17__my_builtin_typeIT_T0_Ev", tmp, sizeof(tmp)));
}

TEST(Demangle, TemplateTemplateParamSubstitution) {
  char tmp[100];

  // template <typename T>
  // concept True = true;
  //
  // template<std::integral T, T> struct Foolable {};
  // template<template<typename T, T> typename> void foo() {}
  //
  // template void foo<Foolable>();
  ASSERT_TRUE(Demangle("_Z3fooITtTyTnTL0__E8FoolableEvv", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "foo<>()");
}

TEST(Demangle, TemplateParamSubstitutionWithGenericLambda) {
  char tmp[100];

  // template <typename>
  // struct Fooer {
  //     template <typename>
  //     void foo(decltype([](auto x, auto y) {})) {}
  // };
  //
  // Fooer<int> f;
  // f.foo<int>({});
  ASSERT_TRUE(
      Demangle("_ZN5FooerIiE3fooIiEEvNS0_UlTL0__TL0_0_E_E", tmp, sizeof(tmp)));
  EXPECT_STREQ(tmp, "Fooer<>::foo<>()");
}

// Test corner cases of boundary conditions.
TEST(Demangle, CornerCases) {
  char tmp[10];
  EXPECT_TRUE(Demangle("_Z6foobarv", tmp, sizeof(tmp)));
  // sizeof("foobar()") == 9
  EXPECT_STREQ("foobar()", tmp);
  EXPECT_TRUE(Demangle("_Z6foobarv", tmp, 9));
  EXPECT_STREQ("foobar()", tmp);
  EXPECT_FALSE(Demangle("_Z6foobarv", tmp, 8));  // Not enough.
  EXPECT_FALSE(Demangle("_Z6foobarv", tmp, 1));
  EXPECT_FALSE(Demangle("_Z6foobarv", tmp, 0));
  EXPECT_FALSE(Demangle("_Z6foobarv", nullptr, 0));  // Should not cause SEGV.
  EXPECT_FALSE(Demangle("_Z1000000", tmp, 9));
}

// Test handling of functions suffixed with .clone.N, which is used
// by GCC 4.5.x (and our locally-modified version of GCC 4.4.x), and
// .constprop.N and .isra.N, which are used by GCC 4.6.x.  These
// suffixes are used to indicate functions which have been cloned
// during optimization.  We ignore these suffixes.
TEST(Demangle, Clones) {
  char tmp[20];
  EXPECT_TRUE(Demangle("_ZL3Foov", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.clone.3", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.constprop.80", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.isra.18", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.isra.2.constprop.18", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  // Demangle suffixes produced by -funique-internal-linkage-names.
  EXPECT_TRUE(Demangle("_ZL3Foov.__uniq.12345", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.__uniq.12345.isra.2.constprop.18", tmp,
                       sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  // Suffixes without the number should also demangle.
  EXPECT_TRUE(Demangle("_ZL3Foov.clo", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  // Suffixes with just the number should also demangle.
  EXPECT_TRUE(Demangle("_ZL3Foov.123", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  // (.clone. followed by non-number), should also demangle.
  EXPECT_TRUE(Demangle("_ZL3Foov.clone.foo", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  // (.clone. followed by multiple numbers), should also demangle.
  EXPECT_TRUE(Demangle("_ZL3Foov.clone.123.456", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  // (a long valid suffix), should demangle.
  EXPECT_TRUE(Demangle("_ZL3Foov.part.9.165493.constprop.775.31805", tmp,
                       sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  // Invalid (. without anything else), should not demangle.
  EXPECT_FALSE(Demangle("_ZL3Foov.", tmp, sizeof(tmp)));
  // Invalid (. with mix of alpha and digits), should not demangle.
  EXPECT_FALSE(Demangle("_ZL3Foov.abc123", tmp, sizeof(tmp)));
  // Invalid (.clone. not followed by number), should not demangle.
  EXPECT_FALSE(Demangle("_ZL3Foov.clone.", tmp, sizeof(tmp)));
  // Invalid (.constprop. not followed by number), should not demangle.
  EXPECT_FALSE(Demangle("_ZL3Foov.isra.2.constprop.", tmp, sizeof(tmp)));
}

// Test the GNU abi_tag extension.
TEST(Demangle, AbiTags) {
  char tmp[80];

  // Mangled name generated via:
  // struct [[gnu::abi_tag("abc")]] A{};
  // A a;
  EXPECT_TRUE(Demangle("_Z1aB3abc", tmp, sizeof(tmp)));
  EXPECT_STREQ("a[abi:abc]", tmp);

  // Mangled name generated via:
  // struct B {
  //   B [[gnu::abi_tag("xyz")]] (){};
  // };
  // B b;
  EXPECT_TRUE(Demangle("_ZN1BC2B3xyzEv", tmp, sizeof(tmp)));
  EXPECT_STREQ("B::B[abi:xyz]()", tmp);

  // Mangled name generated via:
  // [[gnu::abi_tag("foo", "bar")]] void C() {}
  EXPECT_TRUE(Demangle("_Z1CB3barB3foov", tmp, sizeof(tmp)));
  EXPECT_STREQ("C[abi:bar][abi:foo]()", tmp);
}

// Tests that verify that Demangle footprint is within some limit.
// They are not to be run under sanitizers as the sanitizers increase
// stack consumption by about 4x.
#if defined(ABSL_INTERNAL_HAVE_DEBUGGING_STACK_CONSUMPTION) && \
    !defined(ABSL_HAVE_ADDRESS_SANITIZER) &&                   \
    !defined(ABSL_HAVE_MEMORY_SANITIZER) &&                    \
    !defined(ABSL_HAVE_THREAD_SANITIZER)

static const char *g_mangled;
static char g_demangle_buffer[4096];
static char *g_demangle_result;

static void DemangleSignalHandler(int signo) {
  if (Demangle(g_mangled, g_demangle_buffer, sizeof(g_demangle_buffer))) {
    g_demangle_result = g_demangle_buffer;
  } else {
    g_demangle_result = nullptr;
  }
}

// Call Demangle and figure out the stack footprint of this call.
static const char *DemangleStackConsumption(const char *mangled,
                                            int *stack_consumed) {
  g_mangled = mangled;
  *stack_consumed = GetSignalHandlerStackConsumption(DemangleSignalHandler);
  LOG(INFO) << "Stack consumption of Demangle: " << *stack_consumed;
  return g_demangle_result;
}

// Demangle stack consumption should be within 8kB for simple mangled names
// with some level of nesting. With alternate signal stack we have 64K,
// but some signal handlers run on thread stack, and could have arbitrarily
// little space left (so we don't want to make this number too large).
const int kStackConsumptionUpperLimit = 8192;

// Returns a mangled name nested to the given depth.
static std::string NestedMangledName(int depth) {
  std::string mangled_name = "_Z1a";
  if (depth > 0) {
    mangled_name += "IXL";
    mangled_name += NestedMangledName(depth - 1);
    mangled_name += "EEE";
  }
  return mangled_name;
}

TEST(Demangle, DemangleStackConsumption) {
  // Measure stack consumption of Demangle for nested mangled names of varying
  // depth.  Since Demangle is implemented as a recursive descent parser,
  // stack consumption will grow as the nesting depth increases.  By measuring
  // the stack consumption for increasing depths, we can see the growing
  // impact of any stack-saving changes made to the code for Demangle.
  int stack_consumed = 0;

  const char *demangled =
      DemangleStackConsumption("_Z6foobarv", &stack_consumed);
  EXPECT_STREQ("foobar()", demangled);
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);

  const std::string nested_mangled_name0 = NestedMangledName(0);
  demangled = DemangleStackConsumption(nested_mangled_name0.c_str(),
                                       &stack_consumed);
  EXPECT_STREQ("a", demangled);
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);

  const std::string nested_mangled_name1 = NestedMangledName(1);
  demangled = DemangleStackConsumption(nested_mangled_name1.c_str(),
                                       &stack_consumed);
  EXPECT_STREQ("a<>", demangled);
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);

  const std::string nested_mangled_name2 = NestedMangledName(2);
  demangled = DemangleStackConsumption(nested_mangled_name2.c_str(),
                                       &stack_consumed);
  EXPECT_STREQ("a<>", demangled);
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);

  const std::string nested_mangled_name3 = NestedMangledName(3);
  demangled = DemangleStackConsumption(nested_mangled_name3.c_str(),
                                       &stack_consumed);
  EXPECT_STREQ("a<>", demangled);
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);
}

#endif  // Stack consumption tests

static void TestOnInput(const char* input) {
  static const int kOutSize = 1048576;
  auto out = absl::make_unique<char[]>(kOutSize);
  Demangle(input, out.get(), kOutSize);
}

TEST(DemangleRegression, NegativeLength) {
  TestOnInput("_ZZn4");
}

TEST(DemangleRegression, DeeplyNestedArrayType) {
  const int depth = 100000;
  std::string data = "_ZStI";
  data.reserve(data.size() + 3 * depth + 1);
  for (int i = 0; i < depth; i++) {
    data += "A1_";
  }
  TestOnInput(data.c_str());
}

struct Base {
  virtual ~Base() = default;
};

struct Derived : public Base {};

TEST(DemangleStringTest, SupportsSymbolNameReturnedByTypeId) {
  EXPECT_EQ(DemangleString(typeid(int).name()), "int");
  // We want to test that `DemangleString` can demangle the symbol names
  // returned by `typeid`, but without hard-coding the actual demangled values
  // (because they are platform-specific).
  EXPECT_THAT(
      DemangleString(typeid(Base).name()),
      ContainsRegex("absl.*debugging_internal.*anonymous namespace.*::Base"));
  EXPECT_THAT(DemangleString(typeid(Derived).name()),
              ContainsRegex(
                  "absl.*debugging_internal.*anonymous namespace.*::Derived"));
}

}  // namespace
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
