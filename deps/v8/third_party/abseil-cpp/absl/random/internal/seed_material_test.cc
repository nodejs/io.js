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

#include "absl/random/internal/seed_material.h"

#include <bitset>
#include <cstdlib>
#include <cstring>
#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#ifdef __ANDROID__
// Android assert messages only go to system log, so death tests cannot inspect
// the message for matching.
#define ABSL_EXPECT_DEATH_IF_SUPPORTED(statement, regex) \
  EXPECT_DEATH_IF_SUPPORTED(statement, ".*")
#else
#define ABSL_EXPECT_DEATH_IF_SUPPORTED(statement, regex) \
  EXPECT_DEATH_IF_SUPPORTED(statement, regex)
#endif

namespace {

using testing::Each;
using testing::ElementsAre;
using testing::Eq;
using testing::Ne;
using testing::Pointwise;

TEST(SeedBitsToBlocks, VerifyCases) {
  EXPECT_EQ(0, absl::random_internal::SeedBitsToBlocks(0));
  EXPECT_EQ(1, absl::random_internal::SeedBitsToBlocks(1));
  EXPECT_EQ(1, absl::random_internal::SeedBitsToBlocks(31));
  EXPECT_EQ(1, absl::random_internal::SeedBitsToBlocks(32));
  EXPECT_EQ(2, absl::random_internal::SeedBitsToBlocks(33));
  EXPECT_EQ(4, absl::random_internal::SeedBitsToBlocks(127));
  EXPECT_EQ(4, absl::random_internal::SeedBitsToBlocks(128));
  EXPECT_EQ(5, absl::random_internal::SeedBitsToBlocks(129));
}

TEST(ReadSeedMaterialFromOSEntropy, SuccessiveReadsAreDistinct) {
  constexpr size_t kSeedMaterialSize = 64;
  uint32_t seed_material_1[kSeedMaterialSize] = {};
  uint32_t seed_material_2[kSeedMaterialSize] = {};

  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::Span<uint32_t>(seed_material_1, kSeedMaterialSize)));
  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::Span<uint32_t>(seed_material_2, kSeedMaterialSize)));

  EXPECT_THAT(seed_material_1, Pointwise(Ne(), seed_material_2));
}

TEST(ReadSeedMaterialFromOSEntropy, ReadZeroBytesIsNoOp) {
  uint32_t seed_material[32] = {};
  std::memset(seed_material, 0xAA, sizeof(seed_material));
  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::Span<uint32_t>(seed_material, 0)));

  EXPECT_THAT(seed_material, Each(Eq(0xAAAAAAAA)));
}

TEST(ReadSeedMaterialFromOSEntropy, NullPtrVectorArgument) {
#ifdef NDEBUG
  EXPECT_FALSE(absl::random_internal::ReadSeedMaterialFromOSEntropy(
      absl::Span<uint32_t>(nullptr, 32)));
#else
  bool result;
  ABSL_EXPECT_DEATH_IF_SUPPORTED(
      result = absl::random_internal::ReadSeedMaterialFromOSEntropy(
          absl::Span<uint32_t>(nullptr, 32)),
      "!= nullptr");
  (void)result;  // suppress unused-variable warning
#endif
}

TEST(ReadSeedMaterialFromURBG, SeedMaterialEqualsVariateSequence) {
  // Two default-constructed instances of std::mt19937_64 are guaranteed to
  // produce equal variate-sequences.
  std::mt19937 urbg_1;
  std::mt19937 urbg_2;
  constexpr size_t kSeedMaterialSize = 1024;
  uint32_t seed_material[kSeedMaterialSize] = {};

  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromURBG(
      &urbg_1, absl::Span<uint32_t>(seed_material, kSeedMaterialSize)));
  for (uint32_t seed : seed_material) {
    EXPECT_EQ(seed, urbg_2());
  }
}

TEST(ReadSeedMaterialFromURBG, ReadZeroBytesIsNoOp) {
  std::mt19937_64 urbg;
  uint32_t seed_material[32];
  std::memset(seed_material, 0xAA, sizeof(seed_material));
  EXPECT_TRUE(absl::random_internal::ReadSeedMaterialFromURBG(
      &urbg, absl::Span<uint32_t>(seed_material, 0)));

  EXPECT_THAT(seed_material, Each(Eq(0xAAAAAAAA)));
}

