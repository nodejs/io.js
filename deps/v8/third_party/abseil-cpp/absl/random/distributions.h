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
// File: distributions.h
// -----------------------------------------------------------------------------
//
// This header defines functions representing distributions, which you use in
// combination with an Abseil random bit generator to produce random values
// according to the rules of that distribution.
//
// The Abseil random library defines the following distributions within this
// file:
//
//   * `absl::Uniform` for uniform (constant) distributions having constant
//     probability
//   * `absl::Bernoulli` for discrete distributions having exactly two outcomes
//   * `absl::Beta` for continuous distributions parameterized through two
//     free parameters
//   * `absl::Exponential` for discrete distributions of events occurring
//     continuously and independently at a constant average rate
//   * `absl::Gaussian` (also known as "normal distributions") for continuous
//     distributions using an associated quadratic function
//   * `absl::LogUniform` for continuous uniform distributions where the log
//     to the given base of all values is uniform
//   * `absl::Poisson` for discrete probability distributions that express the
//     probability of a given number of events occurring within a fixed interval
//   * `absl::Zipf` for discrete probability distributions commonly used for
//     modelling of rare events
//
// Prefer use of these distribution function classes over manual construction of
// your own distribution classes, as it allows library maintainers greater
// flexibility to change the underlying implementation in the future.

#ifndef ABSL_RANDOM_DISTRIBUTIONS_H_
#define ABSL_RANDOM_DISTRIBUTIONS_H_

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <type_traits>

#include "absl/base/internal/inline_variable.h"
#include "absl/random/bernoulli_distribution.h"
#include "absl/random/beta_distribution.h"
#include "absl/random/exponential_distribution.h"
#include "absl/random/gaussian_distribution.h"
#include "absl/random/internal/distribution_caller.h"  // IWYU pragma: export
#include "absl/random/internal/uniform_helper.h"  // IWYU pragma: export
#include "absl/random/log_uniform_int_distribution.h"
#include "absl/random/poisson_distribution.h"
#include "absl/random/uniform_int_distribution.h"
#include "absl/random/uniform_real_distribution.h"
#include "absl/random/zipf_distribution.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

ABSL_INTERNAL_INLINE_CONSTEXPR(IntervalClosedClosedTag, IntervalClosedClosed,
                               {});
ABSL_INTERNAL_INLINE_CONSTEXPR(IntervalClosedClosedTag, IntervalClosed, {});
ABSL_INTERNAL_INLINE_CONSTEXPR(IntervalClosedOpenTag, IntervalClosedOpen, {});
ABSL_INTERNAL_INLINE_CONSTEXPR(IntervalOpenOpenTag, IntervalOpenOpen, {});
ABSL_INTERNAL_INLINE_CONSTEXPR(IntervalOpenOpenTag, IntervalOpen, {});
ABSL_INTERNAL_INLINE_CONSTEXPR(IntervalOpenClosedTag, IntervalOpenClosed, {});

// -----------------------------------------------------------------------------
// absl::Uniform<T>(tag, bitgen, lo, hi)
// -----------------------------------------------------------------------------
//
// `absl::Uniform()` produces random values of type `T` uniformly distributed in
// a defined interval {lo, hi}. The interval `tag` defines the type of interval
// which should be one of the following possible values:
//
//   * `absl::IntervalOpenOpen`
//   * `absl::IntervalOpenClosed`
//   * `absl::IntervalClosedOpen`
//   * `absl::IntervalClosedClosed`
//
// where "open" refers to an exclusive value (excluded) from the output, while
// "closed" refers to an inclusive value (included) from the output.
//
// In the absence of an explicit return type `T`, `absl::Uniform()` will deduce
// the return type based on the provided endpoint arguments {A lo, B hi}.
// Given these endpoints, one of {A, B} will be chosen as the return type, if
// a type can be implicitly converted into the other in a lossless way. The
// lack of any such implicit conversion between {A, B} will produce a
// compile-time error
//
// See https://en.wikipedia.org/wiki/Uniform_distribution_(continuous)
//
// Example:
//
//   absl::BitGen bitgen;
//
//   // Produce a random float value between 0.0 and 1.0, inclusive
//   auto x = absl::Uniform(absl::IntervalClosedClosed, bitgen, 0.0f, 1.0f);
//
//   // The most common interval of `absl::IntervalClosedOpen` is available by
//   // default:
//
//   auto x = absl::Uniform(bitgen, 0.0f, 1.0f);
//
//   // Return-types are typically inferred from the arguments, however callers
//   // can optionally provide an explicit return-type to the template.
//
//   auto x = absl::Uniform<float>(bitgen, 0, 1);
//
template <typename R = void, typename TagType, typename URBG>
typename absl::enable_if_t<!std::is_same<R, void>::value, R>  //
Uniform(TagType tag,
        URBG&& urbg,  // NOLINT(runtime/references)
        R lo, R hi) {
  using gen_t = absl::decay_t<URBG>;
  using distribution_t = random_internal::UniformDistributionWrapper<R>;

  auto a = random_internal::uniform_lower_bound(tag, lo, hi);
  auto b = random_internal::uniform_upper_bound(tag, lo, hi);
  if (!random_internal::is_uniform_range_valid(a, b)) return lo;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, tag, lo, hi);
}

