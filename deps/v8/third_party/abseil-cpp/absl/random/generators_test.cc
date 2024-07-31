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

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "gtest/gtest.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"

namespace {

template <typename URBG>
void TestUniform(URBG* gen) {
  // [a, b) default-semantics, inferred types.
  absl::Uniform(*gen, 0, 100);     // int
  absl::Uniform(*gen, 0, 1.0);     // Promoted to double
  absl::Uniform(*gen, 0.0f, 1.0);  // Promoted to double
  absl::Uniform(*gen, 0.0, 1.0);   // double
  absl::Uniform(*gen, -1, 1L);     // Promoted to long

  // Roll a die.
  absl::Uniform(absl::IntervalClosedClosed, *gen, 1, 6);

  // Get a fraction.
  absl::Uniform(absl::IntervalOpenOpen, *gen, 0.0, 1.0);

  // Assign a value to a random element.
  std::vector<int> elems = {10, 20, 30, 40, 50};
  elems[absl::Uniform(*gen, 0u, elems.size())] = 5;
  elems[absl::Uniform<size_t>(*gen, 0, elems.size())] = 3;

  // Choose some epsilon around zero.
  absl::Uniform(absl::IntervalOpenOpen, *gen, -1.0, 1.0);

  // (a, b) semantics, inferred types.
  absl::Uniform(absl::IntervalOpenOpen, *gen, 0, 1.0);  // Promoted to double

  // Explicit overriding of types.
  absl::Uniform<int>(*gen, 0, 100);
  absl::Uniform<int8_t>(*gen, 0, 100);
  absl::Uniform<int16_t>(*gen, 0, 100);
  absl::Uniform<uint16_t>(*gen, 0, 100);
  absl::Uniform<int32_t>(*gen, 0, 1 << 10);
  absl::Uniform<uint32_t>(*gen, 0, 1 << 10);
  absl::Uniform<int64_t>(*gen, 0, 1 << 10);
  absl::Uniform<uint64_t>(*gen, 0, 1 << 10);

  absl::Uniform<float>(*gen, 0.0, 1.0);
  absl::Uniform<float>(*gen, 0, 1);
  absl::Uniform<float>(*gen, -1, 1);
  absl::Uniform<double>(*gen, 0.0, 1.0);

  absl::Uniform<float>(*gen, -1.0, 0);
  absl::Uniform<double>(*gen, -1.0, 0);

  // Tagged
  absl::Uniform<double>(absl::IntervalClosedClosed, *gen, 0, 1);
  absl::Uniform<double>(absl::IntervalClosedOpen, *gen, 0, 1);
  absl::Uniform<double>(absl::IntervalOpenOpen, *gen, 0, 1);
  absl::Uniform<double>(absl::IntervalOpenClosed, *gen, 0, 1);
  absl::Uniform<double>(absl::IntervalClosedClosed, *gen, 0, 1);
  absl::Uniform<double>(absl::IntervalOpenOpen, *gen, 0, 1);

  absl::Uniform<int>(absl::IntervalClosedClosed, *gen, 0, 100);
  absl::Uniform<int>(absl::IntervalClosedOpen, *gen, 0, 100);
  absl::Uniform<int>(absl::IntervalOpenOpen, *gen, 0, 100);
  absl::Uniform<int>(absl::IntervalOpenClosed, *gen, 0, 100);
  absl::Uniform<int>(absl::IntervalClosedClosed, *gen, 0, 100);
  absl::Uniform<int>(absl::IntervalOpenOpen, *gen, 0, 100);

  // With *generator as an R-value reference.
  absl::Uniform<int>(URBG(), 0, 100);
  absl::Uniform<double>(URBG(), 0.0, 1.0);
}

template <typename URBG>
void TestExponential(URBG* gen) {
  absl::Exponential<float>(*gen);
  absl::Exponential<double>(*gen);
  absl::Exponential<double>(URBG());
}

template <typename URBG>
void TestPoisson(URBG* gen) {
  // [rand.dist.pois] Indicates that the std::poisson_distribution
  // is parameterized by IntType, however MSVC does not allow 8-bit
  // types.
  absl::Poisson<int>(*gen);
  absl::Poisson<int16_t>(*gen);
  absl::Poisson<uint16_t>(*gen);
  absl::Poisson<int32_t>(*gen);
  absl::Poisson<uint32_t>(*gen);
  absl::Poisson<int64_t>(*gen);
  absl::Poisson<uint64_t>(*gen);
  absl::Poisson<uint64_t>(URBG());
  absl::Poisson<absl::int128>(*gen);
  absl::Poisson<absl::uint128>(*gen);
}

template <typename URBG>
void TestBernoulli(URBG* gen) {
  absl::Bernoulli(*gen, 0.5);
  absl::Bernoulli(*gen, 0.5);
}


template <typename URBG>
void TestZipf(URBG* gen) {
  absl::Zipf<int>(*gen, 100);
  absl::Zipf<int8_t>(*gen, 100);
  absl::Zipf<int16_t>(*gen, 100);
  absl::Zipf<uint16_t>(*gen, 100);
  absl::Zipf<int32_t>(*gen, 1 << 10);
  absl::Zipf<uint32_t>(*gen, 1 << 10);
  absl::Zipf<int64_t>(*gen, 1 << 10);
  absl::Zipf<uint64_t>(*gen, 1 << 10);
  absl::Zipf<uint64_t>(URBG(), 1 << 10);
  absl::Zipf<absl::int128>(*gen, 1 << 10);
  absl::Zipf<absl::uint128>(*gen, 1 << 10);
}

template <typename URBG>
void TestGaussian(URBG* gen) {
  absl::Gaussian<float>(*gen, 1.0, 1.0);
  absl::Gaussian<double>(*gen, 1.0, 1.0);
  absl::Gaussian<double>(URBG(), 1.0, 1.0);
}

template <typename URBG>
void TestLogNormal(URBG* gen) {
  absl::LogUniform<int>(*gen, 0, 100);
  absl::LogUniform<int8_t>(*gen, 0, 100);
  absl::LogUniform<int16_t>(*gen, 0, 100);
  absl::LogUniform<uint16_t>(*gen, 0, 100);
  absl::LogUniform<int32_t>(*gen, 0, 1 << 10);
  absl::LogUniform<uint32_t>(*gen, 0, 1 << 10);
  absl::LogUniform<int64_t>(*gen, 0, 1 << 10);
  absl::LogUniform<uint64_t>(*gen, 0, 1 << 10);
  absl::LogUniform<uint64_t>(URBG(), 0, 1 << 10);
  absl::LogUniform<absl::int128>(*gen, 0, 1 << 10);
  absl::LogUniform<absl::uint128>(*gen, 0, 1 << 10);
}

template <typename URBG>
void CompatibilityTest() {
  URBG gen;

  TestUniform(&gen);
  TestExponential(&gen);
  TestPoisson(&gen);
  TestBernoulli(&gen);
  TestZipf(&gen);
  TestGaussian(&gen);
  TestLogNormal(&gen);
}

TEST(std_mt19937_64, Compatibility) {
  // Validate with std::mt19937_64
  CompatibilityTest<std::mt19937_64>();
}

TEST(BitGen, Compatibility) {
  // Validate with absl::BitGen
  CompatibilityTest<absl::BitGen>();
}

TEST(InsecureBitGen, Compatibility) {
  // Validate with absl::InsecureBitGen
  CompatibilityTest<absl::InsecureBitGen>();
}

}  // namespace
