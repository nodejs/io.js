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

#include "absl/random/discrete_distribution.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <utility>
#include <vector>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// Initializes the distribution table for Walker's Aliasing algorithm, described
// in Knuth, Vol 2. as well as in https://en.wikipedia.org/wiki/Alias_method
std::vector<std::pair<double, size_t>> InitDiscreteDistribution(
    std::vector<double>* probabilities) {
  // The empty-case should already be handled by the constructor.
  assert(probabilities);
  assert(!probabilities->empty());

  // Step 1. Normalize the input probabilities to 1.0.
  double sum = std::accumulate(std::begin(*probabilities),
                               std::end(*probabilities), 0.0);
  if (std::fabs(sum - 1.0) > 1e-6) {
    // Scale `probabilities` only when the sum is too far from 1.0.  Scaling
    // unconditionally will alter the probabilities slightly.
    for (double& item : *probabilities) {
      item = item / sum;
    }
  }

  // Step 2. At this point `probabilities` is set to the conditional
  // probabilities of each element which sum to 1.0, to within reasonable error.
  // These values are used to construct the proportional probability tables for
  // the selection phases of Walker's Aliasing algorithm.
  //
  // To construct the table, pick an element which is under-full (i.e., an
  // element for which `(*probabilities)[i] < 1.0/n`), and pair it with an
  // element which is over-full (i.e., an element for which
  // `(*probabilities)[i] > 1.0/n`). The smaller value can always be retired.
  // The larger may still be greater than 1.0/n, or may now be less than 1.0/n,
  // and put back onto the appropriate collection.
  const size_t n = probabilities->size();
  std::vector<std::pair<double, size_t>> q;
  q.reserve(n);

  std::vector<size_t> over;
  std::vector<size_t> under;
  size_t idx = 0;
  for (const double item : *probabilities) {
    assert(item >= 0);
    const double v = item * n;
    q.emplace_back(v, 0);
    if (v < 1.0) {
      under.push_back(idx++);
    } else {
      over.push_back(idx++);
    }
  }
  while (!over.empty() && !under.empty()) {
    auto lo = under.back();
    under.pop_back();
    auto hi = over.back();
    over.pop_back();

    q[lo].second = hi;
    const double r = q[hi].first - (1.0 - q[lo].first);
    q[hi].first = r;
    if (r < 1.0) {
      under.push_back(hi);
    } else {
      over.push_back(hi);
    }
  }

  // Due to rounding errors, there may be un-paired elements in either
  // collection; these should all be values near 1.0.  For these values, set `q`
  // to 1.0 and set the alternate to the identity.
  for (auto i : over) {
    q[i] = {1.0, i};
  }
  for (auto i : under) {
    q[i] = {1.0, i};
  }
  return q;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
