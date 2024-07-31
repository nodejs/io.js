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
//

#ifndef ABSL_BASE_INTERNAL_IDENTITY_H_
#define ABSL_BASE_INTERNAL_IDENTITY_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace internal {

// This is a back-fill of C++20's `std::type_identity`.
template <typename T>
struct type_identity {
  typedef T type;
};

// This is a back-fill of C++20's `std::type_identity_t`.
template <typename T>
using type_identity_t = typename type_identity<T>::type;

}  // namespace internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_IDENTITY_H_
