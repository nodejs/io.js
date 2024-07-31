// Copyright 2022 The Abseil Authors.
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

#include "absl/functional/any_invocable.h"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <numeric>
#include <type_traits>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/meta/type_traits.h"
#include "absl/utility/utility.h"

static_assert(absl::internal_any_invocable::kStorageSize >= sizeof(void*),
              "These tests assume that the small object storage is at least "
              "the size of a pointer.");

namespace {

// Helper macro used to avoid spelling `noexcept` in language versions older
// than C++17, where it is not part of the type system, in order to avoid
// compilation failures and internal compiler errors.
#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 201703L
#define ABSL_INTERNAL_NOEXCEPT_SPEC(noex) noexcept(noex)
#else
#define ABSL_INTERNAL_NOEXCEPT_SPEC(noex)
#endif

// A dummy type we use when passing qualifiers to metafunctions
struct _ {};

template <class T>
struct Wrapper {
  template <class U,
            class = absl::enable_if_t<std::is_convertible<U, T>::value>>
  Wrapper(U&&);  // NOLINT
};

// This will cause a recursive trait instantiation if the SFINAE checks are
// not ordered correctly for constructibility.
static_assert(std::is_constructible<Wrapper<absl::AnyInvocable<void()>>,
                                    Wrapper<absl::AnyInvocable<void()>>>::value,
              "");

// A metafunction that takes the cv and l-value reference qualifiers that were
// associated with a function type (here passed via qualifiers of an object
// type), and .
template <class Qualifiers, class This>
struct QualifiersForThisImpl {
  static_assert(std::is_object<This>::value, "");
  using type =
      absl::conditional_t<std::is_const<Qualifiers>::value, const This, This>&;
};

template <class Qualifiers, class This>
struct QualifiersForThisImpl<Qualifiers&, This>
    : QualifiersForThisImpl<Qualifiers, This> {};

template <class Qualifiers, class This>
struct QualifiersForThisImpl<Qualifiers&&, This> {
  static_assert(std::is_object<This>::value, "");
  using type =
      absl::conditional_t<std::is_const<Qualifiers>::value, const This, This>&&;
};

template <class Qualifiers, class This>
using QualifiersForThis =
    typename QualifiersForThisImpl<Qualifiers, This>::type;

// A metafunction that takes the cv and l-value reference qualifier of T and
// applies them to U's function type qualifiers.
template <class T, class Fun>
struct GiveQualifiersToFunImpl;

template <class T, class R, class... P>
struct GiveQualifiersToFunImpl<T, R(P...)> {
  using type =
      absl::conditional_t<std::is_const<T>::value, R(P...) const, R(P...)>;
};

template <class T, class R, class... P>
struct GiveQualifiersToFunImpl<T&, R(P...)> {
  using type =
      absl::conditional_t<std::is_const<T>::value, R(P...) const&, R(P...)&>;
};

template <class T, class R, class... P>
struct GiveQualifiersToFunImpl<T&&, R(P...)> {
  using type =
      absl::conditional_t<std::is_const<T>::value, R(P...) const&&, R(P...) &&>;
};

// If noexcept is a part of the type system, then provide the noexcept forms.
#if defined(__cpp_noexcept_function_type)

template <class T, class R, class... P>
struct GiveQualifiersToFunImpl<T, R(P...) noexcept> {
  using type = absl::conditional_t<std::is_const<T>::value,
                                   R(P...) const noexcept, R(P...) noexcept>;
};

template <class T, class R, class... P>
struct GiveQualifiersToFunImpl<T&, R(P...) noexcept> {
  using type =
      absl::conditional_t<std::is_const<T>::value, R(P...) const & noexcept,
                          R(P...) & noexcept>;
};

template <class T, class R, class... P>
struct GiveQualifiersToFunImpl<T&&, R(P...) noexcept> {
  using type =
      absl::conditional_t<std::is_const<T>::value, R(P...) const && noexcept,
                          R(P...) && noexcept>;
};

#endif  // defined(__cpp_noexcept_function_type)

template <class T, class Fun>
using GiveQualifiersToFun = typename GiveQualifiersToFunImpl<T, Fun>::type;

// This is used in template parameters to decide whether or not to use a type
// that fits in the small object optimization storage.
enum class ObjSize { small, large };

// A base type that is used with classes as a means to insert an
// appropriately-sized dummy datamember when Size is ObjSize::large so that the
// user's class type is guaranteed to not fit in small object storage.
template <ObjSize Size>
struct TypeErasedPadding;

template <>
struct TypeErasedPadding<ObjSize::small> {};

template <>
struct TypeErasedPadding<ObjSize::large> {
  char dummy_data[absl::internal_any_invocable::kStorageSize + 1] = {};
};

struct Int {
  Int(int v) noexcept : value(v) {}  // NOLINT
#ifndef _MSC_VER
  Int(Int&&) noexcept {
    // NOTE: Prior to C++17, this not being called requires optimizations to
    //       take place when performing the top-level invocation. In practice,
    //       most supported compilers perform this optimization prior to C++17.
    std::abort();
  }
#else
  Int(Int&& v) noexcept = default;
#endif
  operator int() && noexcept { return value; }  // NOLINT

  int MemberFunctionAdd(int const& b, int c) noexcept {  // NOLINT
    return value + b + c;
  }