// absl::Uniform<T>(bitgen, lo, hi)
//
// Overload of `Uniform()` using the default closed-open interval of [lo, hi),
// and returning values of type `T`
template <typename R = void, typename URBG>
typename absl::enable_if_t<!std::is_same<R, void>::value, R>  //
Uniform(URBG&& urbg,  // NOLINT(runtime/references)
        R lo, R hi) {
  using gen_t = absl::decay_t<URBG>;
  using distribution_t = random_internal::UniformDistributionWrapper<R>;
  constexpr auto tag = absl::IntervalClosedOpen;

  auto a = random_internal::uniform_lower_bound(tag, lo, hi);
  auto b = random_internal::uniform_upper_bound(tag, lo, hi);
  if (!random_internal::is_uniform_range_valid(a, b)) return lo;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, lo, hi);
}

// absl::Uniform(tag, bitgen, lo, hi)
//
// Overload of `Uniform()` using different (but compatible) lo, hi types. Note
// that a compile-error will result if the return type cannot be deduced
// correctly from the passed types.
template <typename R = void, typename TagType, typename URBG, typename A,
          typename B>
typename absl::enable_if_t<std::is_same<R, void>::value,
                           random_internal::uniform_inferred_return_t<A, B>>
Uniform(TagType tag,
        URBG&& urbg,  // NOLINT(runtime/references)
        A lo, B hi) {
  using gen_t = absl::decay_t<URBG>;
  using return_t = typename random_internal::uniform_inferred_return_t<A, B>;
  using distribution_t = random_internal::UniformDistributionWrapper<return_t>;

  auto a = random_internal::uniform_lower_bound<return_t>(tag, lo, hi);
  auto b = random_internal::uniform_upper_bound<return_t>(tag, lo, hi);
  if (!random_internal::is_uniform_range_valid(a, b)) return lo;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, tag, static_cast<return_t>(lo),
                                static_cast<return_t>(hi));
}

// absl::Uniform(bitgen, lo, hi)
//
// Overload of `Uniform()` using different (but compatible) lo, hi types and the
// default closed-open interval of [lo, hi). Note that a compile-error will
// result if the return type cannot be deduced correctly from the passed types.
template <typename R = void, typename URBG, typename A, typename B>
typename absl::enable_if_t<std::is_same<R, void>::value,
                           random_internal::uniform_inferred_return_t<A, B>>
Uniform(URBG&& urbg,  // NOLINT(runtime/references)
        A lo, B hi) {
  using gen_t = absl::decay_t<URBG>;
  using return_t = typename random_internal::uniform_inferred_return_t<A, B>;
  using distribution_t = random_internal::UniformDistributionWrapper<return_t>;

  constexpr auto tag = absl::IntervalClosedOpen;
  auto a = random_internal::uniform_lower_bound<return_t>(tag, lo, hi);
  auto b = random_internal::uniform_upper_bound<return_t>(tag, lo, hi);
  if (!random_internal::is_uniform_range_valid(a, b)) return lo;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, static_cast<return_t>(lo),
                                static_cast<return_t>(hi));
}

