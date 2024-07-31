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
//
// -----------------------------------------------------------------------------
// File: function_ref.h
// -----------------------------------------------------------------------------
//
// This header file defines the `absl::FunctionRef` type for holding a
// non-owning reference to an object of any invocable type. This function
// reference is typically most useful as a type-erased argument type for
// accepting function types that neither take ownership nor copy the type; using
// the reference type in this case avoids a copy and an allocation. Best
// practices of other non-owning reference-like objects (such as
// `absl::string_view`) apply here.
//
//  An `absl::FunctionRef` is similar in usage to a `std::function` but has the
//  following differences:
//
//  * It doesn't own the underlying object.
//  * It doesn't have a null or empty state.
//  * It never performs deep copies or allocations.
//  * It's much faster and cheaper to construct.
//  * It's trivially copyable and destructable.
//
// Generally, `absl::FunctionRef` should not be used as a return value, data
// member, or to initialize a `std::function`. Such usages will often lead to
// problematic lifetime issues. Once you convert something to an
// `absl::FunctionRef` you cannot make a deep copy later.
//
// This class is suitable for use wherever a "const std::function<>&"
// would be used without making a copy. ForEach functions and other versions of
// the visitor pattern are a good example of when this class should be used.
//
// This class is trivial to copy and should be passed by value.
#ifndef ABSL_FUNCTIONAL_FUNCTION_REF_H_
#define ABSL_FUNCTIONAL_FUNCTION_REF_H_

#include <cassert>
#include <functional>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/functional/internal/function_ref.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// FunctionRef
//
// Dummy class declaration to allow the partial specialization based on function
// types below.
template <typename T>
class FunctionRef;

// FunctionRef
//
// An `absl::FunctionRef` is a lightweight wrapper to any invocable object with
// a compatible signature. Generally, an `absl::FunctionRef` should only be used
// as an argument type and should be preferred as an argument over a const
// reference to a `std::function`. `absl::FunctionRef` itself does not allocate,
// although the wrapped invocable may.
//
// Example:
//
//   // The following function takes a function callback by const reference
//   bool Visitor(const std::function<void(my_proto&,
//                                         absl::string_view)>& callback);
//
//   // Assuming that the function is not stored or otherwise copied, it can be
//   // replaced by an `absl::FunctionRef`:
//   bool Visitor(absl::FunctionRef<void(my_proto&, absl::string_view)>
//                  callback);
//
// Note: the assignment operator within an `absl::FunctionRef` is intentionally
// deleted to prevent misuse; because the `absl::FunctionRef` does not own the
// underlying type, assignment likely indicates misuse.
template <typename R, typename... Args>
class FunctionRef<R(Args...)> {
 private:
  // Used to disable constructors for objects that are not compatible with the
  // signature of this FunctionRef.
  template <typename F,
            typename FR = absl::base_internal::invoke_result_t<F, Args&&...>>
  using EnableIfCompatible =
      typename std::enable_if<std::is_void<R>::value ||
                              std::is_convertible<FR, R>::value>::type;

 public:
  // Constructs a FunctionRef from any invocable type.
  template <typename F, typename = EnableIfCompatible<const F&>>
  // NOLINTNEXTLINE(runtime/explicit)
  FunctionRef(const F& f ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : invoker_(&absl::functional_internal::InvokeObject<F, R, Args...>) {
    absl::functional_internal::AssertNonNull(f);
    ptr_.obj = &f;
  }

  // Overload for function pointers. This eliminates a level of indirection that
  // would happen if the above overload was used (it lets us store the pointer
  // instead of a pointer to a pointer).
  //
  // This overload is also used for references to functions, since references to
  // functions can decay to function pointers implicitly.
  template <
      typename F, typename = EnableIfCompatible<F*>,
      absl::functional_internal::EnableIf<absl::is_function<F>::value> = 0>
  FunctionRef(F* f)  // NOLINT(runtime/explicit)
      : invoker_(&absl::functional_internal::InvokeFunction<F*, R, Args...>) {
    assert(f != nullptr);
    ptr_.fun = reinterpret_cast<decltype(ptr_.fun)>(f);
  }

  // To help prevent subtle lifetime bugs, FunctionRef is not assignable.
  // Typically, it should only be used as an argument type.
  FunctionRef& operator=(const FunctionRef& rhs) = delete;
  FunctionRef(const FunctionRef& rhs) = default;

  // Call the underlying object.
  R operator()(Args... args) const {
    return invoker_(ptr_, std::forward<Args>(args)...);
  }

 private:
  absl::functional_internal::VoidPtr ptr_;
  absl::functional_internal::Invoker<R, Args...> invoker_;
};

// Allow const qualified function signatures. Since FunctionRef requires
// constness anyway we can just make this a no-op.
template <typename R, typename... Args>
class FunctionRef<R(Args...) const> : public FunctionRef<R(Args...)> {
 public:
  using FunctionRef<R(Args...)>::FunctionRef;
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_FUNCTIONAL_FUNCTION_REF_H_