  int value;
};

enum class Movable { no, yes, nothrow, trivial };

enum class NothrowCall { no, yes };

enum class Destructible { nothrow, trivial };

enum class ObjAlign : std::size_t {
  normal = absl::internal_any_invocable::kAlignment,
  large = absl::internal_any_invocable::kAlignment * 2,
};

// A function-object template that has knobs for each property that can affect
// how the object is stored in AnyInvocable.
template <Movable Movability, Destructible Destructibility, class Qual,
          NothrowCall CallExceptionSpec, ObjSize Size, ObjAlign Alignment>
struct add;

#define ABSL_INTERNALS_ADD(qual)                                              \
  template <NothrowCall CallExceptionSpec, ObjSize Size, ObjAlign Alignment>  \
  struct alignas(static_cast<std::size_t>(Alignment))                         \
      add<Movable::trivial, Destructible::trivial, _ qual, CallExceptionSpec, \
          Size, Alignment> : TypeErasedPadding<Size> {                        \
    explicit add(int state_init) : state(state_init) {}                       \
    explicit add(std::initializer_list<int> state_init, int tail)             \
        : state(std::accumulate(std::begin(state_init), std::end(state_init), \
                                0) +                                          \
                tail) {}                                                      \
    add(add&& other) = default; /*NOLINT*/                                    \
    Int operator()(int a, int b, int c) qual                                  \
        ABSL_INTERNAL_NOEXCEPT_SPEC(CallExceptionSpec == NothrowCall::yes) {  \
      return state + a + b + c;                                               \
    }                                                                         \
    int state;                                                                \
  };                                                                          \
                                                                              \
  template <NothrowCall CallExceptionSpec, ObjSize Size, ObjAlign Alignment>  \
  struct alignas(static_cast<std::size_t>(Alignment))                         \
      add<Movable::trivial, Destructible::nothrow, _ qual, CallExceptionSpec, \
          Size, Alignment> : TypeErasedPadding<Size> {                        \
    explicit add(int state_init) : state(state_init) {}                       \
    explicit add(std::initializer_list<int> state_init, int tail)             \
        : state(std::accumulate(std::begin(state_init), std::end(state_init), \
                                0) +                                          \
                tail) {}                                                      \
    ~add() noexcept {}                                                        \
    add(add&& other) = default; /*NOLINT*/                                    \
    Int operator()(int a, int b, int c) qual                                  \
        ABSL_INTERNAL_NOEXCEPT_SPEC(CallExceptionSpec == NothrowCall::yes) {  \
      return state + a + b + c;                                               \
    }                                                                         \
    int state;                                                                \
  }

// Explicitly specify an empty argument.
// MSVC (at least up to _MSC_VER 1931, if not beyond) warns that
// ABSL_INTERNALS_ADD() is an undefined zero-arg overload.
#define ABSL_INTERNALS_NOARG
ABSL_INTERNALS_ADD(ABSL_INTERNALS_NOARG);
#undef ABSL_INTERNALS_NOARG

ABSL_INTERNALS_ADD(const);
ABSL_INTERNALS_ADD(&);
ABSL_INTERNALS_ADD(const&);
ABSL_INTERNALS_ADD(&&);       // NOLINT
ABSL_INTERNALS_ADD(const&&);  // NOLINT

#undef ABSL_INTERNALS_ADD

template <Destructible Destructibility, class Qual,
          NothrowCall CallExceptionSpec, ObjSize Size, ObjAlign Alignment>
struct add<Movable::no, Destructibility, Qual, CallExceptionSpec, Size,
           Alignment> : private add<Movable::trivial, Destructibility, Qual,
                                    CallExceptionSpec, Size, Alignment> {
  using Base = add<Movable::trivial, Destructibility, Qual, CallExceptionSpec,
                   Size, Alignment>;

  explicit add(int state_init) : Base(state_init) {}

  explicit add(std::initializer_list<int> state_init, int tail)
      : Base(state_init, tail) {}

  add(add&&) = delete;

  using Base::operator();
  using Base::state;
};

template <Destructible Destructibility, class Qual,
          NothrowCall CallExceptionSpec, ObjSize Size, ObjAlign Alignment>
struct add<Movable::yes, Destructibility, Qual, CallExceptionSpec, Size,
           Alignment> : private add<Movable::trivial, Destructibility, Qual,
                                    CallExceptionSpec, Size, Alignment> {
  using Base = add<Movable::trivial, Destructibility, Qual, CallExceptionSpec,
                   Size, Alignment>;

  explicit add(int state_init) : Base(state_init) {}

  explicit add(std::initializer_list<int> state_init, int tail)
      : Base(state_init, tail) {}

  add(add&& other) noexcept(false) : Base(other.state) {}  // NOLINT

  using Base::operator();
  using Base::state;
};

template <Destructible Destructibility, class Qual,
          NothrowCall CallExceptionSpec, ObjSize Size, ObjAlign Alignment>
struct add<Movable::nothrow, Destructibility, Qual, CallExceptionSpec, Size,
           Alignment> : private add<Movable::trivial, Destructibility, Qual,
                                    CallExceptionSpec, Size, Alignment> {
  using Base = add<Movable::trivial, Destructibility, Qual, CallExceptionSpec,
                   Size, Alignment>;

  explicit add(int state_init) : Base(state_init) {}

  explicit add(std::initializer_list<int> state_init, int tail)
      : Base(state_init, tail) {}

  add(add&& other) noexcept : Base(other.state) {}

  using Base::operator();
  using Base::state;
};

// Actual non-member functions rather than function objects
Int add_function(Int&& a, int b, int c) noexcept { return a.value + b + c; }

Int mult_function(Int&& a, int b, int c) noexcept { return a.value * b * c; }

Int square_function(Int const&& a) noexcept { return a.value * a.value; }

template <class Sig>
using AnyInvocable = absl::AnyInvocable<Sig>;

// Instantiations of this template contains all of the compile-time parameters
// for a given instantiation of the AnyInvocable test suite.
template <Movable Movability, Destructible Destructibility, class Qual,
          NothrowCall CallExceptionSpec, ObjSize Size, ObjAlign Alignment>
struct TestParams {
  static constexpr Movable kMovability = Movability;
  static constexpr Destructible kDestructibility = Destructibility;
  using Qualifiers = Qual;
  static constexpr NothrowCall kCallExceptionSpec = CallExceptionSpec;
  static constexpr bool kIsNoexcept = kCallExceptionSpec == NothrowCall::yes;
  static constexpr bool kIsRvalueQualified =
      std::is_rvalue_reference<Qual>::value;
  static constexpr ObjSize kSize = Size;
  static constexpr ObjAlign kAlignment = Alignment;

  // These types are used when testing with member object pointer Invocables
  using UnqualifiedUnaryFunType = int(Int const&&)
      ABSL_INTERNAL_NOEXCEPT_SPEC(CallExceptionSpec == NothrowCall::yes);
  using UnaryFunType = GiveQualifiersToFun<Qualifiers, UnqualifiedUnaryFunType>;
  using MemObjPtrType = int(Int::*);
  using UnaryAnyInvType = AnyInvocable<UnaryFunType>;
  using UnaryThisParamType = QualifiersForThis<Qualifiers, UnaryAnyInvType>;

  template <class T>
  static UnaryThisParamType ToUnaryThisParam(T&& fun) {
    return static_cast<UnaryThisParamType>(fun);
  }

  // This function type intentionally uses 3 "kinds" of parameter types.
  //     - A user-defined type
  //     - A reference type
  //     - A scalar type
  //
  // These were chosen because internal forwarding takes place on parameters
  // differently depending based on type properties (scalars are forwarded by
  // value).
  using ResultType = Int;
  using AnyInvocableFunTypeNotNoexcept = Int(Int, const int&, int);
  using UnqualifiedFunType =
      typename std::conditional<kIsNoexcept, Int(Int, const int&, int) noexcept,
                                Int(Int, const int&, int)>::type;
  using FunType = GiveQualifiersToFun<Qualifiers, UnqualifiedFunType>;
  using MemFunPtrType =
      typename std::conditional<kIsNoexcept,
                                Int (Int::*)(const int&, int) noexcept,
                                Int (Int::*)(const int&, int)>::type;
  using AnyInvType = AnyInvocable<FunType>;
  using AddType = add<kMovability, kDestructibility, Qualifiers,
                      kCallExceptionSpec, kSize, kAlignment>;
  using ThisParamType = QualifiersForThis<Qualifiers, AnyInvType>;

  template <class T>
  static ThisParamType ToThisParam(T&& fun) {
    return static_cast<ThisParamType>(fun);
  }

  // These typedefs are used when testing void return type covariance.
  using UnqualifiedVoidFunType =
      typename std::conditional<kIsNoexcept,
                                void(Int, const int&, int) noexcept,
                                void(Int, const int&, int)>::type;
  using VoidFunType = GiveQualifiersToFun<Qualifiers, UnqualifiedVoidFunType>;
  using VoidAnyInvType = AnyInvocable<VoidFunType>;
  using VoidThisParamType = QualifiersForThis<Qualifiers, VoidAnyInvType>;

  template <class T>
  static VoidThisParamType ToVoidThisParam(T&& fun) {
    return static_cast<VoidThisParamType>(fun);
  }

  using CompatibleAnyInvocableFunType =
      absl::conditional_t<std::is_rvalue_reference<Qual>::value,
                          GiveQualifiersToFun<const _&&, UnqualifiedFunType>,
                          GiveQualifiersToFun<const _&, UnqualifiedFunType>>;

  using CompatibleAnyInvType = AnyInvocable<CompatibleAnyInvocableFunType>;

