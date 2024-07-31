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

#include "absl/meta/type_traits.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace {

using ::testing::StaticAssertTypeEq;

template <class T, class U>
struct simple_pair {
  T first;
  U second;
};

struct Dummy {};

struct ReturnType {};
struct ConvertibleToReturnType {
  operator ReturnType() const;  // NOLINT
};

// Unique types used as parameter types for testing the detection idiom.
struct StructA {};
struct StructB {};
struct StructC {};

struct TypeWithBarFunction {
  template <class T,
            absl::enable_if_t<std::is_same<T&&, StructA&>::value, int> = 0>
  ReturnType bar(T&&, const StructB&, StructC&&) &&;  // NOLINT
};

struct TypeWithBarFunctionAndConvertibleReturnType {
  template <class T,
            absl::enable_if_t<std::is_same<T&&, StructA&>::value, int> = 0>
  ConvertibleToReturnType bar(T&&, const StructB&, StructC&&) &&;  // NOLINT
};

template <class Class, class... Ts>
using BarIsCallableImpl =
    decltype(std::declval<Class>().bar(std::declval<Ts>()...));

template <class Class, class... T>
using BarIsCallable =
    absl::type_traits_internal::is_detected<BarIsCallableImpl, Class, T...>;

template <class Class, class... T>
using BarIsCallableConv = absl::type_traits_internal::is_detected_convertible<
    ReturnType, BarIsCallableImpl, Class, T...>;

// NOTE: Test of detail type_traits_internal::is_detected.
TEST(IsDetectedTest, BasicUsage) {
  EXPECT_TRUE((BarIsCallable<TypeWithBarFunction, StructA&, const StructB&,
                             StructC>::value));
  EXPECT_TRUE(
      (BarIsCallable<TypeWithBarFunction, StructA&, StructB&, StructC>::value));
  EXPECT_TRUE(
      (BarIsCallable<TypeWithBarFunction, StructA&, StructB, StructC>::value));

  EXPECT_FALSE((BarIsCallable<int, StructA&, const StructB&, StructC>::value));
  EXPECT_FALSE((BarIsCallable<TypeWithBarFunction&, StructA&, const StructB&,
                              StructC>::value));
  EXPECT_FALSE((BarIsCallable<TypeWithBarFunction, StructA, const StructB&,
                              StructC>::value));
}

// NOTE: Test of detail type_traits_internal::is_detected_convertible.
TEST(IsDetectedConvertibleTest, BasicUsage) {
  EXPECT_TRUE((BarIsCallableConv<TypeWithBarFunction, StructA&, const StructB&,
                                 StructC>::value));
  EXPECT_TRUE((BarIsCallableConv<TypeWithBarFunction, StructA&, StructB&,
                                 StructC>::value));
  EXPECT_TRUE((BarIsCallableConv<TypeWithBarFunction, StructA&, StructB,
                                 StructC>::value));
  EXPECT_TRUE((BarIsCallableConv<TypeWithBarFunctionAndConvertibleReturnType,
                                 StructA&, const StructB&, StructC>::value));
  EXPECT_TRUE((BarIsCallableConv<TypeWithBarFunctionAndConvertibleReturnType,
                                 StructA&, StructB&, StructC>::value));
  EXPECT_TRUE((BarIsCallableConv<TypeWithBarFunctionAndConvertibleReturnType,
                                 StructA&, StructB, StructC>::value));

  EXPECT_FALSE(
      (BarIsCallableConv<int, StructA&, const StructB&, StructC>::value));
  EXPECT_FALSE((BarIsCallableConv<TypeWithBarFunction&, StructA&,
                                  const StructB&, StructC>::value));
  EXPECT_FALSE((BarIsCallableConv<TypeWithBarFunction, StructA, const StructB&,
                                  StructC>::value));
  EXPECT_FALSE((BarIsCallableConv<TypeWithBarFunctionAndConvertibleReturnType&,
                                  StructA&, const StructB&, StructC>::value));
  EXPECT_FALSE((BarIsCallableConv<TypeWithBarFunctionAndConvertibleReturnType,
                                  StructA, const StructB&, StructC>::value));
}

