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

#ifndef ABSL_BASE_INTERNAL_NULLABILITY_IMPL_H_
#define ABSL_BASE_INTERNAL_NULLABILITY_IMPL_H_

#include <memory>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/meta/type_traits.h"

namespace absl {

namespace nullability_internal {

// `IsNullabilityCompatible` checks whether its first argument is a class
// explicitly tagged as supporting nullability annotations. The tag is the type
// declaration `absl_nullability_compatible`.
template <typename, typename = void>
struct IsNullabilityCompatible : std::false_type {};

template <typename T>
struct IsNullabilityCompatible<
    T, absl::void_t<typename T::absl_nullability_compatible>> : std::true_type {
};

template <typename T>
constexpr bool IsSupportedType = IsNullabilityCompatible<T>::value;

template <typename T>
constexpr bool IsSupportedType<T*> = true;

template <typename T, typename U>
constexpr bool IsSupportedType<T U::*> = true;

template <typename T, typename... Deleter>
constexpr bool IsSupportedType<std::unique_ptr<T, Deleter...>> = true;

template <typename T>
constexpr bool IsSupportedType<std::shared_ptr<T>> = true;

template <typename T>
struct EnableNullable {
  static_assert(nullability_internal::IsSupportedType<std::remove_cv_t<T>>,
                "Template argument must be a raw or supported smart pointer "
                "type. See absl/base/nullability.h.");
  using type = T;
};

template <typename T>
struct EnableNonnull {
  static_assert(nullability_internal::IsSupportedType<std::remove_cv_t<T>>,
                "Template argument must be a raw or supported smart pointer "
                "type. See absl/base/nullability.h.");
  using type = T;
};

template <typename T>
struct EnableNullabilityUnknown {
  static_assert(nullability_internal::IsSupportedType<std::remove_cv_t<T>>,
                "Template argument must be a raw or supported smart pointer "
                "type. See absl/base/nullability.h.");
  using type = T;
};

// Note: we do not apply Clang nullability attributes (e.g. _Nullable).  These
// only support raw pointers, and conditionally enabling them only for raw
// pointers inhibits template arg deduction.  Ideally, they would support all
// pointer-like types.
template <typename T, typename = typename EnableNullable<T>::type>
using NullableImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nullable")]]
#endif
    = T;

template <typename T, typename = typename EnableNonnull<T>::type>
using NonnullImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nonnull")]]
#endif
    = T;

template <typename T, typename = typename EnableNullabilityUnknown<T>::type>
using NullabilityUnknownImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nullability_Unspecified")]]
#endif
    = T;

}  // namespace nullability_internal
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_NULLABILITY_IMPL_H_