  using IncompatibleInvocable =
      absl::conditional_t<std::is_rvalue_reference<Qual>::value,
                          GiveQualifiersToFun<_&, UnqualifiedFunType>(_::*),
                          GiveQualifiersToFun<_&&, UnqualifiedFunType>(_::*)>;
};

// Given a member-pointer type, this metafunction yields the target type of the
// pointer, not including the class-type. It is used to verify that the function
// call operator of AnyInvocable has the proper signature, corresponding to the
// function type that the user provided.
template <class MemberPtrType>
struct MemberTypeOfImpl;

template <class Class, class T>
struct MemberTypeOfImpl<T(Class::*)> {
  using type = T;
};

template <class MemberPtrType>
using MemberTypeOf = typename MemberTypeOfImpl<MemberPtrType>::type;

template <class T, class = void>
struct IsMemberSwappableImpl : std::false_type {
  static constexpr bool kIsNothrow = false;
};

template <class T>
struct IsMemberSwappableImpl<
    T, absl::void_t<decltype(std::declval<T&>().swap(std::declval<T&>()))>>
    : std::true_type {
  static constexpr bool kIsNothrow =
      noexcept(std::declval<T&>().swap(std::declval<T&>()));
};

template <class T>
using IsMemberSwappable = IsMemberSwappableImpl<T>;

template <class T>
using IsNothrowMemberSwappable =
    std::integral_constant<bool, IsMemberSwappableImpl<T>::kIsNothrow>;

template <class T>
class AnyInvTestBasic : public ::testing::Test {};

TYPED_TEST_SUITE_P(AnyInvTestBasic);

TYPED_TEST_P(AnyInvTestBasic, DefaultConstruction) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun;

  EXPECT_FALSE(static_cast<bool>(fun));

  EXPECT_TRUE(std::is_nothrow_default_constructible<AnyInvType>::value);
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionNullptr) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun = nullptr;

  EXPECT_FALSE(static_cast<bool>(fun));

  EXPECT_TRUE(
      (std::is_nothrow_constructible<AnyInvType, std::nullptr_t>::value));
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionNullFunctionPtr) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using UnqualifiedFunType = typename TypeParam::UnqualifiedFunType;

  UnqualifiedFunType* const null_fun_ptr = nullptr;
  AnyInvType fun = null_fun_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionNullMemberFunctionPtr) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using MemFunPtrType = typename TypeParam::MemFunPtrType;

  const MemFunPtrType null_mem_fun_ptr = nullptr;
  AnyInvType fun = null_mem_fun_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionNullMemberObjectPtr) {
  using UnaryAnyInvType = typename TypeParam::UnaryAnyInvType;
  using MemObjPtrType = typename TypeParam::MemObjPtrType;

  const MemObjPtrType null_mem_obj_ptr = nullptr;
  UnaryAnyInvType fun = null_mem_obj_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionMemberFunctionPtr) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun = &Int::MemberFunctionAdd;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionMemberObjectPtr) {
  using UnaryAnyInvType = typename TypeParam::UnaryAnyInvType;

  UnaryAnyInvType fun = &Int::value;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(13, TypeParam::ToUnaryThisParam(fun)(13));
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionFunctionReferenceDecay) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun = add_function;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionCompatibleAnyInvocableEmpty) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using CompatibleAnyInvType = typename TypeParam::CompatibleAnyInvType;

  CompatibleAnyInvType other;
  AnyInvType fun = std::move(other);

  EXPECT_FALSE(static_cast<bool>(other));  // NOLINT
  EXPECT_EQ(other, nullptr);               // NOLINT
  EXPECT_EQ(nullptr, other);               // NOLINT

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, ConstructionCompatibleAnyInvocableNonempty) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using CompatibleAnyInvType = typename TypeParam::CompatibleAnyInvType;

  CompatibleAnyInvType other = &add_function;
  AnyInvType fun = std::move(other);

  EXPECT_FALSE(static_cast<bool>(other));  // NOLINT
  EXPECT_EQ(other, nullptr);               // NOLINT
  EXPECT_EQ(nullptr, other);               // NOLINT

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestBasic, ConversionToBool) {
  using AnyInvType = typename TypeParam::AnyInvType;

  {
    AnyInvType fun;

    // This tests contextually-convertible-to-bool.
    EXPECT_FALSE(fun ? true : false);  // NOLINT

    // Make sure that the conversion is not implicit.
    EXPECT_TRUE(
        (std::is_nothrow_constructible<bool, const AnyInvType&>::value));
    EXPECT_FALSE((std::is_convertible<const AnyInvType&, bool>::value));
  }

  {
    AnyInvType fun = &add_function;

    // This tests contextually-convertible-to-bool.
    EXPECT_TRUE(fun ? true : false);  // NOLINT
  }
}

TYPED_TEST_P(AnyInvTestBasic, Invocation) {
  using AnyInvType = typename TypeParam::AnyInvType;

  using FunType = typename TypeParam::FunType;
  using AnyInvCallType = MemberTypeOf<decltype(&AnyInvType::operator())>;

  // Make sure the function call operator of AnyInvocable always has the
  // type that was specified via the template argument.
  EXPECT_TRUE((std::is_same<AnyInvCallType, FunType>::value));

  AnyInvType fun = &add_function;

  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceConstruction) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType fun(absl::in_place_type<AddType>, 5);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceConstructionInitializerList) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType fun(absl::in_place_type<AddType>, {1, 2, 3, 4}, 5);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(39, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceNullFunPtrConstruction) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using UnqualifiedFunType = typename TypeParam::UnqualifiedFunType;

  AnyInvType fun(absl::in_place_type<UnqualifiedFunType*>, nullptr);

  // In-place construction does not lead to empty.
  EXPECT_TRUE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceNullFunPtrConstructionValueInit) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using UnqualifiedFunType = typename TypeParam::UnqualifiedFunType;

  AnyInvType fun(absl::in_place_type<UnqualifiedFunType*>);

  // In-place construction does not lead to empty.
  EXPECT_TRUE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceNullMemFunPtrConstruction) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using MemFunPtrType = typename TypeParam::MemFunPtrType;

  AnyInvType fun(absl::in_place_type<MemFunPtrType>, nullptr);

  // In-place construction does not lead to empty.
  EXPECT_TRUE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceNullMemFunPtrConstructionValueInit) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using MemFunPtrType = typename TypeParam::MemFunPtrType;

  AnyInvType fun(absl::in_place_type<MemFunPtrType>);

  // In-place construction does not lead to empty.
  EXPECT_TRUE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceNullMemObjPtrConstruction) {
  using UnaryAnyInvType = typename TypeParam::UnaryAnyInvType;
  using MemObjPtrType = typename TypeParam::MemObjPtrType;

  UnaryAnyInvType fun(absl::in_place_type<MemObjPtrType>, nullptr);

  // In-place construction does not lead to empty.
  EXPECT_TRUE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceNullMemObjPtrConstructionValueInit) {
  using UnaryAnyInvType = typename TypeParam::UnaryAnyInvType;
  using MemObjPtrType = typename TypeParam::MemObjPtrType;

  UnaryAnyInvType fun(absl::in_place_type<MemObjPtrType>);

  // In-place construction does not lead to empty.
  EXPECT_TRUE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, InPlaceVoidCovarianceConstruction) {
  using VoidAnyInvType = typename TypeParam::VoidAnyInvType;
  using AddType = typename TypeParam::AddType;

  VoidAnyInvType fun(absl::in_place_type<AddType>, 5);

  EXPECT_TRUE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestBasic, MoveConstructionFromEmpty) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType source_fun;
  AnyInvType fun(std::move(source_fun));

  EXPECT_FALSE(static_cast<bool>(fun));

  EXPECT_TRUE(std::is_nothrow_move_constructible<AnyInvType>::value);
}