TEST(VoidTTest, BasicUsage) {
  StaticAssertTypeEq<void, absl::void_t<Dummy>>();
  StaticAssertTypeEq<void, absl::void_t<Dummy, Dummy, Dummy>>();
}

TEST(ConjunctionTest, BasicBooleanLogic) {
  EXPECT_TRUE(absl::conjunction<>::value);
  EXPECT_TRUE(absl::conjunction<std::true_type>::value);
  EXPECT_TRUE((absl::conjunction<std::true_type, std::true_type>::value));
  EXPECT_FALSE((absl::conjunction<std::true_type, std::false_type>::value));
  EXPECT_FALSE((absl::conjunction<std::false_type, std::true_type>::value));
  EXPECT_FALSE((absl::conjunction<std::false_type, std::false_type>::value));
}

struct MyTrueType {
  static constexpr bool value = true;
};

struct MyFalseType {
  static constexpr bool value = false;
};

TEST(ConjunctionTest, ShortCircuiting) {
  EXPECT_FALSE(
      (absl::conjunction<std::true_type, std::false_type, Dummy>::value));
  EXPECT_TRUE((std::is_base_of<MyFalseType,
                               absl::conjunction<std::true_type, MyFalseType,
                                                 std::false_type>>::value));
  EXPECT_TRUE(
      (std::is_base_of<MyTrueType,
                       absl::conjunction<std::true_type, MyTrueType>>::value));
}

TEST(DisjunctionTest, BasicBooleanLogic) {
  EXPECT_FALSE(absl::disjunction<>::value);
  EXPECT_FALSE(absl::disjunction<std::false_type>::value);
  EXPECT_TRUE((absl::disjunction<std::true_type, std::true_type>::value));
  EXPECT_TRUE((absl::disjunction<std::true_type, std::false_type>::value));
  EXPECT_TRUE((absl::disjunction<std::false_type, std::true_type>::value));
  EXPECT_FALSE((absl::disjunction<std::false_type, std::false_type>::value));
}

TEST(DisjunctionTest, ShortCircuiting) {
  EXPECT_TRUE(
      (absl::disjunction<std::false_type, std::true_type, Dummy>::value));
  EXPECT_TRUE((
      std::is_base_of<MyTrueType, absl::disjunction<std::false_type, MyTrueType,
                                                    std::true_type>>::value));
  EXPECT_TRUE((
      std::is_base_of<MyFalseType,
                      absl::disjunction<std::false_type, MyFalseType>>::value));
}

TEST(NegationTest, BasicBooleanLogic) {
  EXPECT_FALSE(absl::negation<std::true_type>::value);
  EXPECT_FALSE(absl::negation<MyTrueType>::value);
  EXPECT_TRUE(absl::negation<std::false_type>::value);
  EXPECT_TRUE(absl::negation<MyFalseType>::value);
}

// all member functions are trivial
class Trivial {
  int n_;
};

struct TrivialDestructor {
  ~TrivialDestructor() = default;
};

struct NontrivialDestructor {
  ~NontrivialDestructor() {}
};

struct DeletedDestructor {
  ~DeletedDestructor() = delete;
};

class TrivialDefaultCtor {
 public:
  TrivialDefaultCtor() = default;
  explicit TrivialDefaultCtor(int n) : n_(n) {}

 private:
  int n_;
};

class NontrivialDefaultCtor {
 public:
  NontrivialDefaultCtor() : n_(1) {}

 private:
  int n_;
};

class DeletedDefaultCtor {
 public:
  DeletedDefaultCtor() = delete;
  explicit DeletedDefaultCtor(int n) : n_(n) {}

 private:
  int n_;
};

