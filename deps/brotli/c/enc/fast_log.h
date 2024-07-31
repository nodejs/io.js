/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Utilities for fast computation of logarithms. */

#ifndef BROTLI_ENC_FAST_LOG_H_
#define BROTLI_ENC_FAST_LOG_H_

#include <math.h>

#include <brotli/types.h>

#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static BROTLI_INLINE uint32_t Log2FloorNonZero(size_t n) {
#if defined(BROTLI_BSR32)
  return BROTLI_BSR32((uint32_t)n);
#else
  uint32_t result = 0;
  while (n >>= 1) result++;
  return result;
#endif
}

#define BROTLI_LOG2_TABLE_SIZE 256

/* A lookup table for small values of log2(int) to be used in entropy
   computation. */
BROTLI_INTERNAL extern const double kBrotliLog2Table[BROTLI_LOG2_TABLE_SIZE];

/* Visual Studio 2012 and Android API levels < 18 do not have the log2()
 * function defined, so we use log() and a multiplication instead. */
#if !defined(BROTLI_HAVE_LOG2)
#if ((defined(_MSC_VER) && _MSC_VER <= 1700) || \
     (defined(__ANDROID_API__) && __ANDROID_API__ < 18))
#define BROTLI_HAVE_LOG2 0
#else
#define BROTLI_HAVE_LOG2 1
#endif
#endif

#define LOG_2_INV 1.4426950408889634

/* Faster logarithm for small integers, with the property of log2(0) == 0. */
static BROTLI_INLINE double FastLog2(size_t v) {
  if (v < BROTLI_LOG2_TABLE_SIZE) {
    return kBrotliLog2Table[v];
  }
#if !(BROTLI_HAVE_LOG2)
  return log((double)v) * LOG_2_INV;
#else
  return log2((double)v);
#endif
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_FAST_LOG_H_ */