TYPED_TEST_P(AnyInvTestBasic, MoveConstructionFromNonEmpty) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType source_fun(absl::in_place_type<AddType>, 5);
  AnyInvType fun(std::move(source_fun));

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);

  EXPECT_TRUE(std::is_nothrow_move_constructible<AnyInvType>::value);
}

TYPED_TEST_P(AnyInvTestBasic, ComparisonWithNullptrEmpty) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun;

  EXPECT_TRUE(fun == nullptr);
  EXPECT_TRUE(nullptr == fun);

  EXPECT_FALSE(fun != nullptr);
  EXPECT_FALSE(nullptr != fun);
}

TYPED_TEST_P(AnyInvTestBasic, ComparisonWithNullptrNonempty) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType fun(absl::in_place_type<AddType>, 5);

  EXPECT_FALSE(fun == nullptr);
  EXPECT_FALSE(nullptr == fun);

  EXPECT_TRUE(fun != nullptr);
  EXPECT_TRUE(nullptr != fun);
}

TYPED_TEST_P(AnyInvTestBasic, ResultType) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using ExpectedResultType = typename TypeParam::ResultType;

  EXPECT_TRUE((std::is_same<typename AnyInvType::result_type,
                            ExpectedResultType>::value));
}

template <class T>
class AnyInvTestCombinatoric : public ::testing::Test {};

TYPED_TEST_SUITE_P(AnyInvTestCombinatoric);

TYPED_TEST_P(AnyInvTestCombinatoric, MoveAssignEmptyEmptyLhsRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType source_fun;
  AnyInvType fun;

  fun = std::move(source_fun);

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, MoveAssignEmptyLhsNonemptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType source_fun(absl::in_place_type<AddType>, 5);
  AnyInvType fun;

  fun = std::move(source_fun);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestCombinatoric, MoveAssignNonemptyEmptyLhsRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType source_fun;
  AnyInvType fun(absl::in_place_type<AddType>, 5);

  fun = std::move(source_fun);

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, MoveAssignNonemptyLhsNonemptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType source_fun(absl::in_place_type<AddType>, 5);
  AnyInvType fun(absl::in_place_type<AddType>, 20);

  fun = std::move(source_fun);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestCombinatoric, SelfMoveAssignEmpty) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType source_fun;
  source_fun = std::move(source_fun);

  // This space intentionally left blank.
}

TYPED_TEST_P(AnyInvTestCombinatoric, SelfMoveAssignNonempty) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType source_fun(absl::in_place_type<AddType>, 5);
  source_fun = std::move(source_fun);

  // This space intentionally left blank.
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignNullptrEmptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun;
  fun = nullptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignNullFunctionPtrEmptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using UnqualifiedFunType = typename TypeParam::UnqualifiedFunType;

  UnqualifiedFunType* const null_fun_ptr = nullptr;
  AnyInvType fun;
  fun = null_fun_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignNullMemberFunctionPtrEmptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using MemFunPtrType = typename TypeParam::MemFunPtrType;

  const MemFunPtrType null_mem_fun_ptr = nullptr;
  AnyInvType fun;
  fun = null_mem_fun_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignNullMemberObjectPtrEmptyLhs) {
  using UnaryAnyInvType = typename TypeParam::UnaryAnyInvType;
  using MemObjPtrType = typename TypeParam::MemObjPtrType;

  const MemObjPtrType null_mem_obj_ptr = nullptr;
  UnaryAnyInvType fun;
  fun = null_mem_obj_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignMemberFunctionPtrEmptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun;
  fun = &Int::MemberFunctionAdd;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignMemberObjectPtrEmptyLhs) {
  using UnaryAnyInvType = typename TypeParam::UnaryAnyInvType;

  UnaryAnyInvType fun;
  fun = &Int::value;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(13, TypeParam::ToUnaryThisParam(fun)(13));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignFunctionReferenceDecayEmptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun;
  fun = add_function;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestCombinatoric,
             AssignCompatibleAnyInvocableEmptyLhsEmptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using CompatibleAnyInvType = typename TypeParam::CompatibleAnyInvType;

  CompatibleAnyInvType other;
  AnyInvType fun;
  fun = std::move(other);

  EXPECT_FALSE(static_cast<bool>(other));  // NOLINT
  EXPECT_EQ(other, nullptr);               // NOLINT
  EXPECT_EQ(nullptr, other);               // NOLINT

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric,
             AssignCompatibleAnyInvocableEmptyLhsNonemptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using CompatibleAnyInvType = typename TypeParam::CompatibleAnyInvType;

  CompatibleAnyInvType other = &add_function;
  AnyInvType fun;
  fun = std::move(other);

  EXPECT_FALSE(static_cast<bool>(other));  // NOLINT

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignNullptrNonemptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun = &mult_function;
  fun = nullptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignNullFunctionPtrNonemptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using UnqualifiedFunType = typename TypeParam::UnqualifiedFunType;

  UnqualifiedFunType* const null_fun_ptr = nullptr;
  AnyInvType fun = &mult_function;
  fun = null_fun_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignNullMemberFunctionPtrNonemptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using MemFunPtrType = typename TypeParam::MemFunPtrType;

  const MemFunPtrType null_mem_fun_ptr = nullptr;
  AnyInvType fun = &mult_function;
  fun = null_mem_fun_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignNullMemberObjectPtrNonemptyLhs) {
  using UnaryAnyInvType = typename TypeParam::UnaryAnyInvType;
  using MemObjPtrType = typename TypeParam::MemObjPtrType;

  const MemObjPtrType null_mem_obj_ptr = nullptr;
  UnaryAnyInvType fun = &square_function;
  fun = null_mem_obj_ptr;

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignMemberFunctionPtrNonemptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun = &mult_function;
  fun = &Int::MemberFunctionAdd;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignMemberObjectPtrNonemptyLhs) {
  using UnaryAnyInvType = typename TypeParam::UnaryAnyInvType;

  UnaryAnyInvType fun = &square_function;
  fun = &Int::value;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(13, TypeParam::ToUnaryThisParam(fun)(13));
}

