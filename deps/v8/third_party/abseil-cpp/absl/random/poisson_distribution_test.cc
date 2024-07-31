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

#include "absl/random/poisson_distribution.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

// Notes about generating poisson variates:
//
// It is unlikely that any implementation of std::poisson_distribution
// will be stable over time and across library implementations. For instance
// the three different poisson variate generators listed below all differ:
//
// https://github.com/ampl/gsl/tree/master/randist/poisson.c
// * GSL uses a gamma + binomial + knuth method to compute poisson variates.
//
// https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/include/bits/random.tcc
// * GCC uses the Devroye rejection algorithm, based on
// Devroye, L. Non-Uniform Random Variates Generation. Springer-Verlag,
// New York, 1986, Ch. X, Sects. 3.3 & 3.4 (+ Errata!), ~p.511
//   http://www.nrbook.com/devroye/
//
// https://github.com/llvm-mirror/libcxx/blob/master/include/random
// * CLANG uses a different rejection method, which appears to include a
// normal-distribution approximation and an exponential distribution to
// compute the threshold, including a similar factorial approximation to this
// one, but it is unclear where the algorithm comes from, exactly.
//

namespace {

using absl::random_internal::kChiSquared;

// The PoissonDistributionInterfaceTest provides a basic test that
// absl::poisson_distribution conforms to the interface and serialization
// requirements imposed by [rand.req.dist] for the common integer types.

template <typename IntType>
class PoissonDistributionInterfaceTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int, int8_t, int16_t, int32_t, int64_t,
                                  uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(PoissonDistributionInterfaceTest, IntTypes);

TYPED_TEST(PoissonDistributionInterfaceTest, SerializeTest) {
  using param_type = typename absl::poisson_distribution<TypeParam>::param_type;
  const double kMax =
      std::min(1e10 /* assertion limit */,
               static_cast<double>(std::numeric_limits<TypeParam>::max()));

  const double kParams[] = {
      // Cases around 1.
      1,                         //
      std::nextafter(1.0, 0.0),  // 1 - epsilon
      std::nextafter(1.0, 2.0),  // 1 + epsilon
      // Arbitrary values.
      1e-8, 1e-4,
      0.0000005,  // ~7.2e-7
      0.2,        // ~0.2x
      0.5,        // 0.72
      2,          // ~2.8
      20,         // 3x ~9.6
      100, 1e4, 1e8, 1.5e9, 1e20,
      // Boundary cases.
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::epsilon(),
      std::nextafter(std::numeric_limits<double>::min(),
                     1.0),                        // min + epsilon
      std::numeric_limits<double>::min(),         // smallest normal
      std::numeric_limits<double>::denorm_min(),  // smallest denorm
      std::numeric_limits<double>::min() / 2,     // denorm
      std::nextafter(std::numeric_limits<double>::min(),
                     0.0),  // denorm_max
  };


  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;
  for (const double m : kParams) {
    const double mean = std::min(kMax, m);
    const param_type param(mean);

    // Validate parameters.
    absl::poisson_distribution<TypeParam> before(mean);
    EXPECT_EQ(before.mean(), param.mean());

    {
      absl::poisson_distribution<TypeParam> via_param(param);
      EXPECT_EQ(via_param, before);
      EXPECT_EQ(via_param.param(), before.param());
    }

    // Smoke test.
    auto sample_min = before.max();
    auto sample_max = before.min();
    for (int i = 0; i < kCount; i++) {
      auto sample = before(gen);
      EXPECT_GE(sample, before.min());
      EXPECT_LE(sample, before.max());
      if (sample > sample_max) sample_max = sample;
      if (sample < sample_min) sample_min = sample;
    }

    LOG(INFO) << "Range {" << param.mean() << "}: " << sample_min << ", "
              << sample_max;

    // Validate stream serialization.
    std::stringstream ss;
    ss << before;

    absl::poisson_distribution<TypeParam> after(3.8);

    EXPECT_NE(before.mean(), after.mean());
    EXPECT_NE(before.param(), after.param());
    EXPECT_NE(before, after);

    ss >> after;

    EXPECT_EQ(before.mean(), after.mean())  //
        << ss.str() << " "                  //
        << (ss.good() ? "good " : "")       //
        << (ss.bad() ? "bad " : "")         //
        << (ss.eof() ? "eof " : "")         //
        << (ss.fail() ? "fail " : "");
  }
}

// See http://www.itl.nist.gov/div898/handbook/eda/section3/eda366j.htm

class PoissonModel {
 public:
  explicit PoissonModel(double mean) : mean_(mean) {}

