//
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
//

#ifndef ABSL_BASE_INTERNAL_FAST_TYPE_ID_H_
#define ABSL_BASE_INTERNAL_FAST_TYPE_ID_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

template <typename Type>
struct FastTypeTag {
  constexpr static char dummy_var = 0;
};

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
template <typename Type>
constexpr char FastTypeTag<Type>::dummy_var;
#endif

// FastTypeId<Type>() evaluates at compile/link-time to a unique pointer for the
// passed-in type. These are meant to be good match for keys into maps or
// straight up comparisons.
using FastTypeIdType = const void*;

template <typename Type>
constexpr inline FastTypeIdType FastTypeId() {
  return &FastTypeTag<Type>::dummy_var;
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_FAST_TYPE_ID_H_