TYPED_TEST_P(AnyInvTestCombinatoric, AssignFunctionReferenceDecayNonemptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;

  AnyInvType fun = &mult_function;
  fun = add_function;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestCombinatoric,
             AssignCompatibleAnyInvocableNonemptyLhsEmptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using CompatibleAnyInvType = typename TypeParam::CompatibleAnyInvType;

  CompatibleAnyInvType other;
  AnyInvType fun = &mult_function;
  fun = std::move(other);

  EXPECT_FALSE(static_cast<bool>(other));  // NOLINT
  EXPECT_EQ(other, nullptr);               // NOLINT
  EXPECT_EQ(nullptr, other);               // NOLINT

  EXPECT_FALSE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestCombinatoric,
             AssignCompatibleAnyInvocableNonemptyLhsNonemptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using CompatibleAnyInvType = typename TypeParam::CompatibleAnyInvType;

  CompatibleAnyInvType other = &add_function;
  AnyInvType fun = &mult_function;
  fun = std::move(other);

  EXPECT_FALSE(static_cast<bool>(other));  // NOLINT

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(24, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestCombinatoric, SwapEmptyLhsEmptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;

  // Swap idiom
  {
    AnyInvType fun;
    AnyInvType other;

    using std::swap;
    swap(fun, other);

    EXPECT_FALSE(static_cast<bool>(fun));
    EXPECT_FALSE(static_cast<bool>(other));

    EXPECT_TRUE(
        absl::type_traits_internal::IsNothrowSwappable<AnyInvType>::value);
  }

  // Member swap
  {
    AnyInvType fun;
    AnyInvType other;

    fun.swap(other);

    EXPECT_FALSE(static_cast<bool>(fun));
    EXPECT_FALSE(static_cast<bool>(other));

    EXPECT_TRUE(IsNothrowMemberSwappable<AnyInvType>::value);
  }
}

TYPED_TEST_P(AnyInvTestCombinatoric, SwapEmptyLhsNonemptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  // Swap idiom
  {
    AnyInvType fun;
    AnyInvType other(absl::in_place_type<AddType>, 5);

    using std::swap;
    swap(fun, other);

    EXPECT_TRUE(static_cast<bool>(fun));
    EXPECT_FALSE(static_cast<bool>(other));

    EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);

    EXPECT_TRUE(
        absl::type_traits_internal::IsNothrowSwappable<AnyInvType>::value);
  }

  // Member swap
  {
    AnyInvType fun;
    AnyInvType other(absl::in_place_type<AddType>, 5);

    fun.swap(other);

    EXPECT_TRUE(static_cast<bool>(fun));
    EXPECT_FALSE(static_cast<bool>(other));

    EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);

    EXPECT_TRUE(IsNothrowMemberSwappable<AnyInvType>::value);
  }
}

TYPED_TEST_P(AnyInvTestCombinatoric, SwapNonemptyLhsEmptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  // Swap idiom
  {
    AnyInvType fun(absl::in_place_type<AddType>, 5);
    AnyInvType other;

    using std::swap;
    swap(fun, other);

    EXPECT_FALSE(static_cast<bool>(fun));
    EXPECT_TRUE(static_cast<bool>(other));

    EXPECT_EQ(29, TypeParam::ToThisParam(other)(7, 8, 9).value);

    EXPECT_TRUE(
        absl::type_traits_internal::IsNothrowSwappable<AnyInvType>::value);
  }

  // Member swap
  {
    AnyInvType fun(absl::in_place_type<AddType>, 5);
    AnyInvType other;

    fun.swap(other);

    EXPECT_FALSE(static_cast<bool>(fun));
    EXPECT_TRUE(static_cast<bool>(other));

    EXPECT_EQ(29, TypeParam::ToThisParam(other)(7, 8, 9).value);

    EXPECT_TRUE(IsNothrowMemberSwappable<AnyInvType>::value);
  }
}

TYPED_TEST_P(AnyInvTestCombinatoric, SwapNonemptyLhsNonemptyRhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  // Swap idiom
  {
    AnyInvType fun(absl::in_place_type<AddType>, 5);
    AnyInvType other(absl::in_place_type<AddType>, 6);

    using std::swap;
    swap(fun, other);

    EXPECT_TRUE(static_cast<bool>(fun));
    EXPECT_TRUE(static_cast<bool>(other));

    EXPECT_EQ(30, TypeParam::ToThisParam(fun)(7, 8, 9).value);
    EXPECT_EQ(29, TypeParam::ToThisParam(other)(7, 8, 9).value);

    EXPECT_TRUE(
        absl::type_traits_internal::IsNothrowSwappable<AnyInvType>::value);
  }

  // Member swap
  {
    AnyInvType fun(absl::in_place_type<AddType>, 5);
    AnyInvType other(absl::in_place_type<AddType>, 6);

    fun.swap(other);

    EXPECT_TRUE(static_cast<bool>(fun));
    EXPECT_TRUE(static_cast<bool>(other));

    EXPECT_EQ(30, TypeParam::ToThisParam(fun)(7, 8, 9).value);
    EXPECT_EQ(29, TypeParam::ToThisParam(other)(7, 8, 9).value);

    EXPECT_TRUE(IsNothrowMemberSwappable<AnyInvType>::value);
  }
}

template <class T>
class AnyInvTestMovable : public ::testing::Test {};

TYPED_TEST_SUITE_P(AnyInvTestMovable);

TYPED_TEST_P(AnyInvTestMovable, ConversionConstructionUserDefinedType) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType fun(AddType(5));

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestMovable, ConversionConstructionVoidCovariance) {
  using VoidAnyInvType = typename TypeParam::VoidAnyInvType;
  using AddType = typename TypeParam::AddType;

  VoidAnyInvType fun(AddType(5));

  EXPECT_TRUE(static_cast<bool>(fun));
}

TYPED_TEST_P(AnyInvTestMovable, ConversionAssignUserDefinedTypeEmptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType fun;
  fun = AddType(5);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestMovable, ConversionAssignUserDefinedTypeNonemptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType fun = &add_function;
  fun = AddType(5);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);
}

TYPED_TEST_P(AnyInvTestMovable, ConversionAssignVoidCovariance) {
  using VoidAnyInvType = typename TypeParam::VoidAnyInvType;
  using AddType = typename TypeParam::AddType;

  VoidAnyInvType fun;
  fun = AddType(5);

  EXPECT_TRUE(static_cast<bool>(fun));
}

template <class T>
class AnyInvTestNoexceptFalse : public ::testing::Test {};

TYPED_TEST_SUITE_P(AnyInvTestNoexceptFalse);

TYPED_TEST_P(AnyInvTestNoexceptFalse, ConversionConstructionConstraints) {
  using AnyInvType = typename TypeParam::AnyInvType;

  EXPECT_TRUE((std::is_constructible<
               AnyInvType,
               typename TypeParam::AnyInvocableFunTypeNotNoexcept*>::value));
  EXPECT_FALSE((
      std::is_constructible<AnyInvType,
                            typename TypeParam::IncompatibleInvocable>::value));
}

TYPED_TEST_P(AnyInvTestNoexceptFalse, ConversionAssignConstraints) {
  using AnyInvType = typename TypeParam::AnyInvType;

  EXPECT_TRUE((std::is_assignable<
               AnyInvType&,
               typename TypeParam::AnyInvocableFunTypeNotNoexcept*>::value));
  EXPECT_FALSE(
      (std::is_assignable<AnyInvType&,
                          typename TypeParam::IncompatibleInvocable>::value));
}

template <class T>
class AnyInvTestNoexceptTrue : public ::testing::Test {};

TYPED_TEST_SUITE_P(AnyInvTestNoexceptTrue);

TYPED_TEST_P(AnyInvTestNoexceptTrue, ConversionConstructionConstraints) {
#if ABSL_INTERNAL_CPLUSPLUS_LANG < 201703L
  GTEST_SKIP() << "Noexcept was not part of the type system before C++17.";
#else
  using AnyInvType = typename TypeParam::AnyInvType;

  EXPECT_FALSE((std::is_constructible<
                AnyInvType,
                typename TypeParam::AnyInvocableFunTypeNotNoexcept*>::value));
  EXPECT_FALSE((
      std::is_constructible<AnyInvType,
                            typename TypeParam::IncompatibleInvocable>::value));
#endif
}

