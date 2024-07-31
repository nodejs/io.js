// Copyright 2020 The Abseil Authors.
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

#ifndef ABSL_STRINGS_INTERNAL_STRING_CONSTANT_H_
#define ABSL_STRINGS_INTERNAL_STRING_CONSTANT_H_

#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

// StringConstant<T> represents a compile time string constant.
// It can be accessed via its `absl::string_view value` static member.
// It is guaranteed that the `string_view` returned has constant `.data()`,
// constant `.size()` and constant `value[i]` for all `0 <= i < .size()`
//
// The `T` is an opaque type. It is guaranteed that different string constants
// will have different values of `T`. This allows users to associate the string
// constant with other static state at compile time.
//
// Instances should be made using the `MakeStringConstant()` factory function
// below.
template <typename T>
struct StringConstant {
 private:
  static constexpr bool TryConstexprEval(absl::string_view view) {
    return view.empty() || 2 * view[0] != 1;
  }

 public:
  static constexpr absl::string_view value = T{}();
  constexpr absl::string_view operator()() const { return value; }

  // Check to be sure `view` points to constant data.
  // Otherwise, it can't be constant evaluated.
  static_assert(TryConstexprEval(value),
                "The input string_view must point to constant data.");
};

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
template <typename T>
constexpr absl::string_view StringConstant<T>::value;
#endif

// Factory function for `StringConstant` instances.
// It supports callables that have a constexpr default constructor and a
// constexpr operator().
// It must return an `absl::string_view` or `const char*` pointing to constant
// data. This is validated at compile time.
template <typename T>
constexpr StringConstant<T> MakeStringConstant(T) {
  return {};
}

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STRING_CONSTANT_H_
