/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Literal cost model to allow backward reference replacement to be efficient.
*/

#ifndef BROTLI_ENC_LITERAL_COST_H_
#define BROTLI_ENC_LITERAL_COST_H_

#include <brotli/types.h>

#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Estimates how many bits the literals in the interval [pos, pos + len) in the
   ring-buffer (data, mask) will take entropy coded and writes these estimates
   to the cost[0..len) array. */
BROTLI_INTERNAL void BrotliEstimateBitCostsForLiterals(
    size_t pos, size_t len, size_t mask, const uint8_t* data, size_t* histogram,
    float* cost);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_LITERAL_COST_H_ */
