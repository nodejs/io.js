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

#ifndef ABSL_RANDOM_POISSON_DISTRIBUTION_H_
#define ABSL_RANDOM_POISSON_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>

#include "absl/base/config.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/fastmath.h"
#include "absl/random/internal/generate_real.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::poisson_distribution:
// Generates discrete variates conforming to a Poisson distribution.
//   p(n) = (mean^n / n!) exp(-mean)
//
// Depending on the parameter, the distribution selects one of the following
// algorithms:
// * The standard algorithm, attributed to Knuth, extended using a split method
// for larger values
// * The "Ratio of Uniforms as a convenient method for sampling from classical
// discrete distributions", Stadlober, 1989.
// http://www.sciencedirect.com/science/article/pii/0377042790903495
//
// NOTE: param_type.mean() is a double, which permits values larger than
// poisson_distribution<IntType>::max(), however this should be avoided and
// the distribution results are limited to the max() value.
//
// The goals of this implementation are to provide good performance while still
// being thread-safe: This limits the implementation to not using lgamma
// provided by <math.h>.
//
template <typename IntType = int>
class poisson_distribution {
 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = poisson_distribution;
    explicit param_type(double mean = 1.0);

    double mean() const { return mean_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.mean_ == b.mean_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class poisson_distribution;

    double mean_;
    double emu_;  // e ^ -mean_
    double lmu_;  // ln(mean_)
    double s_;
    double log_k_;
    int split_;

    static_assert(random_internal::IsIntegral<IntType>::value,
                  "Class-template absl::poisson_distribution<> must be "
                  "parameterized using an integral type.");
  };

  poisson_distribution() : poisson_distribution(1.0) {}

  explicit poisson_distribution(double mean) : param_(mean) {}

  explicit poisson_distribution(const param_type& p) : param_(p) {}

  void reset() {}

  // generating functions
  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  param_type param() const { return param_; }
  void param(const param_type& p) { param_ = p; }

  result_type(min)() const { return 0; }
  result_type(max)() const { return (std::numeric_limits<result_type>::max)(); }

  double mean() const { return param_.mean(); }

  friend bool operator==(const poisson_distribution& a,
                         const poisson_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const poisson_distribution& a,
                         const poisson_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
  random_internal::FastUniformBits<uint64_t> fast_u64_;
};

// -----------------------------------------------------------------------------
// Implementation details follow
// -----------------------------------------------------------------------------

template <typename IntType>
poisson_distribution<IntType>::param_type::param_type(double mean)
    : mean_(mean), split_(0) {
  assert(mean >= 0);
  assert(mean <=
         static_cast<double>((std::numeric_limits<result_type>::max)()));
  // As a defensive measure, avoid large values of the mean.  The rejection
  // algorithm used does not support very large values well.  It my be worth
  // changing algorithms to better deal with these cases.
  assert(mean <= 1e10);
  if (mean_ < 10) {
    // For small lambda, use the knuth method.
    split_ = 1;
    emu_ = std::exp(-mean_);
  } else if (mean_ <= 50) {
    // Use split-knuth method.
    split_ = 1 + static_cast<int>(mean_ / 10.0);
    emu_ = std::exp(-mean_ / static_cast<double>(split_));
  } else {
    // Use ratio of uniforms method.
    constexpr double k2E = 0.7357588823428846;
    constexpr double kSA = 0.4494580810294493;

    lmu_ = std::log(mean_);
    double a = mean_ + 0.5;
    s_ = kSA + std::sqrt(k2E * a);
    const double mode = std::ceil(mean_) - 1;
    log_k_ = lmu_ * mode - absl::random_internal::StirlingLogFactorial(mode);
  }
}

template <typename IntType>
template <typename URBG>
typename poisson_distribution<IntType>::result_type
poisson_distribution<IntType>::operator()(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  using random_internal::GeneratePositiveTag;
  using random_internal::GenerateRealFromBits;
  using random_internal::GenerateSignedTag;

  if (p.split_ != 0) {
    // Use Knuth's algorithm with range splitting to avoid floating-point
    // errors. Knuth's algorithm is: Ui is a sequence of uniform variates on
    // (0,1); return the number of variates required for product(Ui) <
    // exp(-lambda).
    //
    // The expected number of variates required for Knuth's method can be
    // computed as follows:
    // The expected value of U is 0.5, so solving for 0.5^n < exp(-lambda) gives
    // the expected number of uniform variates
    // required for a given lambda, which is:
    //  lambda = [2, 5,  9, 10, 11, 12, 13, 14, 15, 16, 17]
    //  n      = [3, 8, 13, 15, 16, 18, 19, 21, 22, 24, 25]
    //
    result_type n = 0;
    for (int split = p.split_; split > 0; --split) {
      double r = 1.0;
      do {
        r *= GenerateRealFromBits<double, GeneratePositiveTag, true>(
            fast_u64_(g));  // U(-1, 0)
        ++n;
      } while (r > p.emu_);
      --n;
    }
    return n;
  }

  // Use ratio of uniforms method.
  //
  // Let u ~ Uniform(0, 1), v ~ Uniform(-1, 1),
  //     a = lambda + 1/2,
  //     s = 1.5 - sqrt(3/e) + sqrt(2(lambda + 1/2)/e),
  //     x = s * v/u + a.
  // P(floor(x) = k | u^2 < f(floor(x))/k), where
  // f(m) = lambda^m exp(-lambda)/ m!, for 0 <= m, and f(m) = 0 otherwise,
  // and k = max(f).
  const double a = p.mean_ + 0.5;
  for (;;) {
    const double u = GenerateRealFromBits<double, GeneratePositiveTag, false>(
        fast_u64_(g));  // U(0, 1)
    const double v = GenerateRealFromBits<double, GenerateSignedTag, false>(
        fast_u64_(g));  // U(-1, 1)

    const double x = std::floor(p.s_ * v / u + a);
    if (x < 0) continue;  // f(negative) = 0
    const double rhs = x * p.lmu_;
    // clang-format off
    double s = (x <= 1.0) ? 0.0
             : (x == 2.0) ? 0.693147180559945
             : absl::random_internal::StirlingLogFactorial(x);
    // clang-format on
    const double lhs = 2.0 * std::log(u) + p.log_k_ + s;
    if (lhs < rhs) {
      return x > static_cast<double>((max)())
                 ? (max)()
                 : static_cast<result_type>(x);  // f(x)/k >= u^2
    }
  }
}

template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const poisson_distribution<IntType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<double>::kPrecision);
  os << x.mean();
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    poisson_distribution<IntType>& x) {     // NOLINT(runtime/references)
  using param_type = typename poisson_distribution<IntType>::param_type;

  auto saver = random_internal::make_istream_state_saver(is);
  double mean = random_internal::read_floating_point<double>(is);
  if (!is.fail()) {
    x.param(param_type(mean));
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_POISSON_DISTRIBUTION_H_
