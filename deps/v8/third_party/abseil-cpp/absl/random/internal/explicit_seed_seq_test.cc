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

#include "absl/random/internal/explicit_seed_seq.h"

#include <iterator>
#include <random>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/random/seed_sequences.h"

namespace {

using ::absl::random_internal::ExplicitSeedSeq;

template <typename Sseq>
bool ConformsToInterface() {
  // Check that the SeedSequence can be default-constructed.
  {
    Sseq default_constructed_seq;
  }
  // Check that the SeedSequence can be constructed with two iterators.
  {
    uint32_t init_array[] = {1, 3, 5, 7, 9};
    Sseq iterator_constructed_seq(init_array, &init_array[5]);
  }
  // Check that the SeedSequence can be std::initializer_list-constructed.
  {
    Sseq list_constructed_seq = {1, 3, 5, 7, 9, 11, 13};
  }
  // Check that param() and size() return state provided to constructor.
  {
    uint32_t init_array[] = {1, 2, 3, 4, 5};
    Sseq seq(init_array, &init_array[ABSL_ARRAYSIZE(init_array)]);
    EXPECT_EQ(seq.size(), ABSL_ARRAYSIZE(init_array));

    uint32_t state_array[ABSL_ARRAYSIZE(init_array)];
    seq.param(state_array);

    for (int i = 0; i < ABSL_ARRAYSIZE(state_array); i++) {
      EXPECT_EQ(state_array[i], i + 1);
    }
  }
  // Check for presence of generate() method.
  {
    Sseq seq;
    uint32_t seeds[5];

    seq.generate(seeds, &seeds[ABSL_ARRAYSIZE(seeds)]);
  }
  return true;
}
}  // namespace

TEST(SeedSequences, CheckInterfaces) {
  // Control case
  EXPECT_TRUE(ConformsToInterface<std::seed_seq>());

  // Abseil classes
  EXPECT_TRUE(ConformsToInterface<ExplicitSeedSeq>());
}

TEST(ExplicitSeedSeq, DefaultConstructorGeneratesZeros) {
  const size_t kNumBlocks = 128;

  uint32_t outputs[kNumBlocks];
  ExplicitSeedSeq seq;
  seq.generate(outputs, &outputs[kNumBlocks]);

  for (uint32_t& seed : outputs) {
    EXPECT_EQ(seed, 0);
  }
}

TEST(ExplicitSeeqSeq, SeedMaterialIsForwardedIdentically) {
  const size_t kNumBlocks = 128;

  uint32_t seed_material[kNumBlocks];
  std::random_device urandom{"/dev/urandom"};
  for (uint32_t& seed : seed_material) {
    seed = urandom();
  }
  ExplicitSeedSeq seq(seed_material, &seed_material[kNumBlocks]);

  // Check that output is same as seed-material provided to constructor.
  {
    const size_t kNumGenerated = kNumBlocks / 2;
    uint32_t outputs[kNumGenerated];
    seq.generate(outputs, &outputs[kNumGenerated]);
    for (size_t i = 0; i < kNumGenerated; i++) {
      EXPECT_EQ(outputs[i], seed_material[i]);
    }
  }
  // Check that SeedSequence is stateless between invocations: Despite the last
  // invocation of generate() only consuming half of the input-entropy, the same
  // entropy will be recycled for the next invocation.
  {
    const size_t kNumGenerated = kNumBlocks;
    uint32_t outputs[kNumGenerated];
    seq.generate(outputs, &outputs[kNumGenerated]);
    for (size_t i = 0; i < kNumGenerated; i++) {
      EXPECT_EQ(outputs[i], seed_material[i]);
    }
  }
  // Check that when more seed-material is asked for than is provided, nonzero
  // values are still written.
  {
    const size_t kNumGenerated = kNumBlocks * 2;
    uint32_t outputs[kNumGenerated];
    seq.generate(outputs, &outputs[kNumGenerated]);
    for (size_t i = 0; i < kNumGenerated; i++) {
      EXPECT_EQ(outputs[i], seed_material[i % kNumBlocks]);
    }
  }
}