class TrivialMoveCtor {
 public:
  explicit TrivialMoveCtor(int n) : n_(n) {}
  TrivialMoveCtor(TrivialMoveCtor&&) = default;
  TrivialMoveCtor& operator=(const TrivialMoveCtor& t) {
    n_ = t.n_;
    return *this;
  }

 private:
  int n_;
};

class NontrivialMoveCtor {
 public:
  explicit NontrivialMoveCtor(int n) : n_(n) {}
  NontrivialMoveCtor(NontrivialMoveCtor&& t) noexcept : n_(t.n_) {}
  NontrivialMoveCtor& operator=(const NontrivialMoveCtor&) = default;

 private:
  int n_;
};

class TrivialCopyCtor {
 public:
  explicit TrivialCopyCtor(int n) : n_(n) {}
  TrivialCopyCtor(const TrivialCopyCtor&) = default;
  TrivialCopyCtor& operator=(const TrivialCopyCtor& t) {
    n_ = t.n_;
    return *this;
  }

 private:
  int n_;
};

class NontrivialCopyCtor {
 public:
  explicit NontrivialCopyCtor(int n) : n_(n) {}
  NontrivialCopyCtor(const NontrivialCopyCtor& t) : n_(t.n_) {}
  NontrivialCopyCtor& operator=(const NontrivialCopyCtor&) = default;

 private:
  int n_;
};

class DeletedCopyCtor {
 public:
  explicit DeletedCopyCtor(int n) : n_(n) {}
  DeletedCopyCtor(const DeletedCopyCtor&) = delete;
  DeletedCopyCtor& operator=(const DeletedCopyCtor&) = default;

 private:
  int n_;
};

class TrivialMoveAssign {
 public:
  explicit TrivialMoveAssign(int n) : n_(n) {}
  TrivialMoveAssign(const TrivialMoveAssign& t) : n_(t.n_) {}
  TrivialMoveAssign& operator=(TrivialMoveAssign&&) = default;
  ~TrivialMoveAssign() {}  // can have nontrivial destructor
 private:
  int n_;
};

class NontrivialMoveAssign {
 public:
  explicit NontrivialMoveAssign(int n) : n_(n) {}
  NontrivialMoveAssign(const NontrivialMoveAssign&) = default;
  NontrivialMoveAssign& operator=(NontrivialMoveAssign&& t) noexcept {
    n_ = t.n_;
    return *this;
  }

 private:
  int n_;
};

class TrivialCopyAssign {
 public:
  explicit TrivialCopyAssign(int n) : n_(n) {}
  TrivialCopyAssign(const TrivialCopyAssign& t) : n_(t.n_) {}
  TrivialCopyAssign& operator=(const TrivialCopyAssign& t) = default;
  ~TrivialCopyAssign() {}  // can have nontrivial destructor
 private:
  int n_;
};

class NontrivialCopyAssign {
 public:
  explicit NontrivialCopyAssign(int n) : n_(n) {}
  NontrivialCopyAssign(const NontrivialCopyAssign&) = default;
  NontrivialCopyAssign& operator=(const NontrivialCopyAssign& t) {
    n_ = t.n_;
    return *this;
  }

 private:
  int n_;
};

class DeletedCopyAssign {
 public:
  explicit DeletedCopyAssign(int n) : n_(n) {}
  DeletedCopyAssign(const DeletedCopyAssign&) = default;
  DeletedCopyAssign& operator=(const DeletedCopyAssign&) = delete;

 private:
  int n_;
};

struct MovableNonCopyable {
  MovableNonCopyable() = default;
  MovableNonCopyable(const MovableNonCopyable&) = delete;
  MovableNonCopyable(MovableNonCopyable&&) = default;
  MovableNonCopyable& operator=(const MovableNonCopyable&) = delete;
  MovableNonCopyable& operator=(MovableNonCopyable&&) = default;
};

