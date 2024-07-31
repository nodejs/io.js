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

#include "absl/random/gaussian_distribution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ios>
#include <iterator>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/log/log.h"
#include "absl/numeric/internal/representation.h"
#include "absl/random/internal/chi_square.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/strip.h"

namespace {

using absl::random_internal::kChiSquared;

template <typename RealType>
class GaussianDistributionInterfaceTest : public ::testing::Test {};

// double-double arithmetic is not supported well by either GCC or Clang; see
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99048,
// https://bugs.llvm.org/show_bug.cgi?id=49131, and
// https://bugs.llvm.org/show_bug.cgi?id=49132. Don't bother running these tests
// with double doubles until compiler support is better.
using RealTypes =
    std::conditional<absl::numeric_internal::IsDoubleDouble(),
                     ::testing::Types<float, double>,
                     ::testing::Types<float, double, long double>>::type;
TYPED_TEST_SUITE(GaussianDistributionInterfaceTest, RealTypes);

TYPED_TEST(GaussianDistributionInterfaceTest, SerializeTest) {
  using param_type =
      typename absl::gaussian_distribution<TypeParam>::param_type;

  const TypeParam kParams[] = {
      // Cases around 1.
      1,                                           //
      std::nextafter(TypeParam(1), TypeParam(0)),  // 1 - epsilon
      std::nextafter(TypeParam(1), TypeParam(2)),  // 1 + epsilon
      // Arbitrary values.
      TypeParam(1e-8), TypeParam(1e-4), TypeParam(2), TypeParam(1e4),
      TypeParam(1e8), TypeParam(1e20), TypeParam(2.5),
      // Boundary cases.
      std::numeric_limits<TypeParam>::infinity(),
      std::numeric_limits<TypeParam>::max(),
      std::numeric_limits<TypeParam>::epsilon(),
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(1)),           // min + epsilon
      std::numeric_limits<TypeParam>::min(),  // smallest normal
      // There are some errors dealing with denorms on apple platforms.
      std::numeric_limits<TypeParam>::denorm_min(),  // smallest denorm
      std::numeric_limits<TypeParam>::min() / 2,
      std::nextafter(std::numeric_limits<TypeParam>::min(),
                     TypeParam(0)),  // denorm_max
  };

  constexpr int kCount = 1000;
  absl::InsecureBitGen gen;

  // Use a loop to generate the combinations of {+/-x, +/-y}, and assign x, y to
  // all values in kParams,
  for (const auto mod : {0, 1, 2, 3}) {
    for (const auto x : kParams) {
      if (!std::isfinite(x)) continue;
      for (const auto y : kParams) {
        const TypeParam mean = (mod & 0x1) ? -x : x;
        const TypeParam stddev = (mod & 0x2) ? -y : y;
        const param_type param(mean, stddev);

        absl::gaussian_distribution<TypeParam> before(mean, stddev);
        EXPECT_EQ(before.mean(), param.mean());
        EXPECT_EQ(before.stddev(), param.stddev());

        {
          absl::gaussian_distribution<TypeParam> via_param(param);
          EXPECT_EQ(via_param, before);
          EXPECT_EQ(via_param.param(), before.param());
        }

        // Smoke test.
        auto sample_min = before.max();
        auto sample_max = before.min();
        for (int i = 0; i < kCount; i++) {
          auto sample = before(gen);
          if (sample > sample_max) sample_max = sample;
          if (sample < sample_min) sample_min = sample;
          EXPECT_GE(sample, before.min()) << before;
          EXPECT_LE(sample, before.max()) << before;
        }
        if (!std::is_same<TypeParam, long double>::value) {
          LOG(INFO) << "Range{" << mean << ", " << stddev << "}: " << sample_min
                    << ", " << sample_max;
        }

        std::stringstream ss;
        ss << before;

        if (!std::isfinite(mean) || !std::isfinite(stddev)) {
          // Streams do not parse inf/nan.
          continue;
        }

        // Validate stream serialization.
        absl::gaussian_distribution<TypeParam> after(-0.53f, 2.3456f);

        EXPECT_NE(before.mean(), after.mean());
        EXPECT_NE(before.stddev(), after.stddev());
        EXPECT_NE(before.param(), after.param());
        EXPECT_NE(before, after);

        ss >> after;

        EXPECT_EQ(before.mean(), after.mean());
        EXPECT_EQ(before.stddev(), after.stddev())  //
            << ss.str() << " "                      //
            << (ss.good() ? "good " : "")           //
            << (ss.bad() ? "bad " : "")             //
            << (ss.eof() ? "eof " : "")             //
            << (ss.fail() ? "fail " : "");
      }
    }
  }
}

