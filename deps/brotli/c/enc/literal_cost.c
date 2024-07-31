/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Literal cost model to allow backward reference replacement to be efficient.
*/

#include "literal_cost.h"

#include <string.h>  /* memset */

#include <brotli/types.h>

#include "../common/platform.h"
#include "fast_log.h"
#include "utf8_util.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static size_t UTF8Position(size_t last, size_t c, size_t clamp) {
  if (c < 128) {
    return 0;  /* Next one is the 'Byte 1' again. */
  } else if (c >= 192) {  /* Next one is the 'Byte 2' of utf-8 encoding. */
    return BROTLI_MIN(size_t, 1, clamp);
  } else {
    /* Let's decide over the last byte if this ends the sequence. */
    if (last < 0xE0) {
      return 0;  /* Completed two or three byte coding. */
    } else {  /* Next one is the 'Byte 3' of utf-8 encoding. */
      return BROTLI_MIN(size_t, 2, clamp);
    }
  }
}

static size_t DecideMultiByteStatsLevel(size_t pos, size_t len, size_t mask,
                                        const uint8_t* data) {
  size_t counts[3] = { 0 };
  size_t max_utf8 = 1;  /* should be 2, but 1 compresses better. */
  size_t last_c = 0;
  size_t i;
  for (i = 0; i < len; ++i) {
    size_t c = data[(pos + i) & mask];
    ++counts[UTF8Position(last_c, c, 2)];
    last_c = c;
  }
  if (counts[2] < 500) {
    max_utf8 = 1;
  }
  if (counts[1] + counts[2] < 25) {
    max_utf8 = 0;
  }
  return max_utf8;
}

static void EstimateBitCostsForLiteralsUTF8(size_t pos, size_t len, size_t mask,
                                            const uint8_t* data,
                                            size_t* histogram, float* cost) {
  /* max_utf8 is 0 (normal ASCII single byte modeling),
     1 (for 2-byte UTF-8 modeling), or 2 (for 3-byte UTF-8 modeling). */
  const size_t max_utf8 = DecideMultiByteStatsLevel(pos, len, mask, data);
  size_t window_half = 495;
  size_t in_window = BROTLI_MIN(size_t, window_half, len);
  size_t in_window_utf8[3] = { 0 };
  size_t i;
  memset(histogram, 0, 3 * 256 * sizeof(histogram[0]));

  {  /* Bootstrap histograms. */
    size_t last_c = 0;
    size_t utf8_pos = 0;
    for (i = 0; i < in_window; ++i) {
      size_t c = data[(pos + i) & mask];
      ++histogram[256 * utf8_pos + c];
      ++in_window_utf8[utf8_pos];
      utf8_pos = UTF8Position(last_c, c, max_utf8);
      last_c = c;
    }
  }

  /* Compute bit costs with sliding window. */
  for (i = 0; i < len; ++i) {
    if (i >= window_half) {
      /* Remove a byte in the past. */
      size_t c =
          i < window_half + 1 ? 0 : data[(pos + i - window_half - 1) & mask];
      size_t last_c =
          i < window_half + 2 ? 0 : data[(pos + i - window_half - 2) & mask];
      size_t utf8_pos2 = UTF8Position(last_c, c, max_utf8);
      --histogram[256 * utf8_pos2 + data[(pos + i - window_half) & mask]];
      --in_window_utf8[utf8_pos2];
    }
    if (i + window_half < len) {
      /* Add a byte in the future. */
      size_t c = data[(pos + i + window_half - 1) & mask];
      size_t last_c = data[(pos + i + window_half - 2) & mask];
      size_t utf8_pos2 = UTF8Position(last_c, c, max_utf8);
      ++histogram[256 * utf8_pos2 + data[(pos + i + window_half) & mask]];
      ++in_window_utf8[utf8_pos2];
    }
    {
      size_t c = i < 1 ? 0 : data[(pos + i - 1) & mask];
      size_t last_c = i < 2 ? 0 : data[(pos + i - 2) & mask];
      size_t utf8_pos = UTF8Position(last_c, c, max_utf8);
      size_t masked_pos = (pos + i) & mask;
      size_t histo = histogram[256 * utf8_pos + data[masked_pos]];
      double lit_cost;
      if (histo == 0) {
        histo = 1;
      }
      lit_cost = FastLog2(in_window_utf8[utf8_pos]) - FastLog2(histo);
      lit_cost += 0.02905;
      if (lit_cost < 1.0) {
        lit_cost *= 0.5;
        lit_cost += 0.5;
      }
      /* Make the first bytes more expensive -- seems to help, not sure why.
         Perhaps because the entropy source is changing its properties
         rapidly in the beginning of the file, perhaps because the beginning
         of the data is a statistical "anomaly". */
      if (i < 2000) {
        lit_cost += 0.7 - ((double)(2000 - i) / 2000.0 * 0.35);
      }
      cost[i] = (float)lit_cost;
    }
  }
}

void BrotliEstimateBitCostsForLiterals(size_t pos, size_t len, size_t mask,
                                       const uint8_t* data,
                                       size_t* histogram, float* cost) {
  if (BrotliIsMostlyUTF8(data, pos, mask, len, kMinUTF8Ratio)) {
    EstimateBitCostsForLiteralsUTF8(pos, len, mask, data, histogram, cost);
    return;
  } else {
    size_t window_half = 2000;
    size_t in_window = BROTLI_MIN(size_t, window_half, len);
    size_t i;
    memset(histogram, 0, 256 * sizeof(histogram[0]));

    /* Bootstrap histogram. */
    for (i = 0; i < in_window; ++i) {
      ++histogram[data[(pos + i) & mask]];
    }

    /* Compute bit costs with sliding window. */
    for (i = 0; i < len; ++i) {
      size_t histo;
      if (i >= window_half) {
        /* Remove a byte in the past. */
        --histogram[data[(pos + i - window_half) & mask]];
        --in_window;
      }
      if (i + window_half < len) {
        /* Add a byte in the future. */
        ++histogram[data[(pos + i + window_half) & mask]];
        ++in_window;
      }
      histo = histogram[data[(pos + i) & mask]];
      if (histo == 0) {
        histo = 1;
      }
      {
        double lit_cost = FastLog2(in_window) - FastLog2(histo);
        lit_cost += 0.029;
        if (lit_cost < 1.0) {
          lit_cost *= 0.5;
          lit_cost += 0.5;
        }
        cost[i] = (float)lit_cost;
      }
    }
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