struct NonCopyableOrMovable {
  NonCopyableOrMovable() = default;
  virtual ~NonCopyableOrMovable() = default;
  NonCopyableOrMovable(const NonCopyableOrMovable&) = delete;
  NonCopyableOrMovable(NonCopyableOrMovable&&) = delete;
  NonCopyableOrMovable& operator=(const NonCopyableOrMovable&) = delete;
  NonCopyableOrMovable& operator=(NonCopyableOrMovable&&) = delete;
};

class Base {
 public:
  virtual ~Base() {}
};

TEST(TypeTraitsTest, TestIsFunction) {
  struct Callable {
    void operator()() {}
  };
  EXPECT_TRUE(absl::is_function<void()>::value);
  EXPECT_TRUE(absl::is_function<void()&>::value);
  EXPECT_TRUE(absl::is_function<void() const>::value);
  EXPECT_TRUE(absl::is_function<void() noexcept>::value);
  EXPECT_TRUE(absl::is_function<void(...) noexcept>::value);

  EXPECT_FALSE(absl::is_function<void (*)()>::value);
  EXPECT_FALSE(absl::is_function<void (&)()>::value);
  EXPECT_FALSE(absl::is_function<int>::value);
  EXPECT_FALSE(absl::is_function<Callable>::value);
}

TEST(TypeTraitsTest, TestRemoveCVRef) {
  EXPECT_TRUE(
      (std::is_same<typename absl::remove_cvref<int>::type, int>::value));
  EXPECT_TRUE(
      (std::is_same<typename absl::remove_cvref<int&>::type, int>::value));
  EXPECT_TRUE(
      (std::is_same<typename absl::remove_cvref<int&&>::type, int>::value));
  EXPECT_TRUE((
      std::is_same<typename absl::remove_cvref<const int&>::type, int>::value));
  EXPECT_TRUE(
      (std::is_same<typename absl::remove_cvref<int*>::type, int*>::value));
  // Does not remove const in this case.
  EXPECT_TRUE((std::is_same<typename absl::remove_cvref<const int*>::type,
                            const int*>::value));
  EXPECT_TRUE(
      (std::is_same<typename absl::remove_cvref<int[2]>::type, int[2]>::value));
  EXPECT_TRUE((std::is_same<typename absl::remove_cvref<int(&)[2]>::type,
                            int[2]>::value));
  EXPECT_TRUE((std::is_same<typename absl::remove_cvref<int(&&)[2]>::type,
                            int[2]>::value));
  EXPECT_TRUE((std::is_same<typename absl::remove_cvref<const int[2]>::type,
                            int[2]>::value));
  EXPECT_TRUE((std::is_same<typename absl::remove_cvref<const int(&)[2]>::type,
                            int[2]>::value));
  EXPECT_TRUE((std::is_same<typename absl::remove_cvref<const int(&&)[2]>::type,
                            int[2]>::value));
}

#define ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(trait_name, ...)          \
  EXPECT_TRUE((std::is_same<typename std::trait_name<__VA_ARGS__>::type, \
                            absl::trait_name##_t<__VA_ARGS__>>::value))

TEST(TypeTraitsTest, TestRemoveCVAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_cv, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_cv, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_cv, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_cv, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_const, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_const, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_const, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_const, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_volatile, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_volatile, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_volatile, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_volatile, const volatile int);
}

TEST(TypeTraitsTest, TestAddCVAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_cv, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_cv, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_cv, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_cv, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_const, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_const, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_const, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_const, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_volatile, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_volatile, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_volatile, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_volatile, const volatile int);
}

TEST(TypeTraitsTest, TestReferenceAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, int&&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_reference, volatile int&&);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, int&&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_lvalue_reference, volatile int&&);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, int&&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_rvalue_reference, volatile int&&);
}

TEST(TypeTraitsTest, TestPointerAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_pointer, int*);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_pointer, volatile int*);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_pointer, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(add_pointer, volatile int);
}

TEST(TypeTraitsTest, TestSignednessAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_signed, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_signed, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_signed, unsigned);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_signed, volatile unsigned);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_unsigned, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_unsigned, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_unsigned, unsigned);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(make_unsigned, volatile unsigned);
}

