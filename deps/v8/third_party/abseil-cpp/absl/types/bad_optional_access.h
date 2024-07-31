// Copyright 2018 The Abseil Authors.
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
// bad_optional_access.h
// -----------------------------------------------------------------------------
//
// This header file defines the `absl::bad_optional_access` type.

#ifndef ABSL_TYPES_BAD_OPTIONAL_ACCESS_H_
#define ABSL_TYPES_BAD_OPTIONAL_ACCESS_H_

#include <stdexcept>

#include "absl/base/config.h"

#ifdef ABSL_USES_STD_OPTIONAL

#include <optional>

namespace absl {
ABSL_NAMESPACE_BEGIN
using std::bad_optional_access;
ABSL_NAMESPACE_END
}  // namespace absl

#else  // ABSL_USES_STD_OPTIONAL

namespace absl {
ABSL_NAMESPACE_BEGIN

// -----------------------------------------------------------------------------
// bad_optional_access
// -----------------------------------------------------------------------------
//
// An `absl::bad_optional_access` type is an exception type that is thrown when
// attempting to access an `absl::optional` object that does not contain a
// value.
//
// Example:
//
//   absl::optional<int> o;
//
//   try {
//     int n = o.value();
//   } catch(const absl::bad_optional_access& e) {
//     std::cout << "Bad optional access: " << e.what() << '\n';
//   }
class bad_optional_access : public std::exception {
 public:
  bad_optional_access() = default;
  ~bad_optional_access() override;
  const char* what() const noexcept override;
};

namespace optional_internal {

// throw delegator
[[noreturn]] ABSL_DLL void throw_bad_optional_access();

}  // namespace optional_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_USES_STD_OPTIONAL

#endif  // ABSL_TYPES_BAD_OPTIONAL_ACCESS_H_
