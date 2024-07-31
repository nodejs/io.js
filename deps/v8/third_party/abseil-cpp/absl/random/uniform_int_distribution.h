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
//
// -----------------------------------------------------------------------------
// File: uniform_int_distribution.h
// -----------------------------------------------------------------------------
//
// This header defines a class for representing a uniform integer distribution
// over the closed (inclusive) interval [a,b]. You use this distribution in
// combination with an Abseil random bit generator to produce random values
// according to the rules of the distribution.
//
// `absl::uniform_int_distribution` is a drop-in replacement for the C++11
// `std::uniform_int_distribution` [rand.dist.uni.int] but is considerably
// faster than the libstdc++ implementation.

#ifndef ABSL_RANDOM_UNIFORM_INT_DISTRIBUTION_H_
#define ABSL_RANDOM_UNIFORM_INT_DISTRIBUTION_H_

#include <cassert>
#include <istream>
#include <limits>
#include <type_traits>

#include "absl/base/optimization.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/traits.h"
#include "absl/random/internal/wide_multiply.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::uniform_int_distribution<T>
//
// This distribution produces random integer values uniformly distributed in the
// closed (inclusive) interval [a, b].
//
// Example:
//
//   absl::BitGen gen;
//
//   // Use the distribution to produce a value between 1 and 6, inclusive.
//   int die_roll = absl::uniform_int_distribution<int>(1, 6)(gen);
//
template <typename IntType = int>
class uniform_int_distribution {
 private:
  using unsigned_type =
      typename random_internal::make_unsigned_bits<IntType>::type;

 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = uniform_int_distribution;

    explicit param_type(
        result_type lo = 0,
        result_type hi = (std::numeric_limits<result_type>::max)())
        : lo_(lo),
          range_(static_cast<unsigned_type>(hi) -
                 static_cast<unsigned_type>(lo)) {
      // [rand.dist.uni.int] precondition 2
      assert(lo <= hi);
    }

    result_type a() const { return lo_; }
    result_type b() const {
      return static_cast<result_type>(static_cast<unsigned_type>(lo_) + range_);
    }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.lo_ == b.lo_ && a.range_ == b.range_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class uniform_int_distribution;
    unsigned_type range() const { return range_; }

    result_type lo_;
    unsigned_type range_;

    static_assert(random_internal::IsIntegral<result_type>::value,
                  "Class-template absl::uniform_int_distribution<> must be "
                  "parameterized using an integral type.");
  };  // param_type

  uniform_int_distribution() : uniform_int_distribution(0) {}

  explicit uniform_int_distribution(
      result_type lo,
      result_type hi = (std::numeric_limits<result_type>::max)())
      : param_(lo, hi) {}

  explicit uniform_int_distribution(const param_type& param) : param_(param) {}

  // uniform_int_distribution<T>::reset()
  //
  // Resets the uniform int distribution. Note that this function has no effect
  // because the distribution already produces independent values.
  void reset() {}

  template <typename URBG>
  result_type operator()(URBG& gen) {  // NOLINT(runtime/references)
    return (*this)(gen, param());
  }

  template <typename URBG>
  result_type operator()(
      URBG& gen, const param_type& param) {  // NOLINT(runtime/references)
    return static_cast<result_type>(param.a() + Generate(gen, param.range()));
  }

  result_type a() const { return param_.a(); }
  result_type b() const { return param_.b(); }

  param_type param() const { return param_; }
  void param(const param_type& params) { param_ = params; }

  result_type(min)() const { return a(); }
  result_type(max)() const { return b(); }

  friend bool operator==(const uniform_int_distribution& a,
                         const uniform_int_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const uniform_int_distribution& a,
                         const uniform_int_distribution& b) {
    return !(a == b);
  }

 private:
  // Generates a value in the *closed* interval [0, R]
  template <typename URBG>
  unsigned_type Generate(URBG& g,  // NOLINT(runtime/references)
                         unsigned_type R);
  param_type param_;
};