TEST(ReadSeedMaterialFromURBG, NullUrbgArgument) {
  constexpr size_t kSeedMaterialSize = 32;
  uint32_t seed_material[kSeedMaterialSize];
#ifdef NDEBUG
  EXPECT_FALSE(absl::random_internal::ReadSeedMaterialFromURBG<std::mt19937_64>(
      nullptr, absl::Span<uint32_t>(seed_material, kSeedMaterialSize)));
#else
  bool result;
  ABSL_EXPECT_DEATH_IF_SUPPORTED(
      result = absl::random_internal::ReadSeedMaterialFromURBG<std::mt19937_64>(
          nullptr, absl::Span<uint32_t>(seed_material, kSeedMaterialSize)),
      "!= nullptr");
  (void)result;  // suppress unused-variable warning
#endif
}

TEST(ReadSeedMaterialFromURBG, NullPtrVectorArgument) {
  std::mt19937_64 urbg;
#ifdef NDEBUG
  EXPECT_FALSE(absl::random_internal::ReadSeedMaterialFromURBG(
      &urbg, absl::Span<uint32_t>(nullptr, 32)));
#else
  bool result;
  ABSL_EXPECT_DEATH_IF_SUPPORTED(
      result = absl::random_internal::ReadSeedMaterialFromURBG(
          &urbg, absl::Span<uint32_t>(nullptr, 32)),
      "!= nullptr");
  (void)result;  // suppress unused-variable warning
#endif
}

// The avalanche effect is a desirable cryptographic property of hashes in which
// changing a single bit in the input causes each bit of the output to be
// changed with probability near 50%.
//
// https://en.wikipedia.org/wiki/Avalanche_effect

TEST(MixSequenceIntoSeedMaterial, AvalancheEffectTestOneBitLong) {
  std::vector<uint32_t> seed_material = {1, 2, 3, 4, 5, 6, 7, 8};

  // For every 32-bit number with exactly one bit set, verify the avalanche
  // effect holds.  In order to reduce flakiness of tests, accept values
  // anywhere in the range of 30%-70%.
  for (uint32_t v = 1; v != 0; v <<= 1) {
    std::vector<uint32_t> seed_material_copy = seed_material;
    absl::random_internal::MixIntoSeedMaterial(
        absl::Span<uint32_t>(&v, 1),
        absl::Span<uint32_t>(seed_material_copy.data(),
                             seed_material_copy.size()));

    uint32_t changed_bits = 0;
    for (size_t i = 0; i < seed_material.size(); i++) {
      std::bitset<sizeof(uint32_t) * 8> bitset(seed_material[i] ^
                                               seed_material_copy[i]);
      changed_bits += bitset.count();
    }

    EXPECT_LE(changed_bits, 0.7 * sizeof(uint32_t) * 8 * seed_material.size());
    EXPECT_GE(changed_bits, 0.3 * sizeof(uint32_t) * 8 * seed_material.size());
  }
}

TEST(MixSequenceIntoSeedMaterial, AvalancheEffectTestOneBitShort) {
  std::vector<uint32_t> seed_material = {1};

  // For every 32-bit number with exactly one bit set, verify the avalanche
  // effect holds.  In order to reduce flakiness of tests, accept values
  // anywhere in the range of 30%-70%.
  for (uint32_t v = 1; v != 0; v <<= 1) {
    std::vector<uint32_t> seed_material_copy = seed_material;
    absl::random_internal::MixIntoSeedMaterial(
        absl::Span<uint32_t>(&v, 1),
        absl::Span<uint32_t>(seed_material_copy.data(),
                             seed_material_copy.size()));

    uint32_t changed_bits = 0;
    for (size_t i = 0; i < seed_material.size(); i++) {
      std::bitset<sizeof(uint32_t) * 8> bitset(seed_material[i] ^
                                               seed_material_copy[i]);
      changed_bits += bitset.count();
    }

    EXPECT_LE(changed_bits, 0.7 * sizeof(uint32_t) * 8 * seed_material.size());
    EXPECT_GE(changed_bits, 0.3 * sizeof(uint32_t) * 8 * seed_material.size());
  }
}

}  // namespace
