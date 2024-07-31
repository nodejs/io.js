/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Constants and formulas that affect speed-ratio trade-offs and thus define
   quality levels. */

#ifndef BROTLI_ENC_QUALITY_H_
#define BROTLI_ENC_QUALITY_H_

#include <brotli/encode.h>

#include "../common/platform.h"
#include "params.h"

#define FAST_ONE_PASS_COMPRESSION_QUALITY 0
#define FAST_TWO_PASS_COMPRESSION_QUALITY 1
#define ZOPFLIFICATION_QUALITY 10
#define HQ_ZOPFLIFICATION_QUALITY 11

#define MAX_QUALITY_FOR_STATIC_ENTROPY_CODES 2
#define MIN_QUALITY_FOR_BLOCK_SPLIT 4
#define MIN_QUALITY_FOR_NONZERO_DISTANCE_PARAMS 4
#define MIN_QUALITY_FOR_OPTIMIZE_HISTOGRAMS 4
#define MIN_QUALITY_FOR_EXTENSIVE_REFERENCE_SEARCH 5
#define MIN_QUALITY_FOR_CONTEXT_MODELING 5
#define MIN_QUALITY_FOR_HQ_CONTEXT_MODELING 7
#define MIN_QUALITY_FOR_HQ_BLOCK_SPLITTING 10

/* For quality below MIN_QUALITY_FOR_BLOCK_SPLIT there is no block splitting,
   so we buffer at most this much literals and commands. */
#define MAX_NUM_DELAYED_SYMBOLS 0x2FFF

/* Returns hash-table size for quality levels 0 and 1. */
static BROTLI_INLINE size_t MaxHashTableSize(int quality) {
  return quality == FAST_ONE_PASS_COMPRESSION_QUALITY ? 1 << 15 : 1 << 17;
}

/* The maximum length for which the zopflification uses distinct distances. */
#define MAX_ZOPFLI_LEN_QUALITY_10 150
#define MAX_ZOPFLI_LEN_QUALITY_11 325

/* Do not thoroughly search when a long copy is found. */
#define BROTLI_LONG_COPY_QUICK_STEP 16384

static BROTLI_INLINE size_t MaxZopfliLen(const BrotliEncoderParams* params) {
  return params->quality <= 10 ?
      MAX_ZOPFLI_LEN_QUALITY_10 :
      MAX_ZOPFLI_LEN_QUALITY_11;
}

/* Number of best candidates to evaluate to expand Zopfli chain. */
static BROTLI_INLINE size_t MaxZopfliCandidates(
  const BrotliEncoderParams* params) {
  return params->quality <= 10 ? 1 : 5;
}

static BROTLI_INLINE void SanitizeParams(BrotliEncoderParams* params) {
  params->quality = BROTLI_MIN(int, BROTLI_MAX_QUALITY,
      BROTLI_MAX(int, BROTLI_MIN_QUALITY, params->quality));
  if (params->quality <= MAX_QUALITY_FOR_STATIC_ENTROPY_CODES) {
    params->large_window = BROTLI_FALSE;
  }
  if (params->lgwin < BROTLI_MIN_WINDOW_BITS) {
    params->lgwin = BROTLI_MIN_WINDOW_BITS;
  } else {
    int max_lgwin = params->large_window ? BROTLI_LARGE_MAX_WINDOW_BITS :
                                           BROTLI_MAX_WINDOW_BITS;
    if (params->lgwin > max_lgwin) params->lgwin = max_lgwin;
  }
}

/* Returns optimized lg_block value. */
static BROTLI_INLINE int ComputeLgBlock(const BrotliEncoderParams* params) {
  int lgblock = params->lgblock;
  if (params->quality == FAST_ONE_PASS_COMPRESSION_QUALITY ||
      params->quality == FAST_TWO_PASS_COMPRESSION_QUALITY) {
    lgblock = params->lgwin;
  } else if (params->quality < MIN_QUALITY_FOR_BLOCK_SPLIT) {
    lgblock = 14;
  } else if (lgblock == 0) {
    lgblock = 16;
    if (params->quality >= 9 && params->lgwin > lgblock) {
      lgblock = BROTLI_MIN(int, 18, params->lgwin);
    }
  } else {
    lgblock = BROTLI_MIN(int, BROTLI_MAX_INPUT_BLOCK_BITS,
        BROTLI_MAX(int, BROTLI_MIN_INPUT_BLOCK_BITS, lgblock));
  }
  return lgblock;
}

/* Returns log2 of the size of main ring buffer area.
   Allocate at least lgwin + 1 bits for the ring buffer so that the newly
   added block fits there completely and we still get lgwin bits and at least
   read_block_size_bits + 1 bits because the copy tail length needs to be
   smaller than ring-buffer size. */
