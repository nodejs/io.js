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

#include "absl/functional/bind_front.h"

#include <stddef.h>

#include <functional>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"

namespace {

char CharAt(const char* s, size_t index) { return s[index]; }

TEST(BindTest, Basics) {
  EXPECT_EQ('C', absl::bind_front(CharAt)("ABC", 2));
  EXPECT_EQ('C', absl::bind_front(CharAt, "ABC")(2));
  EXPECT_EQ('C', absl::bind_front(CharAt, "ABC", 2)());
}

TEST(BindTest, Lambda) {
  auto lambda = [](int x, int y, int z) { return x + y + z; };
  EXPECT_EQ(6, absl::bind_front(lambda)(1, 2, 3));
  EXPECT_EQ(6, absl::bind_front(lambda, 1)(2, 3));
  EXPECT_EQ(6, absl::bind_front(lambda, 1, 2)(3));
  EXPECT_EQ(6, absl::bind_front(lambda, 1, 2, 3)());
}

struct Functor {
  std::string operator()() & { return "&"; }
  std::string operator()() const& { return "const&"; }
  std::string operator()() && { return "&&"; }
  std::string operator()() const&& { return "const&&"; }
};

TEST(BindTest, PerfectForwardingOfBoundArgs) {
  auto f = absl::bind_front(Functor());
  const auto& cf = f;
  EXPECT_EQ("&", f());
  EXPECT_EQ("const&", cf());
  EXPECT_EQ("&&", std::move(f)());
  EXPECT_EQ("const&&", std::move(cf)());
}

struct ArgDescribe {
  std::string operator()(int&) const { return "&"; }             // NOLINT
  std::string operator()(const int&) const { return "const&"; }  // NOLINT
  std::string operator()(int&&) const { return "&&"; }
  std::string operator()(const int&&) const { return "const&&"; }
};

TEST(BindTest, PerfectForwardingOfFreeArgs) {
  ArgDescribe f;
  int i;
  EXPECT_EQ("&", absl::bind_front(f)(static_cast<int&>(i)));
  EXPECT_EQ("const&", absl::bind_front(f)(static_cast<const int&>(i)));
  EXPECT_EQ("&&", absl::bind_front(f)(static_cast<int&&>(i)));
  EXPECT_EQ("const&&", absl::bind_front(f)(static_cast<const int&&>(i)));
}

struct NonCopyableFunctor {
  NonCopyableFunctor() = default;
  NonCopyableFunctor(const NonCopyableFunctor&) = delete;
  NonCopyableFunctor& operator=(const NonCopyableFunctor&) = delete;
  const NonCopyableFunctor* operator()() const { return this; }
};

TEST(BindTest, RefToFunctor) {
  // It won't copy/move the functor and use the original object.
  NonCopyableFunctor ncf;
  auto bound_ncf = absl::bind_front(std::ref(ncf));
  auto bound_ncf_copy = bound_ncf;
  EXPECT_EQ(&ncf, bound_ncf_copy());
}

struct Struct {
  std::string value;
};

TEST(BindTest, StoreByCopy) {
  Struct s = {"hello"};
  auto f = absl::bind_front(&Struct::value, s);
  auto g = f;
  EXPECT_EQ("hello", f());
  EXPECT_EQ("hello", g());
  EXPECT_NE(&s.value, &f());
  EXPECT_NE(&s.value, &g());
  EXPECT_NE(&g(), &f());
}

struct NonCopyable {
  explicit NonCopyable(const std::string& s) : value(s) {}
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;