TEST(TypeTraitsTest, TestExtentAliases) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_extent, int[]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_extent, int[1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_extent, int[1][1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_extent, int[][1]);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_all_extents, int[]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_all_extents, int[1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_all_extents, int[1][1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(remove_all_extents, int[][1]);
}

TEST(TypeTraitsTest, TestAlignedStorageAlias) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 1);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 2);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 3);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 4);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 5);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 6);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 7);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 8);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 9);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 10);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 11);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 12);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 13);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 14);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 15);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 16);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 17);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 18);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 19);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 20);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 21);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 22);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 23);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 24);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 25);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 26);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 27);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 28);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 29);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 30);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 31);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 32);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 33);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 1, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 2, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 3, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 4, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 5, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 6, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 7, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 8, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 9, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 10, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 11, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 12, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 13, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 14, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 15, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 16, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 17, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 18, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 19, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 20, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 21, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 22, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 23, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 24, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 25, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 26, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 27, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 28, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 29, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 30, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 31, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 32, 128);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(aligned_storage, 33, 128);
}

TEST(TypeTraitsTest, TestDecay) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, volatile int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const volatile int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const volatile int&);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, volatile int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, const volatile int&);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int[1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int[1][1]);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int[][1]);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int());
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int(float));      // NOLINT
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(decay, int(char, ...));  // NOLINT
}

struct TypeA {};
struct TypeB {};
struct TypeC {};
struct TypeD {};

template <typename T>
struct Wrap {};

enum class TypeEnum { A, B, C, D };

struct GetTypeT {
  template <typename T,
            absl::enable_if_t<std::is_same<T, TypeA>::value, int> = 0>
  TypeEnum operator()(Wrap<T>) const {
    return TypeEnum::A;
  }

  template <typename T,
            absl::enable_if_t<std::is_same<T, TypeB>::value, int> = 0>
  TypeEnum operator()(Wrap<T>) const {
    return TypeEnum::B;
  }

  template <typename T,
            absl::enable_if_t<std::is_same<T, TypeC>::value, int> = 0>
  TypeEnum operator()(Wrap<T>) const {
    return TypeEnum::C;
  }

  // NOTE: TypeD is intentionally not handled
} constexpr GetType = {};

TEST(TypeTraitsTest, TestEnableIf) {
  EXPECT_EQ(TypeEnum::A, GetType(Wrap<TypeA>()));
  EXPECT_EQ(TypeEnum::B, GetType(Wrap<TypeB>()));
  EXPECT_EQ(TypeEnum::C, GetType(Wrap<TypeC>()));
}

TEST(TypeTraitsTest, TestConditional) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(conditional, true, int, char);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(conditional, false, int, char);
}

// TODO(calabrese) Check with specialized std::common_type
TEST(TypeTraitsTest, TestCommonType) {
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int, char);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int, char, int);

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int, char&);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(common_type, int, char, int&);
}

TEST(TypeTraitsTest, TestUnderlyingType) {
  enum class enum_char : char {};
  enum class enum_long_long : long long {};  // NOLINT(runtime/int)

  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(underlying_type, enum_char);
  ABSL_INTERNAL_EXPECT_ALIAS_EQUIVALENCE(underlying_type, enum_long_long);
}

struct GetTypeExtT {
  template <typename T>
  absl::result_of_t<const GetTypeT&(T)> operator()(T&& arg) const {
    return GetType(std::forward<T>(arg));
  }

  TypeEnum operator()(Wrap<TypeD>) const { return TypeEnum::D; }
} constexpr GetTypeExt = {};

TEST(TypeTraitsTest, TestResultOf) {
  EXPECT_EQ(TypeEnum::A, GetTypeExt(Wrap<TypeA>()));
  EXPECT_EQ(TypeEnum::B, GetTypeExt(Wrap<TypeB>()));
  EXPECT_EQ(TypeEnum::C, GetTypeExt(Wrap<TypeC>()));
  EXPECT_EQ(TypeEnum::D, GetTypeExt(Wrap<TypeD>()));
}

