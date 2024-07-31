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

#include "absl/random/internal/randen.h"

#include "absl/base/internal/raw_logging.h"
#include "absl/random/internal/randen_detect.h"

// RANDen = RANDom generator or beetroots in Swiss High German.
// 'Strong' (well-distributed, unpredictable, backtracking-resistant) random
// generator, faster in some benchmarks than std::mt19937_64 and pcg64_c32.
//
// High-level summary:
// 1) Reverie (see "A Robust and Sponge-Like PRNG with Improved Efficiency") is
//    a sponge-like random generator that requires a cryptographic permutation.
//    It improves upon "Provably Robust Sponge-Based PRNGs and KDFs" by
//    achieving backtracking resistance with only one Permute() per buffer.
//
// 2) "Simpira v2: A Family of Efficient Permutations Using the AES Round
//    Function" constructs up to 1024-bit permutations using an improved
//    Generalized Feistel network with 2-round AES-128 functions. This Feistel
//    block shuffle achieves diffusion faster and is less vulnerable to
//    sliced-biclique attacks than the Type-2 cyclic shuffle.
//
// 3) "Improving the Generalized Feistel" and "New criterion for diffusion
//    property" extends the same kind of improved Feistel block shuffle to 16
//    branches, which enables a 2048-bit permutation.
//
// We combine these three ideas and also change Simpira's subround keys from
// structured/low-entropy counters to digits of Pi.

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

struct RandenState {
  const void* keys;
  bool has_crypto;
};

RandenState GetRandenState() {
  static const RandenState state = []() {
    RandenState tmp;
#if ABSL_RANDOM_INTERNAL_AES_DISPATCH
    // HW AES Dispatch.
    if (HasRandenHwAesImplementation() && CPUSupportsRandenHwAes()) {
      tmp.has_crypto = true;
      tmp.keys = RandenHwAes::GetKeys();
    } else {
      tmp.has_crypto = false;
      tmp.keys = RandenSlow::GetKeys();
    }
#elif ABSL_HAVE_ACCELERATED_AES
    // HW AES is enabled.
    tmp.has_crypto = true;
    tmp.keys = RandenHwAes::GetKeys();
#else
    // HW AES is disabled.
    tmp.has_crypto = false;
    tmp.keys = RandenSlow::GetKeys();
#endif
    return tmp;
  }();
  return state;
}

}  // namespace

Randen::Randen() {
  auto tmp = GetRandenState();
  keys_ = tmp.keys;
#if ABSL_RANDOM_INTERNAL_AES_DISPATCH
  has_crypto_ = tmp.has_crypto;
#endif
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
