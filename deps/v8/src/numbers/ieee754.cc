// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/numbers/ieee754.h"

#include <cmath>

#include "src/base/ieee754.h"
#include "src/flags/flags.h"

namespace v8::internal::math {

double pow(double x, double y) {
  if (v8_flags.use_std_math_pow) {
    if (std::isnan(y)) {
      // 1. If exponent is NaN, return NaN.
      return std::numeric_limits<double>::quiet_NaN();
    }
    if (std::isinf(y) && (x == 1 || x == -1)) {
      // 9. If exponent is +∞𝔽, then
      //   b. If abs(ℝ(base)) = 1, return NaN.
      // and
      // 10. If exponent is -∞𝔽, then
      //   b. If abs(ℝ(base)) = 1, return NaN.
      return std::numeric_limits<double>::quiet_NaN();
    }
    if (std::isnan(x)) {
      // std::pow distinguishes between quiet and signaling NaN; JS doesn't.
      x = std::numeric_limits<double>::quiet_NaN();
    }

    // The following special cases just exist to match the optimizing compilers'
    // behavior, which avoid calls to `pow` in those cases.
    if (y == 2) {
      // x ** 2   ==>   x * x
      return x * x;
    } else if (y == 0.5) {
      // x ** 0.5   ==>  sqrt(x), except if x is -Infinity
      if (std::isinf(x)) {
        return std::numeric_limits<double>::infinity();
      } else {
        // Note the +0 so that we get +0 for -0**0.5 rather than -0.
        return std::sqrt(x + 0);
      }
    }

    return std::pow(x, y);
  }
  return base::ieee754::legacy::pow(x, y);
}

}  // namespace v8::internal::math
