// Copyright 2023 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// The IfConstexpr and IfConstexprElse utilities in this file are meant to be
// used to emulate `if constexpr` in pre-C++17 mode in library implementation.
// The motivation is to allow for avoiding complex SFINAE.
//
// The functions passed in must depend on the type(s) of the object(s) that
// require SFINAE. For example:
// template<typename T>
// int MaybeFoo(T& t) {
//   if constexpr (HasFoo<T>::value) return t.foo();
//   return 0;
// }
//
// can be written in pre-C++17 as:
//
// template<typename T>
// int MaybeFoo(T& t) {
//   int i = 0;
//   absl::utility_internal::IfConstexpr<HasFoo<T>::value>(
//       [&](const auto& fooer) { i = fooer.foo(); }, t);
//   return i;
// }

#ifndef ABSL_UTILITY_INTERNAL_IF_CONSTEXPR_H_
#define ABSL_UTILITY_INTERNAL_IF_CONSTEXPR_H_

#include <tuple>
#include <utility>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace utility_internal {

template <bool condition, typename TrueFunc, typename FalseFunc,
          typename... Args>
auto IfConstexprElse(TrueFunc&& true_func, FalseFunc&& false_func,
                     Args&&... args) {
  return std::get<condition>(std::forward_as_tuple(
      std::forward<FalseFunc>(false_func), std::forward<TrueFunc>(true_func)))(
      std::forward<Args>(args)...);
}

template <bool condition, typename Func, typename... Args>
void IfConstexpr(Func&& func, Args&&... args) {
  IfConstexprElse<condition>(std::forward<Func>(func), [](auto&&...){},
                             std::forward<Args>(args)...);
}

}  // namespace utility_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_UTILITY_INTERNAL_IF_CONSTEXPR_H_
