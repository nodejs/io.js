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

#ifndef ABSL_RANDOM_DISCRETE_DISTRIBUTION_H_
#define ABSL_RANDOM_DISCRETE_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <istream>
#include <limits>
#include <numeric>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/random/bernoulli_distribution.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/uniform_int_distribution.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::discrete_distribution
//
// A discrete distribution produces random integers i, where 0 <= i < n
// distributed according to the discrete probability function:
//
//     P(i|p0,...,pn−1)=pi
//
// This class is an implementation of discrete_distribution (see
// [rand.dist.samp.discrete]).
//
// The algorithm used is Walker's Aliasing algorithm, described in Knuth, Vol 2.
// absl::discrete_distribution takes O(N) time to precompute the probabilities
// (where N is the number of possible outcomes in the distribution) at
// construction, and then takes O(1) time for each variate generation.  Many
// other implementations also take O(N) time to construct an ordered sequence of
// partial sums, plus O(log N) time per variate to binary search.
//
template <typename IntType = int>
class discrete_distribution {
 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = discrete_distribution;

    param_type() { init(); }

    template <typename InputIterator>
    explicit param_type(InputIterator begin, InputIterator end)
        : p_(begin, end) {
      init();
    }

    explicit param_type(std::initializer_list<double> weights) : p_(weights) {
      init();
    }

    template <class UnaryOperation>
    explicit param_type(size_t nw, double xmin, double xmax,
                        UnaryOperation fw) {
      if (nw > 0) {
        p_.reserve(nw);
        double delta = (xmax - xmin) / static_cast<double>(nw);
        assert(delta > 0);
        double t = delta * 0.5;
        for (size_t i = 0; i < nw; ++i) {
          p_.push_back(fw(xmin + i * delta + t));
        }
      }
      init();
    }

    const std::vector<double>& probabilities() const { return p_; }
    size_t n() const { return p_.size() - 1; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.probabilities() == b.probabilities();
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class discrete_distribution;

    void init();

    std::vector<double> p_;                     // normalized probabilities
    std::vector<std::pair<double, size_t>> q_;  // (acceptance, alternate) pairs

    static_assert(std::is_integral<result_type>::value,
                  "Class-template absl::discrete_distribution<> must be "
                  "parameterized using an integral type.");
  };

  discrete_distribution() : param_() {}

  explicit discrete_distribution(const param_type& p) : param_(p) {}

  template <typename InputIterator>
  explicit discrete_distribution(InputIterator begin, InputIterator end)
      : param_(begin, end) {}

  explicit discrete_distribution(std::initializer_list<double> weights)
      : param_(weights) {}

  template <class UnaryOperation>
  explicit discrete_distribution(size_t nw, double xmin, double xmax,
                                 UnaryOperation fw)
      : param_(nw, xmin, xmax, std::move(fw)) {}

  void reset() {}

  // generating functions
  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  const param_type& param() const { return param_; }
  void param(const param_type& p) { param_ = p; }

  result_type(min)() const { return 0; }
  result_type(max)() const {
    return static_cast<result_type>(param_.n());
  }  // inclusive

  // NOTE [rand.dist.sample.discrete] returns a std::vector<double> not a
  // const std::vector<double>&.
  const std::vector<double>& probabilities() const {
    return param_.probabilities();
  }

  friend bool operator==(const discrete_distribution& a,
                         const discrete_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const discrete_distribution& a,
                         const discrete_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
};

// --------------------------------------------------------------------------
// Implementation details only below
// --------------------------------------------------------------------------

namespace random_internal {

// Using the vector `*probabilities`, whose values are the weights or
// probabilities of an element being selected, constructs the proportional
// probabilities used by the discrete distribution.  `*probabilities` will be
// scaled, if necessary, so that its entries sum to a value sufficiently close
// to 1.0.
std::vector<std::pair<double, size_t>> InitDiscreteDistribution(
    std::vector<double>* probabilities);

}  // namespace random_internal

template <typename IntType>
void discrete_distribution<IntType>::param_type::init() {
  if (p_.empty()) {
    p_.push_back(1.0);
    q_.emplace_back(1.0, 0);
  } else {
    assert(n() <= (std::numeric_limits<IntType>::max)());
    q_ = random_internal::InitDiscreteDistribution(&p_);
  }
}

template <typename IntType>
template <typename URBG>
typename discrete_distribution<IntType>::result_type
discrete_distribution<IntType>::operator()(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  const auto idx = absl::uniform_int_distribution<result_type>(0, p.n())(g);
  const auto& q = p.q_[idx];
  const bool selected = absl::bernoulli_distribution(q.first)(g);
  return selected ? idx : static_cast<result_type>(q.second);
}

template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const discrete_distribution<IntType>& x) {
  auto saver = random_internal::make_ostream_state_saver(os);
  const auto& probabilities = x.param().probabilities();
  os << probabilities.size();

  os.precision(random_internal::stream_precision_helper<double>::kPrecision);
  for (const auto& p : probabilities) {
    os << os.fill() << p;
  }
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    discrete_distribution<IntType>& x) {    // NOLINT(runtime/references)
  using param_type = typename discrete_distribution<IntType>::param_type;
  auto saver = random_internal::make_istream_state_saver(is);

  size_t n;
  std::vector<double> p;

  is >> n;
  if (is.fail()) return is;
  if (n > 0) {
    p.reserve(n);
    for (IntType i = 0; i < n && !is.fail(); ++i) {
      auto tmp = random_internal::read_floating_point<double>(is);
      if (is.fail()) return is;
      p.push_back(tmp);
    }
  }
  x.param(param_type(p.begin(), p.end()));
  return is;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_DISCRETE_DISTRIBUTION_H_