TEST(ExplicitSeedSeq, CopyAndMoveConstructors) {
  using testing::Each;
  using testing::Eq;
  using testing::Not;
  using testing::Pointwise;

  uint32_t entropy[4];
  std::random_device urandom("/dev/urandom");
  for (uint32_t& entry : entropy) {
    entry = urandom();
  }
  ExplicitSeedSeq seq_from_entropy(std::begin(entropy), std::end(entropy));
  // Copy constructor.
  {
    ExplicitSeedSeq seq_copy(seq_from_entropy);
    EXPECT_EQ(seq_copy.size(), seq_from_entropy.size());

    std::vector<uint32_t> seeds_1(1000, 0);
    std::vector<uint32_t> seeds_2(1000, 1);

    seq_from_entropy.generate(seeds_1.begin(), seeds_1.end());
    seq_copy.generate(seeds_2.begin(), seeds_2.end());

    EXPECT_THAT(seeds_1, Pointwise(Eq(), seeds_2));
  }
  // Assignment operator.
  {
    for (uint32_t& entry : entropy) {
      entry = urandom();
    }
    ExplicitSeedSeq another_seq(std::begin(entropy), std::end(entropy));

    std::vector<uint32_t> seeds_1(1000, 0);
    std::vector<uint32_t> seeds_2(1000, 0);

    seq_from_entropy.generate(seeds_1.begin(), seeds_1.end());
    another_seq.generate(seeds_2.begin(), seeds_2.end());

    // Assert precondition: Sequences generated by seed-sequences are not equal.
    EXPECT_THAT(seeds_1, Not(Pointwise(Eq(), seeds_2)));

    // Apply the assignment-operator.
    // GCC 12 has a false-positive -Wstringop-overflow warning here.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
    another_seq = seq_from_entropy;
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic pop
#endif

    // Re-generate seeds.
    seq_from_entropy.generate(seeds_1.begin(), seeds_1.end());
    another_seq.generate(seeds_2.begin(), seeds_2.end());

    // Seeds generated by seed-sequences should now be equal.
    EXPECT_THAT(seeds_1, Pointwise(Eq(), seeds_2));
  }
  // Move constructor.
  {
    // Get seeds from seed-sequence constructed from entropy.
    std::vector<uint32_t> seeds_1(1000, 0);
    seq_from_entropy.generate(seeds_1.begin(), seeds_1.end());

    // Apply move-constructor move the sequence to another instance.
    absl::random_internal::ExplicitSeedSeq moved_seq(
        std::move(seq_from_entropy));
    std::vector<uint32_t> seeds_2(1000, 1);
    moved_seq.generate(seeds_2.begin(), seeds_2.end());
    // Verify that seeds produced by moved-instance are the same as original.
    EXPECT_THAT(seeds_1, Pointwise(Eq(), seeds_2));

    // Verify that the moved-from instance now behaves like a
    // default-constructed instance.
    EXPECT_EQ(seq_from_entropy.size(), 0);
    seq_from_entropy.generate(seeds_1.begin(), seeds_1.end());
    EXPECT_THAT(seeds_1, Each(Eq(0)));
  }
}

TEST(ExplicitSeedSeq, StdURBGGoldenTests) {
  // Verify that for std::- URBG instances the results are stable across
  // platforms (these should have deterministic output).
  {
    ExplicitSeedSeq seed_sequence{12, 34, 56};
    std::minstd_rand rng(seed_sequence);

    std::minstd_rand::result_type values[4] = {rng(), rng(), rng(), rng()};
    EXPECT_THAT(values,
                testing::ElementsAre(579252, 43785881, 464353103, 1501811174));
  }

  {
    ExplicitSeedSeq seed_sequence{12, 34, 56};
    std::mt19937 rng(seed_sequence);

    std::mt19937::result_type values[4] = {rng(), rng(), rng(), rng()};
    EXPECT_THAT(values, testing::ElementsAre(138416803, 151130212, 33817739,
                                             138416803));
  }

  {
    ExplicitSeedSeq seed_sequence{12, 34, 56};
    std::mt19937_64 rng(seed_sequence);

    std::mt19937_64::result_type values[4] = {rng(), rng(), rng(), rng()};
    EXPECT_THAT(values,
                testing::ElementsAre(19738651785169348, 1464811352364190456,
                                     18054685302720800, 19738651785169348));
  }
}