// http://www.itl.nist.gov/div898/handbook/eda/section3/eda3661.htm

class GaussianModel {
 public:
  GaussianModel(double mean, double stddev) : mean_(mean), stddev_(stddev) {}

  double mean() const { return mean_; }
  double variance() const { return stddev() * stddev(); }
  double stddev() const { return stddev_; }
  double skew() const { return 0; }
  double kurtosis() const { return 3.0; }

  // The inverse CDF, or PercentPoint function.
  double InverseCDF(double p) {
    ABSL_ASSERT(p >= 0.0);
    ABSL_ASSERT(p < 1.0);
    return mean() + stddev() * -absl::random_internal::InverseNormalSurvival(p);
  }

 private:
  const double mean_;
  const double stddev_;
};

struct Param {
  double mean;
  double stddev;
  double p_fail;  // Z-Test probability of failure.
  int trials;     // Z-Test trials.
};

// GaussianDistributionTests implements a z-test for the gaussian
// distribution.
class GaussianDistributionTests : public testing::TestWithParam<Param>,
                                  public GaussianModel {
 public:
  GaussianDistributionTests()
      : GaussianModel(GetParam().mean, GetParam().stddev) {}

  // SingleZTest provides a basic z-squared test of the mean vs. expected
  // mean for data generated by the poisson distribution.
  template <typename D>
  bool SingleZTest(const double p, const size_t samples);

  // SingleChiSquaredTest provides a basic chi-squared test of the normal
  // distribution.
  template <typename D>
  double SingleChiSquaredTest();

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
};

template <typename D>
bool GaussianDistributionTests::SingleZTest(const double p,
                                            const size_t samples) {
  D dis(mean(), stddev());

  std::vector<double> data;
  data.reserve(samples);
  for (size_t i = 0; i < samples; i++) {
    const double x = dis(rng_);
    data.push_back(x);
  }

  const double max_err = absl::random_internal::MaxErrorTolerance(p);
  const auto m = absl::random_internal::ComputeDistributionMoments(data);
  const double z = absl::random_internal::ZScore(mean(), m);
  const bool pass = absl::random_internal::Near("z", z, 0.0, max_err);

  // NOTE: Informational statistical test:
  //
  // Compute the Jarque-Bera test statistic given the excess skewness
  // and kurtosis. The statistic is drawn from a chi-square(2) distribution.
  // https://en.wikipedia.org/wiki/Jarque%E2%80%93Bera_test
  //
  // The null-hypothesis (normal distribution) is rejected when
  // (p = 0.05 => jb > 5.99)
  // (p = 0.01 => jb > 9.21)
  // NOTE: JB has a large type-I error rate, so it will reject the
  // null-hypothesis even when it is true more often than the z-test.
  //
  const double jb =
      static_cast<double>(m.n) / 6.0 *
      (std::pow(m.skewness, 2.0) + std::pow(m.kurtosis - 3.0, 2.0) / 4.0);

  if (!pass || jb > 9.21) {
    // clang-format off
    LOG(INFO)
        << "p=" << p << " max_err=" << max_err << "\n"
           " mean=" << m.mean << " vs. " << mean() << "\n"
           " stddev=" << std::sqrt(m.variance) << " vs. " << stddev() << "\n"
           " skewness=" << m.skewness << " vs. " << skew() << "\n"
           " kurtosis=" << m.kurtosis << " vs. " << kurtosis() << "\n"
           " z=" << z << " vs. 0\n"
           " jb=" << jb << " vs. 9.21";
    // clang-format on
  }
  return pass;
}

