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

#include "absl/random/internal/distribution_test_util.h"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

#if defined(__EMSCRIPTEN__)
// Workaround __EMSCRIPTEN__ error: llvm_fma_f64 not found.
inline double fma(double x, double y, double z) { return (x * y) + z; }
#endif

}  // namespace

DistributionMoments ComputeDistributionMoments(
    absl::Span<const double> data_points) {
  DistributionMoments result;

  // Compute m1
  for (double x : data_points) {
    result.n++;
    result.mean += x;
  }
  result.mean /= static_cast<double>(result.n);

  // Compute m2, m3, m4
  for (double x : data_points) {
    double v = x - result.mean;
    result.variance += v * v;
    result.skewness += v * v * v;
    result.kurtosis += v * v * v * v;
  }
  result.variance /= static_cast<double>(result.n - 1);

  result.skewness /= static_cast<double>(result.n);
  result.skewness /= std::pow(result.variance, 1.5);

  result.kurtosis /= static_cast<double>(result.n);
  result.kurtosis /= std::pow(result.variance, 2.0);
  return result;

  // When validating the min/max count, the following confidence intervals may
  // be of use:
  // 3.291 * stddev = 99.9% CI
  // 2.576 * stddev = 99% CI
  // 1.96 * stddev  = 95% CI
  // 1.65 * stddev  = 90% CI
}

std::ostream& operator<<(std::ostream& os, const DistributionMoments& moments) {
  return os << absl::StrFormat("mean=%f, stddev=%f, skewness=%f, kurtosis=%f",
                               moments.mean, std::sqrt(moments.variance),
                               moments.skewness, moments.kurtosis);
}

double InverseNormalSurvival(double x) {
  // inv_sf(u) = -sqrt(2) * erfinv(2u-1)
  static constexpr double kSqrt2 = 1.4142135623730950488;
  return -kSqrt2 * absl::random_internal::erfinv(2 * x - 1.0);
}

bool Near(absl::string_view msg, double actual, double expected, double bound) {
  assert(bound > 0.0);
  double delta = fabs(expected - actual);
  if (delta < bound) {
    return true;
  }

  std::string formatted = absl::StrCat(
      msg, " actual=", actual, " expected=", expected, " err=", delta / bound);
  ABSL_RAW_LOG(INFO, "%s", formatted.c_str());
  return false;
}

// TODO(absl-team): Replace with an "ABSL_HAVE_SPECIAL_MATH" and try
// to use std::beta().  As of this writing P0226R1 is not implemented
// in libc++: http://libcxx.llvm.org/cxx1z_status.html
double beta(double p, double q) {
  // Beta(x, y) = Gamma(x) * Gamma(y) / Gamma(x+y)
  double lbeta = std::lgamma(p) + std::lgamma(q) - std::lgamma(p + q);
  return std::exp(lbeta);
}

