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

#include "absl/random/internal/nonsecure_base.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>

#include "gtest/gtest.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"

namespace {

using ExampleNonsecureURBG =
    absl::random_internal::NonsecureURBGBase<std::mt19937>;

template <typename T>
void Use(const T&) {}

}  // namespace

TEST(NonsecureURBGBase, DefaultConstructorIsValid) {
  ExampleNonsecureURBG urbg;
}

// Ensure that the recommended template-instantiations are valid.
TEST(RecommendedTemplates, CanBeConstructed) {
  absl::BitGen default_generator;
  absl::InsecureBitGen insecure_generator;
}

TEST(RecommendedTemplates, CanDiscardValues) {
  absl::BitGen default_generator;
  absl::InsecureBitGen insecure_generator;

  default_generator.discard(5);
  insecure_generator.discard(5);
}

TEST(NonsecureURBGBase, StandardInterface) {
  // Names after definition of [rand.req.urbg] in C++ standard.
  // e us a value of E
  // v is a lvalue of E
  // x, y are possibly const values of E
  // s is a value of T
  // q is a value satisfying requirements of seed_sequence
  // z is a value of type unsigned long long
  // os is a some specialization of basic_ostream
  // is is a some specialization of basic_istream

  using E = absl::random_internal::NonsecureURBGBase<std::minstd_rand>;

  using T = typename E::result_type;

  static_assert(!std::is_copy_constructible<E>::value,
                "NonsecureURBGBase should not be copy constructible");

  static_assert(!absl::is_copy_assignable<E>::value,
                "NonsecureURBGBase should not be copy assignable");

  static_assert(std::is_move_constructible<E>::value,
                "NonsecureURBGBase should be move constructible");

  static_assert(absl::is_move_assignable<E>::value,
                "NonsecureURBGBase should be move assignable");

  static_assert(std::is_same<decltype(std::declval<E>()()), T>::value,
                "return type of operator() must be result_type");

  {
    const E x, y;
    Use(x);
    Use(y);

    static_assert(std::is_same<decltype(x == y), bool>::value,
                  "return type of operator== must be bool");

    static_assert(std::is_same<decltype(x != y), bool>::value,
                  "return type of operator== must be bool");
  }

  E e;
  std::seed_seq q{1, 2, 3};

  E{};
  E{q};

  // Copy constructor not supported.
  // E{x};

  // result_type seed constructor not supported.
  // E{T{1}};

  // Move constructors are supported.
  {
    E tmp(q);
    E m = std::move(tmp);
    E n(std::move(m));
    EXPECT_TRUE(e != n);
  }

  // Comparisons work.
  {
    // MSVC emits error 2718 when using EXPECT_EQ(e, x)
    //  * actual parameter with __declspec(align('#')) won't be aligned
    E a(q);
    E b(q);

    EXPECT_TRUE(a != e);
    EXPECT_TRUE(a == b);

    a();
    EXPECT_TRUE(a != b);
  }

  // e.seed(s) not supported.

  // [rand.req.eng] specifies the parameter as 'unsigned long long'
  // e.discard(unsigned long long) is supported.
  unsigned long long z = 1;  // NOLINT(runtime/int)
  e.discard(z);
}

TEST(NonsecureURBGBase, SeedSeqConstructorIsValid) {
  std::seed_seq seq;
  ExampleNonsecureURBG rbg(seq);
}

TEST(NonsecureURBGBase, CompatibleWithDistributionUtils) {
  ExampleNonsecureURBG rbg;

  absl::Uniform(rbg, 0, 100);
  absl::Uniform(rbg, 0.5, 0.7);
  absl::Poisson<uint32_t>(rbg);
  absl::Exponential<float>(rbg);
}

TEST(NonsecureURBGBase, CompatibleWithStdDistributions) {
  ExampleNonsecureURBG rbg;

  // Cast to void to suppress [[nodiscard]] warnings
  static_cast<void>(std::uniform_int_distribution<uint32_t>(0, 100)(rbg));
  static_cast<void>(std::uniform_real_distribution<float>()(rbg));
  static_cast<void>(std::bernoulli_distribution(0.2)(rbg));
}

TEST(NonsecureURBGBase, ConsecutiveDefaultInstancesYieldUniqueVariates) {
  const size_t kNumSamples = 128;

  ExampleNonsecureURBG rbg1;
  ExampleNonsecureURBG rbg2;

  for (size_t i = 0; i < kNumSamples; i++) {
    EXPECT_NE(rbg1(), rbg2());
  }
}

TEST(NonsecureURBGBase, EqualSeedSequencesYieldEqualVariates) {
  std::seed_seq seq;

  ExampleNonsecureURBG rbg1(seq);
  ExampleNonsecureURBG rbg2(seq);

  // ExampleNonsecureURBG rbg3({1, 2, 3});  // Should not compile.

  for (uint32_t i = 0; i < 1000; i++) {
    EXPECT_EQ(rbg1(), rbg2());
  }

  rbg1.discard(100);
  rbg2.discard(100);

  // The sequences should continue after discarding
  for (uint32_t i = 0; i < 1000; i++) {
    EXPECT_EQ(rbg1(), rbg2());
  }
}

TEST(RandenPoolSeedSeqTest, SeederWorksForU32) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  uint32_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForU64) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  uint64_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
  EXPECT_FALSE((state[0] >> 32) == 0 && (state[1] >> 32) == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForS32) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  int32_t state[2] = {0, 0};
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}

TEST(RandenPoolSeedSeqTest, SeederWorksForVector) {
  absl::random_internal::RandenPoolSeedSeq seeder;

  std::vector<uint32_t> state(2);
  seeder.generate(std::begin(state), std::end(state));
  EXPECT_FALSE(state[0] == 0 && state[1] == 0);
}