template <typename D>
double GaussianDistributionTests::SingleChiSquaredTest() {
  const size_t kSamples = 10000;
  const int kBuckets = 50;

  // The InverseCDF is the percent point function of the
  // distribution, and can be used to assign buckets
  // roughly uniformly.
  std::vector<double> cutoffs;
  const double kInc = 1.0 / static_cast<double>(kBuckets);
  for (double p = kInc; p < 1.0; p += kInc) {
    cutoffs.push_back(InverseCDF(p));
  }
  if (cutoffs.back() != std::numeric_limits<double>::infinity()) {
    cutoffs.push_back(std::numeric_limits<double>::infinity());
  }

  D dis(mean(), stddev());

  std::vector<int32_t> counts(cutoffs.size(), 0);
  for (int j = 0; j < kSamples; j++) {
    const double x = dis(rng_);
    auto it = std::upper_bound(cutoffs.begin(), cutoffs.end(), x);
    counts[std::distance(cutoffs.begin(), it)]++;
  }

  // Null-hypothesis is that the distribution is a gaussian distribution
  // with the provided mean and stddev (not estimated from the data).
  const int dof = static_cast<int>(counts.size()) - 1;

  // Our threshold for logging is 1-in-50.
  const double threshold = absl::random_internal::ChiSquareValue(dof, 0.98);

  const double expected =
      static_cast<double>(kSamples) / static_cast<double>(counts.size());

  double chi_square = absl::random_internal::ChiSquareWithExpected(
      std::begin(counts), std::end(counts), expected);
  double p = absl::random_internal::ChiSquarePValue(chi_square, dof);

  // Log if the chi_square value is above the threshold.
  if (chi_square > threshold) {
    for (size_t i = 0; i < cutoffs.size(); i++) {
      LOG(INFO) << i << " : (" << cutoffs[i] << ") = " << counts[i];
    }

    // clang-format off
    LOG(INFO) << "mean=" << mean() << " stddev=" << stddev() << "\n"
                 " expected " << expected << "\n"
              << kChiSquared << " " << chi_square << " (" << p << ")\n"
              << kChiSquared << " @ 0.98 = " << threshold;
    // clang-format on
  }
  return p;
}

TEST_P(GaussianDistributionTests, ZTest) {
  // TODO(absl-team): Run these tests against std::normal_distribution<double>
  // to validate outcomes are similar.
  const size_t kSamples = 10000;
  const auto& param = GetParam();
  const int expected_failures =
      std::max(1, static_cast<int>(std::ceil(param.trials * param.p_fail)));
  const double p = absl::random_internal::RequiredSuccessProbability(
      param.p_fail, param.trials);

  int failures = 0;
  for (int i = 0; i < param.trials; i++) {
    failures +=
        SingleZTest<absl::gaussian_distribution<double>>(p, kSamples) ? 0 : 1;
  }
  EXPECT_LE(failures, expected_failures);
}

TEST_P(GaussianDistributionTests, ChiSquaredTest) {
  const int kTrials = 20;
  int failures = 0;

  for (int i = 0; i < kTrials; i++) {
    double p_value =
        SingleChiSquaredTest<absl::gaussian_distribution<double>>();
    if (p_value < 0.0025) {  // 1/400
      failures++;
    }
  }
  // There is a 0.05% chance of producing at least one failure, so raise the
  // failure threshold high enough to allow for a flake rate of less than one in
  // 10,000.
  EXPECT_LE(failures, 4);
}

std::vector<Param> GenParams() {
  return {
      // Mean around 0.
      Param{0.0, 1.0, 0.01, 100},
      Param{0.0, 1e2, 0.01, 100},
      Param{0.0, 1e4, 0.01, 100},
      Param{0.0, 1e8, 0.01, 100},
      Param{0.0, 1e16, 0.01, 100},
      Param{0.0, 1e-3, 0.01, 100},
      Param{0.0, 1e-5, 0.01, 100},
      Param{0.0, 1e-9, 0.01, 100},
      Param{0.0, 1e-17, 0.01, 100},

      // Mean around 1.
      Param{1.0, 1.0, 0.01, 100},
      Param{1.0, 1e2, 0.01, 100},
      Param{1.0, 1e-2, 0.01, 100},

      // Mean around 100 / -100
      Param{1e2, 1.0, 0.01, 100},
      Param{-1e2, 1.0, 0.01, 100},
      Param{1e2, 1e6, 0.01, 100},
      Param{-1e2, 1e6, 0.01, 100},

      // More extreme
      Param{1e4, 1e4, 0.01, 100},
      Param{1e8, 1e4, 0.01, 100},
      Param{1e12, 1e4, 0.01, 100},
  };
}