// -----------------------------------------------------------------------------
// Implementation details follow
// -----------------------------------------------------------------------------
template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,
    const uniform_int_distribution<IntType>& x) {
  using stream_type =
      typename random_internal::stream_format_type<IntType>::type;
  auto saver = random_internal::make_ostream_state_saver(os);
  os << static_cast<stream_type>(x.a()) << os.fill()
     << static_cast<stream_type>(x.b());
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,
    uniform_int_distribution<IntType>& x) {
  using param_type = typename uniform_int_distribution<IntType>::param_type;
  using result_type = typename uniform_int_distribution<IntType>::result_type;
  using stream_type =
      typename random_internal::stream_format_type<IntType>::type;

  stream_type a;
  stream_type b;

  auto saver = random_internal::make_istream_state_saver(is);
  is >> a >> b;
  if (!is.fail()) {
    x.param(
        param_type(static_cast<result_type>(a), static_cast<result_type>(b)));
  }
  return is;
}

template <typename IntType>
template <typename URBG>
typename random_internal::make_unsigned_bits<IntType>::type
uniform_int_distribution<IntType>::Generate(
    URBG& g,  // NOLINT(runtime/references)
    typename random_internal::make_unsigned_bits<IntType>::type R) {
  random_internal::FastUniformBits<unsigned_type> fast_bits;
  unsigned_type bits = fast_bits(g);
  const unsigned_type Lim = R + 1;
  if ((R & Lim) == 0) {
    // If the interval's length is a power of two range, just take the low bits.
    return bits & R;
  }

  // Generates a uniform variate on [0, Lim) using fixed-point multiplication.
  // The above fast-path guarantees that Lim is representable in unsigned_type.
  //
  // Algorithm adapted from
  // http://lemire.me/blog/2016/06/30/fast-random-shuffling/, with added
  // explanation.
  //
  // The algorithm creates a uniform variate `bits` in the interval [0, 2^N),
  // and treats it as the fractional part of a fixed-point real value in [0, 1),
  // multiplied by 2^N.  For example, 0.25 would be represented as 2^(N - 2),
  // because 2^N * 0.25 == 2^(N - 2).
  //
  // Next, `bits` and `Lim` are multiplied with a wide-multiply to bring the
  // value into the range [0, Lim).  The integral part (the high word of the
  // multiplication result) is then very nearly the desired result.  However,
  // this is not quite accurate; viewing the multiplication result as one
  // double-width integer, the resulting values for the sample are mapped as
  // follows:
  //
  // If the result lies in this interval:       Return this value:
  //        [0, 2^N)                                    0
  //        [2^N, 2 * 2^N)                              1
  //        ...                                         ...
  //        [K * 2^N, (K + 1) * 2^N)                    K
  //        ...                                         ...
  //        [(Lim - 1) * 2^N, Lim * 2^N)                Lim - 1
  //
  // While all of these intervals have the same size, the result of `bits * Lim`
  // must be a multiple of `Lim`, and not all of these intervals contain the
  // same number of multiples of `Lim`.  In particular, some contain
  // `F = floor(2^N / Lim)` and some contain `F + 1 = ceil(2^N / Lim)`.  This
  // difference produces a small nonuniformity, which is corrected by applying
  // rejection sampling to one of the values in the "larger intervals" (i.e.,
  // the intervals containing `F + 1` multiples of `Lim`.
  //
  // An interval contains `F + 1` multiples of `Lim` if and only if its smallest
  // value modulo 2^N is less than `2^N % Lim`.  The unique value satisfying
  // this property is used as the one for rejection.  That is, a value of
  // `bits * Lim` is rejected if `(bit * Lim) % 2^N < (2^N % Lim)`.

  using helper = random_internal::wide_multiply<unsigned_type>;
  auto product = helper::multiply(bits, Lim);

  // Two optimizations here:
  // * Rejection occurs with some probability less than 1/2, and for reasonable
  //   ranges considerably less (in particular, less than 1/(F+1)), so
  //   ABSL_PREDICT_FALSE is apt.
  // * `Lim` is an overestimate of `threshold`, and doesn't require a divide.
  if (ABSL_PREDICT_FALSE(helper::lo(product) < Lim)) {
    // This quantity is exactly equal to `2^N % Lim`, but does not require high
    // precision calculations: `2^N % Lim` is congruent to `(2^N - Lim) % Lim`.
    // Ideally this could be expressed simply as `-X` rather than `2^N - X`, but
    // for types smaller than int, this calculation is incorrect due to integer
    // promotion rules.
    const unsigned_type threshold =
        ((std::numeric_limits<unsigned_type>::max)() - Lim + 1) % Lim;
    while (helper::lo(product) < threshold) {
      bits = fast_bits(g);
      product = helper::multiply(bits, Lim);
    }
  }

  return helper::hi(product);
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_UNIFORM_INT_DISTRIBUTION_H_
