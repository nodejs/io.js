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

#include "absl/random/beta_distribution.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/numeric/internal/representation.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

namespace {

template <typename IntType>
class BetaDistributionInterfaceTest : public ::testing::Test {};

constexpr bool ShouldExerciseLongDoubleTests() {
  // long double arithmetic is not supported well by either GCC or Clang on
  // most platforms specifically not when implemented in terms of double-double;
  // see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99048,
  // https://bugs.llvm.org/show_bug.cgi?id=49131, and
  // https://bugs.llvm.org/show_bug.cgi?id=49132.
  // So a conservative choice here is to disable long-double tests pretty much
  // everywhere except on x64 but only if long double is not implemented as
  // double-double.
#if defined(__i686__) && defined(__x86_64__)
  return !absl::numeric_internal::IsDoubleDouble();
#else
  return false;
#endif
}

using RealTypes = std::conditional<ShouldExerciseLongDoubleTests(),
                                   ::testing::Types<float, double, long double>,
                                   ::testing::Types<float, double>>::type;
TYPED_TEST_SUITE(BetaDistributionInterfaceTest, RealTypes);

TYPED_TEST(BetaDistributionInterfaceTest, SerializeTest) {
  // The threshold for whether std::exp(1/a) is finite.
  const TypeParam kSmallA =
      1.0f / std::log((std::numeric_limits<TypeParam>::max)());
  // The threshold for whether a * std::log(a) is finite.
  const TypeParam kLargeA =
      std::exp(std::log((std::numeric_limits<TypeParam>::max)()) -
               std::log(std::log((std::numeric_limits<TypeParam>::max)())));
  using param_type = typename absl::beta_distribution<TypeParam>::param_type;

  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;
  const TypeParam kValues[] = {
      TypeParam(1e-20), TypeParam(1e-12), TypeParam(1e-8), TypeParam(1e-4),
      TypeParam(1e-3), TypeParam(0.1), TypeParam(0.25),
      std::nextafter(TypeParam(0.5), TypeParam(0)),  // 0.5 - epsilon
      std::nextafter(TypeParam(0.5), TypeParam(1)),  // 0.5 + epsilon
      TypeParam(0.5), TypeParam(1.0),                //
      std::nextafter(TypeParam(1), TypeParam(0)),    // 1 - epsilon
      std::nextafter(TypeParam(1), TypeParam(2)),    // 1 + epsilon
      TypeParam(12.5), TypeParam(1e2), TypeParam(1e8), TypeParam(1e12),
      TypeParam(1e20),                        //
      kSmallA,                                //
      std::nextafter(kSmallA, TypeParam(0)),  //
      std::nextafter(kSmallA, TypeParam(1)),  //
      kLargeA,                                //
      std::nextafter(kLargeA, TypeParam(0)),  //
      std::nextafter(kLargeA, std::numeric_limits<TypeParam>::max()),
      // Boundary cases.
      std::numeric_limits<TypeParam>::max(),
      std::numeric_limits<TypeParam>::epsilon(),
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(1)),                  // min + epsilon
      std::numeric_limits<TypeParam>::min(),         // smallest normal
      std::numeric_limits<TypeParam>::denorm_min(),  // smallest denorm
      std::numeric_limits<TypeParam>::min() / 2,     // denorm
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(0)),  // denorm_max
  };
  for (TypeParam alpha : kValues) {
    for (TypeParam beta : kValues) {
      LOG(INFO) << absl::StreamFormat("Smoke test for Beta(%a, %a)", alpha,
                                      beta);

      param_type param(alpha, beta);
      absl::beta_distribution<TypeParam> before(alpha, beta);
      EXPECT_EQ(before.alpha(), param.alpha());
      EXPECT_EQ(before.beta(), param.beta());

      {
        absl::beta_distribution<TypeParam> via_param(param);
        EXPECT_EQ(via_param, before);
        EXPECT_EQ(via_param.param(), before.param());
      }

      // Smoke test.
      for (int i = 0; i < kCount; ++i) {
        auto sample = before(gen);
        EXPECT_TRUE(std::isfinite(sample));
        EXPECT_GE(sample, before.min());
        EXPECT_LE(sample, before.max());
      }

      // Validate stream serialization.
      std::stringstream ss;
      ss << before;
      absl::beta_distribution<TypeParam> after(3.8f, 1.43f);
      EXPECT_NE(before.alpha(), after.alpha());
      EXPECT_NE(before.beta(), after.beta());
      EXPECT_NE(before.param(), after.param());
      EXPECT_NE(before, after);

      ss >> after;

      EXPECT_EQ(before.alpha(), after.alpha());
      EXPECT_EQ(before.beta(), after.beta());
      EXPECT_EQ(before, after)           //
          << ss.str() << " "             //
          << (ss.good() ? "good " : "")  //
          << (ss.bad() ? "bad " : "")    //
          << (ss.eof() ? "eof " : "")    //
          << (ss.fail() ? "fail " : "");
    }
  }
}

