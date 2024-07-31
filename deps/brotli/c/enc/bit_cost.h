/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Functions to estimate the bit cost of Huffman trees. */

#ifndef BROTLI_ENC_BIT_COST_H_
#define BROTLI_ENC_BIT_COST_H_

#include <brotli/types.h>

#include "../common/platform.h"
#include "fast_log.h"
#include "histogram.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static BROTLI_INLINE double ShannonEntropy(
    const uint32_t* population, size_t size, size_t* total) {
  size_t sum = 0;
  double retval = 0;
  const uint32_t* population_end = population + size;
  size_t p;
  if (size & 1) {
    goto odd_number_of_elements_left;
  }
  while (population < population_end) {
    p = *population++;
    sum += p;
    retval -= (double)p * FastLog2(p);
 odd_number_of_elements_left:
    p = *population++;
    sum += p;
    retval -= (double)p * FastLog2(p);
  }
  if (sum) retval += (double)sum * FastLog2(sum);
  *total = sum;
  return retval;
}

static BROTLI_INLINE double BitsEntropy(
    const uint32_t* population, size_t size) {
  size_t sum;
  double retval = ShannonEntropy(population, size, &sum);
  if (retval < (double)sum) {
    /* At least one bit per literal is needed. */
    retval = (double)sum;
  }
  return retval;
}

BROTLI_INTERNAL double BrotliPopulationCostLiteral(const HistogramLiteral*);
BROTLI_INTERNAL double BrotliPopulationCostCommand(const HistogramCommand*);
BROTLI_INTERNAL double BrotliPopulationCostDistance(const HistogramDistance*);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_BIT_COST_H_ */
