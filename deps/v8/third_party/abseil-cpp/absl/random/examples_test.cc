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

#include <cinttypes>
#include <random>
#include <sstream>
#include <vector>

#include "gtest/gtest.h"
#include "absl/random/random.h"

template <typename T>
void Use(T) {}

TEST(Examples, Basic) {
  absl::BitGen gen;
  std::vector<int> objs = {10, 20, 30, 40, 50};

  // Choose an element from a set.
  auto elem = objs[absl::Uniform(gen, 0u, objs.size())];
  Use(elem);

  // Generate a uniform value between 1 and 6.
  auto dice_roll = absl::Uniform<int>(absl::IntervalClosedClosed, gen, 1, 6);
  Use(dice_roll);

  // Generate a random byte.
  auto byte = absl::Uniform<uint8_t>(gen);
  Use(byte);

  // Generate a fractional value from [0f, 1f).
  auto fraction = absl::Uniform<float>(gen, 0, 1);
  Use(fraction);

  // Toss a fair coin; 50/50 probability.
  bool coin_toss = absl::Bernoulli(gen, 0.5);
  Use(coin_toss);

  // Select a file size between 1k and 10MB, biased towards smaller file sizes.
  auto file_size = absl::LogUniform<size_t>(gen, 1000, 10 * 1000 * 1000);
  Use(file_size);

  // Randomize (shuffle) a collection.
  std::shuffle(std::begin(objs), std::end(objs), gen);
}

TEST(Examples, CreateingCorrelatedVariateSequences) {
  // Unexpected PRNG correlation is often a source of bugs,
  // so when using absl::BitGen it must be an intentional choice.
  // NOTE: All of these only exhibit process-level stability.

  // Create a correlated sequence from system entropy.
  {
    auto my_seed = absl::MakeSeedSeq();

    absl::BitGen gen_1(my_seed);
    absl::BitGen gen_2(my_seed);  // Produces same variates as gen_1.

    EXPECT_EQ(absl::Bernoulli(gen_1, 0.5), absl::Bernoulli(gen_2, 0.5));
    EXPECT_EQ(absl::Uniform<uint32_t>(gen_1), absl::Uniform<uint32_t>(gen_2));
  }

  // Create a correlated sequence from an existing URBG.
  {
    absl::BitGen gen;

    auto my_seed = absl::CreateSeedSeqFrom(&gen);
    absl::BitGen gen_1(my_seed);
    absl::BitGen gen_2(my_seed);

    EXPECT_EQ(absl::Bernoulli(gen_1, 0.5), absl::Bernoulli(gen_2, 0.5));
    EXPECT_EQ(absl::Uniform<uint32_t>(gen_1), absl::Uniform<uint32_t>(gen_2));
  }

  // An alternate construction which uses user-supplied data
  // instead of a random seed.
  {
    const char kData[] = "A simple seed string";
    std::seed_seq my_seed(std::begin(kData), std::end(kData));

    absl::BitGen gen_1(my_seed);
    absl::BitGen gen_2(my_seed);

    EXPECT_EQ(absl::Bernoulli(gen_1, 0.5), absl::Bernoulli(gen_2, 0.5));
    EXPECT_EQ(absl::Uniform<uint32_t>(gen_1), absl::Uniform<uint32_t>(gen_2));
  }
}
