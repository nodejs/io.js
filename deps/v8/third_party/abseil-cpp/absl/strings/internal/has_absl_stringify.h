// Copyright 2024 The Abseil Authors
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

#ifndef ABSL_STRINGS_INTERNAL_HAS_ABSL_STRINGIFY_H_
#define ABSL_STRINGS_INTERNAL_HAS_ABSL_STRINGIFY_H_

#include "absl/strings/has_absl_stringify.h"

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace strings_internal {

// This exists to fix a circular dependency problem with the GoogleTest release.
// GoogleTest referenced this internal file and this internal trait.  Since
// simultaneous releases are not possible since once release must reference
// another, we will temporarily add this back.
// https://github.com/google/googletest/blob/v1.14.x/googletest/include/gtest/gtest-printers.h#L119
//
// This file can be deleted after the next Abseil and GoogleTest release.
//
// https://github.com/google/googletest/pull/4368#issuecomment-1717699895
// https://github.com/google/googletest/pull/4368#issuecomment-1717699895
using ::absl::HasAbslStringify;

}  // namespace strings_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_HAS_ABSL_STRINGIFY_H_
