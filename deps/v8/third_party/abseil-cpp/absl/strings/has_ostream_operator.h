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

#ifndef ABSL_STRINGS_HAS_OSTREAM_OPERATOR_H_
#define ABSL_STRINGS_HAS_OSTREAM_OPERATOR_H_

#include <ostream>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Detects if type `T` supports streaming to `std::ostream`s with `operator<<`.

template <typename T, typename = void>
struct HasOstreamOperator : std::false_type {};

template <typename T>
struct HasOstreamOperator<
    T, std::enable_if_t<std::is_same<
           std::ostream&, decltype(std::declval<std::ostream&>()
                                   << std::declval<const T&>())>::value>>
    : std::true_type {};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_HAS_OSTREAM_OPERATOR_H_