TYPED_TEST_P(AnyInvTestNoexceptTrue, ConversionAssignConstraints) {
#if ABSL_INTERNAL_CPLUSPLUS_LANG < 201703L
  GTEST_SKIP() << "Noexcept was not part of the type system before C++17.";
#else
  using AnyInvType = typename TypeParam::AnyInvType;

  EXPECT_FALSE((std::is_assignable<
                AnyInvType&,
                typename TypeParam::AnyInvocableFunTypeNotNoexcept*>::value));
  EXPECT_FALSE(
      (std::is_assignable<AnyInvType&,
                          typename TypeParam::IncompatibleInvocable>::value));
#endif
}

template <class T>
class AnyInvTestNonRvalue : public ::testing::Test {};

TYPED_TEST_SUITE_P(AnyInvTestNonRvalue);

TYPED_TEST_P(AnyInvTestNonRvalue, ConversionConstructionReferenceWrapper) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AddType add(4);
  AnyInvType fun = std::ref(add);
  add.state = 5;

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(38, TypeParam::ToThisParam(fun)(10, 11, 12).value);
}

TYPED_TEST_P(AnyInvTestNonRvalue, NonMoveableResultType) {
#if ABSL_INTERNAL_CPLUSPLUS_LANG < 201703L
  GTEST_SKIP() << "Copy/move elision was not standard before C++17";
#else
  // Define a result type that cannot be copy- or move-constructed.
  struct Result {
    int x;

    explicit Result(const int x_in) : x(x_in) {}
    Result(Result&&) = delete;
  };

  static_assert(!std::is_move_constructible<Result>::value, "");
  static_assert(!std::is_copy_constructible<Result>::value, "");

  // Assumption check: it should nevertheless be possible to use functors that
  // return a Result struct according to the language rules.
  const auto return_17 = []() noexcept { return Result(17); };
  EXPECT_EQ(17, return_17().x);

  // Just like plain functors, it should work fine to use an AnyInvocable that
  // returns the non-moveable type.
  using UnqualifiedFun =
      absl::conditional_t<TypeParam::kIsNoexcept, Result() noexcept, Result()>;

  using Fun =
      GiveQualifiersToFun<typename TypeParam::Qualifiers, UnqualifiedFun>;

  AnyInvocable<Fun> any_inv(return_17);
  EXPECT_EQ(17, any_inv().x);
#endif
}

TYPED_TEST_P(AnyInvTestNonRvalue, ConversionAssignReferenceWrapperEmptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AddType add(4);
  AnyInvType fun;
  fun = std::ref(add);
  add.state = 5;
  EXPECT_TRUE(
      (std::is_nothrow_assignable<AnyInvType&,
                                  std::reference_wrapper<AddType>>::value));

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(38, TypeParam::ToThisParam(fun)(10, 11, 12).value);
}

TYPED_TEST_P(AnyInvTestNonRvalue, ConversionAssignReferenceWrapperNonemptyLhs) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AddType add(4);
  AnyInvType fun = &mult_function;
  fun = std::ref(add);
  add.state = 5;
  EXPECT_TRUE(
      (std::is_nothrow_assignable<AnyInvType&,
                                  std::reference_wrapper<AddType>>::value));

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(29, TypeParam::ToThisParam(fun)(7, 8, 9).value);

  EXPECT_TRUE(static_cast<bool>(fun));
  EXPECT_EQ(38, TypeParam::ToThisParam(fun)(10, 11, 12).value);
}

template <class T>
class AnyInvTestRvalue : public ::testing::Test {};

TYPED_TEST_SUITE_P(AnyInvTestRvalue);

TYPED_TEST_P(AnyInvTestRvalue, ConversionConstructionReferenceWrapper) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  EXPECT_FALSE((
      std::is_convertible<std::reference_wrapper<AddType>, AnyInvType>::value));
}

TYPED_TEST_P(AnyInvTestRvalue, NonMoveableResultType) {
#if ABSL_INTERNAL_CPLUSPLUS_LANG < 201703L
  GTEST_SKIP() << "Copy/move elision was not standard before C++17";
#else
  // Define a result type that cannot be copy- or move-constructed.
  struct Result {
    int x;

    explicit Result(const int x_in) : x(x_in) {}
    Result(Result&&) = delete;
  };

  static_assert(!std::is_move_constructible<Result>::value, "");
  static_assert(!std::is_copy_constructible<Result>::value, "");

  // Assumption check: it should nevertheless be possible to use functors that
  // return a Result struct according to the language rules.
  const auto return_17 = []() noexcept { return Result(17); };
  EXPECT_EQ(17, return_17().x);

  // Just like plain functors, it should work fine to use an AnyInvocable that
  // returns the non-moveable type.
  using UnqualifiedFun =
      absl::conditional_t<TypeParam::kIsNoexcept, Result() noexcept, Result()>;

  using Fun =
      GiveQualifiersToFun<typename TypeParam::Qualifiers, UnqualifiedFun>;

  EXPECT_EQ(17, AnyInvocable<Fun>(return_17)().x);
#endif
}

TYPED_TEST_P(AnyInvTestRvalue, ConversionAssignReferenceWrapper) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  EXPECT_FALSE((
      std::is_assignable<AnyInvType&, std::reference_wrapper<AddType>>::value));
}

TYPED_TEST_P(AnyInvTestRvalue, NonConstCrashesOnSecondCall) {
  using AnyInvType = typename TypeParam::AnyInvType;
  using AddType = typename TypeParam::AddType;

  AnyInvType fun(absl::in_place_type<AddType>, 5);

  EXPECT_TRUE(static_cast<bool>(fun));
  std::move(fun)(7, 8, 9);

  // Ensure we're still valid
  EXPECT_TRUE(static_cast<bool>(fun));  // NOLINT(bugprone-use-after-move)

#if !defined(NDEBUG)
  EXPECT_DEATH_IF_SUPPORTED(std::move(fun)(7, 8, 9), "");
#endif
}

// Ensure that any qualifiers (in particular &&-qualifiers) do not affect
// when the destructor is actually run.
TYPED_TEST_P(AnyInvTestRvalue, QualifierIndependentObjectLifetime) {
  using AnyInvType = typename TypeParam::AnyInvType;

  auto refs = std::make_shared<std::nullptr_t>();
  {
    AnyInvType fun([refs](auto&&...) noexcept { return 0; });
    EXPECT_GT(refs.use_count(), 1);

    std::move(fun)(7, 8, 9);

    // Ensure destructor hasn't run even if rref-qualified
    EXPECT_GT(refs.use_count(), 1);
  }
  EXPECT_EQ(refs.use_count(), 1);
}

// NOTE: This test suite originally attempted to enumerate all possible
// combinations of type properties but the build-time started getting too large.
// Instead, it is now assumed that certain parameters are orthogonal and so
// some combinations are elided.

// A metafunction to form a TypeList of all cv and non-rvalue ref combinations,
// coupled with all of the other explicitly specified parameters.
template <Movable Mov, Destructible Dest, NothrowCall CallExceptionSpec,
          ObjSize Size, ObjAlign Align>
using NonRvalueQualifiedTestParams = ::testing::Types<               //
    TestParams<Mov, Dest, _, CallExceptionSpec, Size, Align>,        //
    TestParams<Mov, Dest, const _, CallExceptionSpec, Size, Align>,  //
    TestParams<Mov, Dest, _&, CallExceptionSpec, Size, Align>,       //
    TestParams<Mov, Dest, const _&, CallExceptionSpec, Size, Align>>;

// A metafunction to form a TypeList of const and non-const rvalue ref
// qualifiers, coupled with all of the other explicitly specified parameters.
template <Movable Mov, Destructible Dest, NothrowCall CallExceptionSpec,
          ObjSize Size, ObjAlign Align>
