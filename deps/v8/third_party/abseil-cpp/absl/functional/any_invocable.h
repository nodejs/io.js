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
//
// -----------------------------------------------------------------------------
// File: any_invocable.h
// -----------------------------------------------------------------------------
//
// This header file defines an `absl::AnyInvocable` type that assumes ownership
// and wraps an object of an invocable type. (Invocable types adhere to the
// concept specified in https://en.cppreference.com/w/cpp/concepts/invocable.)
//
// In general, prefer `absl::AnyInvocable` when you need a type-erased
// function parameter that needs to take ownership of the type.
//
// NOTE: `absl::AnyInvocable` is similar to the C++23 `std::move_only_function`
// abstraction, but has a slightly different API and is not designed to be a
// drop-in replacement or C++11-compatible backfill of that type.
//
// Credits to Matt Calabrese (https://github.com/mattcalabrese) for the original
// implementation.

#ifndef ABSL_FUNCTIONAL_ANY_INVOCABLE_H_
#define ABSL_FUNCTIONAL_ANY_INVOCABLE_H_

#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/functional/internal/any_invocable.h"
#include "absl/meta/type_traits.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::AnyInvocable
//
// `absl::AnyInvocable` is a functional wrapper type, like `std::function`, that
// assumes ownership of an invocable object. Unlike `std::function`, an
// `absl::AnyInvocable` is more type-safe and provides the following additional
// benefits:
//
// * Properly adheres to const correctness of the underlying type
// * Is move-only so avoids concurrency problems with copied invocables and
//   unnecessary copies in general.
// * Supports reference qualifiers allowing it to perform unique actions (noted
//   below).
//
// `absl::AnyInvocable` is a template, and an `absl::AnyInvocable` instantiation
// may wrap any invocable object with a compatible function signature, e.g.
// having arguments and return types convertible to types matching the
// `absl::AnyInvocable` signature, and also matching any stated reference
// qualifiers, as long as that type is moveable. It therefore provides broad
// type erasure for functional objects.
//
// An `absl::AnyInvocable` is typically used as a type-erased function parameter
// for accepting various functional objects:
//
// // Define a function taking an AnyInvocable parameter.
// void my_func(absl::AnyInvocable<int()> f) {
//   ...
// };
//
// // That function can accept any invocable type:
//
// // Accept a function reference. We don't need to move a reference.
// int func1() { return 0; };
// my_func(func1);
//
// // Accept a lambda. We use std::move here because otherwise my_func would
// // copy the lambda.
// auto lambda = []() { return 0; };
// my_func(std::move(lambda));
//
// // Accept a function pointer. We don't need to move a function pointer.
// func2 = &func1;
// my_func(func2);
//
// // Accept an std::function by moving it. Note that the lambda is copyable
// // (satisfying std::function requirements) and moveable (satisfying
// // absl::AnyInvocable requirements).
// std::function<int()> func6 = []() { return 0; };
// my_func(std::move(func6));
//
// `AnyInvocable` also properly respects `const` qualifiers, reference
// qualifiers, and the `noexcept` specification (only in C++ 17 and beyond) as
// part of the user-specified function type (e.g.
// `AnyInvocable<void()&& const noexcept>`). These qualifiers will be applied to
// the `AnyInvocable` object's `operator()`, and the underlying invocable must
// be compatible with those qualifiers.
//
// Comparison of const and non-const function types:
//
//   // Store a closure inside of `func` with the function type `int()`.
//   // Note that we have made `func` itself `const`.
//   const AnyInvocable<int()> func = [](){ return 0; };
//
//   func();  // Compile-error: the passed type `int()` isn't `const`.
//
//   // Store a closure inside of `const_func` with the function type
//   // `int() const`.
//   // Note that we have also made `const_func` itself `const`.
//   const AnyInvocable<int() const> const_func = [](){ return 0; };
//
//   const_func();  // Fine: `int() const` is `const`.
//
// In the above example, the call `func()` would have compiled if
// `std::function` were used even though the types are not const compatible.
// This is a bug, and using `absl::AnyInvocable` properly detects that bug.
//
// In addition to affecting the signature of `operator()`, the `const` and
// reference qualifiers of the function type also appropriately constrain which
// kinds of invocable objects you are allowed to place into the `AnyInvocable`
// instance. If you specify a function type that is const-qualified, then
// anything that you attempt to put into the `AnyInvocable` must be callable on
// a `const` instance of that type.
//
// Constraint example:
//
//   // Fine because the lambda is callable when `const`.
//   AnyInvocable<int() const> func = [=](){ return 0; };
//
//   // This is a compile-error because the lambda isn't callable when `const`.
//   AnyInvocable<int() const> error = [=]() mutable { return 0; };
//
// An `&&` qualifier can be used to express that an `absl::AnyInvocable`
// instance should be invoked at most once:
//
//   // Invokes `continuation` with the logical result of an operation when
//   // that operation completes (common in asynchronous code).
//   void CallOnCompletion(AnyInvocable<void(int)&&> continuation) {
//     int result_of_foo = foo();
//
//     // `std::move` is required because the `operator()` of `continuation` is
//     // rvalue-reference qualified.
//     std::move(continuation)(result_of_foo);
//   }
//
// Attempting to call `absl::AnyInvocable` multiple times in such a case
// results in undefined behavior.
template <class Sig>
class AnyInvocable : private internal_any_invocable::Impl<Sig> {
 private:
  static_assert(
      std::is_function<Sig>::value,
      "The template argument of AnyInvocable must be a function type.");

