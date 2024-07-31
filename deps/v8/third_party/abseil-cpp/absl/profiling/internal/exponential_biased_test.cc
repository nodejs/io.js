// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/profiling/internal/exponential_biased.h"

#include <stddef.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

using ::testing::Ge;

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {
namespace {

MATCHER_P2(IsBetween, a, b,
           absl::StrCat(std::string(negation ? "isn't" : "is"), " between ", a,
                        " and ", b)) {
  return a <= arg && arg <= b;
}

// Tests of the quality of the random numbers generated
// This uses the Anderson Darling test for uniformity.
// See "Evaluating the Anderson-Darling Distribution" by Marsaglia
// for details.

// Short cut version of ADinf(z), z>0 (from Marsaglia)
// This returns the p-value for Anderson Darling statistic in
// the limit as n-> infinity. For finite n, apply the error fix below.
double AndersonDarlingInf(double z) {
  if (z < 2) {
    return exp(-1.2337141 / z) / sqrt(z) *
           (2.00012 +
            (0.247105 -
             (0.0649821 - (0.0347962 - (0.011672 - 0.00168691 * z) * z) * z) *
                 z) *
                z);
  }
  return exp(
      -exp(1.0776 -
           (2.30695 -
            (0.43424 - (0.082433 - (0.008056 - 0.0003146 * z) * z) * z) * z) *
               z));
}

// Corrects the approximation error in AndersonDarlingInf for small values of n
// Add this to AndersonDarlingInf to get a better approximation
// (from Marsaglia)
double AndersonDarlingErrFix(int n, double x) {
  if (x > 0.8) {
    return (-130.2137 +
            (745.2337 -
             (1705.091 - (1950.646 - (1116.360 - 255.7844 * x) * x) * x) * x) *
                x) /
           n;
  }
  double cutoff = 0.01265 + 0.1757 / n;
  if (x < cutoff) {
    double t = x / cutoff;
    t = sqrt(t) * (1 - t) * (49 * t - 102);
    return t * (0.0037 / (n * n) + 0.00078 / n + 0.00006) / n;
  } else {
    double t = (x - cutoff) / (0.8 - cutoff);
    t = -0.00022633 +
        (6.54034 - (14.6538 - (14.458 - (8.259 - 1.91864 * t) * t) * t) * t) *
            t;
    return t * (0.04213 + 0.01365 / n) / n;
  }
}

// Returns the AndersonDarling p-value given n and the value of the statistic
double AndersonDarlingPValue(int n, double z) {
  double ad = AndersonDarlingInf(z);
  double errfix = AndersonDarlingErrFix(n, ad);
  return ad + errfix;
}

double AndersonDarlingStatistic(const std::vector<double>& random_sample) {
  size_t n = random_sample.size();
  double ad_sum = 0;
  for (size_t i = 0; i < n; i++) {
    ad_sum += (2 * i + 1) *
              std::log(random_sample[i] * (1 - random_sample[n - 1 - i]));
  }
  const auto n_as_double = static_cast<double>(n);
  double ad_statistic = -n_as_double - 1 / n_as_double * ad_sum;
  return ad_statistic;
}

// Tests if the array of doubles is uniformly distributed.
// Returns the p-value of the Anderson Darling Statistic
// for the given set of sorted random doubles
// See "Evaluating the Anderson-Darling Distribution" by
// Marsaglia and Marsaglia for details.
double AndersonDarlingTest(const std::vector<double>& random_sample) {
  double ad_statistic = AndersonDarlingStatistic(random_sample);
  double p = AndersonDarlingPValue(static_cast<int>(random_sample.size()),
                                   ad_statistic);
  return p;
}

TEST(ExponentialBiasedTest, CoinTossDemoWithGetSkipCount) {
  ExponentialBiased eb;
  for (int runs = 0; runs < 10; ++runs) {
    for (int64_t flips = eb.GetSkipCount(1); flips > 0; --flips) {
      printf("head...");
    }
    printf("tail\n");
  }
  int heads = 0;
  for (int i = 0; i < 10000000; i += 1 + eb.GetSkipCount(1)) {
    ++heads;
  }
  printf("Heads = %d (%f%%)\n", heads, 100.0 * heads / 10000000);
}

TEST(ExponentialBiasedTest, SampleDemoWithStride) {
  ExponentialBiased eb;
  int64_t stride = eb.GetStride(10);
  int samples = 0;
  for (int i = 0; i < 10000000; ++i) {
    if (--stride == 0) {
      ++samples;
      stride = eb.GetStride(10);
    }
  }
  printf("Samples = %d (%f%%)\n", samples, 100.0 * samples / 10000000);
}


// Testing that NextRandom generates uniform random numbers. Applies the
// Anderson-Darling test for uniformity
TEST(ExponentialBiasedTest, TestNextRandom) {
  for (auto n : std::vector<size_t>({
           10,  // Check short-range correlation
           100, 1000,
           10000  // Make sure there's no systemic error
       })) {
    uint64_t x = 1;
    // This assumes that the prng returns 48 bit numbers
    uint64_t max_prng_value = static_cast<uint64_t>(1) << 48;
    // Initialize.
    for (int i = 1; i <= 20; i++) {
      x = ExponentialBiased::NextRandom(x);
    }
    std::vector<uint64_t> int_random_sample(n);
    // Collect samples
    for (size_t i = 0; i < n; i++) {
      int_random_sample[i] = x;
      x = ExponentialBiased::NextRandom(x);
    }
    // First sort them...
    std::sort(int_random_sample.begin(), int_random_sample.end());
    std::vector<double> random_sample(n);
    // Convert them to uniform randoms (in the range [0,1])
    for (size_t i = 0; i < n; i++) {
      random_sample[i] =
          static_cast<double>(int_random_sample[i]) / max_prng_value;
    }
    // Now compute the Anderson-Darling statistic
    double ad_pvalue = AndersonDarlingTest(random_sample);
    EXPECT_GT(std::min(ad_pvalue, 1 - ad_pvalue), 0.0001)
        << "prng is not uniform: n = " << n << " p = " << ad_pvalue;
  }
}

// The generator needs to be available as a thread_local and as a static
// variable.
TEST(ExponentialBiasedTest, InitializationModes) {
  ABSL_CONST_INIT static ExponentialBiased eb_static;
  EXPECT_THAT(eb_static.GetSkipCount(2), Ge(0));

#ifdef ABSL_HAVE_THREAD_LOCAL
  thread_local ExponentialBiased eb_thread;
  EXPECT_THAT(eb_thread.GetSkipCount(2), Ge(0));
#endif

  ExponentialBiased eb_stack;
  EXPECT_THAT(eb_stack.GetSkipCount(2), Ge(0));
}

}  // namespace
}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl
