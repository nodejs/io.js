// Copyright 2024 Google LLC
// SPDX-License-Identifier: Apache-2.0
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

#ifndef HIGHWAY_HWY_STATS_H_
#define HIGHWAY_HWY_STATS_H_

#include <stdint.h>
#include <stdio.h>

#include <cmath>
#include <string>

#include "hwy/base.h"  // HWY_ASSERT

namespace hwy {

// Thread-compatible.
template <size_t N>
class Bins {
 public:
  Bins() { Reset(); }

  template <typename T>
  void Notify(T bin) {
    HWY_ASSERT(T{0} <= bin && bin < static_cast<T>(N));
    counts_[static_cast<int32_t>(bin)]++;
  }

  void Assimilate(const Bins<N>& other) {
    for (size_t i = 0; i < N; ++i) {
      counts_[i] += other.counts_[i];
    }
  }

  void Print(const char* caption) const {
    fprintf(stderr, "\n%s [%zu]\n", caption, N);
    size_t last_nonzero = 0;
    for (size_t i = N - 1; i < N; --i) {
      if (counts_[i] != 0) {
        last_nonzero = i;
        break;
      }
    }
    for (size_t i = 0; i <= last_nonzero; ++i) {
      fprintf(stderr, "  %zu\n", counts_[i]);
    }
  }

  void Reset() {
    for (size_t i = 0; i < N; ++i) {
      counts_[i] = 0;
    }
  }

 private:
  size_t counts_[N];
};

// Descriptive statistics of a variable (4 moments). Thread-compatible.
class Stats {
 public:
  Stats() { Reset(); }

  void Notify(const float x) {
    ++n_;

    min_ = HWY_MIN(min_, x);
    max_ = HWY_MAX(max_, x);

    // Logarithmic transform avoids/delays underflow and overflow.
    sum_log_ += std::log(static_cast<double>(x));

    // Online moments. Reference: https://goo.gl/9ha694
    const double d = x - m1_;
    const double d_div_n = d / n_;
    const double d2n1_div_n = d * (n_ - 1) * d_div_n;
    const int64_t n_poly = n_ * n_ - 3 * n_ + 3;
    m1_ += d_div_n;
    m4_ += d_div_n * (d_div_n * (d2n1_div_n * n_poly + 6.0 * m2_) - 4.0 * m3_);
    m3_ += d_div_n * (d2n1_div_n * (n_ - 2) - 3.0 * m2_);
    m2_ += d2n1_div_n;
  }

  void Assimilate(const Stats& other);

  int64_t Count() const { return n_; }

  float Min() const { return min_; }
  float Max() const { return max_; }

  double GeometricMean() const {
    return n_ == 0 ? 0.0 : std::exp(sum_log_ / n_);
  }

  double Mean() const { return m1_; }
  // Same as Mu2. Assumes n_ is large.
  double SampleVariance() const {
    return n_ == 0 ? 0.0 : m2_ / static_cast<int>(n_);
  }
  // Unbiased estimator for population variance even for smaller n_.
  double Variance() const {
    if (n_ == 0) return 0.0;
    if (n_ == 1) return m2_;
    return m2_ / static_cast<int>(n_ - 1);
  }
  double StandardDeviation() const { return std::sqrt(Variance()); }
  // Near zero for normal distributions; if positive on a unimodal distribution,
  // the right tail is fatter. Assumes n_ is large.
  double SampleSkewness() const {
    if (ScalarAbs(m2_) < 1E-7) return 0.0;
    return m3_ * std::sqrt(static_cast<double>(n_)) / std::pow(m2_, 1.5);
  }
  // Corrected for bias (same as Wikipedia and Minitab but not Excel).
  double Skewness() const {
    if (n_ == 0) return 0.0;
    const double biased = SampleSkewness();
    const double r = (n_ - 1.0) / n_;
    return biased * std::pow(r, 1.5);
  }
  // Near zero for normal distributions; smaller values indicate fewer/smaller
  // outliers and larger indicates more/larger outliers. Assumes n_ is large.
  double SampleKurtosis() const {
    if (ScalarAbs(m2_) < 1E-7) return 0.0;
    return m4_ * n_ / (m2_ * m2_);
  }
  // Corrected for bias (same as Wikipedia and Minitab but not Excel).
  double Kurtosis() const {
    if (n_ == 0) return 0.0;
    const double biased = SampleKurtosis();
    const double r = (n_ - 1.0) / n_;
    return biased * r * r;
  }

  // Central moments, useful for "method of moments"-based parameter estimation
  // of a mixture of two Gaussians. Assumes Count() != 0.
  double Mu1() const { return m1_; }
  double Mu2() const { return m2_ / static_cast<int>(n_); }
  double Mu3() const { return m3_ / static_cast<int>(n_); }
  double Mu4() const { return m4_ / static_cast<int>(n_); }

  // Which statistics to EXCLUDE in ToString
  enum {
    kNoCount = 1,
    kNoMeanSD = 2,
    kNoMinMax = 4,
    kNoSkewKurt = 8,
    kNoGeomean = 16
  };
  std::string ToString(int exclude = 0) const;

  void Reset() {
    n_ = 0;

    min_ = hwy::HighestValue<float>();
    max_ = hwy::LowestValue<float>();

    sum_log_ = 0.0;

    m1_ = 0.0;
    m2_ = 0.0;
    m3_ = 0.0;
    m4_ = 0.0;
  }

 private:
  int64_t n_;  // signed for faster conversion + safe subtraction

  float min_;
  float max_;

  double sum_log_;  // for geomean

  // Moments
  double m1_;
  double m2_;
  double m3_;
  double m4_;
};

}  // namespace hwy

#endif  // HIGHWAY_HWY_STATS_H_