  using Impl = internal_any_invocable::Impl<Sig>;

 public:
  // The return type of Sig
  using result_type = typename Impl::result_type;

  // Constructors

  // Constructs the `AnyInvocable` in an empty state.
  AnyInvocable() noexcept = default;
  AnyInvocable(std::nullptr_t) noexcept {}  // NOLINT

  // Constructs the `AnyInvocable` from an existing `AnyInvocable` by a move.
  // Note that `f` is not guaranteed to be empty after move-construction,
  // although it may be.
  AnyInvocable(AnyInvocable&& /*f*/) noexcept = default;

  // Constructs an `AnyInvocable` from an invocable object.
  //
  // Upon construction, `*this` is only empty if `f` is a function pointer or
  // member pointer type and is null, or if `f` is an `AnyInvocable` that is
  // empty.
  template <class F, typename = absl::enable_if_t<
                         internal_any_invocable::CanConvert<Sig, F>::value>>
  AnyInvocable(F&& f)  // NOLINT
      : Impl(internal_any_invocable::ConversionConstruct(),
             std::forward<F>(f)) {}

  // Constructs an `AnyInvocable` that holds an invocable object of type `T`,
  // which is constructed in-place from the given arguments.
  //
  // Example:
  //
  //   AnyInvocable<int(int)> func(
  //       absl::in_place_type<PossiblyImmovableType>, arg1, arg2);
  //
  template <class T, class... Args,
            typename = absl::enable_if_t<
                internal_any_invocable::CanEmplace<Sig, T, Args...>::value>>
  explicit AnyInvocable(absl::in_place_type_t<T>, Args&&... args)
      : Impl(absl::in_place_type<absl::decay_t<T>>,
             std::forward<Args>(args)...) {
    static_assert(std::is_same<T, absl::decay_t<T>>::value,
                  "The explicit template argument of in_place_type is required "
                  "to be an unqualified object type.");
  }

  // Overload of the above constructor to support list-initialization.
  template <class T, class U, class... Args,
            typename = absl::enable_if_t<internal_any_invocable::CanEmplace<
                Sig, T, std::initializer_list<U>&, Args...>::value>>
  explicit AnyInvocable(absl::in_place_type_t<T>,
                        std::initializer_list<U> ilist, Args&&... args)
      : Impl(absl::in_place_type<absl::decay_t<T>>, ilist,
             std::forward<Args>(args)...) {
    static_assert(std::is_same<T, absl::decay_t<T>>::value,
                  "The explicit template argument of in_place_type is required "
                  "to be an unqualified object type.");
  }