TYPED_TEST(BetaDistributionInterfaceTest, DegenerateCases) {
  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng(0x2B7E151628AED2A6);

  // Extreme cases when the params are abnormal.
  constexpr int kCount = 1000;
  const TypeParam kSmallValues[] = {
      std::numeric_limits<TypeParam>::min(),
      std::numeric_limits<TypeParam>::denorm_min(),
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(0)),  // denorm_max
      std::numeric_limits<TypeParam>::epsilon(),
  };
  const TypeParam kLargeValues[] = {
      std::numeric_limits<TypeParam>::max() * static_cast<TypeParam>(0.9999),
      std::numeric_limits<TypeParam>::max() - 1,
      std::numeric_limits<TypeParam>::max(),
  };
  {
    // Small alpha and beta.
    // Useful WolframAlpha plots:
    //   * plot InverseBetaRegularized[x, 0.0001, 0.0001] from 0.495 to 0.505
    //   * Beta[1.0, 0.0000001, 0.0000001]
    //   * Beta[0.9999, 0.0000001, 0.0000001]
    for (TypeParam alpha : kSmallValues) {
      for (TypeParam beta : kSmallValues) {
        int zeros = 0;
        int ones = 0;
        absl::beta_distribution<TypeParam> d(alpha, beta);
        for (int i = 0; i < kCount; ++i) {
          TypeParam x = d(rng);
          if (x == 0.0) {
            zeros++;
          } else if (x == 1.0) {
            ones++;
          }
        }
        EXPECT_EQ(ones + zeros, kCount);
        if (alpha == beta) {
          EXPECT_NE(ones, 0);
          EXPECT_NE(zeros, 0);
        }
      }
    }
  }
  {
    // Small alpha, large beta.
    // Useful WolframAlpha plots:
    //   * plot InverseBetaRegularized[x, 0.0001, 10000] from 0.995 to 1
    //   * Beta[0, 0.0000001, 1000000]
    //   * Beta[0.001, 0.0000001, 1000000]
    //   * Beta[1, 0.0000001, 1000000]
    for (TypeParam alpha : kSmallValues) {
      for (TypeParam beta : kLargeValues) {
        absl::beta_distribution<TypeParam> d(alpha, beta);
        for (int i = 0; i < kCount; ++i) {
          EXPECT_EQ(d(rng), 0.0);
        }
      }
    }
  }
  {
    // Large alpha, small beta.
    // Useful WolframAlpha plots:
    //   * plot InverseBetaRegularized[x, 10000, 0.0001] from 0 to 0.001
    //   * Beta[0.99, 1000000, 0.0000001]
    //   * Beta[1, 1000000, 0.0000001]
    for (TypeParam alpha : kLargeValues) {
      for (TypeParam beta : kSmallValues) {
        absl::beta_distribution<TypeParam> d(alpha, beta);
        for (int i = 0; i < kCount; ++i) {
          EXPECT_EQ(d(rng), 1.0);
        }
      }
    }
  }
  {
    // Large alpha and beta.
    absl::beta_distribution<TypeParam> d(std::numeric_limits<TypeParam>::max(),
                                         std::numeric_limits<TypeParam>::max());
    for (int i = 0; i < kCount; ++i) {
      EXPECT_EQ(d(rng), 0.5);
    }
  }
  {
    // Large alpha and beta but unequal.
    absl::beta_distribution<TypeParam> d(
        std::numeric_limits<TypeParam>::max(),
        std::numeric_limits<TypeParam>::max() * 0.9999);
    for (int i = 0; i < kCount; ++i) {
      TypeParam x = d(rng);
      EXPECT_NE(x, 0.5f);
      EXPECT_FLOAT_EQ(x, 0.500025f);
    }
  }
}