namespace adl_namespace {

struct DeletedSwap {};

void swap(DeletedSwap&, DeletedSwap&) = delete;

struct SpecialNoexceptSwap {
  SpecialNoexceptSwap(SpecialNoexceptSwap&&) {}
  SpecialNoexceptSwap& operator=(SpecialNoexceptSwap&&) { return *this; }
  ~SpecialNoexceptSwap() = default;
};

void swap(SpecialNoexceptSwap&, SpecialNoexceptSwap&) noexcept {}

}  // namespace adl_namespace

TEST(TypeTraitsTest, IsSwappable) {
  using absl::type_traits_internal::IsSwappable;
  using absl::type_traits_internal::StdSwapIsUnconstrained;

  EXPECT_TRUE(IsSwappable<int>::value);

  struct S {};
  EXPECT_TRUE(IsSwappable<S>::value);

  struct NoConstruct {
    NoConstruct(NoConstruct&&) = delete;
    NoConstruct& operator=(NoConstruct&&) { return *this; }
    ~NoConstruct() = default;
  };

  EXPECT_EQ(IsSwappable<NoConstruct>::value, StdSwapIsUnconstrained::value);
  struct NoAssign {
    NoAssign(NoAssign&&) {}
    NoAssign& operator=(NoAssign&&) = delete;
    ~NoAssign() = default;
  };

  EXPECT_EQ(IsSwappable<NoAssign>::value, StdSwapIsUnconstrained::value);

  EXPECT_FALSE(IsSwappable<adl_namespace::DeletedSwap>::value);

  EXPECT_TRUE(IsSwappable<adl_namespace::SpecialNoexceptSwap>::value);
}

TEST(TypeTraitsTest, IsNothrowSwappable) {
  using absl::type_traits_internal::IsNothrowSwappable;
  using absl::type_traits_internal::StdSwapIsUnconstrained;

  EXPECT_TRUE(IsNothrowSwappable<int>::value);

  struct NonNoexceptMoves {
    NonNoexceptMoves(NonNoexceptMoves&&) {}
    NonNoexceptMoves& operator=(NonNoexceptMoves&&) { return *this; }
    ~NonNoexceptMoves() = default;
  };

  EXPECT_FALSE(IsNothrowSwappable<NonNoexceptMoves>::value);

  struct NoConstruct {
    NoConstruct(NoConstruct&&) = delete;
    NoConstruct& operator=(NoConstruct&&) { return *this; }
    ~NoConstruct() = default;
  };

  EXPECT_FALSE(IsNothrowSwappable<NoConstruct>::value);

  struct NoAssign {
    NoAssign(NoAssign&&) {}
    NoAssign& operator=(NoAssign&&) = delete;
    ~NoAssign() = default;
  };

  EXPECT_FALSE(IsNothrowSwappable<NoAssign>::value);

  EXPECT_FALSE(IsNothrowSwappable<adl_namespace::DeletedSwap>::value);

  EXPECT_TRUE(IsNothrowSwappable<adl_namespace::SpecialNoexceptSwap>::value);
}

TEST(TriviallyRelocatable, PrimitiveTypes) {
  static_assert(absl::is_trivially_relocatable<int>::value, "");
  static_assert(absl::is_trivially_relocatable<char>::value, "");
  static_assert(absl::is_trivially_relocatable<void*>::value, "");
}

// User-defined types can be trivially relocatable as long as they don't have a
// user-provided move constructor or destructor.
TEST(TriviallyRelocatable, UserDefinedTriviallyRelocatable) {
  struct S {
    int x;
    int y;
  };

  static_assert(absl::is_trivially_relocatable<S>::value, "");
}