static BROTLI_INLINE int ComputeRbBits(const BrotliEncoderParams* params) {
  return 1 + BROTLI_MAX(int, params->lgwin, params->lgblock);
}

static BROTLI_INLINE size_t MaxMetablockSize(
    const BrotliEncoderParams* params) {
  int bits =
      BROTLI_MIN(int, ComputeRbBits(params), BROTLI_MAX_INPUT_BLOCK_BITS);
  return (size_t)1 << bits;
}

/* When searching for backward references and have not seen matches for a long
   time, we can skip some match lookups. Unsuccessful match lookups are very
   expensive and this kind of a heuristic speeds up compression quite a lot.
   At first 8 byte strides are taken and every second byte is put to hasher.
   After 4x more literals stride by 16 bytes, every put 4-th byte to hasher.
   Applied only to qualities 2 to 9. */
static BROTLI_INLINE size_t LiteralSpreeLengthForSparseSearch(
    const BrotliEncoderParams* params) {
  return params->quality < 9 ? 64 : 512;
}

/* Quality to hasher mapping:

   - q02: h02 (longest_match_quickly), b16, l5

   - q03: h03 (longest_match_quickly), b17, l5

   - q04: h04 (longest_match_quickly), b17, l5
   - q04: h54 (longest_match_quickly), b20, l7 | for large files

   - q05: h05 (longest_match        ), b14, l4
   - q05: h06 (longest_match64      ), b15, l5 | for large files
   - q05: h40 (forgetful_chain      ), b15, l4 | for small window

   - q06: h05 (longest_match        ), b14, l4
   - q06: h06 (longest_match64      ), b15, l5 | for large files
   - q06: h40 (forgetful_chain      ), b15, l4 | for small window

   - q07: h05 (longest_match        ), b15, l4
   - q07: h06 (longest_match64      ), b15, l5 | for large files
   - q07: h41 (forgetful_chain      ), b15, l4 | for small window

   - q08: h05 (longest_match        ), b15, l4
   - q08: h06 (longest_match64      ), b15, l5 | for large files
   - q08: h41 (forgetful_chain      ), b15, l4 | for small window

   - q09: h05 (longest_match        ), b15, l4
   - q09: h06 (longest_match64      ), b15, l5 | for large files
   - q09: h42 (forgetful_chain      ), b15, l4 | for small window

   - q10: t10 (to_binary_tree       ), b17, l128

   - q11: t10 (to_binary_tree       ), b17, l128

  Where "q" is quality, "h" is hasher type, "b" is bucket bits,
  "l" is source len. */
static BROTLI_INLINE void ChooseHasher(const BrotliEncoderParams* params,
                                       BrotliHasherParams* hparams) {
  if (params->quality > 9) {
    hparams->type = 10;
  } else if (params->quality == 4 && params->size_hint >= (1 << 20)) {
    hparams->type = 54;
  } else if (params->quality < 5) {
    hparams->type = params->quality;
  } else if (params->lgwin <= 16) {
    hparams->type = params->quality < 7 ? 40 : params->quality < 9 ? 41 : 42;
  } else if (params->size_hint >= (1 << 20) && params->lgwin >= 19) {
    hparams->type = 6;
    hparams->block_bits = params->quality - 1;
    hparams->bucket_bits = 15;
    hparams->num_last_distances_to_check =
        params->quality < 7 ? 4 : params->quality < 9 ? 10 : 16;
  } else {
    /* TODO(eustas): often previous setting (H6) is faster and denser; consider
                     adding an option to use it. */
    hparams->type = 5;
    hparams->block_bits = params->quality - 1;
    hparams->bucket_bits = params->quality < 7 ? 14 : 15;
    hparams->num_last_distances_to_check =
        params->quality < 7 ? 4 : params->quality < 9 ? 10 : 16;
  }

  if (params->lgwin > 24) {
    /* Different hashers for large window brotli: not for qualities <= 2,
       these are too fast for large window. Not for qualities >= 10: their
       hasher already works well with large window. So the changes are:
       H3 --> H35: for quality 3.
       H54 --> H55: for quality 4 with size hint > 1MB
       H6 --> H65: for qualities 5, 6, 7, 8, 9. */
    if (hparams->type == 3) {
      hparams->type = 35;
    }
    if (hparams->type == 54) {
      hparams->type = 55;
    }
    if (hparams->type == 6) {
      hparams->type = 65;
    }
  }
}

#endif  /* BROTLI_ENC_QUALITY_H_ */