class BetaDistributionModel {
 public:
  explicit BetaDistributionModel(::testing::tuple<double, double> p)
      : alpha_(::testing::get<0>(p)), beta_(::testing::get<1>(p)) {}

  double Mean() const { return alpha_ / (alpha_ + beta_); }

  double Variance() const {
    return alpha_ * beta_ / (alpha_ + beta_ + 1) / (alpha_ + beta_) /
           (alpha_ + beta_);
  }

  double Kurtosis() const {
    return 3 + 6 *
                   ((alpha_ - beta_) * (alpha_ - beta_) * (alpha_ + beta_ + 1) -
                    alpha_ * beta_ * (2 + alpha_ + beta_)) /
                   alpha_ / beta_ / (alpha_ + beta_ + 2) / (alpha_ + beta_ + 3);
  }

 protected:
  const double alpha_;
  const double beta_;
};

class BetaDistributionTest
    : public ::testing::TestWithParam<::testing::tuple<double, double>>,
      public BetaDistributionModel {
 public:
  BetaDistributionTest() : BetaDistributionModel(GetParam()) {}

 protected:
  template <class D>
  bool SingleZTestOnMeanAndVariance(double p, size_t samples);

  template <class D>
  bool SingleChiSquaredTest(double p, size_t samples, size_t buckets);

  absl::InsecureBitGen rng_;
};

template <class D>
bool BetaDistributionTest::SingleZTestOnMeanAndVariance(double p,
                                                        size_t samples) {
  D dis(alpha_, beta_);

  std::vector<double> data;
  data.reserve(samples);
  for (size_t i = 0; i < samples; i++) {
    const double variate = dis(rng_);
    EXPECT_FALSE(std::isnan(variate));
    // Note that equality is allowed on both sides.
    EXPECT_GE(variate, 0.0);
    EXPECT_LE(variate, 1.0);
    data.push_back(variate);
  }

  // We validate that the sample mean and sample variance are indeed from a
  // Beta distribution with the given shape parameters.
  const auto m = absl::random_internal::ComputeDistributionMoments(data);

  // The variance of the sample mean is variance / n.
  const double mean_stddev = std::sqrt(Variance() / static_cast<double>(m.n));

  // The variance of the sample variance is (approximately):
  //   (kurtosis - 1) * variance^2 / n
  const double variance_stddev = std::sqrt(
      (Kurtosis() - 1) * Variance() * Variance() / static_cast<double>(m.n));
  // z score for the sample variance.
  const double z_variance = (m.variance - Variance()) / variance_stddev;

  const double max_err = absl::random_internal::MaxErrorTolerance(p);
  const double z_mean = absl::random_internal::ZScore(Mean(), m);
  const bool pass =
      absl::random_internal::Near("z", z_mean, 0.0, max_err) &&
      absl::random_internal::Near("z_variance", z_variance, 0.0, max_err);
  if (!pass) {
    LOG(INFO) << "Beta(" << alpha_ << ", " << beta_ << "), mean: sample "
              << m.mean << ", expect " << Mean() << ", which is "
              << std::abs(m.mean - Mean()) / mean_stddev
              << " stddevs away, variance: sample " << m.variance << ", expect "
              << Variance() << ", which is "
              << std::abs(m.variance - Variance()) / variance_stddev
              << " stddevs away.";
  }
  return pass;
}

