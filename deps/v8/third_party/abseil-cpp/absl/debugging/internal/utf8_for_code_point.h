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

#ifndef ABSL_DEBUGGING_INTERNAL_UTF8_FOR_CODE_POINT_H_
#define ABSL_DEBUGGING_INTERNAL_UTF8_FOR_CODE_POINT_H_

#include <cstdint>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

struct Utf8ForCodePoint {
  // Converts a Unicode code point to the corresponding UTF-8 byte sequence.
  // Async-signal-safe to support use in symbolizing stack traces from a signal
  // handler.
  explicit Utf8ForCodePoint(uint64_t code_point);

  // Returns true if the constructor's code_point argument was valid.
  bool ok() const { return length != 0; }

  // If code_point was in range, then 1 <= length <= 4, and the UTF-8 encoding
  // is found in bytes[0 .. (length - 1)].  If code_point was invalid, then
  // length == 0.  In either case, the contents of bytes[length .. 3] are
  // unspecified.
  char bytes[4] = {};
  uint32_t length = 0;
};

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_DEBUGGING_INTERNAL_UTF8_FOR_CODE_POINT_H_
