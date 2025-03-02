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
#include "absl/base/config.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace nullability_internal {

template <typename T>
using NullableImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nullable")]]
#endif
// Don't add the _Nullable attribute in Objective-C compiles. Many Objective-C
// projects enable the `-Wnullable-to-nonnull-conversion warning`, which is
// liable to produce false positives.
#if ABSL_HAVE_FEATURE(nullability_on_classes) && !defined(__OBJC__)
    = T _Nullable;
#else
    = T;
#endif

template <typename T>
using NonnullImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nonnull")]]
#endif
#if ABSL_HAVE_FEATURE(nullability_on_classes) && !defined(__OBJC__)
    = T _Nonnull;
#else
    = T;
#endif

template <typename T>
using NullabilityUnknownImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nullability_Unspecified")]]
#endif
#if ABSL_HAVE_FEATURE(nullability_on_classes) && !defined(__OBJC__)
    = T _Null_unspecified;
#else
    = T;
#endif

}  // namespace nullability_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_NULLABILITY_IMPL_H_
