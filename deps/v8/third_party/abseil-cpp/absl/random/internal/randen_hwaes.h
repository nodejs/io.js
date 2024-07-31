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

#ifndef ABSL_RANDOM_INTERNAL_RANDEN_HWAES_H_
#define ABSL_RANDOM_INTERNAL_RANDEN_HWAES_H_

#include "absl/base/config.h"

// HERMETIC NOTE: The randen_hwaes target must not introduce duplicate
// symbols from arbitrary system and other headers, since it may be built
// with different flags from other targets, using different levels of
// optimization, potentially introducing ODR violations.

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// RANDen = RANDom generator or beetroots in Swiss High German.
// 'Strong' (well-distributed, unpredictable, backtracking-resistant) random
// generator, faster in some benchmarks than std::mt19937_64 and pcg64_c32.
//
// RandenHwAes implements the basic state manipulation methods.
class RandenHwAes {
 public:
  static void Generate(const void* keys, void* state_void);
  static void Absorb(const void* seed_void, void* state_void);
  static const void* GetKeys();
};

// HasRandenHwAesImplementation returns true when there is an accelerated
// implementation, and false otherwise.  If there is no implementation,
// then attempting to use it will abort the program.
bool HasRandenHwAesImplementation();

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_RANDEN_HWAES_H_