// Approximation to inverse of the Error Function in double precision.
// (http://people.maths.ox.ac.uk/gilesm/files/gems_erfinv.pdf)
double erfinv(double x) {
#if !defined(__EMSCRIPTEN__)
  using std::fma;
#endif

  double w = 0.0;
  double p = 0.0;
  w = -std::log((1.0 - x) * (1.0 + x));
  if (w < 6.250000) {
    w = w - 3.125000;
    p = -3.6444120640178196996e-21;
    p = fma(p, w, -1.685059138182016589e-19);
    p = fma(p, w, 1.2858480715256400167e-18);
    p = fma(p, w, 1.115787767802518096e-17);
    p = fma(p, w, -1.333171662854620906e-16);
    p = fma(p, w, 2.0972767875968561637e-17);
    p = fma(p, w, 6.6376381343583238325e-15);
    p = fma(p, w, -4.0545662729752068639e-14);
    p = fma(p, w, -8.1519341976054721522e-14);
    p = fma(p, w, 2.6335093153082322977e-12);
    p = fma(p, w, -1.2975133253453532498e-11);
    p = fma(p, w, -5.4154120542946279317e-11);
    p = fma(p, w, 1.051212273321532285e-09);
    p = fma(p, w, -4.1126339803469836976e-09);
    p = fma(p, w, -2.9070369957882005086e-08);
    p = fma(p, w, 4.2347877827932403518e-07);
    p = fma(p, w, -1.3654692000834678645e-06);
    p = fma(p, w, -1.3882523362786468719e-05);
    p = fma(p, w, 0.0001867342080340571352);
    p = fma(p, w, -0.00074070253416626697512);
    p = fma(p, w, -0.0060336708714301490533);
    p = fma(p, w, 0.24015818242558961693);
    p = fma(p, w, 1.6536545626831027356);
  } else if (w < 16.000000) {
    w = std::sqrt(w) - 3.250000;
    p = 2.2137376921775787049e-09;
    p = fma(p, w, 9.0756561938885390979e-08);
    p = fma(p, w, -2.7517406297064545428e-07);
    p = fma(p, w, 1.8239629214389227755e-08);
    p = fma(p, w, 1.5027403968909827627e-06);
    p = fma(p, w, -4.013867526981545969e-06);
    p = fma(p, w, 2.9234449089955446044e-06);
    p = fma(p, w, 1.2475304481671778723e-05);
    p = fma(p, w, -4.7318229009055733981e-05);
    p = fma(p, w, 6.8284851459573175448e-05);
    p = fma(p, w, 2.4031110387097893999e-05);
    p = fma(p, w, -0.0003550375203628474796);
    p = fma(p, w, 0.00095328937973738049703);
    p = fma(p, w, -0.0016882755560235047313);
    p = fma(p, w, 0.0024914420961078508066);
    p = fma(p, w, -0.0037512085075692412107);
    p = fma(p, w, 0.005370914553590063617);
    p = fma(p, w, 1.0052589676941592334);
    p = fma(p, w, 3.0838856104922207635);
  } else {
    w = std::sqrt(w) - 5.000000;
    p = -2.7109920616438573243e-11;
    p = fma(p, w, -2.5556418169965252055e-10);
    p = fma(p, w, 1.5076572693500548083e-09);
    p = fma(p, w, -3.7894654401267369937e-09);
    p = fma(p, w, 7.6157012080783393804e-09);
    p = fma(p, w, -1.4960026627149240478e-08);
    p = fma(p, w, 2.9147953450901080826e-08);
    p = fma(p, w, -6.7711997758452339498e-08);
    p = fma(p, w, 2.2900482228026654717e-07);
    p = fma(p, w, -9.9298272942317002539e-07);
    p = fma(p, w, 4.5260625972231537039e-06);
    p = fma(p, w, -1.9681778105531670567e-05);
    p = fma(p, w, 7.5995277030017761139e-05);
    p = fma(p, w, -0.00021503011930044477347);
    p = fma(p, w, -0.00013871931833623122026);
    p = fma(p, w, 1.0103004648645343977);
    p = fma(p, w, 4.8499064014085844221);
  }
  return p * x;
}

