/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Heuristics for deciding about the UTF8-ness of strings. */

#ifndef BROTLI_ENC_UTF8_UTIL_H_
#define BROTLI_ENC_UTF8_UTIL_H_

#include <brotli/types.h>

#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static const double kMinUTF8Ratio = 0.75;

/* Returns 1 if at least min_fraction of the bytes between pos and
   pos + length in the (data, mask) ring-buffer is UTF8-encoded, otherwise
   returns 0. */
BROTLI_INTERNAL BROTLI_BOOL BrotliIsMostlyUTF8(
    const uint8_t* data, const size_t pos, const size_t mask,
    const size_t length, const double min_fraction);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_UTF8_UTIL_H_ */
