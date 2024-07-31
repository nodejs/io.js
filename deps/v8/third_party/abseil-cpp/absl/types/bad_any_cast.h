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
// bad_any_cast.h
// -----------------------------------------------------------------------------
//
// This header file defines the `absl::bad_any_cast` type.

#ifndef ABSL_TYPES_BAD_ANY_CAST_H_
#define ABSL_TYPES_BAD_ANY_CAST_H_

#include <typeinfo>

#include "absl/base/config.h"

#ifdef ABSL_USES_STD_ANY

#include <any>

namespace absl {
ABSL_NAMESPACE_BEGIN
using std::bad_any_cast;
ABSL_NAMESPACE_END
}  // namespace absl

#else  // ABSL_USES_STD_ANY

namespace absl {
ABSL_NAMESPACE_BEGIN

// -----------------------------------------------------------------------------
// bad_any_cast
// -----------------------------------------------------------------------------
//
// An `absl::bad_any_cast` type is an exception type that is thrown when
// failing to successfully cast the return value of an `absl::any` object.
//
// Example:
//
//   auto a = absl::any(65);
//   absl::any_cast<int>(a);         // 65
//   try {
//     absl::any_cast<char>(a);
//   } catch(const absl::bad_any_cast& e) {
//     std::cout << "Bad any cast: " << e.what() << '\n';
//   }
class bad_any_cast : public std::bad_cast {
 public:
  ~bad_any_cast() override;
  const char* what() const noexcept override;
};

namespace any_internal {

[[noreturn]] void ThrowBadAnyCast();

}  // namespace any_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_USES_STD_ANY

#endif  // ABSL_TYPES_BAD_ANY_CAST_H_