  // Assignment Operators

  // Assigns an `AnyInvocable` through move-assignment.
  // Note that `f` is not guaranteed to be empty after move-assignment
  // although it may be.
  AnyInvocable& operator=(AnyInvocable&& /*f*/) noexcept = default;

  // Assigns an `AnyInvocable` from a nullptr, clearing the `AnyInvocable`. If
  // not empty, destroys the target, putting `*this` into an empty state.
  AnyInvocable& operator=(std::nullptr_t) noexcept {
    this->Clear();
    return *this;
  }

  // Assigns an `AnyInvocable` from an existing `AnyInvocable` instance.
  //
  // Upon assignment, `*this` is only empty if `f` is a function pointer or
  // member pointer type and is null, or if `f` is an `AnyInvocable` that is
  // empty.
  template <class F, typename = absl::enable_if_t<
                         internal_any_invocable::CanAssign<Sig, F>::value>>
  AnyInvocable& operator=(F&& f) {
    *this = AnyInvocable(std::forward<F>(f));
    return *this;
  }

  // Assigns an `AnyInvocable` from a reference to an invocable object.
  // Upon assignment, stores a reference to the invocable object in the
  // `AnyInvocable` instance.
  template <
      class F,
      typename = absl::enable_if_t<
          internal_any_invocable::CanAssignReferenceWrapper<Sig, F>::value>>
  AnyInvocable& operator=(std::reference_wrapper<F> f) noexcept {
    *this = AnyInvocable(f);
    return *this;
  }

  // Destructor

  // If not empty, destroys the target.
  ~AnyInvocable() = default;

  // absl::AnyInvocable::swap()
  //
  // Exchanges the targets of `*this` and `other`.
  void swap(AnyInvocable& other) noexcept { std::swap(*this, other); }

  // absl::AnyInvocable::operator bool()
  //
  // Returns `true` if `*this` is not empty.
  //
  // WARNING: An `AnyInvocable` that wraps an empty `std::function` is not
  // itself empty. This behavior is consistent with the standard equivalent
  // `std::move_only_function`.
  //
  // In other words:
  //   std::function<void()> f;  // empty
  //   absl::AnyInvocable<void()> a = std::move(f);  // not empty
  explicit operator bool() const noexcept { return this->HasValue(); }

  // Invokes the target object of `*this`. `*this` must not be empty.
  //
  // Note: The signature of this function call operator is the same as the
  //       template parameter `Sig`.
  using Impl::operator();

  // Equality operators

  // Returns `true` if `*this` is empty.
  friend bool operator==(const AnyInvocable& f, std::nullptr_t) noexcept {
    return !f.HasValue();
  }

  // Returns `true` if `*this` is empty.
  friend bool operator==(std::nullptr_t, const AnyInvocable& f) noexcept {
    return !f.HasValue();
  }

  // Returns `false` if `*this` is empty.
  friend bool operator!=(const AnyInvocable& f, std::nullptr_t) noexcept {
    return f.HasValue();
  }

  // Returns `false` if `*this` is empty.
  friend bool operator!=(std::nullptr_t, const AnyInvocable& f) noexcept {
    return f.HasValue();
  }

  // swap()
  //
  // Exchanges the targets of `f1` and `f2`.
  friend void swap(AnyInvocable& f1, AnyInvocable& f2) noexcept { f1.swap(f2); }

 private:
  // Friending other instantiations is necessary for conversions.
  template <bool /*SigIsNoexcept*/, class /*ReturnType*/, class... /*P*/>
  friend class internal_any_invocable::CoreImpl;
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FUNCTIONAL_ANY_INVOCABLE_H_
