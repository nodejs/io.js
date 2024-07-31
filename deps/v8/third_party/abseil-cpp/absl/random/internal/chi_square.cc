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

#include "absl/random/internal/chi_square.h"

#include <cmath>

#include "absl/random/internal/distribution_test_util.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

#if defined(__EMSCRIPTEN__)
// Workaround __EMSCRIPTEN__ error: llvm_fma_f64 not found.
inline double fma(double x, double y, double z) {
  return (x * y) + z;
}
#endif

// Use Horner's method to evaluate a polynomial.
template <typename T, unsigned N>
inline T EvaluatePolynomial(T x, const T (&poly)[N]) {
#if !defined(__EMSCRIPTEN__)
  using std::fma;
#endif
  T p = poly[N - 1];
  for (unsigned i = 2; i <= N; i++) {
    p = fma(p, x, poly[N - i]);
  }
  return p;
}

static constexpr int kLargeDOF = 150;

// Returns the probability of a normal z-value.
//
// Adapted from the POZ function in:
//     Ibbetson D, Algorithm 209
//     Collected Algorithms of the CACM 1963 p. 616
//
double POZ(double z) {
  static constexpr double kP1[] = {
      0.797884560593,  -0.531923007300, 0.319152932694,
      -0.151968751364, 0.059054035642,  -0.019198292004,
      0.005198775019,  -0.001075204047, 0.000124818987,
  };
  static constexpr double kP2[] = {
      0.999936657524,  0.000535310849,  -0.002141268741, 0.005353579108,
      -0.009279453341, 0.011630447319,  -0.010557625006, 0.006549791214,
      -0.002034254874, -0.000794620820, 0.001390604284,  -0.000676904986,
      -0.000019538132, 0.000152529290,  -0.000045255659,
  };

  const double kZMax = 6.0;  // Maximum meaningful z-value.
  if (z == 0.0) {
    return 0.5;
  }
  double x;
  double y = 0.5 * std::fabs(z);
  if (y >= (kZMax * 0.5)) {
    x = 1.0;
  } else if (y < 1.0) {
    double w = y * y;
    x = EvaluatePolynomial(w, kP1) * y * 2.0;
  } else {
    y -= 2.0;
    x = EvaluatePolynomial(y, kP2);
  }
  return z > 0.0 ? ((x + 1.0) * 0.5) : ((1.0 - x) * 0.5);
}

// Approximates the survival function of the normal distribution.
//
// Algorithm 26.2.18, from:
// [Abramowitz and Stegun, Handbook of Mathematical Functions,p.932]
// http://people.math.sfu.ca/~cbm/aands/abramowitz_and_stegun.pdf
//
double normal_survival(double z) {
  // Maybe replace with the alternate formulation.
  // 0.5 * erfc((x - mean)/(sqrt(2) * sigma))
  static constexpr double kR[] = {
      1.0, 0.196854, 0.115194, 0.000344, 0.019527,
  };
  double r = EvaluatePolynomial(z, kR);
  r *= r;
  return 0.5 / (r * r);
}

}  // namespace

// Calculates the critical chi-square value given degrees-of-freedom and a
// p-value, usually using bisection. Also known by the name CRITCHI.
double ChiSquareValue(int dof, double p) {
  static constexpr double kChiEpsilon =
      0.000001;  // Accuracy of the approximation.
  static constexpr double kChiMax =
      99999.0;  // Maximum chi-squared value.

  const double p_value = 1.0 - p;
  if (dof < 1 || p_value > 1.0) {
    return 0.0;
  }

  if (dof > kLargeDOF) {
    // For large degrees of freedom, use the normal approximation by
    //     Wilson, E. B. and Hilferty, M. M. (1931)
    //                     chi^2 - mean
    //                Z = --------------
    //                        stddev
    const double z = InverseNormalSurvival(p_value);
    const double mean = 1 - 2.0 / (9 * dof);
    const double variance = 2.0 / (9 * dof);
    // Cannot use this method if the variance is 0.
    if (variance != 0) {
      double term = z * std::sqrt(variance) + mean;
      return dof * (term * term * term);
    }
  }

  if (p_value <= 0.0) return kChiMax;

  // Otherwise search for the p value by bisection
  double min_chisq = 0.0;
  double max_chisq = kChiMax;
  double current = dof / std::sqrt(p_value);
  while ((max_chisq - min_chisq) > kChiEpsilon) {
    if (ChiSquarePValue(current, dof) < p_value) {
      max_chisq = current;
    } else {
      min_chisq = current;
    }
    current = (max_chisq + min_chisq) * 0.5;
  }
  return current;
}

// Calculates the p-value (probability) of a given chi-square value
// and degrees of freedom.
//
// Adapted from the POCHISQ function from:
//     Hill, I. D. and Pike, M. C.  Algorithm 299
//     Collected Algorithms of the CACM 1963 p. 243
//
double ChiSquarePValue(double chi_square, int dof) {
  static constexpr double kLogSqrtPi =
      0.5723649429247000870717135;  // Log[Sqrt[Pi]]
  static constexpr double kInverseSqrtPi =
      0.5641895835477562869480795;  // 1/(Sqrt[Pi])

  // For large degrees of freedom, use the normal approximation by
  //     Wilson, E. B. and Hilferty, M. M. (1931)
  // Via Wikipedia:
  //   By the Central Limit Theorem, because the chi-square distribution is the
  //   sum of k independent random variables with finite mean and variance, it
  //   converges to a normal distribution for large k.
  if (dof > kLargeDOF) {
    // Re-scale everything.
    const double chi_square_scaled = std::pow(chi_square / dof, 1.0 / 3);
    const double mean = 1 - 2.0 / (9 * dof);
    const double variance = 2.0 / (9 * dof);
    // If variance is 0, this method cannot be used.
    if (variance != 0) {
      const double z = (chi_square_scaled - mean) / std::sqrt(variance);
      if (z > 0) {
        return normal_survival(z);
      } else if (z < 0) {
        return 1.0 - normal_survival(-z);
      } else {
        return 0.5;
      }
    }
  }

  // The chi square function is >= 0 for any degrees of freedom.
  // In other words, probability that the chi square function >= 0 is 1.
  if (chi_square <= 0.0) return 1.0;

  // If the degrees of freedom is zero, the chi square function is always 0 by
  // definition. In other words, the probability that the chi square function
  // is > 0 is zero (chi square values <= 0 have been filtered above).
  if (dof < 1) return 0;

  auto capped_exp = [](double x) { return x < -20 ? 0.0 : std::exp(x); };
  static constexpr double kBigX = 20;

  double a = 0.5 * chi_square;
  const bool even = !(dof & 1);  // True if dof is an even number.
  const double y = capped_exp(-a);
  double s = even ? y : (2.0 * POZ(-std::sqrt(chi_square)));

  if (dof <= 2) {
    return s;
  }

  chi_square = 0.5 * (dof - 1.0);
  double z = (even ? 1.0 : 0.5);
  if (a > kBigX) {
    double e = (even ? 0.0 : kLogSqrtPi);
    double c = std::log(a);
    while (z <= chi_square) {
      e = std::log(z) + e;
      s += capped_exp(c * z - a - e);
      z += 1.0;
    }
    return s;
  }

  double e = (even ? 1.0 : (kInverseSqrtPi / std::sqrt(a)));
  double c = 0.0;
  while (z <= chi_square) {
    e = e * (a / z);
    c = c + e;
    z += 1.0;
  }
  return c * y + s;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
