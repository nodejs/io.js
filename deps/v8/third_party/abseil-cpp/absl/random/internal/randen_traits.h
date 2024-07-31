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

#ifndef ABSL_RANDOM_INTERNAL_RANDEN_TRAITS_H_
#define ABSL_RANDOM_INTERNAL_RANDEN_TRAITS_H_

// HERMETIC NOTE: The randen_hwaes target must not introduce duplicate
// symbols from arbitrary system and other headers, since it may be built
// with different flags from other targets, using different levels of
// optimization, potentially introducing ODR violations.

#include <cstddef>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

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
// Combine these three ideas and also change Simpira's subround keys from
// structured/low-entropy counters to digits of Pi (or other random source).

// RandenTraits contains the basic algorithm traits, such as the size of the
// state, seed, sponge, etc.
struct RandenTraits {
  // Size of the entire sponge / state for the randen PRNG.
  static constexpr size_t kStateBytes = 256;  // 2048-bit

  // Size of the 'inner' (inaccessible) part of the sponge. Larger values would
  // require more frequent calls to RandenGenerate.
  static constexpr size_t kCapacityBytes = 16;  // 128-bit

  // Size of the default seed consumed by the sponge.
  static constexpr size_t kSeedBytes = kStateBytes - kCapacityBytes;

  // Assuming 128-bit blocks, the number of blocks in the state.
  // Largest size for which security proofs are known.
  static constexpr size_t kFeistelBlocks = 16;

  // Ensures SPRP security and two full subblock diffusions.
  // Must be > 4 * log2(kFeistelBlocks).
  static constexpr size_t kFeistelRounds = 16 + 1;

  // Size of the key. A 128-bit key block is used for every-other
  // feistel block (Type-2 generalized Feistel network) in each round.
  static constexpr size_t kKeyBytes = 16 * kFeistelRounds * kFeistelBlocks / 2;
};

// Randen key arrays. In randen_round_keys.cc
extern const unsigned char kRandenRoundKeys[RandenTraits::kKeyBytes];
extern const unsigned char kRandenRoundKeysBE[RandenTraits::kKeyBytes];

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_RANDEN_TRAITS_H_
