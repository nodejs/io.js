/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Functions for encoding of integers into prefix codes the amount of extra
   bits, and the actual values of the extra bits. */

#ifndef BROTLI_ENC_PREFIX_H_
#define BROTLI_ENC_PREFIX_H_

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/platform.h"
#include "fast_log.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Here distance_code is an intermediate code, i.e. one of the special codes or
   the actual distance increased by BROTLI_NUM_DISTANCE_SHORT_CODES - 1. */
static BROTLI_INLINE void PrefixEncodeCopyDistance(size_t distance_code,
                                                   size_t num_direct_codes,
                                                   size_t postfix_bits,
                                                   uint16_t* code,
                                                   uint32_t* extra_bits) {
  if (distance_code < BROTLI_NUM_DISTANCE_SHORT_CODES + num_direct_codes) {
    *code = (uint16_t)distance_code;
    *extra_bits = 0;
    return;
  } else {
    size_t dist = ((size_t)1 << (postfix_bits + 2u)) +
        (distance_code - BROTLI_NUM_DISTANCE_SHORT_CODES - num_direct_codes);
    size_t bucket = Log2FloorNonZero(dist) - 1;
    size_t postfix_mask = (1u << postfix_bits) - 1;
    size_t postfix = dist & postfix_mask;
    size_t prefix = (dist >> bucket) & 1;
    size_t offset = (2 + prefix) << bucket;
    size_t nbits = bucket - postfix_bits;
    *code = (uint16_t)((nbits << 10) |
        (BROTLI_NUM_DISTANCE_SHORT_CODES + num_direct_codes +
         ((2 * (nbits - 1) + prefix) << postfix_bits) + postfix));
    *extra_bits = (uint32_t)((dist - offset) >> postfix_bits);
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_PREFIX_H_ */
