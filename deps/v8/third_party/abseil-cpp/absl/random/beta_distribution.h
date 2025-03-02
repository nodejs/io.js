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

#ifndef ABSL_RANDOM_BETA_DISTRIBUTION_H_
#define ABSL_RANDOM_BETA_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <istream>
#include <limits>
#include <ostream>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/meta/type_traits.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/generate_real.h"
#include "absl/random/internal/iostream_state_saver.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::beta_distribution:
// Generate a floating-point variate conforming to a Beta distribution:
//   pdf(x) \propto x^(alpha-1) * (1-x)^(beta-1),
// where the params alpha and beta are both strictly positive real values.
//
// The support is the open interval (0, 1), but the return value might be equal
// to 0 or 1, due to numerical errors when alpha and beta are very different.
//
// Usage note: One usage is that alpha and beta are counts of number of
// successes and failures. When the total number of trials are large, consider
// approximating a beta distribution with a Gaussian distribution with the same
// mean and variance. One could use the skewness, which depends only on the
// smaller of alpha and beta when the number of trials are sufficiently large,
// to quantify how far a beta distribution is from the normal distribution.
template <typename RealType = double>
class beta_distribution {
 public:
  using result_type = RealType;

  class param_type {
   public:
    using distribution_type = beta_distribution;

    explicit param_type(result_type alpha, result_type beta)
        : alpha_(alpha), beta_(beta) {
      assert(alpha >= 0);
      assert(beta >= 0);
      assert(alpha <= (std::numeric_limits<result_type>::max)());
      assert(beta <= (std::numeric_limits<result_type>::max)());
      if (alpha == 0 || beta == 0) {
        method_ = DEGENERATE_SMALL;
        x_ = (alpha >= beta) ? 1 : 0;
        return;
      }
      // a_ = min(beta, alpha), b_ = max(beta, alpha).
      if (beta < alpha) {
        inverted_ = true;
        a_ = beta;
        b_ = alpha;
      } else {
        inverted_ = false;
        a_ = alpha;
        b_ = beta;
      }
      if (a_ <= 1 && b_ >= ThresholdForLargeA()) {
        method_ = DEGENERATE_SMALL;
        x_ = inverted_ ? result_type(1) : result_type(0);
        return;
      }
      // For threshold values, see also:
      // Evaluation of Beta Generation Algorithms, Ying-Chao Hung, et. al.
      // February, 2009.
      if ((b_ < 1.0 && a_ + b_ <= 1.2) || a_ <= ThresholdForSmallA()) {
        // Choose Joehnk over Cheng when it's faster or when Cheng encounters
        // numerical issues.
        method_ = JOEHNK;
        a_ = result_type(1) / alpha_;
        b_ = result_type(1) / beta_;
        if (std::isinf(a_) || std::isinf(b_)) {
          method_ = DEGENERATE_SMALL;
          x_ = inverted_ ? result_type(1) : result_type(0);
        }
        return;
      }
      if (a_ >= ThresholdForLargeA()) {
        method_ = DEGENERATE_LARGE;
        // Note: on PPC for long double, evaluating
        // `std::numeric_limits::max() / ThresholdForLargeA` results in NaN.
        result_type r = a_ / b_;
        x_ = (inverted_ ? result_type(1) : r) / (1 + r);
        return;
      }
      x_ = a_ + b_;
      log_x_ = std::log(x_);
      if (a_ <= 1) {
        method_ = CHENG_BA;
        y_ = result_type(1) / a_;
        gamma_ = a_ + a_;
        return;
      }
      method_ = CHENG_BB;
      result_type r = (a_ - 1) / (b_ - 1);
      y_ = std::sqrt((1 + r) / (b_ * r * 2 - r + 1));
      gamma_ = a_ + result_type(1) / y_;
    }

