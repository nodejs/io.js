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

#ifndef ABSL_RANDOM_BERNOULLI_DISTRIBUTION_H_
#define ABSL_RANDOM_BERNOULLI_DISTRIBUTION_H_

#include <cstdint>
#include <istream>
#include <limits>

#include "absl/base/optimization.h"
#include "absl/random/internal/fast_uniform_bits.h"
#include "absl/random/internal/iostream_state_saver.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::bernoulli_distribution is a drop in replacement for
// std::bernoulli_distribution. It guarantees that (given a perfect
// UniformRandomBitGenerator) the acceptance probability is *exactly* equal to
// the given double.
//
// The implementation assumes that double is IEEE754
class bernoulli_distribution {
 public:
  using result_type = bool;

  class param_type {
   public:
    using distribution_type = bernoulli_distribution;

    explicit param_type(double p = 0.5) : prob_(p) {
      assert(p >= 0.0 && p <= 1.0);
    }

    double p() const { return prob_; }

    friend bool operator==(const param_type& p1, const param_type& p2) {
      return p1.p() == p2.p();
    }
    friend bool operator!=(const param_type& p1, const param_type& p2) {
      return p1.p() != p2.p();
    }

   private:
    double prob_;
  };

  bernoulli_distribution() : bernoulli_distribution(0.5) {}

  explicit bernoulli_distribution(double p) : param_(p) {}

  explicit bernoulli_distribution(param_type p) : param_(p) {}

  // no-op
  void reset() {}

  template <typename URBG>
  bool operator()(URBG& g) {  // NOLINT(runtime/references)
    return Generate(param_.p(), g);
  }

  template <typename URBG>
  bool operator()(URBG& g,  // NOLINT(runtime/references)
                  const param_type& param) {
    return Generate(param.p(), g);
  }

  param_type param() const { return param_; }
  void param(const param_type& param) { param_ = param; }

  double p() const { return param_.p(); }

  result_type(min)() const { return false; }
  result_type(max)() const { return true; }

  friend bool operator==(const bernoulli_distribution& d1,
                         const bernoulli_distribution& d2) {
    return d1.param_ == d2.param_;
  }

  friend bool operator!=(const bernoulli_distribution& d1,
                         const bernoulli_distribution& d2) {
    return d1.param_ != d2.param_;
  }

 private:
  static constexpr uint64_t kP32 = static_cast<uint64_t>(1) << 32;

  template <typename URBG>
  static bool Generate(double p, URBG& g);  // NOLINT(runtime/references)

  param_type param_;
};

template <typename CharT, typename Traits>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const bernoulli_distribution& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  os.precision(random_internal::stream_precision_helper<double>::kPrecision);
  os << x.p();
  return os;
}

template <typename CharT, typename Traits>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    bernoulli_distribution& x) {            // NOLINT(runtime/references)
  auto saver = random_internal::make_istream_state_saver(is);
  auto p = random_internal::read_floating_point<double>(is);
  if (!is.fail()) {
    x.param(bernoulli_distribution::param_type(p));
  }
  return is;
}

template <typename URBG>
bool bernoulli_distribution::Generate(double p,
                                      URBG& g) {  // NOLINT(runtime/references)
  random_internal::FastUniformBits<uint32_t> fast_u32;

  while (true) {
    // There are two aspects of the definition of `c` below that are worth
    // commenting on.  First, because `p` is in the range [0, 1], `c` is in the
    // range [0, 2^32] which does not fit in a uint32_t and therefore requires
    // 64 bits.
    //
    // Second, `c` is constructed by first casting explicitly to a signed
    // integer and then casting explicitly to an unsigned integer of the same
    // size.  This is done because the hardware conversion instructions produce
    // signed integers from double; if taken as a uint64_t the conversion would
    // be wrong for doubles greater than 2^63 (not relevant in this use-case).
    // If converted directly to an unsigned integer, the compiler would end up
    // emitting code to handle such large values that are not relevant due to
    // the known bounds on `c`.  To avoid these extra instructions this
    // implementation converts first to the signed type and then convert to
    // unsigned (which is a no-op).
    const uint64_t c = static_cast<uint64_t>(static_cast<int64_t>(p * kP32));
    const uint32_t v = fast_u32(g);
    // FAST PATH: this path fails with probability 1/2^32.  Note that simply
    // returning v <= c would approximate P very well (up to an absolute error
    // of 1/2^32); the slow path (taken in that range of possible error, in the
    // case of equality) eliminates the remaining error.
    if (ABSL_PREDICT_TRUE(v != c)) return v < c;

    // It is guaranteed that `q` is strictly less than 1, because if `q` were
    // greater than or equal to 1, the same would be true for `p`. Certainly `p`
    // cannot be greater than 1, and if `p == 1`, then the fast path would
    // necessary have been taken already.
    const double q = static_cast<double>(c) / kP32;

    // The probability of acceptance on the fast path is `q` and so the
    // probability of acceptance here should be `p - q`.
    //
    // Note that `q` is obtained from `p` via some shifts and conversions, the
    // upshot of which is that `q` is simply `p` with some of the
    // least-significant bits of its mantissa set to zero. This means that the
    // difference `p - q` will not have any rounding errors. To see why, pretend
    // that double has 10 bits of resolution and q is obtained from `p` in such
    // a way that the 4 least-significant bits of its mantissa are set to zero.
    // For example:
    //   p   = 1.1100111011 * 2^-1
    //   q   = 1.1100110000 * 2^-1
    // p - q = 1.011        * 2^-8
    // The difference `p - q` has exactly the nonzero mantissa bits that were
    // "lost" in `q` producing a number which is certainly representable in a
    // double.
    const double left = p - q;

    // By construction, the probability of being on this slow path is 1/2^32, so
    // P(accept in slow path) = P(accept| in slow path) * P(slow path),
    // which means the probability of acceptance here is `1 / (left * kP32)`:
    const double here = left * kP32;

    // The simplest way to compute the result of this trial is to repeat the
    // whole algorithm with the new probability. This terminates because even
    // given  arbitrarily unfriendly "random" bits, each iteration either
    // multiplies a tiny probability by 2^32 (if c == 0) or strips off some
    // number of nonzero mantissa bits. That process is bounded.
    if (here == 0) return false;
    p = here;
  }
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_BERNOULLI_DISTRIBUTION_H_