// absl::Uniform<unsigned T>(bitgen)
//
// Overload of Uniform() using the minimum and maximum values of a given type
// `T` (which must be unsigned), returning a value of type `unsigned T`
template <typename R, typename URBG>
typename absl::enable_if_t<!std::is_signed<R>::value, R>  //
Uniform(URBG&& urbg) {  // NOLINT(runtime/references)
  using gen_t = absl::decay_t<URBG>;
  using distribution_t = random_internal::UniformDistributionWrapper<R>;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg);
}

// -----------------------------------------------------------------------------
// absl::Bernoulli(bitgen, p)
// -----------------------------------------------------------------------------
//
// `absl::Bernoulli` produces a random boolean value, with probability `p`
// (where 0.0 <= p <= 1.0) equaling `true`.
//
// Prefer `absl::Bernoulli` to produce boolean values over other alternatives
// such as comparing an `absl::Uniform()` value to a specific output.
//
// See https://en.wikipedia.org/wiki/Bernoulli_distribution
//
// Example:
//
//   absl::BitGen bitgen;
//   ...
//   if (absl::Bernoulli(bitgen, 1.0/3721.0)) {
//     std::cout << "Asteroid field navigation successful.";
//   }
//
template <typename URBG>
bool Bernoulli(URBG&& urbg,  // NOLINT(runtime/references)
               double p) {
  using gen_t = absl::decay_t<URBG>;
  using distribution_t = absl::bernoulli_distribution;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, p);
}

