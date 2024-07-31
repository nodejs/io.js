/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Functions to estimate the bit cost of Huffman trees. */

#include "bit_cost.h"

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/platform.h"
#include "fast_log.h"
#include "histogram.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define FN(X) X ## Literal
#include "bit_cost_inc.h"  /* NOLINT(build/include) */
#undef FN

#define FN(X) X ## Command
#include "bit_cost_inc.h"  /* NOLINT(build/include) */
#undef FN

#define FN(X) X ## Distance
#include "bit_cost_inc.h"  /* NOLINT(build/include) */
#undef FN

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
