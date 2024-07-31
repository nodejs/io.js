//
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
// This test helper library contains a table of powers of 10, to guarantee
// precise values are computed across the full range of doubles. We can't rely
// on the pow() function, because not all standard libraries ship a version
// that is precise.
#ifndef ABSL_STRINGS_INTERNAL_POW10_HELPER_H_
#define ABSL_STRINGS_INTERNAL_POW10_HELPER_H_

#include <vector>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

// Computes the precise value of 10^exp. (I.e. the nearest representable
// double to the exact value, rounding to nearest-even in the (single) case of
// being exactly halfway between.)
double Pow10(int exp);

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_POW10_HELPER_H_