// -----------------------------------------------------------------------------
// absl::Beta<T>(bitgen, alpha, beta)
// -----------------------------------------------------------------------------
//
// `absl::Beta` produces a floating point number distributed in the closed
// interval [0,1] and parameterized by two values `alpha` and `beta` as per a
// Beta distribution. `T` must be a floating point type, but may be inferred
// from the types of `alpha` and `beta`.
//
// See https://en.wikipedia.org/wiki/Beta_distribution.
//
// Example:
//
//   absl::BitGen bitgen;
//   ...
//   double sample = absl::Beta(bitgen, 3.0, 2.0);
//
template <typename RealType, typename URBG>
RealType Beta(URBG&& urbg,  // NOLINT(runtime/references)
              RealType alpha, RealType beta) {
  static_assert(
      std::is_floating_point<RealType>::value,
      "Template-argument 'RealType' must be a floating-point type, in "
      "absl::Beta<RealType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = typename absl::beta_distribution<RealType>;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, alpha, beta);
}

// -----------------------------------------------------------------------------
// absl::Exponential<T>(bitgen, lambda = 1)
// -----------------------------------------------------------------------------
//
// `absl::Exponential` produces a floating point number representing the
// distance (time) between two consecutive events in a point process of events
// occurring continuously and independently at a constant average rate. `T` must
// be a floating point type, but may be inferred from the type of `lambda`.
//
// See https://en.wikipedia.org/wiki/Exponential_distribution.
//
// Example:
//
//   absl::BitGen bitgen;
//   ...
//   double call_length = absl::Exponential(bitgen, 7.0);
//
template <typename RealType, typename URBG>
RealType Exponential(URBG&& urbg,  // NOLINT(runtime/references)
                     RealType lambda = 1) {
  static_assert(
      std::is_floating_point<RealType>::value,
      "Template-argument 'RealType' must be a floating-point type, in "
      "absl::Exponential<RealType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = typename absl::exponential_distribution<RealType>;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, lambda);
}

// -----------------------------------------------------------------------------
// absl::Gaussian<T>(bitgen, mean = 0, stddev = 1)
// -----------------------------------------------------------------------------
//
// `absl::Gaussian` produces a floating point number selected from the Gaussian
// (ie. "Normal") distribution. `T` must be a floating point type, but may be
// inferred from the types of `mean` and `stddev`.
//
// See https://en.wikipedia.org/wiki/Normal_distribution
//
// Example:
//
//   absl::BitGen bitgen;
//   ...
//   double giraffe_height = absl::Gaussian(bitgen, 16.3, 3.3);
//
template <typename RealType, typename URBG>
RealType Gaussian(URBG&& urbg,  // NOLINT(runtime/references)
                  RealType mean = 0, RealType stddev = 1) {
  static_assert(
      std::is_floating_point<RealType>::value,
      "Template-argument 'RealType' must be a floating-point type, in "
      "absl::Gaussian<RealType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = typename absl::gaussian_distribution<RealType>;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, mean, stddev);
}

// -----------------------------------------------------------------------------
// absl::LogUniform<T>(bitgen, lo, hi, base = 2)
// -----------------------------------------------------------------------------
//
// `absl::LogUniform` produces random values distributed where the log to a
// given base of all values is uniform in a closed interval [lo, hi]. `T` must
// be an integral type, but may be inferred from the types of `lo` and `hi`.
//
// I.e., `LogUniform(0, n, b)` is uniformly distributed across buckets
// [0], [1, b-1], [b, b^2-1] .. [b^(k-1), (b^k)-1] .. [b^floor(log(n, b)), n]
// and is uniformly distributed within each bucket.
//
// The resulting probability density is inversely related to bucket size, though
// values in the final bucket may be more likely than previous values. (In the
// extreme case where n = b^i the final value will be tied with zero as the most
// probable result.
//
// If `lo` is nonzero then this distribution is shifted to the desired interval,
// so LogUniform(lo, hi, b) is equivalent to LogUniform(0, hi-lo, b)+lo.
//
// See https://en.wikipedia.org/wiki/Log-normal_distribution
//
// Example:
//
//   absl::BitGen bitgen;
//   ...
//   int v = absl::LogUniform(bitgen, 0, 1000);
//
template <typename IntType, typename URBG>
IntType LogUniform(URBG&& urbg,  // NOLINT(runtime/references)
                   IntType lo, IntType hi, IntType base = 2) {
  static_assert(random_internal::IsIntegral<IntType>::value,
                "Template-argument 'IntType' must be an integral type, in "
                "absl::LogUniform<IntType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = typename absl::log_uniform_int_distribution<IntType>;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, lo, hi, base);
}

// -----------------------------------------------------------------------------
// absl::Poisson<T>(bitgen, mean = 1)
// -----------------------------------------------------------------------------
//
// `absl::Poisson` produces discrete probabilities for a given number of events
// occurring within a fixed interval within the closed interval [0, max]. `T`
// must be an integral type.
//
// See https://en.wikipedia.org/wiki/Poisson_distribution
//
// Example:
//
//   absl::BitGen bitgen;
//   ...
//   int requests_per_minute = absl::Poisson<int>(bitgen, 3.2);
//
template <typename IntType, typename URBG>
IntType Poisson(URBG&& urbg,  // NOLINT(runtime/references)
                double mean = 1.0) {
  static_assert(random_internal::IsIntegral<IntType>::value,
                "Template-argument 'IntType' must be an integral type, in "
                "absl::Poisson<IntType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = typename absl::poisson_distribution<IntType>;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, mean);
}

// -----------------------------------------------------------------------------
// absl::Zipf<T>(bitgen, hi = max, q = 2, v = 1)
// -----------------------------------------------------------------------------
//
// `absl::Zipf` produces discrete probabilities commonly used for modelling of
// rare events over the closed interval [0, hi]. The parameters `v` and `q`
// determine the skew of the distribution. `T`  must be an integral type, but
// may be inferred from the type of `hi`.
//
// See http://mathworld.wolfram.com/ZipfDistribution.html
//
// Example:
//
//   absl::BitGen bitgen;
//   ...
//   int term_rank = absl::Zipf<int>(bitgen);
//
template <typename IntType, typename URBG>
IntType Zipf(URBG&& urbg,  // NOLINT(runtime/references)
             IntType hi = (std::numeric_limits<IntType>::max)(), double q = 2.0,
             double v = 1.0) {
  static_assert(random_internal::IsIntegral<IntType>::value,
                "Template-argument 'IntType' must be an integral type, in "
                "absl::Zipf<IntType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = typename absl::zipf_distribution<IntType>;

  return random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, hi, q, v);
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_DISTRIBUTIONS_H_