std::string ParamName(const ::testing::TestParamInfo<Param>& info) {
  const auto& p = info.param;
  std::string name = absl::StrCat("mean_", absl::SixDigits(p.mean), "__stddev_",
                                  absl::SixDigits(p.stddev));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(All, GaussianDistributionTests,
                         ::testing::ValuesIn(GenParams()), ParamName);

// NOTE: absl::gaussian_distribution is not guaranteed to be stable.
TEST(GaussianDistributionTest, StabilityTest) {
  // absl::gaussian_distribution stability relies on the underlying zignor
  // data, absl::random_interna::RandU64ToDouble, std::exp, std::log, and
  // std::abs.
  absl::random_internal::sequence_urbg urbg(
      {0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
       0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
       0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
       0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull});

  std::vector<int> output(11);

  {
    absl::gaussian_distribution<double> dist;
    std::generate(std::begin(output), std::end(output),
                  [&] { return static_cast<int>(10000000.0 * dist(urbg)); });

    EXPECT_EQ(13, urbg.invocations());
    EXPECT_THAT(output,  //
                testing::ElementsAre(1494, 25518841, 9991550, 1351856,
                                     -20373238, 3456682, 333530, -6804981,
                                     -15279580, -16459654, 1494));
  }

  urbg.reset();
  {
    absl::gaussian_distribution<float> dist;
    std::generate(std::begin(output), std::end(output),
                  [&] { return static_cast<int>(1000000.0f * dist(urbg)); });

    EXPECT_EQ(13, urbg.invocations());
    EXPECT_THAT(
        output,  //
        testing::ElementsAre(149, 2551884, 999155, 135185, -2037323, 345668,
                             33353, -680498, -1527958, -1645965, 149));
  }
}

// This is an implementation-specific test. If any part of the implementation
// changes, then it is likely that this test will change as well.
// Also, if dependencies of the distribution change, such as RandU64ToDouble,
// then this is also likely to change.
TEST(GaussianDistributionTest, AlgorithmBounds) {
  absl::gaussian_distribution<double> dist;

  // In ~95% of cases, a single value is used to generate the output.
  // for all inputs where |x| < 0.750461021389 this should be the case.
  //
  // The exact constraints are based on the ziggurat tables, and any
  // changes to the ziggurat tables may require adjusting these bounds.
  //
  // for i in range(0, len(X)-1):
  //   print i, X[i+1]/X[i], (X[i+1]/X[i] > 0.984375)
  //
  // 0.125 <= |values| <= 0.75
  const uint64_t kValues[] = {
      0x1000000000000100ull, 0x2000000000000100ull, 0x3000000000000100ull,
      0x4000000000000100ull, 0x5000000000000100ull, 0x6000000000000100ull,
      // negative values
      0x9000000000000100ull, 0xa000000000000100ull, 0xb000000000000100ull,
      0xc000000000000100ull, 0xd000000000000100ull, 0xe000000000000100ull};

  // 0.875 <= |values| <= 0.984375
  const uint64_t kExtraValues[] = {
      0x7000000000000100ull, 0x7800000000000100ull,  //
      0x7c00000000000100ull, 0x7e00000000000100ull,  //
      // negative values
      0xf000000000000100ull, 0xf800000000000100ull,  //
      0xfc00000000000100ull, 0xfe00000000000100ull};

  auto make_box = [](uint64_t v, uint64_t box) {
    return (v & 0xffffffffffffff80ull) | box;
  };

  // The box is the lower 7 bits of the value. When the box == 0, then
  // the algorithm uses an escape hatch to select the result for large
  // outputs.
  for (uint64_t box = 0; box < 0x7f; box++) {
    for (const uint64_t v : kValues) {
      // Extra values are added to the sequence to attempt to avoid
      // infinite loops from rejection sampling on bugs/errors.
      absl::random_internal::sequence_urbg urbg(
          {make_box(v, box), 0x0003eb76f6f7f755ull, 0x5FCEA50FDB2F953Bull});

      auto a = dist(urbg);
      EXPECT_EQ(1, urbg.invocations()) << box << " " << std::hex << v;
      if (v & 0x8000000000000000ull) {
        EXPECT_LT(a, 0.0) << box << " " << std::hex << v;
      } else {
        EXPECT_GT(a, 0.0) << box << " " << std::hex << v;
      }
    }
    if (box > 10 && box < 100) {
      // The center boxes use the fast algorithm for more
      // than 98.4375% of values.
      for (const uint64_t v : kExtraValues) {
        absl::random_internal::sequence_urbg urbg(
            {make_box(v, box), 0x0003eb76f6f7f755ull, 0x5FCEA50FDB2F953Bull});

        auto a = dist(urbg);
        EXPECT_EQ(1, urbg.invocations()) << box << " " << std::hex << v;
        if (v & 0x8000000000000000ull) {
          EXPECT_LT(a, 0.0) << box << " " << std::hex << v;
        } else {
          EXPECT_GT(a, 0.0) << box << " " << std::hex << v;
        }
      }
    }
  }

  // When the box == 0, the fallback algorithm uses a ratio of uniforms,
  // which consumes 2 additional values from the urbg.
  // Fallback also requires that the initial value be > 0.9271586026096681.
  auto make_fallback = [](uint64_t v) { return (v & 0xffffffffffffff80ull); };

  double tail[2];
  {
    // 0.9375
    absl::random_internal::sequence_urbg urbg(
        {make_fallback(0x7800000000000000ull), 0x13CCA830EB61BD96ull,
         0x00000076f6f7f755ull});
    tail[0] = dist(urbg);
    EXPECT_EQ(3, urbg.invocations());
    EXPECT_GT(tail[0], 0);
  }
  {
    // -0.9375
    absl::random_internal::sequence_urbg urbg(
        {make_fallback(0xf800000000000000ull), 0x13CCA830EB61BD96ull,
         0x00000076f6f7f755ull});
    tail[1] = dist(urbg);
    EXPECT_EQ(3, urbg.invocations());
    EXPECT_LT(tail[1], 0);
  }
  EXPECT_EQ(tail[0], -tail[1]);
  EXPECT_EQ(418610, static_cast<int64_t>(tail[0] * 100000.0));

  // When the box != 0, the fallback algorithm computes a wedge function.
  // Depending on the box, the threshold for varies as high as
  // 0.991522480228.
  {
    // 0.9921875, 0.875
    absl::random_internal::sequence_urbg urbg(
        {make_box(0x7f00000000000000ull, 120), 0xe000000000000001ull,
         0x13CCA830EB61BD96ull});
    tail[0] = dist(urbg);
    EXPECT_EQ(2, urbg.invocations());
    EXPECT_GT(tail[0], 0);
  }
  {
    // -0.9921875, 0.875
    absl::random_internal::sequence_urbg urbg(
        {make_box(0xff00000000000000ull, 120), 0xe000000000000001ull,
         0x13CCA830EB61BD96ull});
    tail[1] = dist(urbg);
    EXPECT_EQ(2, urbg.invocations());
    EXPECT_LT(tail[1], 0);
  }
  EXPECT_EQ(tail[0], -tail[1]);
  EXPECT_EQ(61948, static_cast<int64_t>(tail[0] * 100000.0));

  // Fallback rejected, try again.
  {
    // -0.9921875, 0.0625
    absl::random_internal::sequence_urbg urbg(
        {make_box(0xff00000000000000ull, 120), 0x1000000000000001,
         make_box(0x1000000000000100ull, 50), 0x13CCA830EB61BD96ull});
    dist(urbg);
    EXPECT_EQ(3, urbg.invocations());
  }
}

}  // namespace
