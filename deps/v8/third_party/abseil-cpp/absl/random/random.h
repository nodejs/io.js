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
//
// -----------------------------------------------------------------------------
// File: random.h
// -----------------------------------------------------------------------------
//
// This header defines the recommended Uniform Random Bit Generator (URBG)
// types for use within the Abseil Random library. These types are not
// suitable for security-related use-cases, but should suffice for most other
// uses of generating random values.
//
// The Abseil random library provides the following URBG types:
//
//   * BitGen, a good general-purpose bit generator, optimized for generating
//     random (but not cryptographically secure) values
//   * InsecureBitGen, a slightly faster, though less random, bit generator, for
//     cases where the existing BitGen is a drag on performance.

#ifndef ABSL_RANDOM_RANDOM_H_
#define ABSL_RANDOM_RANDOM_H_

#include <random>

#include "absl/random/distributions.h"  // IWYU pragma: export
#include "absl/random/internal/nonsecure_base.h"  // IWYU pragma: export
#include "absl/random/internal/pcg_engine.h"  // IWYU pragma: export
#include "absl/random/internal/pool_urbg.h"
#include "absl/random/internal/randen_engine.h"
#include "absl/random/seed_sequences.h"  // IWYU pragma: export

namespace absl {
ABSL_NAMESPACE_BEGIN

// -----------------------------------------------------------------------------
// absl::BitGen
// -----------------------------------------------------------------------------
//
// `absl::BitGen` is a general-purpose random bit generator for generating
// random values for use within the Abseil random library. Typically, you use a
// bit generator in combination with a distribution to provide random values.
//
// Example:
//
//   // Create an absl::BitGen. There is no need to seed this bit generator.
//   absl::BitGen gen;
//
//   // Generate an integer value in the closed interval [1,6]
//   int die_roll = absl::uniform_int_distribution<int>(1, 6)(gen);
//
// `absl::BitGen` is seeded by default with non-deterministic data to produce
// different sequences of random values across different instances, including
// different binary invocations. This behavior is different than the standard
// library bit generators, which use golden values as their seeds. Default
// construction intentionally provides no stability guarantees, to avoid
// accidental dependence on such a property.
//
// `absl::BitGen` may be constructed with an optional seed sequence type,
// conforming to [rand.req.seed_seq], which will be mixed with additional
// non-deterministic data as detailed below.
//
// Example:
//
//  // Create an absl::BitGen using an std::seed_seq seed sequence
//  std::seed_seq seq{1,2,3};
//  absl::BitGen gen_with_seed(seq);
//
//  // Generate an integer value in the closed interval [1,6]
//  int die_roll2 = absl::uniform_int_distribution<int>(1, 6)(gen_with_seed);
//
// Constructing two `absl::BitGen`s with the same seed sequence in the same
// process will produce the same sequence of variates, but need not do so across
// multiple processes even if they're executing the same binary.
//
// `absl::BitGen` meets the requirements of the Uniform Random Bit Generator
// (URBG) concept as per the C++17 standard [rand.req.urng] though differs
// slightly with [rand.req.eng]. Like its standard library equivalents (e.g.
// `std::mersenne_twister_engine`) `absl::BitGen` is not cryptographically
// secure.
//
// This type has been optimized to perform better than Mersenne Twister
// (https://en.wikipedia.org/wiki/Mersenne_Twister) and many other complex URBG
// types on modern x86, ARM, and PPC architectures.
//
// This type is thread-compatible, but not thread-safe.

// ---------------------------------------------------------------------------
// absl::BitGen member functions
// ---------------------------------------------------------------------------

// absl::BitGen::operator()()
//
// Calls the BitGen, returning a generated value.

// absl::BitGen::min()
//
// Returns the smallest possible value from this bit generator.

// absl::BitGen::max()
//
// Returns the largest possible value from this bit generator.

// absl::BitGen::discard(num)
//
// Advances the internal state of this bit generator by `num` times, and
// discards the intermediate results.
// ---------------------------------------------------------------------------

using BitGen = random_internal::NonsecureURBGBase<
    random_internal::randen_engine<uint64_t>>;

// -----------------------------------------------------------------------------
// absl::InsecureBitGen
// -----------------------------------------------------------------------------
//
// `absl::InsecureBitGen` is an efficient random bit generator for generating
// random values, recommended only for performance-sensitive use cases where
// `absl::BitGen` is not satisfactory when compute-bounded by bit generation
// costs.
//
// Example:
//
//   // Create an absl::InsecureBitGen
//   absl::InsecureBitGen gen;
//   for (size_t i = 0; i < 1000000; i++) {
//
//     // Generate a bunch of random values from some complex distribution
//     auto my_rnd = some_distribution(gen, 1, 1000);
//   }
//
// Like `absl::BitGen`, `absl::InsecureBitGen` is seeded by default with
// non-deterministic data to produce different sequences of random values across
// different instances, including different binary invocations. (This behavior
// is different than the standard library bit generators, which use golden
// values as their seeds.)
//
// `absl::InsecureBitGen` may be constructed with an optional seed sequence
// type, conforming to [rand.req.seed_seq], which will be mixed with additional
// non-deterministic data, as detailed in the `absl::BitGen` comment.
//
// `absl::InsecureBitGen` meets the requirements of the Uniform Random Bit
// Generator (URBG) concept as per the C++17 standard [rand.req.urng] though
// its implementation differs slightly with [rand.req.eng]. Like its standard
// library equivalents (e.g. `std::mersenne_twister_engine`)
// `absl::InsecureBitGen` is not cryptographically secure.
//
// Prefer `absl::BitGen` over `absl::InsecureBitGen` as the general type is
// often fast enough for the vast majority of applications.

using InsecureBitGen =
    random_internal::NonsecureURBGBase<random_internal::pcg64_2018_engine>;

// ---------------------------------------------------------------------------
// absl::InsecureBitGen member functions
// ---------------------------------------------------------------------------

// absl::InsecureBitGen::operator()()
//
// Calls the InsecureBitGen, returning a generated value.

// absl::InsecureBitGen::min()
//
// Returns the smallest possible value from this bit generator.

// absl::InsecureBitGen::max()
//
// Returns the largest possible value from this bit generator.

// absl::InsecureBitGen::discard(num)
//
// Advances the internal state of this bit generator by `num` times, and
// discards the intermediate results.
// ---------------------------------------------------------------------------

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_RANDOM_H_
