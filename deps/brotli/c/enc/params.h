/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Parameters for the Brotli encoder with chosen quality levels. */

#ifndef BROTLI_ENC_PARAMS_H_
#define BROTLI_ENC_PARAMS_H_

#include <brotli/encode.h>

#include "encoder_dict.h"

typedef struct BrotliHasherParams {
  int type;
  int bucket_bits;
  int block_bits;
  int num_last_distances_to_check;
} BrotliHasherParams;

typedef struct BrotliDistanceParams {
  uint32_t distance_postfix_bits;
  uint32_t num_direct_distance_codes;
  uint32_t alphabet_size_max;
  uint32_t alphabet_size_limit;
  size_t max_distance;
} BrotliDistanceParams;

/* Encoding parameters */
typedef struct BrotliEncoderParams {
  BrotliEncoderMode mode;
  int quality;
  int lgwin;
  int lgblock;
  size_t stream_offset;
  size_t size_hint;
  BROTLI_BOOL disable_literal_context_modeling;
  BROTLI_BOOL large_window;
  BrotliHasherParams hasher;
  BrotliDistanceParams dist;
  /* TODO(eustas): rename to BrotliShared... */
  SharedEncoderDictionary dictionary;
} BrotliEncoderParams;

#endif  /* BROTLI_ENC_PARAMS_H_ */