namespace {

// Direct implementation of AS63, BETAIN()
// https://www.jstor.org/stable/2346797?seq=3#page_scan_tab_contents.
//
// BETAIN(x, p, q, beta)
//  x:     the value of the upper limit x.
//  p:     the value of the parameter p.
//  q:     the value of the parameter q.
//  beta:  the value of ln B(p, q)
//
double BetaIncompleteImpl(const double x, const double p, const double q,
                          const double beta) {
  if (p < (p + q) * x) {
    // Incomplete beta function is symmetrical, so return the complement.
    return 1. - BetaIncompleteImpl(1.0 - x, q, p, beta);
  }

  double psq = p + q;
  const double kErr = 1e-14;
  const double xc = 1. - x;
  const double pre =
      std::exp(p * std::log(x) + (q - 1.) * std::log(xc) - beta) / p;

  double term = 1.;
  double ai = 1.;
  double result = 1.;
  int ns = static_cast<int>(q + xc * psq);

  // Use the soper reduction formula.
  double rx = (ns == 0) ? x : x / xc;
  double temp = q - ai;
  for (;;) {
    term = term * temp * rx / (p + ai);
    result = result + term;
    temp = std::fabs(term);
    if (temp < kErr && temp < kErr * result) {
      return result * pre;
    }
    ai = ai + 1.;
    --ns;
    if (ns >= 0) {
      temp = q - ai;
      if (ns == 0) {
        rx = x;
      }
    } else {
      temp = psq;
      psq = psq + 1.;
    }
  }

  // NOTE: See also TOMS Algorithm 708.
  // http://www.netlib.org/toms/index.html
  //
  // NOTE: The NWSC library also includes BRATIO / ISUBX (p87)
  // https://archive.org/details/DTIC_ADA261511/page/n75
}

// Direct implementation of AS109, XINBTA(p, q, beta, alpha)
// https://www.jstor.org/stable/2346798?read-now=1&seq=4#page_scan_tab_contents
// https://www.jstor.org/stable/2346887?seq=1#page_scan_tab_contents
//
// XINBTA(p, q, beta, alpha)
//  p:     the value of the parameter p.
//  q:     the value of the parameter q.
//  beta:  the value of ln B(p, q)
//  alpha: the value of the lower tail area.
//
double BetaIncompleteInvImpl(const double p, const double q, const double beta,
                             const double alpha) {
  if (alpha < 0.5) {
    // Inverse Incomplete beta function is symmetrical, return the complement.
    return 1. - BetaIncompleteInvImpl(q, p, beta, 1. - alpha);
  }
  const double kErr = 1e-14;
  double value = kErr;

  // Compute the initial estimate.
  {
    double r = std::sqrt(-std::log(alpha * alpha));
    double y =
        r - fma(r, 0.27061, 2.30753) / fma(r, fma(r, 0.04481, 0.99229), 1.0);
    if (p > 1. && q > 1.) {
      r = (y * y - 3.) / 6.;
      double s = 1. / (p + p - 1.);
      double t = 1. / (q + q - 1.);
      double h = 2. / s + t;
      double w =
          y * std::sqrt(h + r) / h - (t - s) * (r + 5. / 6. - t / (3. * h));
      value = p / (p + q * std::exp(w + w));
    } else {
      r = q + q;
      double t = 1.0 / (9. * q);
      double u = 1.0 - t + y * std::sqrt(t);
      t = r * (u * u * u);
      if (t <= 0) {
        value = 1.0 - std::exp((std::log((1.0 - alpha) * q) + beta) / q);
      } else {
        t = (4.0 * p + r - 2.0) / t;
        if (t <= 1) {
          value = std::exp((std::log(alpha * p) + beta) / p);
        } else {
          value = 1.0 - 2.0 / (t + 1.0);
        }
      }
    }
  }

  // Solve for x using a modified newton-raphson method using the function
  // BetaIncomplete.
  {
    value = std::max(value, kErr);
    value = std::min(value, 1.0 - kErr);

    const double r = 1.0 - p;
    const double t = 1.0 - q;
    double y;
    double yprev = 0;
    double sq = 1;
    double prev = 1;
    for (;;) {
      if (value < 0 || value > 1.0) {
        // Error case; value went infinite.
        return std::numeric_limits<double>::infinity();
      } else if (value == 0 || value == 1) {
        y = value;
      } else {
        y = BetaIncompleteImpl(value, p, q, beta);
        if (!std::isfinite(y)) {
          return y;
        }
      }
      y = (y - alpha) *
          std::exp(beta + r * std::log(value) + t * std::log(1.0 - value));
      if (y * yprev <= 0) {
        prev = std::max(sq, std::numeric_limits<double>::min());
      }
      double g = 1.0;
      for (;;) {
        const double adj = g * y;
        const double adj_sq = adj * adj;
        if (adj_sq >= prev) {
          g = g / 3.0;
          continue;
        }
        const double tx = value - adj;
        if (tx < 0 || tx > 1) {
          g = g / 3.0;
          continue;
        }
        if (prev < kErr) {
          return value;
        }
        if (y * y < kErr) {
          return value;
        }
        if (tx == value) {
          return value;
        }
        if (tx == 0 || tx == 1) {
          g = g / 3.0;
          continue;
        }
        value = tx;
        yprev = y;
        break;
      }
    }
  }

  // NOTES: See also: Asymptotic inversion of the incomplete beta function.
  // https://core.ac.uk/download/pdf/82140723.pdf
  //
  // NOTE: See the Boost library documentation as well:
  // https://www.boost.org/doc/libs/1_52_0/libs/math/doc/sf_and_dist/html/math_toolkit/special/sf_beta/ibeta_function.html
}

}  // namespace

double BetaIncomplete(const double x, const double p, const double q) {
  // Error cases.
  if (p < 0 || q < 0 || x < 0 || x > 1.0) {
    return std::numeric_limits<double>::infinity();
  }
  if (x == 0 || x == 1) {
    return x;
  }
  // ln(Beta(p, q))
  double beta = std::lgamma(p) + std::lgamma(q) - std::lgamma(p + q);
  return BetaIncompleteImpl(x, p, q, beta);
}

double BetaIncompleteInv(const double p, const double q, const double alpha) {
  // Error cases.
  if (p < 0 || q < 0 || alpha < 0 || alpha > 1.0) {
    return std::numeric_limits<double>::infinity();
  }
  if (alpha == 0 || alpha == 1) {
    return alpha;
  }
  // ln(Beta(p, q))
  double beta = std::lgamma(p) + std::lgamma(q) - std::lgamma(p + q);
  return BetaIncompleteInvImpl(p, q, beta, alpha);
}

// Given `num_trials` trials each with probability `p` of success, the
// probability of no failures is `p^k`. To ensure the probability of a failure
// is no more than `p_fail`, it must be that `p^k == 1 - p_fail`. This function
// computes `p` from that equation.
double RequiredSuccessProbability(const double p_fail, const int num_trials) {
  double p = std::exp(std::log(1.0 - p_fail) / static_cast<double>(num_trials));
  ABSL_ASSERT(p > 0);
  return p;
}

double ZScore(double expected_mean, const DistributionMoments& moments) {
  return (moments.mean - expected_mean) /
         (std::sqrt(moments.variance) /
          std::sqrt(static_cast<double>(moments.n)));
}

double MaxErrorTolerance(double acceptance_probability) {
  double one_sided_pvalue = 0.5 * (1.0 - acceptance_probability);
  const double max_err = InverseNormalSurvival(one_sided_pvalue);
  ABSL_ASSERT(max_err > 0);
  return max_err;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
