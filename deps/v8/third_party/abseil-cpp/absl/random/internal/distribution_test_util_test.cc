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

#include "absl/random/internal/distribution_test_util.h"

#include "gtest/gtest.h"

namespace {

TEST(TestUtil, InverseErf) {
  const struct {
    const double z;
    const double value;
  } kErfInvTable[] = {
      {0.0000001, 8.86227e-8},
      {0.00001, 8.86227e-6},
      {0.5, 0.4769362762044},
      {0.6, 0.5951160814499},
      {0.99999, 3.1234132743},
      {0.9999999, 3.7665625816},
      {0.999999944, 3.8403850690566985},  // = log((1-x) * (1+x)) =~ 16.004
      {0.999999999, 4.3200053849134452},
  };

  for (const auto& data : kErfInvTable) {
    auto value = absl::random_internal::erfinv(data.z);

    // Log using the Wolfram-alpha function name & parameters.
    EXPECT_NEAR(value, data.value, 1e-8)
        << " InverseErf[" << data.z << "]  (expected=" << data.value << ")  -> "
        << value;
  }
}

const struct {
  const double p;
  const double q;
  const double x;
  const double alpha;
} kBetaTable[] = {
    {0.5, 0.5, 0.01, 0.06376856085851985},
    {0.5, 0.5, 0.1, 0.2048327646991335},
    {0.5, 0.5, 1, 1},
    {1, 0.5, 0, 0},
    {1, 0.5, 0.01, 0.005012562893380045},
    {1, 0.5, 0.1, 0.0513167019494862},
    {1, 0.5, 0.5, 0.2928932188134525},
    {1, 1, 0.5, 0.5},
    {2, 2, 0.1, 0.028},
    {2, 2, 0.2, 0.104},
    {2, 2, 0.3, 0.216},
    {2, 2, 0.4, 0.352},
    {2, 2, 0.5, 0.5},
    {2, 2, 0.6, 0.648},
    {2, 2, 0.7, 0.784},
    {2, 2, 0.8, 0.896},
    {2, 2, 0.9, 0.972},
    {5.5, 5, 0.5, 0.4361908850559777},
    {10, 0.5, 0.9, 0.1516409096346979},
    {10, 5, 0.5, 0.08978271484375},
    {10, 5, 1, 1},
    {10, 10, 0.5, 0.5},
    {20, 5, 0.8, 0.4598773297575791},
    {20, 10, 0.6, 0.2146816102371739},
    {20, 10, 0.8, 0.9507364826957875},
    {20, 20, 0.5, 0.5},
    {20, 20, 0.6, 0.8979413687105918},
    {30, 10, 0.7, 0.2241297491808366},
    {30, 10, 0.8, 0.7586405487192086},
    {40, 20, 0.7, 0.7001783247477069},
    {1, 0.5, 0.1, 0.0513167019494862},
    {1, 0.5, 0.2, 0.1055728090000841},
    {1, 0.5, 0.3, 0.1633399734659245},
    {1, 0.5, 0.4, 0.2254033307585166},
    {1, 2, 0.2, 0.36},
    {1, 3, 0.2, 0.488},
    {1, 4, 0.2, 0.5904},
    {1, 5, 0.2, 0.67232},
    {2, 2, 0.3, 0.216},
    {3, 2, 0.3, 0.0837},
    {4, 2, 0.3, 0.03078},
    {5, 2, 0.3, 0.010935},

    // These values test small & large points along the range of the Beta
    // function.
    //
    // When selecting test points, remember that if BetaIncomplete(x, p, q)
    // returns the same value to within the limits of precision over a large
    // domain of the input, x, then BetaIncompleteInv(alpha, p, q) may return an
    // essentially arbitrary value where BetaIncomplete(x, p, q) =~ alpha.

    // BetaRegularized[x, 0.00001, 0.00001],
    // For x in {~0.001 ... ~0.999}, => ~0.5
    {1e-5, 1e-5, 1e-5, 0.4999424388184638311},
    {1e-5, 1e-5, (1.0 - 1e-8), 0.5000920948389232964},

    // BetaRegularized[x, 0.00001, 10000].
    // For x in {~epsilon ... 1.0}, => ~1
    {1e-5, 1e5, 1e-6, 0.9999817708130066936},
    {1e-5, 1e5, (1.0 - 1e-7), 1.0},

    // BetaRegularized[x, 10000, 0.00001].
    // For x in {0 .. 1-epsilon}, => ~0
    {1e5, 1e-5, 1e-6, 0},
    {1e5, 1e-5, (1.0 - 1e-6), 1.8229186993306369e-5},
};

TEST(BetaTest, BetaIncomplete) {
  for (const auto& data : kBetaTable) {
    auto value = absl::random_internal::BetaIncomplete(data.x, data.p, data.q);

    // Log using the Wolfram-alpha function name & parameters.
    EXPECT_NEAR(value, data.alpha, 1e-12)
        << " BetaRegularized[" << data.x << ", " << data.p << ", " << data.q
        << "]  (expected=" << data.alpha << ")  -> " << value;
  }
}

TEST(BetaTest, BetaIncompleteInv) {
  for (const auto& data : kBetaTable) {
    auto value =
        absl::random_internal::BetaIncompleteInv(data.p, data.q, data.alpha);

    // Log using the Wolfram-alpha function name & parameters.
    EXPECT_NEAR(value, data.x, 1e-6)
        << " InverseBetaRegularized[" << data.alpha << ", " << data.p << ", "
        << data.q << "]  (expected=" << data.x << ")  -> " << value;
  }
}

TEST(MaxErrorTolerance, MaxErrorTolerance) {
  std::vector<std::pair<double, double>> cases = {
      {0.0000001, 8.86227e-8 * 1.41421356237},
      {0.00001, 8.86227e-6 * 1.41421356237},
      {0.5, 0.4769362762044 * 1.41421356237},
      {0.6, 0.5951160814499 * 1.41421356237},
      {0.99999, 3.1234132743 * 1.41421356237},
      {0.9999999, 3.7665625816 * 1.41421356237},
      {0.999999944, 3.8403850690566985 * 1.41421356237},
      {0.999999999, 4.3200053849134452 * 1.41421356237}};
  for (auto entry : cases) {
    EXPECT_NEAR(absl::random_internal::MaxErrorTolerance(entry.first),
                entry.second, 1e-8);
  }
}

TEST(ZScore, WithSameMean) {
  absl::random_internal::DistributionMoments m;
  m.n = 100;
  m.mean = 5;
  m.variance = 1;
  EXPECT_NEAR(absl::random_internal::ZScore(5, m), 0, 1e-12);

  m.n = 1;
  m.mean = 0;
  m.variance = 1;
  EXPECT_NEAR(absl::random_internal::ZScore(0, m), 0, 1e-12);

  m.n = 10000;
  m.mean = -5;
  m.variance = 100;
  EXPECT_NEAR(absl::random_internal::ZScore(-5, m), 0, 1e-12);
}

TEST(ZScore, DifferentMean) {
  absl::random_internal::DistributionMoments m;
  m.n = 100;
  m.mean = 5;
  m.variance = 1;
  EXPECT_NEAR(absl::random_internal::ZScore(4, m), 10, 1e-12);

  m.n = 1;
  m.mean = 0;
  m.variance = 1;
  EXPECT_NEAR(absl::random_internal::ZScore(-1, m), 1, 1e-12);

  m.n = 10000;
  m.mean = -5;
  m.variance = 100;
  EXPECT_NEAR(absl::random_internal::ZScore(-4, m), -10, 1e-12);
}
}  // namespace