using RvalueQualifiedTestParams = ::testing::Types<
    TestParams<Mov, Dest, _&&, CallExceptionSpec, Size, Align>,       //
    TestParams<Mov, Dest, const _&&, CallExceptionSpec, Size, Align>  //
    >;

// All qualifier combinations and a noexcept function type
using TestParameterListNonRvalueQualifiersNothrowCall =
    NonRvalueQualifiedTestParams<Movable::trivial, Destructible::trivial,
                                 NothrowCall::yes, ObjSize::small,
                                 ObjAlign::normal>;
using TestParameterListRvalueQualifiersNothrowCall =
    RvalueQualifiedTestParams<Movable::trivial, Destructible::trivial,
                              NothrowCall::yes, ObjSize::small,
                              ObjAlign::normal>;

// All qualifier combinations and a non-noexcept function type
using TestParameterListNonRvalueQualifiersCallMayThrow =
    NonRvalueQualifiedTestParams<Movable::trivial, Destructible::trivial,
                                 NothrowCall::no, ObjSize::small,
                                 ObjAlign::normal>;
using TestParameterListRvalueQualifiersCallMayThrow =
    RvalueQualifiedTestParams<Movable::trivial, Destructible::trivial,
                              NothrowCall::no, ObjSize::small,
                              ObjAlign::normal>;

// Lists of various cases that should lead to remote storage
using TestParameterListRemoteMovable = ::testing::Types<
    // "Normal" aligned types that are large and have trivial destructors
    TestParams<Movable::trivial, Destructible::trivial, _, NothrowCall::no,
               ObjSize::large, ObjAlign::normal>,  //
    TestParams<Movable::nothrow, Destructible::trivial, _, NothrowCall::no,
               ObjSize::large, ObjAlign::normal>,  //
    TestParams<Movable::yes, Destructible::trivial, _, NothrowCall::no,
               ObjSize::small, ObjAlign::normal>,  //
    TestParams<Movable::yes, Destructible::trivial, _, NothrowCall::no,
               ObjSize::large, ObjAlign::normal>,  //

    // Same as above but with non-trivial destructors
    TestParams<Movable::trivial, Destructible::nothrow, _, NothrowCall::no,
               ObjSize::large, ObjAlign::normal>,  //
    TestParams<Movable::nothrow, Destructible::nothrow, _, NothrowCall::no,
               ObjSize::large, ObjAlign::normal>,  //
    TestParams<Movable::yes, Destructible::nothrow, _, NothrowCall::no,
               ObjSize::small, ObjAlign::normal>,  //
    TestParams<Movable::yes, Destructible::nothrow, _, NothrowCall::no,
               ObjSize::large, ObjAlign::normal>  //

// Dynamic memory allocation for over-aligned data was introduced in C++17.
// See https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0035r4.html
#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 201703L
    // Types that must use remote storage because of a large alignment.
    ,
    TestParams<Movable::trivial, Destructible::trivial, _, NothrowCall::no,
               ObjSize::small, ObjAlign::large>,  //
    TestParams<Movable::nothrow, Destructible::trivial, _, NothrowCall::no,
               ObjSize::small, ObjAlign::large>,  //
    TestParams<Movable::trivial, Destructible::nothrow, _, NothrowCall::no,
               ObjSize::small, ObjAlign::large>,  //
    TestParams<Movable::nothrow, Destructible::nothrow, _, NothrowCall::no,
               ObjSize::small, ObjAlign::large>  //
#endif
    >;
using TestParameterListRemoteNonMovable = ::testing::Types<
    // "Normal" aligned types that are large and have trivial destructors
    TestParams<Movable::no, Destructible::trivial, _, NothrowCall::no,
               ObjSize::small, ObjAlign::normal>,  //
    TestParams<Movable::no, Destructible::trivial, _, NothrowCall::no,
               ObjSize::large, ObjAlign::normal>,  //
    // Same as above but with non-trivial destructors
    TestParams<Movable::no, Destructible::nothrow, _, NothrowCall::no,
               ObjSize::small, ObjAlign::normal>,  //
    TestParams<Movable::no, Destructible::nothrow, _, NothrowCall::no,
               ObjSize::large, ObjAlign::normal>  //
    >;

// Parameters that lead to local storage
using TestParameterListLocal = ::testing::Types<
    // Types that meet the requirements and have trivial destructors
    TestParams<Movable::trivial, Destructible::trivial, _, NothrowCall::no,
               ObjSize::small, ObjAlign::normal>,  //
    TestParams<Movable::nothrow, Destructible::trivial, _, NothrowCall::no,
               ObjSize::small, ObjAlign::normal>,  //

    // Same as above but with non-trivial destructors
    TestParams<Movable::trivial, Destructible::trivial, _, NothrowCall::no,
               ObjSize::small, ObjAlign::normal>,  //
    TestParams<Movable::nothrow, Destructible::trivial, _, NothrowCall::no,
               ObjSize::small, ObjAlign::normal>  //
    >;

// All of the tests that are run for every possible combination of types.
REGISTER_TYPED_TEST_SUITE_P(
    AnyInvTestBasic, DefaultConstruction, ConstructionNullptr,
    ConstructionNullFunctionPtr, ConstructionNullMemberFunctionPtr,
    ConstructionNullMemberObjectPtr, ConstructionMemberFunctionPtr,
    ConstructionMemberObjectPtr, ConstructionFunctionReferenceDecay,
    ConstructionCompatibleAnyInvocableEmpty,
    ConstructionCompatibleAnyInvocableNonempty, InPlaceConstruction,
    ConversionToBool, Invocation, InPlaceConstructionInitializerList,
    InPlaceNullFunPtrConstruction, InPlaceNullFunPtrConstructionValueInit,
    InPlaceNullMemFunPtrConstruction, InPlaceNullMemFunPtrConstructionValueInit,
    InPlaceNullMemObjPtrConstruction, InPlaceNullMemObjPtrConstructionValueInit,
    InPlaceVoidCovarianceConstruction, MoveConstructionFromEmpty,
    MoveConstructionFromNonEmpty, ComparisonWithNullptrEmpty,
    ComparisonWithNullptrNonempty, ResultType);

INSTANTIATE_TYPED_TEST_SUITE_P(
    NonRvalueCallMayThrow, AnyInvTestBasic,
    TestParameterListNonRvalueQualifiersCallMayThrow);
INSTANTIATE_TYPED_TEST_SUITE_P(RvalueCallMayThrow, AnyInvTestBasic,
                               TestParameterListRvalueQualifiersCallMayThrow);

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteMovable, AnyInvTestBasic,
                               TestParameterListRemoteMovable);
INSTANTIATE_TYPED_TEST_SUITE_P(RemoteNonMovable, AnyInvTestBasic,
                               TestParameterListRemoteNonMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestBasic, TestParameterListLocal);

INSTANTIATE_TYPED_TEST_SUITE_P(NonRvalueCallNothrow, AnyInvTestBasic,
                               TestParameterListNonRvalueQualifiersNothrowCall);
INSTANTIATE_TYPED_TEST_SUITE_P(CallNothrowRvalue, AnyInvTestBasic,
                               TestParameterListRvalueQualifiersNothrowCall);

