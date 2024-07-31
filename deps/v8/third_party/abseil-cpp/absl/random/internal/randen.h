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

#ifndef ABSL_RANDOM_INTERNAL_RANDEN_H_
#define ABSL_RANDOM_INTERNAL_RANDEN_H_

#include <cstddef>

#include "absl/random/internal/platform.h"
#include "absl/random/internal/randen_hwaes.h"
#include "absl/random/internal/randen_slow.h"
#include "absl/random/internal/randen_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// RANDen = RANDom generator or beetroots in Swiss High German.
// 'Strong' (well-distributed, unpredictable, backtracking-resistant) random
// generator, faster in some benchmarks than std::mt19937_64 and pcg64_c32.
//
// Randen implements the basic state manipulation methods.
class Randen {
 public:
  static constexpr size_t kStateBytes = RandenTraits::kStateBytes;
  static constexpr size_t kCapacityBytes = RandenTraits::kCapacityBytes;
  static constexpr size_t kSeedBytes = RandenTraits::kSeedBytes;

  ~Randen() = default;

  Randen();

  // Generate updates the randen sponge. The outer portion of the sponge
  // (kCapacityBytes .. kStateBytes) may be consumed as PRNG state.
  // REQUIRES: state points to kStateBytes of state.
  inline void Generate(void* state) const {
#if ABSL_RANDOM_INTERNAL_AES_DISPATCH
    // HW AES Dispatch.
    if (has_crypto_) {
      RandenHwAes::Generate(keys_, state);
    } else {
      RandenSlow::Generate(keys_, state);
    }
#elif ABSL_HAVE_ACCELERATED_AES
    // HW AES is enabled.
    RandenHwAes::Generate(keys_, state);
#else
    // HW AES is disabled.
    RandenSlow::Generate(keys_, state);
#endif
  }

  // Absorb incorporates additional seed material into the randen sponge.  After
  // absorb returns, Generate must be called before the state may be consumed.
  // REQUIRES: seed points to kSeedBytes of seed.
  // REQUIRES: state points to kStateBytes of state.
  inline void Absorb(const void* seed, void* state) const {
#if ABSL_RANDOM_INTERNAL_AES_DISPATCH
    // HW AES Dispatch.
    if (has_crypto_) {
      RandenHwAes::Absorb(seed, state);
    } else {
      RandenSlow::Absorb(seed, state);
    }
#elif ABSL_HAVE_ACCELERATED_AES
    // HW AES is enabled.
    RandenHwAes::Absorb(seed, state);
#else
    // HW AES is disabled.
    RandenSlow::Absorb(seed, state);
#endif
  }

 private:
  const void* keys_;
#if ABSL_RANDOM_INTERNAL_AES_DISPATCH
  bool has_crypto_;
#endif
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_RANDEN_H_
