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

#ifndef ABSL_RANDOM_EXPONENTIAL_DISTRIBUTION_H_
#define ABSL_RANDOM_EXPONENTIAL_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <istream>
#include <limits>
#include <type_traits>

#include "absl/meta/type_traits.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/generate_real.h"
#include "absl/random/internal/iostream_state_saver.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::exponential_distribution:
// Generates a number conforming to an exponential distribution and is
// equivalent to the standard [rand.dist.pois.exp] distribution.
template <typename RealType = double>
class exponential_distribution {
 public:
  using result_type = RealType;

  class param_type {
   public:
    using distribution_type = exponential_distribution;

    explicit param_type(result_type lambda = 1) : lambda_(lambda) {
      assert(lambda > 0);
      neg_inv_lambda_ = -result_type(1) / lambda_;
    }

    result_type lambda() const { return lambda_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.lambda_ == b.lambda_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class exponential_distribution;

    result_type lambda_;
    result_type neg_inv_lambda_;

    static_assert(
        std::is_floating_point<RealType>::value,
        "Class-template absl::exponential_distribution<> must be parameterized "
        "using a floating-point type.");
  };

  exponential_distribution() : exponential_distribution(1) {}

  explicit exponential_distribution(result_type lambda) : param_(lambda) {}

  explicit exponential_distribution(const param_type& p) : param_(p) {}

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
  result_type(max)() const {
    return std::numeric_limits<result_type>::infinity();
  }

  result_type lambda() const { return param_.lambda(); }

  friend bool operator==(const exponential_distribution& a,
                         const exponential_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const exponential_distribution& a,
                         const exponential_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
  random_internal::FastUniformBits<uint64_t> fast_u64_;
};

// --------------------------------------------------------------------------
// Implementation details follow
// --------------------------------------------------------------------------

template <typename RealType>
template <typename URBG>
typename exponential_distribution<RealType>::result_type
exponential_distribution<RealType>::operator()(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  using random_internal::GenerateNegativeTag;
  using random_internal::GenerateRealFromBits;
  using real_type =
      absl::conditional_t<std::is_same<RealType, float>::value, float, double>;

  const result_type u = GenerateRealFromBits<real_type, GenerateNegativeTag,
                                             false>(fast_u64_(g));  // U(-1, 0)

  // log1p(-x) is mathematically equivalent to log(1 - x) but has more
  // accuracy for x near zero.
  return p.neg_inv_lambda_ * std::log1p(u);
}

template <typename CharT, typename Traits, typename RealType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const exponential_distribution<RealType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<RealType>::kPrecision);
  os << x.lambda();
  return os;
}

template <typename CharT, typename Traits, typename RealType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,    // NOLINT(runtime/references)
    exponential_distribution<RealType>& x) {  // NOLINT(runtime/references)
  using result_type = typename exponential_distribution<RealType>::result_type;
  using param_type = typename exponential_distribution<RealType>::param_type;
  result_type lambda;

  auto saver = random_internal::make_istream_state_saver(is);
  lambda = random_internal::read_floating_point<result_type>(is);
  if (!is.fail()) {
    x.param(param_type(lambda));
  }
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_EXPONENTIAL_DISTRIBUTION_H_