// A user-provided move constructor disqualifies a type from being trivially
// relocatable.
TEST(TriviallyRelocatable, UserProvidedMoveConstructor) {
  struct S {
    S(S&&) {}  // NOLINT(modernize-use-equals-default)
  };

  static_assert(!absl::is_trivially_relocatable<S>::value, "");
}

// A user-provided copy constructor disqualifies a type from being trivially
// relocatable.
TEST(TriviallyRelocatable, UserProvidedCopyConstructor) {
  struct S {
    S(const S&) {}  // NOLINT(modernize-use-equals-default)
  };

  static_assert(!absl::is_trivially_relocatable<S>::value, "");
}

// A user-provided copy assignment operator disqualifies a type from
// being trivially relocatable.
TEST(TriviallyRelocatable, UserProvidedCopyAssignment) {
  struct S {
    S(const S&) = default;
    S& operator=(const S&) {  // NOLINT(modernize-use-equals-default)
      return *this;
    }
  };

  static_assert(!absl::is_trivially_relocatable<S>::value, "");
}

// A user-provided move assignment operator disqualifies a type from
// being trivially relocatable.
TEST(TriviallyRelocatable, UserProvidedMoveAssignment) {
  struct S {
    S(S&&) = default;
    S& operator=(S&&) { return *this; }  // NOLINT(modernize-use-equals-default)
  };

  static_assert(!absl::is_trivially_relocatable<S>::value, "");
}

// A user-provided destructor disqualifies a type from being trivially
// relocatable.
TEST(TriviallyRelocatable, UserProvidedDestructor) {
  struct S {
    ~S() {}  // NOLINT(modernize-use-equals-default)
  };

  static_assert(!absl::is_trivially_relocatable<S>::value, "");
}

// TODO(b/275003464): remove the opt-out for Clang on Windows once
// __is_trivially_relocatable is used there again.
// TODO(b/324278148): remove the opt-out for Apple once
// __is_trivially_relocatable is fixed there.
#if defined(ABSL_HAVE_ATTRIBUTE_TRIVIAL_ABI) &&      \
    ABSL_HAVE_BUILTIN(__is_trivially_relocatable) && \
    (defined(__cpp_impl_trivially_relocatable) ||    \
     (!defined(__clang__) && !defined(__APPLE__) && !defined(__NVCC__)))
// A type marked with the "trivial ABI" attribute is trivially relocatable even
// if it has user-provided special members.
TEST(TriviallyRelocatable, TrivialAbi) {
  struct ABSL_ATTRIBUTE_TRIVIAL_ABI S {
    S(S&&) {}       // NOLINT(modernize-use-equals-default)
    S(const S&) {}  // NOLINT(modernize-use-equals-default)
    void operator=(S&&) {}
    void operator=(const S&) {}
    ~S() {}  // NOLINT(modernize-use-equals-default)
  };

  static_assert(absl::is_trivially_relocatable<S>::value, "");
}
#endif

#ifdef ABSL_HAVE_CONSTANT_EVALUATED

constexpr int64_t NegateIfConstantEvaluated(int64_t i) {
  if (absl::is_constant_evaluated()) {
    return -i;
  } else {
    return i;
  }
}

#endif  // ABSL_HAVE_CONSTANT_EVALUATED

TEST(IsConstantEvaluated, is_constant_evaluated) {
#ifdef ABSL_HAVE_CONSTANT_EVALUATED
  constexpr int64_t constant = NegateIfConstantEvaluated(42);
  EXPECT_EQ(constant, -42);

  int64_t now = absl::ToUnixSeconds(absl::Now());
  int64_t not_constant = NegateIfConstantEvaluated(now);
  EXPECT_EQ(not_constant, now);

  static int64_t const_init = NegateIfConstantEvaluated(42);
  EXPECT_EQ(const_init, -42);
#else
  GTEST_SKIP() << "absl::is_constant_evaluated is not defined";
#endif  // ABSL_HAVE_CONSTANT_EVALUATED
}

}  // namespace
