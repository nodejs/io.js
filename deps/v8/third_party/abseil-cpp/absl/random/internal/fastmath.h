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

#ifndef ABSL_RANDOM_INTERNAL_FASTMATH_H_
#define ABSL_RANDOM_INTERNAL_FASTMATH_H_

// This file contains fast math functions (bitwise ops as well as some others)
// which are implementation details of various absl random number distributions.

#include <cassert>
#include <cmath>
#include <cstdint>

#include "absl/numeric/bits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// Compute log2(n) using integer operations.
// While std::log2 is more accurate than std::log(n) / std::log(2), for
// very large numbers--those close to std::numeric_limits<uint64_t>::max() - 2,
// for instance--std::log2 rounds up rather than down, which introduces
// definite skew in the results.
inline int IntLog2Floor(uint64_t n) {
  return (n <= 1) ? 0 : (63 - countl_zero(n));
}
inline int IntLog2Ceil(uint64_t n) {
  return (n <= 1) ? 0 : (64 - countl_zero(n - 1));
}

inline double StirlingLogFactorial(double n) {
  assert(n >= 1);
  // Using Stirling's approximation.
  constexpr double kLog2PI = 1.83787706640934548356;
  const double logn = std::log(n);
  const double ninv = 1.0 / static_cast<double>(n);
  return n * logn - n + 0.5 * (kLog2PI + logn) + (1.0 / 12.0) * ninv -
         (1.0 / 360.0) * ninv * ninv * ninv;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_FASTMATH_H_