template <class D>
bool BetaDistributionTest::SingleChiSquaredTest(double p, size_t samples,
                                                size_t buckets) {
  constexpr double kErr = 1e-7;
  std::vector<double> cutoffs, expected;
  const double bucket_width = 1.0 / static_cast<double>(buckets);
  int i = 1;
  int unmerged_buckets = 0;
  for (; i < buckets; ++i) {
    const double p = bucket_width * static_cast<double>(i);
    const double boundary =
        absl::random_internal::BetaIncompleteInv(alpha_, beta_, p);
    // The intention is to add `boundary` to the list of `cutoffs`. It becomes
    // problematic, however, when the boundary values are not monotone, due to
    // numerical issues when computing the inverse regularized incomplete
    // Beta function. In these cases, we merge that bucket with its previous
    // neighbor and merge their expected counts.
    if ((cutoffs.empty() && boundary < kErr) ||
        (!cutoffs.empty() && boundary <= cutoffs.back())) {
      unmerged_buckets++;
      continue;
    }
    if (boundary >= 1.0 - 1e-10) {
      break;
    }
    cutoffs.push_back(boundary);
    expected.push_back(static_cast<double>(1 + unmerged_buckets) *
                       bucket_width * static_cast<double>(samples));
    unmerged_buckets = 0;
  }
  cutoffs.push_back(std::numeric_limits<double>::infinity());
  // Merge all remaining buckets.
  expected.push_back(static_cast<double>(buckets - i + 1) * bucket_width *
                     static_cast<double>(samples));
  // Make sure that we don't merge all the buckets, making this test
  // meaningless.
  EXPECT_GE(cutoffs.size(), 3) << alpha_ << ", " << beta_;

  D dis(alpha_, beta_);

  std::vector<int32_t> counts(cutoffs.size(), 0);
  for (int i = 0; i < samples; i++) {
    const double x = dis(rng_);
    auto it = std::upper_bound(cutoffs.begin(), cutoffs.end(), x);
    counts[std::distance(cutoffs.begin(), it)]++;
  }

  // Null-hypothesis is that the distribution is beta distributed with the
  // provided alpha, beta params (not estimated from the data).
  const int dof = cutoffs.size() - 1;

  const double chi_square = absl::random_internal::ChiSquare(
      counts.begin(), counts.end(), expected.begin(), expected.end());
  const bool pass =
      (absl::random_internal::ChiSquarePValue(chi_square, dof) >= p);
  if (!pass) {
    for (size_t i = 0; i < cutoffs.size(); i++) {
      LOG(INFO) << "cutoff[" << i << "] = " << cutoffs[i] << ", actual count "
                << counts[i] << ", expected " << static_cast<int>(expected[i]);
    }

    LOG(INFO) << "Beta(" << alpha_ << ", " << beta_ << ") "
              << absl::random_internal::kChiSquared << " " << chi_square
              << ", p = "
              << absl::random_internal::ChiSquarePValue(chi_square, dof);
  }
  return pass;
}

TEST_P(BetaDistributionTest, TestSampleStatistics) {
  static constexpr int kRuns = 20;
  static constexpr double kPFail = 0.02;
  const double p =
      absl::random_internal::RequiredSuccessProbability(kPFail, kRuns);
  static constexpr int kSampleCount = 10000;
  static constexpr int kBucketCount = 100;
  int failed = 0;
  for (int i = 0; i < kRuns; ++i) {
    if (!SingleZTestOnMeanAndVariance<absl::beta_distribution<double>>(
            p, kSampleCount)) {
      failed++;
    }
    if (!SingleChiSquaredTest<absl::beta_distribution<double>>(
            0.005, kSampleCount, kBucketCount)) {
      failed++;
    }
  }
  // Set so that the test is not flaky at --runs_per_test=10000
  EXPECT_LE(failed, 5);
}