    result_type alpha() const { return alpha_; }
    result_type beta() const { return beta_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.alpha_ == b.alpha_ && a.beta_ == b.beta_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class beta_distribution;

#ifdef _MSC_VER
    // MSVC does not have constexpr implementations for std::log and std::exp
    // so they are computed at runtime.
#define ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR
#else
#define ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR constexpr
#endif

    // The threshold for whether std::exp(1/a) is finite.
    // Note that this value is quite large, and a smaller a_ is NOT abnormal.
    static ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR result_type
    ThresholdForSmallA() {
      return result_type(1) /
             std::log((std::numeric_limits<result_type>::max)());
    }

    // The threshold for whether a * std::log(a) is finite.
    static ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR result_type
    ThresholdForLargeA() {
      return std::exp(
          std::log((std::numeric_limits<result_type>::max)()) -
          std::log(std::log((std::numeric_limits<result_type>::max)())) -
          ThresholdPadding());
    }

#undef ABSL_RANDOM_INTERNAL_LOG_EXP_CONSTEXPR

    // Pad the threshold for large A for long double on PPC. This is done via a
    // template specialization below.
    static constexpr result_type ThresholdPadding() { return 0; }

    enum Method {
      JOEHNK,    // Uses algorithm Joehnk
      CHENG_BA,  // Uses algorithm BA in Cheng
      CHENG_BB,  // Uses algorithm BB in Cheng

      // Note: See also:
      //   Hung et al. Evaluation of beta generation algorithms. Communications
      //   in Statistics-Simulation and Computation 38.4 (2009): 750-770.
      // especially:
      //   Zechner, Heinz, and Ernst Stadlober. Generating beta variates via
      //   patchwork rejection. Computing 50.1 (1993): 1-18.

      DEGENERATE_SMALL,  // a_ is abnormally small.
      DEGENERATE_LARGE,  // a_ is abnormally large.
    };

    result_type alpha_;
    result_type beta_;

    result_type a_{};  // the smaller of {alpha, beta}, or 1.0/alpha_ in JOEHNK
    result_type b_{};  // the larger of {alpha, beta}, or 1.0/beta_ in JOEHNK
    result_type x_{};  // alpha + beta, or the result in degenerate cases
    result_type log_x_{};  // log(x_)
    result_type y_{};      // "beta" in Cheng
    result_type gamma_{};  // "gamma" in Cheng

    Method method_{};

    // Placing this last for optimal alignment.
    // Whether alpha_ != a_, i.e. true iff alpha_ > beta_.
    bool inverted_{};

    static_assert(std::is_floating_point<RealType>::value,
                  "Class-template absl::beta_distribution<> must be "
                  "parameterized using a floating-point type.");
  };

  beta_distribution() : beta_distribution(1) {}

  explicit beta_distribution(result_type alpha, result_type beta = 1)
      : param_(alpha, beta) {}

  explicit beta_distribution(const param_type& p) : param_(p) {}

  void reset() {}

  // Generating functions
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
  result_type(max)() const { return 1; }

  result_type alpha() const { return param_.alpha(); }
  result_type beta() const { return param_.beta(); }

