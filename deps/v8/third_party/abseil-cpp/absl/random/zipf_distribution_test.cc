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

#include "absl/random/zipf_distribution.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

namespace {

using ::absl::random_internal::kChiSquared;
using ::testing::ElementsAre;

template <typename IntType>
class ZipfDistributionTypedTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int, int8_t, int16_t, int32_t, int64_t,
                                  uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(ZipfDistributionTypedTest, IntTypes);

TYPED_TEST(ZipfDistributionTypedTest, SerializeTest) {
  using param_type = typename absl::zipf_distribution<TypeParam>::param_type;

  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;
  for (const auto& param : {
           param_type(),
           param_type(32),
           param_type(100, 3, 2),
           param_type(std::numeric_limits<TypeParam>::max(), 4, 3),
           param_type(std::numeric_limits<TypeParam>::max() / 2),
       }) {
    // Validate parameters.
    const auto k = param.k();
    const auto q = param.q();
    const auto v = param.v();

    absl::zipf_distribution<TypeParam> before(k, q, v);
    EXPECT_EQ(before.k(), param.k());
    EXPECT_EQ(before.q(), param.q());
    EXPECT_EQ(before.v(), param.v());

    {
      absl::zipf_distribution<TypeParam> via_param(param);
      EXPECT_EQ(via_param, before);
    }

    // Validate stream serialization.
    std::stringstream ss;
    ss << before;
    absl::zipf_distribution<TypeParam> after(4, 5.5, 4.4);

    EXPECT_NE(before.k(), after.k());
    EXPECT_NE(before.q(), after.q());
    EXPECT_NE(before.v(), after.v());
    EXPECT_NE(before.param(), after.param());
    EXPECT_NE(before, after);

    ss >> after;

    EXPECT_EQ(before.k(), after.k());
    EXPECT_EQ(before.q(), after.q());
    EXPECT_EQ(before.v(), after.v());
    EXPECT_EQ(before.param(), after.param());
    EXPECT_EQ(before, after);

    // Smoke test.
    auto sample_min = after.max();
    auto sample_max = after.min();
    for (int i = 0; i < kCount; i++) {
      auto sample = after(gen);
      EXPECT_GE(sample, after.min());
      EXPECT_LE(sample, after.max());
      if (sample > sample_max) sample_max = sample;
      if (sample < sample_min) sample_min = sample;
    }
    LOG(INFO) << "Range: " << sample_min << ", " << sample_max;
  }
}

class ZipfModel {
 public:
  ZipfModel(size_t k, double q, double v) : k_(k), q_(q), v_(v) {}

  double mean() const { return mean_; }

  // For the other moments of the Zipf distribution, see, for example,
  // http://mathworld.wolfram.com/ZipfDistribution.html

  // PMF(k) = (1 / k^s) / H(N,s)
  // Returns the probability that any single invocation returns k.
  double PMF(size_t i) { return i >= hnq_.size() ? 0.0 : hnq_[i] / sum_hnq_; }

  // CDF = H(k, s) / H(N,s)
  double CDF(size_t i) {
    if (i >= hnq_.size()) {
      return 1.0;
    }
    auto it = std::begin(hnq_);
    double h = 0.0;
    for (const auto end = it; it != end; it++) {
      h += *it;
    }
    return h / sum_hnq_;
  }

  // The InverseCDF returns the k values which bound p on the upper and lower
  // bound. Since there is no closed-form solution, this is implemented as a
  // bisction of the cdf.
  std::pair<size_t, size_t> InverseCDF(double p) {
    size_t min = 0;
    size_t max = hnq_.size();
    while (max > min + 1) {
      size_t target = (max + min) >> 1;
      double x = CDF(target);
      if (x > p) {
        max = target;
      } else {
        min = target;
      }
    }
    return {min, max};
  }

  // Compute the probability totals, which are based on the generalized harmonic
  // number, H(N,s).
  //   H(N,s) == SUM(k=1..N, 1 / k^s)
  //
  // In the limit, H(N,s) == zetac(s) + 1.
  //
  // NOTE: The mean of a zipf distribution could be computed here as well.
  // Mean :=  H(N, s-1) / H(N,s).
  // Given the parameter v = 1, this gives the following function:
  // (Hn(100, 1) - Hn(1,1)) / (Hn(100,2) - Hn(1,2)) = 6.5944
  //
  void Init() {
    if (!hnq_.empty()) {
      return;
    }
    hnq_.clear();
    hnq_.reserve(std::min(k_, size_t{1000}));

    sum_hnq_ = 0;
    double qm1 = q_ - 1.0;
    double sum_hnq_m1 = 0;
    for (size_t i = 0; i < k_; i++) {
      // Partial n-th generalized harmonic number
      const double x = v_ + i;

      // H(n, q-1)
      const double hnqm1 =
          (q_ == 2.0) ? (1.0 / x)
                      : (q_ == 3.0) ? (1.0 / (x * x)) : std::pow(x, -qm1);
      sum_hnq_m1 += hnqm1;

      // H(n, q)
      const double hnq =
          (q_ == 2.0) ? (1.0 / (x * x))
                      : (q_ == 3.0) ? (1.0 / (x * x * x)) : std::pow(x, -q_);
      sum_hnq_ += hnq;
      hnq_.push_back(hnq);
      if (i > 1000 && hnq <= 1e-10) {
        // The harmonic number is too small.
        break;
      }
    }
    assert(sum_hnq_ > 0);
    mean_ = sum_hnq_m1 / sum_hnq_;
  }