std::string ParamName(
    const ::testing::TestParamInfo<::testing::tuple<double, double>>& info) {
  std::string name = absl::StrCat("alpha_", ::testing::get<0>(info.param),
                                  "__beta_", ::testing::get<1>(info.param));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(
    TestSampleStatisticsCombinations, BetaDistributionTest,
    ::testing::Combine(::testing::Values(0.1, 0.2, 0.9, 1.1, 2.5, 10.0, 123.4),
                       ::testing::Values(0.1, 0.2, 0.9, 1.1, 2.5, 10.0, 123.4)),
    ParamName);

INSTANTIATE_TEST_SUITE_P(
    TestSampleStatistics_SelectedPairs, BetaDistributionTest,
    ::testing::Values(std::make_pair(0.5, 1000), std::make_pair(1000, 0.5),
                      std::make_pair(900, 1000), std::make_pair(10000, 20000),
                      std::make_pair(4e5, 2e7), std::make_pair(1e7, 1e5)),
    ParamName);

// NOTE: absl::beta_distribution is not guaranteed to be stable.
TEST(BetaDistributionTest, StabilityTest) {
  // absl::beta_distribution stability relies on the stability of
  // absl::random_interna::RandU64ToDouble, std::exp, std::log, std::pow,
  // and std::sqrt.
  //
  // This test also depends on the stability of std::frexp.
  using testing::ElementsAre;
  absl::random_internal::sequence_urbg urbg({
      0xffff00000000e6c8ull, 0xffff0000000006c8ull, 0x800003766295CFA9ull,
      0x11C819684E734A41ull, 0x832603766295CFA9ull, 0x7fbe76c8b4395800ull,
      0xB3472DCA7B14A94Aull, 0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull,
      0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull, 0x00035C904C70A239ull,
      0x00009E0BCBAADE14ull, 0x0000000000622CA7ull, 0x4864f22c059bf29eull,
      0x247856d8b862665cull, 0xe46e86e9a1337e10ull, 0xd8c8541f3519b133ull,
      0xffe75b52c567b9e4ull, 0xfffff732e5709c5bull, 0xff1f7f0b983532acull,
      0x1ec2e8986d2362caull, 0xC332DDEFBE6C5AA5ull, 0x6558218568AB9702ull,
      0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull, 0xECDD4775619F1510ull,
      0x814c8e35fe9a961aull, 0x0c3cd59c9b638a02ull, 0xcb3bb6478a07715cull,
      0x1224e62c978bbc7full, 0x671ef2cb04e81f6eull, 0x3c1cbd811eaf1808ull,
      0x1bbc23cfa8fac721ull, 0xa4c2cda65e596a51ull, 0xb77216fad37adf91ull,
      0x836d794457c08849ull, 0xe083df03475f49d7ull, 0xbc9feb512e6b0d6cull,
      0xb12d74fdd718c8c5ull, 0x12ff09653bfbe4caull, 0x8dd03a105bc4ee7eull,
      0x5738341045ba0d85ull, 0xf3fd722dc65ad09eull, 0xfa14fd21ea2a5705ull,
      0xffe6ea4d6edb0c73ull, 0xD07E9EFE2BF11FB4ull, 0x95DBDA4DAE909198ull,
      0xEAAD8E716B93D5A0ull, 0xD08ED1D0AFC725E0ull, 0x8E3C5B2F8E7594B7ull,
      0x8FF6E2FBF2122B64ull, 0x8888B812900DF01Cull, 0x4FAD5EA0688FC31Cull,
      0xD1CFF191B3A8C1ADull, 0x2F2F2218BE0E1777ull, 0xEA752DFE8B021FA1ull,
  });

  // Convert the real-valued result into a unit64 where we compare
  // 5 (float) or 10 (double) decimal digits plus the base-2 exponent.
  auto float_to_u64 = [](float d) {
    int exp = 0;
    auto f = std::frexp(d, &exp);
    return (static_cast<uint64_t>(1e5 * f) * 10000) + std::abs(exp);
  };
  auto double_to_u64 = [](double d) {
    int exp = 0;
    auto f = std::frexp(d, &exp);
    return (static_cast<uint64_t>(1e10 * f) * 10000) + std::abs(exp);
  };

  std::vector<uint64_t> output(20);
  {
    // Algorithm Joehnk (float)
    absl::beta_distribution<float> dist(0.1f, 0.2f);
    std::generate(std::begin(output), std::end(output),
                  [&] { return float_to_u64(dist(urbg)); });
    EXPECT_EQ(44, urbg.invocations());
    EXPECT_THAT(output,  //
                testing::ElementsAre(
                    998340000, 619030004, 500000001, 999990000, 996280000,
                    500000001, 844740004, 847210001, 999970000, 872320000,
                    585480007, 933280000, 869080042, 647670031, 528240004,
                    969980004, 626050008, 915930002, 833440033, 878040015));
  }

  urbg.reset();
  {
    // Algorithm Joehnk (double)
    absl::beta_distribution<double> dist(0.1, 0.2);
    std::generate(std::begin(output), std::end(output),
                  [&] { return double_to_u64(dist(urbg)); });
    EXPECT_EQ(44, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(
            99834713000000, 61903356870004, 50000000000001, 99999721170000,
            99628374770000, 99999999990000, 84474397860004, 84721276240001,
            99997407490000, 87232528120000, 58548364780007, 93328932910000,
            86908237770042, 64767917930031, 52824581970004, 96998544140004,
            62605946270008, 91593604380002, 83345031740033, 87804397230015));
  }

  urbg.reset();
  {
    // Algorithm Cheng 1
    absl::beta_distribution<double> dist(0.9, 2.0);
    std::generate(std::begin(output), std::end(output),
                  [&] { return double_to_u64(dist(urbg)); });
    EXPECT_EQ(62, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(
            62069004780001, 64433204450001, 53607416560000, 89644295430008,
            61434586310019, 55172615890002, 62187161490000, 56433684810003,
            80454622050005, 86418558710003, 92920514700001, 64645184680001,
            58549183380000, 84881283650005, 71078728590002, 69949694970000,
            73157461710001, 68592191300001, 70747623900000, 78584696930005));
  }

  urbg.reset();
  {
    // Algorithm Cheng 2
    absl::beta_distribution<double> dist(1.5, 2.5);
    std::generate(std::begin(output), std::end(output),
                  [&] { return double_to_u64(dist(urbg)); });
    EXPECT_EQ(54, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(
            75000029250001, 76751482860001, 53264575220000, 69193133650005,
            78028324470013, 91573587560002, 59167523770000, 60658618560002,
            80075870540000, 94141320460004, 63196592770003, 78883906300002,
            96797992590001, 76907587800001, 56645167560000, 65408302280003,
            53401156320001, 64731238570000, 83065573750001, 79788333820001));
  }
}

// This is an implementation-specific test. If any part of the implementation
// changes, then it is likely that this test will change as well.  Also, if
// dependencies of the distribution change, such as RandU64ToDouble, then this
// is also likely to change.
TEST(BetaDistributionTest, AlgorithmBounds) {
#if (defined(__i386__) || defined(_M_IX86)) && FLT_EVAL_METHOD != 0
  // We're using an x87-compatible FPU, and intermediate operations are
  // performed with 80-bit floats. This produces slightly different results from
  // what we expect below.
  GTEST_SKIP()
      << "Skipping the test because we detected x87 floating-point semantics";
#endif

  {
    absl::random_internal::sequence_urbg urbg(
        {0x7fbe76c8b4395800ull, 0x8000000000000000ull});
    // u=0.499, v=0.5
    absl::beta_distribution<double> dist(1e-4, 1e-4);
    double a = dist(urbg);
    EXPECT_EQ(a, 2.0202860861567108529e-09);
    EXPECT_EQ(2, urbg.invocations());
  }

  // Test that both the float & double algorithms appropriately reject the
  // initial draw.
  {
    // 1/alpha = 1/beta = 2.
    absl::beta_distribution<float> dist(0.5, 0.5);

    // first two outputs are close to 1.0 - epsilon,
    // thus:  (u ^ 2 + v ^ 2) > 1.0
    absl::random_internal::sequence_urbg urbg(
        {0xffff00000006e6c8ull, 0xffff00000007c7c8ull, 0x800003766295CFA9ull,
         0x11C819684E734A41ull});
    {
      double y = absl::beta_distribution<double>(0.5, 0.5)(urbg);
      EXPECT_EQ(4, urbg.invocations());
      EXPECT_EQ(y, 0.9810668952633862) << y;
    }

    // ...and:  log(u) * a ~= log(v) * b ~= -0.02
    // thus z ~= -0.02 + log(1 + e(~0))
    //        ~= -0.02 + 0.69
    // thus z > 0
    urbg.reset();
    {
      float x = absl::beta_distribution<float>(0.5, 0.5)(urbg);
      EXPECT_EQ(4, urbg.invocations());
      EXPECT_NEAR(0.98106688261032104, x, 0.0000005) << x << "f";
    }
  }
}

}  // namespace
