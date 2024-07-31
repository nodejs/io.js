// Copyright 2021 The Abseil Authors
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

#ifndef ABSL_NUMERIC_INTERNAL_REPRESENTATION_H_
#define ABSL_NUMERIC_INTERNAL_REPRESENTATION_H_

#include <limits>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace numeric_internal {

// Returns true iff long double is represented as a pair of doubles added
// together.
inline constexpr bool IsDoubleDouble() {
  // A double-double value always has exactly twice the precision of a double
  // value--one double carries the high digits and one double carries the low
  // digits. This property is not shared with any other common floating-point
  // representation, so this test won't trigger false positives. For reference,
  // this table gives the number of bits of precision of each common
  // floating-point representation:
  //
  //                type     precision
  //         IEEE single          24 b
  //         IEEE double          53
  //     x86 long double          64
  //       double-double         106
  //      IEEE quadruple         113
  //
  // Note in particular that a quadruple-precision float has greater precision
  // than a double-double float despite taking up the same amount of memory; the
  // quad has more of its bits allocated to the mantissa than the double-double
  // has.
  return std::numeric_limits<long double>::digits ==
         2 * std::numeric_limits<double>::digits;
}

}  // namespace numeric_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_NUMERIC_INTERNAL_REPRESENTATION_H_