  std::string value;
};

const std::string& GetNonCopyableValue(const NonCopyable& n) { return n.value; }

TEST(BindTest, StoreByRef) {
  NonCopyable s("hello");
  auto f = absl::bind_front(&GetNonCopyableValue, std::ref(s));
  EXPECT_EQ("hello", f());
  EXPECT_EQ(&s.value, &f());
  auto g = std::move(f);  // NOLINT
  EXPECT_EQ("hello", g());
  EXPECT_EQ(&s.value, &g());
  s.value = "goodbye";
  EXPECT_EQ("goodbye", g());
}

TEST(BindTest, StoreByCRef) {
  NonCopyable s("hello");
  auto f = absl::bind_front(&GetNonCopyableValue, std::cref(s));
  EXPECT_EQ("hello", f());
  EXPECT_EQ(&s.value, &f());
  auto g = std::move(f);  // NOLINT
  EXPECT_EQ("hello", g());
  EXPECT_EQ(&s.value, &g());
  s.value = "goodbye";
  EXPECT_EQ("goodbye", g());
}

const std::string& GetNonCopyableValueByWrapper(
    std::reference_wrapper<NonCopyable> n) {
  return n.get().value;
}

TEST(BindTest, StoreByRefInvokeByWrapper) {
  NonCopyable s("hello");
  auto f = absl::bind_front(GetNonCopyableValueByWrapper, std::ref(s));
  EXPECT_EQ("hello", f());
  EXPECT_EQ(&s.value, &f());
  auto g = std::move(f);
  EXPECT_EQ("hello", g());
  EXPECT_EQ(&s.value, &g());
  s.value = "goodbye";
  EXPECT_EQ("goodbye", g());
}

TEST(BindTest, StoreByPointer) {
  NonCopyable s("hello");
  auto f = absl::bind_front(&NonCopyable::value, &s);
  EXPECT_EQ("hello", f());
  EXPECT_EQ(&s.value, &f());
  auto g = std::move(f);
  EXPECT_EQ("hello", g());
  EXPECT_EQ(&s.value, &g());
}

int Sink(std::unique_ptr<int> p) {
  return *p;
}

std::unique_ptr<int> Factory(int n) { return absl::make_unique<int>(n); }

TEST(BindTest, NonCopyableArg) {
  EXPECT_EQ(42, absl::bind_front(Sink)(absl::make_unique<int>(42)));
  EXPECT_EQ(42, absl::bind_front(Sink, absl::make_unique<int>(42))());
}

TEST(BindTest, NonCopyableResult) {
  EXPECT_THAT(absl::bind_front(Factory)(42), ::testing::Pointee(42));
  EXPECT_THAT(absl::bind_front(Factory, 42)(), ::testing::Pointee(42));
}

// is_copy_constructible<FalseCopyable<unique_ptr<T>> is true but an attempt to
// instantiate the copy constructor leads to a compile error. This is similar
// to how standard containers behave.
template <class T>
struct FalseCopyable {
  FalseCopyable() {}
  FalseCopyable(const FalseCopyable& other) : m(other.m) {}
  FalseCopyable(FalseCopyable&& other) : m(std::move(other.m)) {}
  T m;
};

int GetMember(FalseCopyable<std::unique_ptr<int>> x) { return *x.m; }

TEST(BindTest, WrappedMoveOnly) {
  FalseCopyable<std::unique_ptr<int>> x;
  x.m = absl::make_unique<int>(42);
  auto f = absl::bind_front(&GetMember, std::move(x));
  EXPECT_EQ(42, std::move(f)());
}

int Plus(int a, int b) { return a + b; }

TEST(BindTest, ConstExpr) {
  constexpr auto f = absl::bind_front(CharAt);
  EXPECT_EQ(f("ABC", 1), 'B');
  static constexpr int five = 5;
  constexpr auto plus5 = absl::bind_front(Plus, five);
  EXPECT_EQ(plus5(1), 6);

  // There seems to be a bug in MSVC dealing constexpr construction of
  // char[]. Notice 'plus5' above; 'int' works just fine.
#if !(defined(_MSC_VER) && _MSC_VER < 1910)
  static constexpr char data[] = "DEF";
  constexpr auto g = absl::bind_front(CharAt, data);
  EXPECT_EQ(g(1), 'E');
#endif
}

struct ManglingCall {
  int operator()(int, double, std::string) const { return 0; }
};

TEST(BindTest, Mangling) {
  // We just want to generate a particular instantiation to see its mangling.
  absl::bind_front(ManglingCall{}, 1, 3.3)("A");
}

}  // namespace