  double mean() const { return mean_; }
  double variance() const { return mean_; }
  double stddev() const { return std::sqrt(variance()); }
  double skew() const { return 1.0 / mean_; }
  double kurtosis() const { return 3.0 + 1.0 / mean_; }

  // InitCDF() initializes the CDF for the distribution parameters.
  void InitCDF();

  // The InverseCDF, or the Percent-point function returns x, P(x) < v.
  struct CDF {
    size_t index;
    double pmf;
    double cdf;
  };
  CDF InverseCDF(double p) {
    CDF target{0, 0, p};
    auto it = std::upper_bound(
        std::begin(cdf_), std::end(cdf_), target,
        [](const CDF& a, const CDF& b) { return a.cdf < b.cdf; });
    return *it;
  }

  void LogCDF() {
    LOG(INFO) << "CDF (mean = " << mean_ << ")";
    for (const auto c : cdf_) {
      LOG(INFO) << c.index << ": pmf=" << c.pmf << " cdf=" << c.cdf;
    }
  }

 private:
  const double mean_;

  std::vector<CDF> cdf_;
};

// The goal is to compute an InverseCDF function, or percent point function for
// the poisson distribution, and use that to partition our output into equal
// range buckets.  However there is no closed form solution for the inverse cdf
// for poisson distributions (the closest is the incomplete gamma function).
// Instead, `InitCDF` iteratively computes the PMF and the CDF. This enables
// searching for the bucket points.
void PoissonModel::InitCDF() {
  if (!cdf_.empty()) {
    // State already initialized.
    return;
  }
  ABSL_ASSERT(mean_ < 201.0);

  const size_t max_i = 50 * stddev() + mean();
  const double e_neg_mean = std::exp(-mean());
  ABSL_ASSERT(e_neg_mean > 0);

  double d = 1;
  double last_result = e_neg_mean;
  double cumulative = e_neg_mean;
  if (e_neg_mean > 1e-10) {
    cdf_.push_back({0, e_neg_mean, cumulative});
  }
  for (size_t i = 1; i < max_i; i++) {
    d *= (mean() / i);
    double result = e_neg_mean * d;
    cumulative += result;
    if (result < 1e-10 && result < last_result && cumulative > 0.999999) {
      break;
    }
    if (result > 1e-7) {
      cdf_.push_back({i, result, cumulative});
    }
    last_result = result;
  }
  ABSL_ASSERT(!cdf_.empty());
}

// PoissonDistributionZTest implements a z-test for the poisson distribution.

struct ZParam {
  double mean;
  double p_fail;   // Z-Test probability of failure.
  int trials;      // Z-Test trials.
  size_t samples;  // Z-Test samples.
};

class PoissonDistributionZTest : public testing::TestWithParam<ZParam>,
                                 public PoissonModel {
 public:
  PoissonDistributionZTest() : PoissonModel(GetParam().mean) {}

  // ZTestImpl provides a basic z-squared test of the mean vs. expected
  // mean for data generated by the poisson distribution.
  template <typename D>
  bool SingleZTest(const double p, const size_t samples);

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
};

template <typename D>
bool PoissonDistributionZTest::SingleZTest(const double p,
                                           const size_t samples) {
  D dis(mean());

  absl::flat_hash_map<int32_t, int> buckets;
  std::vector<double> data;
  data.reserve(samples);
  for (int j = 0; j < samples; j++) {
    const auto x = dis(rng_);
    buckets[x]++;
    data.push_back(x);
  }

  // The null-hypothesis is that the distribution is a poisson distribution with
  // the provided mean (not estimated from the data).
  const auto m = absl::random_internal::ComputeDistributionMoments(data);
  const double max_err = absl::random_internal::MaxErrorTolerance(p);
  const double z = absl::random_internal::ZScore(mean(), m);
  const bool pass = absl::random_internal::Near("z", z, 0.0, max_err);

  if (!pass) {
    // clang-format off
    LOG(INFO)
        << "p=" << p << " max_err=" << max_err << "\n"
           " mean=" << m.mean << " vs. " << mean() << "\n"
           " stddev=" << std::sqrt(m.variance) << " vs. " << stddev() << "\n"
           " skewness=" << m.skewness << " vs. " << skew() << "\n"
           " kurtosis=" << m.kurtosis << " vs. " << kurtosis() << "\n"
           " z=" << z;
    // clang-format on
  }
  return pass;
}

TEST_P(PoissonDistributionZTest, AbslPoissonDistribution) {
  const auto& param = GetParam();
  const int expected_failures =
      std::max(1, static_cast<int>(std::ceil(param.trials * param.p_fail)));
  const double p = absl::random_internal::RequiredSuccessProbability(
      param.p_fail, param.trials);

  int failures = 0;
  for (int i = 0; i < param.trials; i++) {
    failures +=
        SingleZTest<absl::poisson_distribution<int32_t>>(p, param.samples) ? 0
                                                                           : 1;
  }
  EXPECT_LE(failures, expected_failures);
}

std::vector<ZParam> GetZParams() {
  // These values have been adjusted from the "exact" computed values to reduce
  // failure rates.
  //
  // It turns out that the actual values are not as close to the expected values
  // as would be ideal.
  return std::vector<ZParam>({
      // Knuth method.
      ZParam{0.5, 0.01, 100, 1000},
      ZParam{1.0, 0.01, 100, 1000},
      ZParam{10.0, 0.01, 100, 5000},
      // Split-knuth method.
      ZParam{20.0, 0.01, 100, 10000},
      ZParam{50.0, 0.01, 100, 10000},
      // Ratio of gaussians method.
      ZParam{51.0, 0.01, 100, 10000},
      ZParam{200.0, 0.05, 10, 100000},
      ZParam{100000.0, 0.05, 10, 1000000},
  });
}

std::string ZParamName(const ::testing::TestParamInfo<ZParam>& info) {
  const auto& p = info.param;
  std::string name = absl::StrCat("mean_", absl::SixDigits(p.mean));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(All, PoissonDistributionZTest,
                         ::testing::ValuesIn(GetZParams()), ZParamName);

// The PoissonDistributionChiSquaredTest class provides a basic test framework
// for variates generated by a conforming poisson_distribution.
class PoissonDistributionChiSquaredTest : public testing::TestWithParam<double>,
                                          public PoissonModel {
 public:
  PoissonDistributionChiSquaredTest() : PoissonModel(GetParam()) {}

  // The ChiSquaredTestImpl provides a chi-squared goodness of fit test for data
  // generated by the poisson distribution.
  template <typename D>
  double ChiSquaredTestImpl();

 private:
  void InitChiSquaredTest(const double buckets);

  std::vector<size_t> cutoffs_;
  std::vector<double> expected_;

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
};

void PoissonDistributionChiSquaredTest::InitChiSquaredTest(
    const double buckets) {
  if (!cutoffs_.empty() && !expected_.empty()) {
    return;
  }
  InitCDF();

  // The code below finds cuttoffs that yield approximately equally-sized
  // buckets to the extent that it is possible. However for poisson
  // distributions this is particularly challenging for small mean parameters.
  // Track the expected proportion of items in each bucket.
  double last_cdf = 0;
  const double inc = 1.0 / buckets;
  for (double p = inc; p <= 1.0; p += inc) {
    auto result = InverseCDF(p);
    if (!cutoffs_.empty() && cutoffs_.back() == result.index) {
      continue;
    }
    double d = result.cdf - last_cdf;
    cutoffs_.push_back(result.index);
    expected_.push_back(d);
    last_cdf = result.cdf;
  }
  cutoffs_.push_back(std::numeric_limits<size_t>::max());
  expected_.push_back(std::max(0.0, 1.0 - last_cdf));
}

template <typename D>
double PoissonDistributionChiSquaredTest::ChiSquaredTestImpl() {
  const int kSamples = 2000;
  const int kBuckets = 50;

  // The poisson CDF fails for large mean values, since e^-mean exceeds the
  // machine precision. For these cases, using a normal approximation would be
  // appropriate.
  ABSL_ASSERT(mean() <= 200);
  InitChiSquaredTest(kBuckets);

  D dis(mean());

  std::vector<int32_t> counts(cutoffs_.size(), 0);
  for (int j = 0; j < kSamples; j++) {
    const size_t x = dis(rng_);
    auto it = std::lower_bound(std::begin(cutoffs_), std::end(cutoffs_), x);
    counts[std::distance(cutoffs_.begin(), it)]++;
  }

  // Normalize the counts.
  std::vector<int32_t> e(expected_.size(), 0);
  for (int i = 0; i < e.size(); i++) {
    e[i] = kSamples * expected_[i];
  }

  // The null-hypothesis is that the distribution is a poisson distribution with
  // the provided mean (not estimated from the data).
  const int dof = static_cast<int>(counts.size()) - 1;

  // The threshold for logging is 1-in-50.
  const double threshold = absl::random_internal::ChiSquareValue(dof, 0.98);

  const double chi_square = absl::random_internal::ChiSquare(
      std::begin(counts), std::end(counts), std::begin(e), std::end(e));

  const double p = absl::random_internal::ChiSquarePValue(chi_square, dof);

  // Log if the chi_squared value is above the threshold.
  if (chi_square > threshold) {
    LogCDF();

    LOG(INFO) << "VALUES  buckets=" << counts.size()
              << "  samples=" << kSamples;
    for (size_t i = 0; i < counts.size(); i++) {
      LOG(INFO) << cutoffs_[i] << ": " << counts[i] << " vs. E=" << e[i];
    }

    LOG(INFO) << kChiSquared << "(data, dof=" << dof << ") = " << chi_square
              << " (" << p << ")\n"
              << " vs.\n"
              << kChiSquared << " @ 0.98 = " << threshold;
  }
  return p;
}

TEST_P(PoissonDistributionChiSquaredTest, AbslPoissonDistribution) {
  const int kTrials = 20;

  // Large values are not yet supported -- this requires estimating the cdf
  // using the normal distribution instead of the poisson in this case.
  ASSERT_LE(mean(), 200.0);
  if (mean() > 200.0) {
    return;
  }

  int failures = 0;
  for (int i = 0; i < kTrials; i++) {
    double p_value = ChiSquaredTestImpl<absl::poisson_distribution<int32_t>>();
    if (p_value < 0.005) {
      failures++;
    }
  }
  // There is a 0.10% chance of producing at least one failure, so raise the
  // failure threshold high enough to allow for a flake rate < 10,000.
  EXPECT_LE(failures, 4);
}

INSTANTIATE_TEST_SUITE_P(All, PoissonDistributionChiSquaredTest,
                         ::testing::Values(0.5, 1.0, 2.0, 10.0, 50.0, 51.0,
                                           200.0));

// NOTE: absl::poisson_distribution is not guaranteed to be stable.
TEST(PoissonDistributionTest, StabilityTest) {
  using testing::ElementsAre;
  // absl::poisson_distribution stability relies on stability of
  // std::exp, std::log, std::sqrt, std::ceil, std::floor, and
  // absl::FastUniformBits, absl::StirlingLogFactorial, absl::RandU64ToDouble.
  absl::random_internal::sequence_urbg urbg({
      0x035b0dc7e0a18acfull, 0x06cebe0d2653682eull, 0x0061e9b23861596bull,
      0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
      0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
      0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
      0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull,
      0x4864f22c059bf29eull, 0x247856d8b862665cull, 0xe46e86e9a1337e10ull,
      0xd8c8541f3519b133ull, 0xe75b5162c567b9e4ull, 0xf732e5ded7009c5bull,
      0xb170b98353121eacull, 0x1ec2e8986d2362caull, 0x814c8e35fe9a961aull,
      0x0c3cd59c9b638a02ull, 0xcb3bb6478a07715cull, 0x1224e62c978bbc7full,
      0x671ef2cb04e81f6eull, 0x3c1cbd811eaf1808ull, 0x1bbc23cfa8fac721ull,
      0xa4c2cda65e596a51ull, 0xb77216fad37adf91ull, 0x836d794457c08849ull,
      0xe083df03475f49d7ull, 0xbc9feb512e6b0d6cull, 0xb12d74fdd718c8c5ull,
      0x12ff09653bfbe4caull, 0x8dd03a105bc4ee7eull, 0x5738341045ba0d85ull,
      0xf3fd722dc65ad09eull, 0xfa14fd21ea2a5705ull, 0xffe6ea4d6edb0c73ull,
      0xD07E9EFE2BF11FB4ull, 0x95DBDA4DAE909198ull, 0xEAAD8E716B93D5A0ull,
      0xD08ED1D0AFC725E0ull, 0x8E3C5B2F8E7594B7ull, 0x8FF6E2FBF2122B64ull,
      0x8888B812900DF01Cull, 0x4FAD5EA0688FC31Cull, 0xD1CFF191B3A8C1ADull,
      0x2F2F2218BE0E1777ull, 0xEA752DFE8B021FA1ull, 0xE5A0CC0FB56F74E8ull,
      0x18ACF3D6CE89E299ull, 0xB4A84FE0FD13E0B7ull, 0x7CC43B81D2ADA8D9ull,
      0x165FA26680957705ull, 0x93CC7314211A1477ull, 0xE6AD206577B5FA86ull,
      0xC75442F5FB9D35CFull, 0xEBCDAF0C7B3E89A0ull, 0xD6411BD3AE1E7E49ull,
      0x00250E2D2071B35Eull, 0x226800BB57B8E0AFull, 0x2464369BF009B91Eull,
      0x5563911D59DFA6AAull, 0x78C14389D95A537Full, 0x207D5BA202E5B9C5ull,
      0x832603766295CFA9ull, 0x11C819684E734A41ull, 0xB3472DCA7B14A94Aull,
  });

  std::vector<int> output(10);

  // Method 1.
  {
    absl::poisson_distribution<int> dist(5);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
  }
  EXPECT_THAT(output,  // mean = 4.2
              ElementsAre(1, 0, 0, 4, 2, 10, 3, 3, 7, 12));

  // Method 2.
  {
    urbg.reset();
    absl::poisson_distribution<int> dist(25);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
  }
  EXPECT_THAT(output,  // mean = 19.8
              ElementsAre(9, 35, 18, 10, 35, 18, 10, 35, 18, 10));

  // Method 3.
  {
    urbg.reset();
    absl::poisson_distribution<int> dist(121);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
  }
  EXPECT_THAT(output,  // mean = 124.1
              ElementsAre(161, 122, 129, 124, 112, 112, 117, 120, 130, 114));
}

TEST(PoissonDistributionTest, AlgorithmExpectedValue_1) {
  // This tests small values of the Knuth method.
  // The underlying uniform distribution will generate exactly 0.5.
  absl::random_internal::sequence_urbg urbg({0x8000000000000001ull});
  absl::poisson_distribution<int> dist(5);
  EXPECT_EQ(7, dist(urbg));
}

TEST(PoissonDistributionTest, AlgorithmExpectedValue_2) {
  // This tests larger values of the Knuth method.
  // The underlying uniform distribution will generate exactly 0.5.
  absl::random_internal::sequence_urbg urbg({0x8000000000000001ull});
  absl::poisson_distribution<int> dist(25);
  EXPECT_EQ(36, dist(urbg));
}

TEST(PoissonDistributionTest, AlgorithmExpectedValue_3) {
  // This variant uses the ratio of uniforms method.
  absl::random_internal::sequence_urbg urbg(
      {0x7fffffffffffffffull, 0x8000000000000000ull});

  absl::poisson_distribution<int> dist(121);
  EXPECT_EQ(121, dist(urbg));
}

}  // namespace
