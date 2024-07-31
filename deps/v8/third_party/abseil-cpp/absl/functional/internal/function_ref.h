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

#ifndef ABSL_FUNCTIONAL_INTERNAL_FUNCTION_REF_H_
#define ABSL_FUNCTIONAL_INTERNAL_FUNCTION_REF_H_

#include <cassert>
#include <functional>
#include <type_traits>

#include "absl/base/internal/invoke.h"
#include "absl/functional/any_invocable.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace functional_internal {

// Like a void* that can handle function pointers as well. The standard does not
// allow function pointers to round-trip through void*, but void(*)() is fine.
//
// Note: It's important that this class remains trivial and is the same size as
// a pointer, since this allows the compiler to perform tail-call optimizations
// when the underlying function is a callable object with a matching signature.
union VoidPtr {
  const void* obj;
  void (*fun)();
};

// Chooses the best type for passing T as an argument.
// Attempt to be close to SystemV AMD64 ABI. Objects with trivial copy ctor are
// passed by value.
template <typename T,
          bool IsLValueReference = std::is_lvalue_reference<T>::value>
struct PassByValue : std::false_type {};

template <typename T>
struct PassByValue<T, /*IsLValueReference=*/false>
    : std::integral_constant<bool,
                             absl::is_trivially_copy_constructible<T>::value &&
                                 absl::is_trivially_copy_assignable<
                                     typename std::remove_cv<T>::type>::value &&
                                 std::is_trivially_destructible<T>::value &&
                                 sizeof(T) <= 2 * sizeof(void*)> {};

template <typename T>
struct ForwardT : std::conditional<PassByValue<T>::value, T, T&&> {};

// An Invoker takes a pointer to the type-erased invokable object, followed by
// the arguments that the invokable object expects.
//
// Note: The order of arguments here is an optimization, since member functions
// have an implicit "this" pointer as their first argument, putting VoidPtr
// first allows the compiler to perform tail-call optimization in many cases.
template <typename R, typename... Args>
using Invoker = R (*)(VoidPtr, typename ForwardT<Args>::type...);

//
// InvokeObject and InvokeFunction provide static "Invoke" functions that can be
// used as Invokers for objects or functions respectively.
//
// static_cast<R> handles the case the return type is void.
template <typename Obj, typename R, typename... Args>
R InvokeObject(VoidPtr ptr, typename ForwardT<Args>::type... args) {
  auto o = static_cast<const Obj*>(ptr.obj);
  return static_cast<R>(
      absl::base_internal::invoke(*o, std::forward<Args>(args)...));
}

template <typename Fun, typename R, typename... Args>
R InvokeFunction(VoidPtr ptr, typename ForwardT<Args>::type... args) {
  auto f = reinterpret_cast<Fun>(ptr.fun);
  return static_cast<R>(
      absl::base_internal::invoke(f, std::forward<Args>(args)...));
}

template <typename Sig>
void AssertNonNull(const std::function<Sig>& f) {
  assert(f != nullptr);
  (void)f;
}

template <typename Sig>
void AssertNonNull(const AnyInvocable<Sig>& f) {
  assert(f != nullptr);
  (void)f;
}

template <typename F>
void AssertNonNull(const F&) {}

template <typename F, typename C>
void AssertNonNull(F C::*f) {
  assert(f != nullptr);
  (void)f;
}

template <bool C>
using EnableIf = typename ::std::enable_if<C, int>::type;

}  // namespace functional_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FUNCTIONAL_INTERNAL_FUNCTION_REF_H_
