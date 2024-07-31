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

#include "absl/random/seed_sequences.h"

#include <iterator>
#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/random/internal/nonsecure_base.h"
#include "absl/random/random.h"
namespace {

TEST(SeedSequences, Examples) {
  {
    absl::SeedSeq seed_seq({1, 2, 3});
    absl::BitGen bitgen(seed_seq);

    EXPECT_NE(0, bitgen());
  }
  {
    absl::BitGen engine;
    auto seed_seq = absl::CreateSeedSeqFrom(&engine);
    absl::BitGen bitgen(seed_seq);

    EXPECT_NE(engine(), bitgen());
  }
  {
    auto seed_seq = absl::MakeSeedSeq();
    std::mt19937 random(seed_seq);

    EXPECT_NE(0, random());
  }
}

TEST(CreateSeedSeqFrom, CompatibleWithStdTypes) {
  using ExampleNonsecureURBG =
      absl::random_internal::NonsecureURBGBase<std::minstd_rand0>;

  // Construct a URBG instance.
  ExampleNonsecureURBG rng;

  // Construct a Seed Sequence from its variates.
  auto seq_from_rng = absl::CreateSeedSeqFrom(&rng);

  // Ensure that another URBG can be validly constructed from the Seed Sequence.
  std::mt19937_64{seq_from_rng};
}

TEST(CreateSeedSeqFrom, CompatibleWithBitGenerator) {
  // Construct a URBG instance.
  absl::BitGen rng;

  // Construct a Seed Sequence from its variates.
  auto seq_from_rng = absl::CreateSeedSeqFrom(&rng);

  // Ensure that another URBG can be validly constructed from the Seed Sequence.
  std::mt19937_64{seq_from_rng};
}

TEST(CreateSeedSeqFrom, CompatibleWithInsecureBitGen) {
  // Construct a URBG instance.
  absl::InsecureBitGen rng;

  // Construct a Seed Sequence from its variates.
  auto seq_from_rng = absl::CreateSeedSeqFrom(&rng);

  // Ensure that another URBG can be validly constructed from the Seed Sequence.
  std::mt19937_64{seq_from_rng};
}

TEST(CreateSeedSeqFrom, CompatibleWithRawURBG) {
  // Construct a URBG instance.
  std::random_device urandom;

  // Construct a Seed Sequence from its variates, using 64b of seed-material.
  auto seq_from_rng = absl::CreateSeedSeqFrom(&urandom);

  // Ensure that another URBG can be validly constructed from the Seed Sequence.
  std::mt19937_64{seq_from_rng};
}

template <typename URBG>
void TestReproducibleVariateSequencesForNonsecureURBG() {
  const size_t kNumVariates = 1000;

  URBG rng;
  // Reused for both RNG instances.
  auto reusable_seed = absl::CreateSeedSeqFrom(&rng);

  typename URBG::result_type variates[kNumVariates];
  {
    URBG child(reusable_seed);
    for (auto& variate : variates) {
      variate = child();
    }
  }
  // Ensure that variate-sequence can be "replayed" by identical RNG.
  {
    URBG child(reusable_seed);
    for (auto& variate : variates) {
      ASSERT_EQ(variate, child());
    }
  }
}

TEST(CreateSeedSeqFrom, ReproducesVariateSequencesForInsecureBitGen) {
  TestReproducibleVariateSequencesForNonsecureURBG<absl::InsecureBitGen>();
}

TEST(CreateSeedSeqFrom, ReproducesVariateSequencesForBitGenerator) {
  TestReproducibleVariateSequencesForNonsecureURBG<absl::BitGen>();
}
}  // namespace