  friend bool operator==(const beta_distribution& a,
                         const beta_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const beta_distribution& a,
                         const beta_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  template <typename URBG>
  result_type AlgorithmJoehnk(URBG& g,  // NOLINT(runtime/references)
                              const param_type& p);

  template <typename URBG>
  result_type AlgorithmCheng(URBG& g,  // NOLINT(runtime/references)
                             const param_type& p);

  template <typename URBG>
  result_type DegenerateCase(URBG& g,  // NOLINT(runtime/references)
                             const param_type& p) {
    if (p.method_ == param_type::DEGENERATE_SMALL && p.alpha_ == p.beta_) {
      // Returns 0 or 1 with equal probability.
      random_internal::FastUniformBits<uint8_t> fast_u8;
      return static_cast<result_type>((fast_u8(g) & 0x10) !=
                                      0);  // pick any single bit.
    }
    return p.x_;
  }

  param_type param_;
  random_internal::FastUniformBits<uint64_t> fast_u64_;
};

#if defined(__powerpc64__) || defined(__PPC64__) || defined(__powerpc__) || \
    defined(__ppc__) || defined(__PPC__)
// PPC needs a more stringent boundary for long double.
template <>
constexpr long double
beta_distribution<long double>::param_type::ThresholdPadding() {
  return 10;
}
#endif

template <typename RealType>
template <typename URBG>
typename beta_distribution<RealType>::result_type
beta_distribution<RealType>::AlgorithmJoehnk(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  using random_internal::GeneratePositiveTag;
  using random_internal::GenerateRealFromBits;
  using real_type =
      absl::conditional_t<std::is_same<RealType, float>::value, float, double>;

  // Based on Joehnk, M. D. Erzeugung von betaverteilten und gammaverteilten
  // Zufallszahlen. Metrika 8.1 (1964): 5-15.
  // This method is described in Knuth, Vol 2 (Third Edition), pp 134.

  result_type u, v, x, y, z;
  for (;;) {
    u = GenerateRealFromBits<real_type, GeneratePositiveTag, false>(
        fast_u64_(g));
    v = GenerateRealFromBits<real_type, GeneratePositiveTag, false>(
        fast_u64_(g));

    // Direct method. std::pow is slow for float, so rely on the optimizer to
    // remove the std::pow() path for that case.
    if (!std::is_same<float, result_type>::value) {
      x = std::pow(u, p.a_);
      y = std::pow(v, p.b_);
      z = x + y;
      if (z > 1) {
        // Reject if and only if `x + y > 1.0`
        continue;
      }
      if (z > 0) {
        // When both alpha and beta are small, x and y are both close to 0, so
        // divide by (x+y) directly may result in nan.
        return x / z;
      }
    }

    // Log transform.
    // x = log( pow(u, p.a_) ), y = log( pow(v, p.b_) )
    // since u, v <= 1.0,  x, y < 0.
    x = std::log(u) * p.a_;
    y = std::log(v) * p.b_;
    if (!std::isfinite(x) || !std::isfinite(y)) {
      continue;
    }
    // z = log( pow(u, a) + pow(v, b) )
    z = x > y ? (x + std::log(1 + std::exp(y - x)))
              : (y + std::log(1 + std::exp(x - y)));
    // Reject iff log(x+y) > 0.
    if (z > 0) {
      continue;
    }
    return std::exp(x - z);
  }
}

template <typename RealType>
template <typename URBG>
typename beta_distribution<RealType>::result_type
beta_distribution<RealType>::AlgorithmCheng(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  using random_internal::GeneratePositiveTag;
  using random_internal::GenerateRealFromBits;
  using real_type =
      absl::conditional_t<std::is_same<RealType, float>::value, float, double>;

  // Based on Cheng, Russell CH. Generating beta variates with nonintegral
  // shape parameters. Communications of the ACM 21.4 (1978): 317-322.
  // (https://dl.acm.org/citation.cfm?id=359482).
  static constexpr result_type kLogFour =
      result_type(1.3862943611198906188344642429163531361);  // log(4)
  static constexpr result_type kS =
      result_type(2.6094379124341003746007593332261876);  // 1+log(5)

  const bool use_algorithm_ba = (p.method_ == param_type::CHENG_BA);
  result_type u1, u2, v, w, z, r, s, t, bw_inv, lhs;
  for (;;) {
    u1 = GenerateRealFromBits<real_type, GeneratePositiveTag, false>(
        fast_u64_(g));
    u2 = GenerateRealFromBits<real_type, GeneratePositiveTag, false>(
        fast_u64_(g));
    v = p.y_ * std::log(u1 / (1 - u1));
    w = p.a_ * std::exp(v);
    bw_inv = result_type(1) / (p.b_ + w);
    r = p.gamma_ * v - kLogFour;
    s = p.a_ + r - w;
    z = u1 * u1 * u2;
    if (!use_algorithm_ba && s + kS >= 5 * z) {
      break;
    }
    t = std::log(z);
    if (!use_algorithm_ba && s >= t) {
      break;
    }
    lhs = p.x_ * (p.log_x_ + std::log(bw_inv)) + r;
    if (lhs >= t) {
      break;
    }
  }
  return p.inverted_ ? (1 - w * bw_inv) : w * bw_inv;
}

template <typename RealType>
template <typename URBG>
typename beta_distribution<RealType>::result_type
beta_distribution<RealType>::operator()(URBG& g,  // NOLINT(runtime/references)
                                        const param_type& p) {
  switch (p.method_) {
    case param_type::JOEHNK:
      return AlgorithmJoehnk(g, p);
    case param_type::CHENG_BA:
      ABSL_FALLTHROUGH_INTENDED;
    case param_type::CHENG_BB:
      return AlgorithmCheng(g, p);
    default:
      return DegenerateCase(g, p);
  }
}

template <typename CharT, typename Traits, typename RealType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const beta_distribution<RealType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<RealType>::kPrecision);
  os << x.alpha() << os.fill() << x.beta();
  return os;
}

template <typename CharT, typename Traits, typename RealType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    beta_distribution<RealType>& x) {       // NOLINT(runtime/references)
  using result_type = typename beta_distribution<RealType>::result_type;
  using param_type = typename beta_distribution<RealType>::param_type;
  result_type alpha, beta;

  auto saver = random_internal::make_istream_state_saver(is);
  alpha = random_internal::read_floating_point<result_type>(is);
  if (is.fail()) return is;
  beta = random_internal::read_floating_point<result_type>(is);
  if (!is.fail()) {
    x.param(param_type(alpha, beta));
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_BETA_DISTRIBUTION_H_