 private:
  const size_t k_;
  const double q_;
  const double v_;

  double mean_;
  std::vector<double> hnq_;
  double sum_hnq_;
};

using zipf_u64 = absl::zipf_distribution<uint64_t>;

class ZipfTest : public testing::TestWithParam<zipf_u64::param_type>,
                 public ZipfModel {
 public:
  ZipfTest() : ZipfModel(GetParam().k(), GetParam().q(), GetParam().v()) {}

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
};

TEST_P(ZipfTest, ChiSquaredTest) {
  const auto& param = GetParam();
  Init();

  size_t trials = 10000;

  // Find the split-points for the buckets.
  std::vector<size_t> points;
  std::vector<double> expected;
  {
    double last_cdf = 0.0;
    double min_p = 1.0;
    for (double p = 0.01; p < 1.0; p += 0.01) {
      auto x = InverseCDF(p);
      if (points.empty() || points.back() < x.second) {
        const double p = CDF(x.second);
        points.push_back(x.second);
        double q = p - last_cdf;
        expected.push_back(q);
        last_cdf = p;
        if (q < min_p) {
          min_p = q;
        }
      }
    }
    if (last_cdf < 0.999) {
      points.push_back(std::numeric_limits<size_t>::max());
      double q = 1.0 - last_cdf;
      expected.push_back(q);
      if (q < min_p) {
        min_p = q;
      }
    } else {
      points.back() = std::numeric_limits<size_t>::max();
      expected.back() += (1.0 - last_cdf);
    }
    // The Chi-Squared score is not completely scale-invariant; it works best
    // when the small values are in the small digits.
    trials = static_cast<size_t>(8.0 / min_p);
  }
  ASSERT_GT(points.size(), 0);

  // Generate n variates and fill the counts vector with the count of their
  // occurrences.
  std::vector<int64_t> buckets(points.size(), 0);
  double avg = 0;
  {
    zipf_u64 dis(param);
    for (size_t i = 0; i < trials; i++) {
      uint64_t x = dis(rng_);
      ASSERT_LE(x, dis.max());
      ASSERT_GE(x, dis.min());
      avg += static_cast<double>(x);
      auto it = std::upper_bound(std::begin(points), std::end(points),
                                 static_cast<size_t>(x));
      buckets[std::distance(std::begin(points), it)]++;
    }
    avg = avg / static_cast<double>(trials);
  }

  // Validate the output using the Chi-Squared test.
  for (auto& e : expected) {
    e *= trials;
  }

  // The null-hypothesis is that the distribution is a poisson distribution with
  // the provided mean (not estimated from the data).
  const int dof = static_cast<int>(expected.size()) - 1;

  // NOTE: This test runs about 15x per invocation, so a value of 0.9995 is
  // approximately correct for a test suite failure rate of 1 in 100.  In
  // practice we see failures slightly higher than that.
  const double threshold = absl::random_internal::ChiSquareValue(dof, 0.9999);

  const double chi_square = absl::random_internal::ChiSquare(
      std::begin(buckets), std::end(buckets), std::begin(expected),
      std::end(expected));

  const double p_actual =
      absl::random_internal::ChiSquarePValue(chi_square, dof);

  // Log if the chi_squared value is above the threshold.
  if (chi_square > threshold) {
    LOG(INFO) << "values";
    for (size_t i = 0; i < expected.size(); i++) {
      LOG(INFO) << points[i] << ": " << buckets[i] << " vs. E=" << expected[i];
    }
    LOG(INFO) << "trials " << trials;
    LOG(INFO) << "mean " << avg << " vs. expected " << mean();
    LOG(INFO) << kChiSquared << "(data, " << dof << ") = " << chi_square << " ("
              << p_actual << ")";
    LOG(INFO) << kChiSquared << " @ 0.9995 = " << threshold;
    FAIL() << kChiSquared << " value of " << chi_square
           << " is above the threshold.";
  }
}

std::vector<zipf_u64::param_type> GenParams() {
  using param = zipf_u64::param_type;
  const auto k = param().k();
  const auto q = param().q();
  const auto v = param().v();
  const uint64_t k2 = 1 << 10;
  return std::vector<zipf_u64::param_type>{
      // Default
      param(k, q, v),
      // vary K
      param(4, q, v), param(1 << 4, q, v), param(k2, q, v),
      // vary V
      param(k2, q, 0.5), param(k2, q, 1.5), param(k2, q, 2.5), param(k2, q, 10),
      // vary Q
      param(k2, 1.5, v), param(k2, 3, v), param(k2, 5, v), param(k2, 10, v),
      // Vary V & Q
      param(k2, 1.5, 0.5), param(k2, 3, 1.5), param(k, 10, 10)};
}

std::string ParamName(
    const ::testing::TestParamInfo<zipf_u64::param_type>& info) {
  const auto& p = info.param;
  std::string name = absl::StrCat("k_", p.k(), "__q_", absl::SixDigits(p.q()),
                                  "__v_", absl::SixDigits(p.v()));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(All, ZipfTest, ::testing::ValuesIn(GenParams()),
                         ParamName);

// NOTE: absl::zipf_distribution is not guaranteed to be stable.
TEST(ZipfDistributionTest, StabilityTest) {
  // absl::zipf_distribution stability relies on
  // absl::uniform_real_distribution, std::log, std::exp, std::log1p
  absl::random_internal::sequence_urbg urbg(
      {0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
       0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
       0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
       0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull});

  std::vector<int> output(10);

  {
    absl::zipf_distribution<int32_t> dist;
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
    EXPECT_THAT(output, ElementsAre(10031, 0, 0, 3, 6, 0, 7, 47, 0, 0));
  }
  urbg.reset();
  {
    absl::zipf_distribution<int32_t> dist(std::numeric_limits<int32_t>::max(),
                                          3.3);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
    EXPECT_THAT(output, ElementsAre(44, 0, 0, 0, 0, 1, 0, 1, 3, 0));
  }
}

TEST(ZipfDistributionTest, AlgorithmBounds) {
  absl::zipf_distribution<int32_t> dist;

  // Small values from absl::uniform_real_distribution map to larger Zipf
  // distribution values.
  const std::pair<uint64_t, int32_t> kInputs[] = {
      {0xffffffffffffffff, 0x0}, {0x7fffffffffffffff, 0x0},
      {0x3ffffffffffffffb, 0x1}, {0x1ffffffffffffffd, 0x4},
      {0xffffffffffffffe, 0x9},  {0x7ffffffffffffff, 0x12},
      {0x3ffffffffffffff, 0x25}, {0x1ffffffffffffff, 0x4c},
      {0xffffffffffffff, 0x99},  {0x7fffffffffffff, 0x132},
      {0x3fffffffffffff, 0x265}, {0x1fffffffffffff, 0x4cc},
      {0xfffffffffffff, 0x999},  {0x7ffffffffffff, 0x1332},
      {0x3ffffffffffff, 0x2665}, {0x1ffffffffffff, 0x4ccc},
      {0xffffffffffff, 0x9998},  {0x7fffffffffff, 0x1332f},
      {0x3fffffffffff, 0x2665a}, {0x1fffffffffff, 0x4cc9e},
      {0xfffffffffff, 0x998e0},  {0x7ffffffffff, 0x133051},
      {0x3ffffffffff, 0x265ae4}, {0x1ffffffffff, 0x4c9ed3},
      {0xffffffffff, 0x98e223},  {0x7fffffffff, 0x13058c4},
      {0x3fffffffff, 0x25b178e}, {0x1fffffffff, 0x4a062b2},
      {0xfffffffff, 0x8ee23b8},  {0x7ffffffff, 0x10b21642},
      {0x3ffffffff, 0x1d89d89d}, {0x1ffffffff, 0x2fffffff},
      {0xffffffff, 0x45d1745d},  {0x7fffffff, 0x5a5a5a5a},
      {0x3fffffff, 0x69ee5846},  {0x1fffffff, 0x73ecade3},
      {0xfffffff, 0x79a9d260},   {0x7ffffff, 0x7cc0532b},
      {0x3ffffff, 0x7e5ad146},   {0x1ffffff, 0x7f2c0bec},
      {0xffffff, 0x7f95adef},    {0x7fffff, 0x7fcac0da},
      {0x3fffff, 0x7fe55ae2},    {0x1fffff, 0x7ff2ac0e},
      {0xfffff, 0x7ff955ae},     {0x7ffff, 0x7ffcaac1},
      {0x3ffff, 0x7ffe555b},     {0x1ffff, 0x7fff2aac},
      {0xffff, 0x7fff9556},      {0x7fff, 0x7fffcaab},
      {0x3fff, 0x7fffe555},      {0x1fff, 0x7ffff2ab},
      {0xfff, 0x7ffff955},       {0x7ff, 0x7ffffcab},
      {0x3ff, 0x7ffffe55},       {0x1ff, 0x7fffff2b},
      {0xff, 0x7fffff95},        {0x7f, 0x7fffffcb},
      {0x3f, 0x7fffffe5},        {0x1f, 0x7ffffff3},
      {0xf, 0x7ffffff9},         {0x7, 0x7ffffffd},
      {0x3, 0x7ffffffe},         {0x1, 0x7fffffff},
  };

  for (const auto& instance : kInputs) {
    absl::random_internal::sequence_urbg urbg({instance.first});
    EXPECT_EQ(instance.second, dist(urbg));
  }
}

}  // namespace