// Tests for functions that take two operands.
REGISTER_TYPED_TEST_SUITE_P(
    AnyInvTestCombinatoric, MoveAssignEmptyEmptyLhsRhs,
    MoveAssignEmptyLhsNonemptyRhs, MoveAssignNonemptyEmptyLhsRhs,
    MoveAssignNonemptyLhsNonemptyRhs, SelfMoveAssignEmpty,
    SelfMoveAssignNonempty, AssignNullptrEmptyLhs,
    AssignNullFunctionPtrEmptyLhs, AssignNullMemberFunctionPtrEmptyLhs,
    AssignNullMemberObjectPtrEmptyLhs, AssignMemberFunctionPtrEmptyLhs,
    AssignMemberObjectPtrEmptyLhs, AssignFunctionReferenceDecayEmptyLhs,
    AssignCompatibleAnyInvocableEmptyLhsEmptyRhs,
    AssignCompatibleAnyInvocableEmptyLhsNonemptyRhs, AssignNullptrNonemptyLhs,
    AssignNullFunctionPtrNonemptyLhs, AssignNullMemberFunctionPtrNonemptyLhs,
    AssignNullMemberObjectPtrNonemptyLhs, AssignMemberFunctionPtrNonemptyLhs,
    AssignMemberObjectPtrNonemptyLhs, AssignFunctionReferenceDecayNonemptyLhs,
    AssignCompatibleAnyInvocableNonemptyLhsEmptyRhs,
    AssignCompatibleAnyInvocableNonemptyLhsNonemptyRhs, SwapEmptyLhsEmptyRhs,
    SwapEmptyLhsNonemptyRhs, SwapNonemptyLhsEmptyRhs,
    SwapNonemptyLhsNonemptyRhs);

INSTANTIATE_TYPED_TEST_SUITE_P(
    NonRvalueCallMayThrow, AnyInvTestCombinatoric,
    TestParameterListNonRvalueQualifiersCallMayThrow);
INSTANTIATE_TYPED_TEST_SUITE_P(RvalueCallMayThrow, AnyInvTestCombinatoric,
                               TestParameterListRvalueQualifiersCallMayThrow);

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteMovable, AnyInvTestCombinatoric,
                               TestParameterListRemoteMovable);
INSTANTIATE_TYPED_TEST_SUITE_P(RemoteNonMovable, AnyInvTestCombinatoric,
                               TestParameterListRemoteNonMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestCombinatoric,
                               TestParameterListLocal);

INSTANTIATE_TYPED_TEST_SUITE_P(NonRvalueCallNothrow, AnyInvTestCombinatoric,
                               TestParameterListNonRvalueQualifiersNothrowCall);
INSTANTIATE_TYPED_TEST_SUITE_P(RvalueCallNothrow, AnyInvTestCombinatoric,
                               TestParameterListRvalueQualifiersNothrowCall);

REGISTER_TYPED_TEST_SUITE_P(AnyInvTestMovable,
                            ConversionConstructionUserDefinedType,
                            ConversionConstructionVoidCovariance,
                            ConversionAssignUserDefinedTypeEmptyLhs,
                            ConversionAssignUserDefinedTypeNonemptyLhs,
                            ConversionAssignVoidCovariance);

INSTANTIATE_TYPED_TEST_SUITE_P(
    NonRvalueCallMayThrow, AnyInvTestMovable,
    TestParameterListNonRvalueQualifiersCallMayThrow);
INSTANTIATE_TYPED_TEST_SUITE_P(RvalueCallMayThrow, AnyInvTestMovable,
                               TestParameterListRvalueQualifiersCallMayThrow);

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteMovable, AnyInvTestMovable,
                               TestParameterListRemoteMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestMovable,
                               TestParameterListLocal);

INSTANTIATE_TYPED_TEST_SUITE_P(NonRvalueCallNothrow, AnyInvTestMovable,
                               TestParameterListNonRvalueQualifiersNothrowCall);
INSTANTIATE_TYPED_TEST_SUITE_P(RvalueCallNothrow, AnyInvTestMovable,
                               TestParameterListRvalueQualifiersNothrowCall);

REGISTER_TYPED_TEST_SUITE_P(AnyInvTestNoexceptFalse,
                            ConversionConstructionConstraints,
                            ConversionAssignConstraints);

INSTANTIATE_TYPED_TEST_SUITE_P(
    NonRvalueCallMayThrow, AnyInvTestNoexceptFalse,
    TestParameterListNonRvalueQualifiersCallMayThrow);
INSTANTIATE_TYPED_TEST_SUITE_P(RvalueCallMayThrow, AnyInvTestNoexceptFalse,
                               TestParameterListRvalueQualifiersCallMayThrow);

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteMovable, AnyInvTestNoexceptFalse,
                               TestParameterListRemoteMovable);
INSTANTIATE_TYPED_TEST_SUITE_P(RemoteNonMovable, AnyInvTestNoexceptFalse,
                               TestParameterListRemoteNonMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestNoexceptFalse,
                               TestParameterListLocal);

REGISTER_TYPED_TEST_SUITE_P(AnyInvTestNoexceptTrue,
                            ConversionConstructionConstraints,
                            ConversionAssignConstraints);

INSTANTIATE_TYPED_TEST_SUITE_P(NonRvalueCallNothrow, AnyInvTestNoexceptTrue,
                               TestParameterListNonRvalueQualifiersNothrowCall);
INSTANTIATE_TYPED_TEST_SUITE_P(RvalueCallNothrow, AnyInvTestNoexceptTrue,
                               TestParameterListRvalueQualifiersNothrowCall);

REGISTER_TYPED_TEST_SUITE_P(AnyInvTestNonRvalue,
                            ConversionConstructionReferenceWrapper,
                            NonMoveableResultType,
                            ConversionAssignReferenceWrapperEmptyLhs,
                            ConversionAssignReferenceWrapperNonemptyLhs);

INSTANTIATE_TYPED_TEST_SUITE_P(
    NonRvalueCallMayThrow, AnyInvTestNonRvalue,
    TestParameterListNonRvalueQualifiersCallMayThrow);

INSTANTIATE_TYPED_TEST_SUITE_P(RemoteMovable, AnyInvTestNonRvalue,
                               TestParameterListRemoteMovable);
INSTANTIATE_TYPED_TEST_SUITE_P(RemoteNonMovable, AnyInvTestNonRvalue,
                               TestParameterListRemoteNonMovable);

INSTANTIATE_TYPED_TEST_SUITE_P(Local, AnyInvTestNonRvalue,
                               TestParameterListLocal);

INSTANTIATE_TYPED_TEST_SUITE_P(NonRvalueCallNothrow, AnyInvTestNonRvalue,
                               TestParameterListNonRvalueQualifiersNothrowCall);

REGISTER_TYPED_TEST_SUITE_P(AnyInvTestRvalue,
                            ConversionConstructionReferenceWrapper,
                            NonMoveableResultType,
                            ConversionAssignReferenceWrapper,
                            NonConstCrashesOnSecondCall,
                            QualifierIndependentObjectLifetime);

INSTANTIATE_TYPED_TEST_SUITE_P(RvalueCallMayThrow, AnyInvTestRvalue,
                               TestParameterListRvalueQualifiersCallMayThrow);

INSTANTIATE_TYPED_TEST_SUITE_P(CallNothrowRvalue, AnyInvTestRvalue,
                               TestParameterListRvalueQualifiersNothrowCall);

// Minimal SFINAE testing for platforms where we can't run the tests, but we can
// build binaries for.
static_assert(
    std::is_convertible<void (*)(), absl::AnyInvocable<void() &&>>::value, "");
static_assert(!std::is_convertible<void*, absl::AnyInvocable<void() &&>>::value,
              "");

#undef ABSL_INTERNAL_NOEXCEPT_SPEC

}  // namespace
