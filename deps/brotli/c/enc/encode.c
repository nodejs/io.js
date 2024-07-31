/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Implementation of Brotli compressor. */

#include <brotli/encode.h>
#include <brotli/shared_dictionary.h>
#include <brotli/types.h>

#include <stdlib.h>  /* free, malloc */
#include <string.h>  /* memcpy, memset */

#include "../common/constants.h"
#include "../common/context.h"
#include "../common/platform.h"
#include "../common/version.h"
#include "backward_references.h"
#include "backward_references_hq.h"
#include "bit_cost.h"
#include "brotli_bit_stream.h"
#include "compress_fragment.h"
#include "compress_fragment_two_pass.h"
#include "dictionary_hash.h"
#include "encoder_dict.h"
#include "entropy_encode.h"
#include "fast_log.h"
#include "hash.h"
#include "histogram.h"
#include "memory.h"
#include "metablock.h"
#include "prefix.h"
#include "state.h"
#include "quality.h"
#include "ringbuffer.h"
#include "utf8_util.h"
#include "write_bits.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define COPY_ARRAY(dst, src) memcpy(dst, src, sizeof(src));

static size_t InputBlockSize(BrotliEncoderState* s) {
  return (size_t)1 << s->params.lgblock;
}

static uint64_t UnprocessedInputSize(BrotliEncoderState* s) {
  return s->input_pos_ - s->last_processed_pos_;
}

static size_t RemainingInputBlockSize(BrotliEncoderState* s) {
  const uint64_t delta = UnprocessedInputSize(s);
  size_t block_size = InputBlockSize(s);
  if (delta >= block_size) return 0;
  return block_size - (size_t)delta;
}

BROTLI_BOOL BrotliEncoderSetParameter(
    BrotliEncoderState* state, BrotliEncoderParameter p, uint32_t value) {
  /* Changing parameters on the fly is not implemented yet. */
  if (state->is_initialized_) return BROTLI_FALSE;
  /* TODO(eustas): Validate/clamp parameters here. */
  switch (p) {
    case BROTLI_PARAM_MODE:
      state->params.mode = (BrotliEncoderMode)value;
      return BROTLI_TRUE;

    case BROTLI_PARAM_QUALITY:
      state->params.quality = (int)value;
      return BROTLI_TRUE;

    case BROTLI_PARAM_LGWIN:
      state->params.lgwin = (int)value;
      return BROTLI_TRUE;

    case BROTLI_PARAM_LGBLOCK:
      state->params.lgblock = (int)value;
      return BROTLI_TRUE;

    case BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING:
      if ((value != 0) && (value != 1)) return BROTLI_FALSE;
      state->params.disable_literal_context_modeling = TO_BROTLI_BOOL(!!value);
      return BROTLI_TRUE;

    case BROTLI_PARAM_SIZE_HINT:
      state->params.size_hint = value;
      return BROTLI_TRUE;

    case BROTLI_PARAM_LARGE_WINDOW:
      state->params.large_window = TO_BROTLI_BOOL(!!value);
      return BROTLI_TRUE;

    case BROTLI_PARAM_NPOSTFIX:
      state->params.dist.distance_postfix_bits = value;
      return BROTLI_TRUE;

    case BROTLI_PARAM_NDIRECT:
      state->params.dist.num_direct_distance_codes = value;
      return BROTLI_TRUE;

    case BROTLI_PARAM_STREAM_OFFSET:
      if (value > (1u << 30)) return BROTLI_FALSE;
      state->params.stream_offset = value;
      return BROTLI_TRUE;

    default: return BROTLI_FALSE;
  }
}

/* Wraps 64-bit input position to 32-bit ring-buffer position preserving
   "not-a-first-lap" feature. */
static uint32_t WrapPosition(uint64_t position) {
  uint32_t result = (uint32_t)position;
  uint64_t gb = position >> 30;
  if (gb > 2) {
    /* Wrap every 2GiB; The first 3GB are continuous. */
    result = (result & ((1u << 30) - 1)) | ((uint32_t)((gb - 1) & 1) + 1) << 30;
  }
  return result;
}

static uint8_t* GetBrotliStorage(BrotliEncoderState* s, size_t size) {
  MemoryManager* m = &s->memory_manager_;
  if (s->storage_size_ < size) {
    BROTLI_FREE(m, s->storage_);
    s->storage_ = BROTLI_ALLOC(m, uint8_t, size);
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(s->storage_)) return NULL;
    s->storage_size_ = size;
  }
  return s->storage_;
}

static size_t HashTableSize(size_t max_table_size, size_t input_size) {
  size_t htsize = 256;
  while (htsize < max_table_size && htsize < input_size) {
    htsize <<= 1;
  }
  return htsize;
}

static int* GetHashTable(BrotliEncoderState* s, int quality,
                         size_t input_size, size_t* table_size) {
  /* Use smaller hash table when input.size() is smaller, since we
     fill the table, incurring O(hash table size) overhead for
     compression, and if the input is short, we won't need that
     many hash table entries anyway. */
  MemoryManager* m = &s->memory_manager_;
  const size_t max_table_size = MaxHashTableSize(quality);
  size_t htsize = HashTableSize(max_table_size, input_size);
  int* table;
  BROTLI_DCHECK(max_table_size >= 256);
  if (quality == FAST_ONE_PASS_COMPRESSION_QUALITY) {
    /* Only odd shifts are supported by fast-one-pass. */
    if ((htsize & 0xAAAAA) == 0) {
      htsize <<= 1;
    }
  }

  if (htsize <= sizeof(s->small_table_) / sizeof(s->small_table_[0])) {
    table = s->small_table_;
  } else {
    if (htsize > s->large_table_size_) {
      s->large_table_size_ = htsize;
      BROTLI_FREE(m, s->large_table_);
      s->large_table_ = BROTLI_ALLOC(m, int, htsize);
      if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(s->large_table_)) return 0;
    }
    table = s->large_table_;
  }

  *table_size = htsize;
  memset(table, 0, htsize * sizeof(*table));
  return table;
}

static void EncodeWindowBits(int lgwin, BROTLI_BOOL large_window,
    uint16_t* last_bytes, uint8_t* last_bytes_bits) {
  if (large_window) {
    *last_bytes = (uint16_t)(((lgwin & 0x3F) << 8) | 0x11);
    *last_bytes_bits = 14;
  } else {
    if (lgwin == 16) {
      *last_bytes = 0;
      *last_bytes_bits = 1;
    } else if (lgwin == 17) {
      *last_bytes = 1;
      *last_bytes_bits = 7;
    } else if (lgwin > 17) {
      *last_bytes = (uint16_t)(((lgwin - 17) << 1) | 0x01);
      *last_bytes_bits = 4;
    } else {
      *last_bytes = (uint16_t)(((lgwin - 8) << 4) | 0x01);
      *last_bytes_bits = 7;
    }
  }
}

/* TODO(eustas): move to compress_fragment.c? */
/* Initializes the command and distance prefix codes for the first block. */
static void InitCommandPrefixCodes(BrotliOnePassArena* s) {
  static const uint8_t kDefaultCommandDepths[128] = {
    0, 4, 4, 5, 6, 6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8,
    0, 0, 0, 4, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7,
    7, 7, 10, 10, 10, 10, 10, 10, 0, 4, 4, 5, 5, 5, 6, 6,
    7, 8, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4,
    4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 7, 7, 7, 8, 10,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
  };
  static const uint16_t kDefaultCommandBits[128] = {
    0,   0,   8,   9,   3,  35,   7,   71,
    39, 103,  23,  47, 175, 111, 239,   31,
    0,   0,   0,   4,  12,   2,  10,    6,
    13,  29,  11,  43,  27,  59,  87,   55,
    15,  79, 319, 831, 191, 703, 447,  959,
    0,  14,   1,  25,   5,  21,  19,   51,
    119, 159,  95, 223, 479, 991,  63,  575,
    127, 639, 383, 895, 255, 767, 511, 1023,
    14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    27, 59, 7, 39, 23, 55, 30, 1, 17, 9, 25, 5, 0, 8, 4, 12,
    2, 10, 6, 21, 13, 29, 3, 19, 11, 15, 47, 31, 95, 63, 127, 255,
    767, 2815, 1791, 3839, 511, 2559, 1535, 3583, 1023, 3071, 2047, 4095,
  };
  static const uint8_t kDefaultCommandCode[] = {
    0xff, 0x77, 0xd5, 0xbf, 0xe7, 0xde, 0xea, 0x9e, 0x51, 0x5d, 0xde, 0xc6,
    0x70, 0x57, 0xbc, 0x58, 0x58, 0x58, 0xd8, 0xd8, 0x58, 0xd5, 0xcb, 0x8c,
    0xea, 0xe0, 0xc3, 0x87, 0x1f, 0x83, 0xc1, 0x60, 0x1c, 0x67, 0xb2, 0xaa,
    0x06, 0x83, 0xc1, 0x60, 0x30, 0x18, 0xcc, 0xa1, 0xce, 0x88, 0x54, 0x94,
    0x46, 0xe1, 0xb0, 0xd0, 0x4e, 0xb2, 0xf7, 0x04, 0x00,
  };
  static const size_t kDefaultCommandCodeNumBits = 448;
  COPY_ARRAY(s->cmd_depth, kDefaultCommandDepths);
  COPY_ARRAY(s->cmd_bits, kDefaultCommandBits);

  /* Initialize the pre-compressed form of the command and distance prefix
     codes. */
  COPY_ARRAY(s->cmd_code, kDefaultCommandCode);
  s->cmd_code_numbits = kDefaultCommandCodeNumBits;
}

/* Decide about the context map based on the ability of the prediction
   ability of the previous byte UTF8-prefix on the next byte. The
   prediction ability is calculated as Shannon entropy. Here we need
   Shannon entropy instead of 'BitsEntropy' since the prefix will be
   encoded with the remaining 6 bits of the following byte, and
   BitsEntropy will assume that symbol to be stored alone using Huffman
   coding. */
static void ChooseContextMap(int quality,
                             uint32_t* bigram_histo,
                             size_t* num_literal_contexts,
                             const uint32_t** literal_context_map) {
  static const uint32_t kStaticContextMapContinuation[64] = {
    1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  static const uint32_t kStaticContextMapSimpleUTF8[64] = {
    0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };

  uint32_t monogram_histo[3] = { 0 };
  uint32_t two_prefix_histo[6] = { 0 };
  size_t total;
  size_t i;
  size_t dummy;
  double entropy[4];
  for (i = 0; i < 9; ++i) {
    monogram_histo[i % 3] += bigram_histo[i];
    two_prefix_histo[i % 6] += bigram_histo[i];
  }
  entropy[1] = ShannonEntropy(monogram_histo, 3, &dummy);
  entropy[2] = (ShannonEntropy(two_prefix_histo, 3, &dummy) +
                ShannonEntropy(two_prefix_histo + 3, 3, &dummy));
  entropy[3] = 0;
  for (i = 0; i < 3; ++i) {
    entropy[3] += ShannonEntropy(bigram_histo + 3 * i, 3, &dummy);
  }

  total = monogram_histo[0] + monogram_histo[1] + monogram_histo[2];
  BROTLI_DCHECK(total != 0);
  entropy[0] = 1.0 / (double)total;
  entropy[1] *= entropy[0];
  entropy[2] *= entropy[0];
  entropy[3] *= entropy[0];

  if (quality < MIN_QUALITY_FOR_HQ_CONTEXT_MODELING) {
    /* 3 context models is a bit slower, don't use it at lower qualities. */
    entropy[3] = entropy[1] * 10;
  }
  /* If expected savings by symbol are less than 0.2 bits, skip the
     context modeling -- in exchange for faster decoding speed. */
  if (entropy[1] - entropy[2] < 0.2 &&
      entropy[1] - entropy[3] < 0.2) {
    *num_literal_contexts = 1;
  } else if (entropy[2] - entropy[3] < 0.02) {
    *num_literal_contexts = 2;
    *literal_context_map = kStaticContextMapSimpleUTF8;
  } else {
    *num_literal_contexts = 3;
    *literal_context_map = kStaticContextMapContinuation;
  }
}

/* Decide if we want to use a more complex static context map containing 13
   context values, based on the entropy reduction of histograms over the
   first 5 bits of literals. */
static BROTLI_BOOL ShouldUseComplexStaticContextMap(const uint8_t* input,
    size_t start_pos, size_t length, size_t mask, int quality, size_t size_hint,
    size_t* num_literal_contexts, const uint32_t** literal_context_map,
    uint32_t* arena) {
  static const uint32_t kStaticContextMapComplexUTF8[64] = {
    11, 11, 12, 12, /* 0 special */
    0, 0, 0, 0, /* 4 lf */
    1, 1, 9, 9, /* 8 space */
    2, 2, 2, 2, /* !, first after space/lf and after something else. */
    1, 1, 1, 1, /* " */
    8, 3, 3, 3, /* % */
    1, 1, 1, 1, /* ({[ */
    2, 2, 2, 2, /* }]) */
    8, 4, 4, 4, /* :; */
    8, 7, 4, 4, /* . */
    8, 0, 0, 0, /* > */
    3, 3, 3, 3, /* [0..9] */
    5, 5, 10, 5, /* [A-Z] */
    5, 5, 10, 5,
    6, 6, 6, 6, /* [a-z] */
    6, 6, 6, 6,
  };
  BROTLI_UNUSED(quality);
  /* Try the more complex static context map only for long data. */
  if (size_hint < (1 << 20)) {
    return BROTLI_FALSE;
  } else {
    const size_t end_pos = start_pos + length;
    /* To make entropy calculations faster, we collect histograms
       over the 5 most significant bits of literals. One histogram
       without context and 13 additional histograms for each context value. */
    uint32_t* BROTLI_RESTRICT const combined_histo = arena;
    uint32_t* BROTLI_RESTRICT const context_histo = arena + 32;
    uint32_t total = 0;
    double entropy[3];
    size_t dummy;
    size_t i;
    ContextLut utf8_lut = BROTLI_CONTEXT_LUT(CONTEXT_UTF8);
    memset(arena, 0, sizeof(arena[0]) * 32 * 14);
    for (; start_pos + 64 <= end_pos; start_pos += 4096) {
      const size_t stride_end_pos = start_pos + 64;
      uint8_t prev2 = input[start_pos & mask];
      uint8_t prev1 = input[(start_pos + 1) & mask];
      size_t pos;
      /* To make the analysis of the data faster we only examine 64 byte long
         strides at every 4kB intervals. */
      for (pos = start_pos + 2; pos < stride_end_pos; ++pos) {
        const uint8_t literal = input[pos & mask];
        const uint8_t context = (uint8_t)kStaticContextMapComplexUTF8[
            BROTLI_CONTEXT(prev1, prev2, utf8_lut)];
        ++total;
        ++combined_histo[literal >> 3];
        ++context_histo[(context << 5) + (literal >> 3)];
        prev2 = prev1;
        prev1 = literal;
      }
    }
    entropy[1] = ShannonEntropy(combined_histo, 32, &dummy);
    entropy[2] = 0;
    for (i = 0; i < 13; ++i) {
      entropy[2] += ShannonEntropy(context_histo + (i << 5), 32, &dummy);
    }
    entropy[0] = 1.0 / (double)total;
    entropy[1] *= entropy[0];
    entropy[2] *= entropy[0];
    /* The triggering heuristics below were tuned by compressing the individual
       files of the silesia corpus. If we skip this kind of context modeling
       for not very well compressible input (i.e. entropy using context modeling
       is 60% of maximal entropy) or if expected savings by symbol are less
       than 0.2 bits, then in every case when it triggers, the final compression
       ratio is improved. Note however that this heuristics might be too strict
       for some cases and could be tuned further. */
    if (entropy[2] > 3.0 || entropy[1] - entropy[2] < 0.2) {
      return BROTLI_FALSE;
    } else {
      *num_literal_contexts = 13;
      *literal_context_map = kStaticContextMapComplexUTF8;
      return BROTLI_TRUE;
    }
  }
}

static void DecideOverLiteralContextModeling(const uint8_t* input,
    size_t start_pos, size_t length, size_t mask, int quality, size_t size_hint,
    size_t* num_literal_contexts, const uint32_t** literal_context_map,
    uint32_t* arena) {
  if (quality < MIN_QUALITY_FOR_CONTEXT_MODELING || length < 64) {
    return;
  } else if (ShouldUseComplexStaticContextMap(
      input, start_pos, length, mask, quality, size_hint,
      num_literal_contexts, literal_context_map, arena)) {
    /* Context map was already set, nothing else to do. */
  } else {
    /* Gather bi-gram data of the UTF8 byte prefixes. To make the analysis of
       UTF8 data faster we only examine 64 byte long strides at every 4kB
       intervals. */
    const size_t end_pos = start_pos + length;
    uint32_t* BROTLI_RESTRICT const bigram_prefix_histo = arena;
    memset(bigram_prefix_histo, 0, sizeof(arena[0]) * 9);
    for (; start_pos + 64 <= end_pos; start_pos += 4096) {
      static const int lut[4] = { 0, 0, 1, 2 };
      const size_t stride_end_pos = start_pos + 64;
      int prev = lut[input[start_pos & mask] >> 6] * 3;
      size_t pos;
      for (pos = start_pos + 1; pos < stride_end_pos; ++pos) {
        const uint8_t literal = input[pos & mask];
        ++bigram_prefix_histo[prev + lut[literal >> 6]];
        prev = lut[literal >> 6] * 3;
      }
    }
    ChooseContextMap(quality, &bigram_prefix_histo[0], num_literal_contexts,
                     literal_context_map);
  }
}

static BROTLI_BOOL ShouldCompress(
    const uint8_t* data, const size_t mask, const uint64_t last_flush_pos,
    const size_t bytes, const size_t num_literals, const size_t num_commands) {
  /* TODO(eustas): find more precise minimal block overhead. */
  if (bytes <= 2) return BROTLI_FALSE;
  if (num_commands < (bytes >> 8) + 2) {
    if ((double)num_literals > 0.99 * (double)bytes) {
      uint32_t literal_histo[256] = { 0 };
      static const uint32_t kSampleRate = 13;
      static const double kMinEntropy = 7.92;
      const double bit_cost_threshold =
          (double)bytes * kMinEntropy / kSampleRate;
      size_t t = (bytes + kSampleRate - 1) / kSampleRate;
      uint32_t pos = (uint32_t)last_flush_pos;
      size_t i;
      for (i = 0; i < t; i++) {
        ++literal_histo[data[pos & mask]];
        pos += kSampleRate;
      }
      if (BitsEntropy(literal_histo, 256) > bit_cost_threshold) {
        return BROTLI_FALSE;
      }
    }
  }
  return BROTLI_TRUE;
}

/* Chooses the literal context mode for a metablock */
static ContextType ChooseContextMode(const BrotliEncoderParams* params,
    const uint8_t* data, const size_t pos, const size_t mask,
    const size_t length) {
  /* We only do the computation for the option of something else than
     CONTEXT_UTF8 for the highest qualities */
  if (params->quality >= MIN_QUALITY_FOR_HQ_BLOCK_SPLITTING &&
      !BrotliIsMostlyUTF8(data, pos, mask, length, kMinUTF8Ratio)) {
    return CONTEXT_SIGNED;
  }
  return CONTEXT_UTF8;
}

static void WriteMetaBlockInternal(MemoryManager* m,
                                   const uint8_t* data,
                                   const size_t mask,
                                   const uint64_t last_flush_pos,
                                   const size_t bytes,
                                   const BROTLI_BOOL is_last,
                                   ContextType literal_context_mode,
                                   const BrotliEncoderParams* params,
                                   const uint8_t prev_byte,
                                   const uint8_t prev_byte2,
                                   const size_t num_literals,
                                   const size_t num_commands,
                                   Command* commands,
                                   const int* saved_dist_cache,
                                   int* dist_cache,
                                   size_t* storage_ix,
                                   uint8_t* storage) {
  const uint32_t wrapped_last_flush_pos = WrapPosition(last_flush_pos);
  uint16_t last_bytes;
  uint8_t last_bytes_bits;
  ContextLut literal_context_lut = BROTLI_CONTEXT_LUT(literal_context_mode);
  BrotliEncoderParams block_params = *params;

  if (bytes == 0) {
    /* Write the ISLAST and ISEMPTY bits. */
    BrotliWriteBits(2, 3, storage_ix, storage);
    *storage_ix = (*storage_ix + 7u) & ~7u;
    return;
  }

  if (!ShouldCompress(data, mask, last_flush_pos, bytes,
                      num_literals, num_commands)) {
    /* Restore the distance cache, as its last update by
       CreateBackwardReferences is now unused. */
    memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
    BrotliStoreUncompressedMetaBlock(is_last, data,
                                     wrapped_last_flush_pos, mask, bytes,
                                     storage_ix, storage);
    return;
  }

  BROTLI_DCHECK(*storage_ix <= 14);
  last_bytes = (uint16_t)((storage[1] << 8) | storage[0]);
  last_bytes_bits = (uint8_t)(*storage_ix);
  if (params->quality <= MAX_QUALITY_FOR_STATIC_ENTROPY_CODES) {
    BrotliStoreMetaBlockFast(m, data, wrapped_last_flush_pos,
                             bytes, mask, is_last, params,
                             commands, num_commands,
                             storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return;
  } else if (params->quality < MIN_QUALITY_FOR_BLOCK_SPLIT) {
    BrotliStoreMetaBlockTrivial(m, data, wrapped_last_flush_pos,
                                bytes, mask, is_last, params,
                                commands, num_commands,
                                storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return;
  } else {
    MetaBlockSplit mb;
    InitMetaBlockSplit(&mb);
    if (params->quality < MIN_QUALITY_FOR_HQ_BLOCK_SPLITTING) {
      size_t num_literal_contexts = 1;
      const uint32_t* literal_context_map = NULL;
      if (!params->disable_literal_context_modeling) {
        /* TODO(eustas): pull to higher level and reuse. */
        uint32_t* arena = BROTLI_ALLOC(m, uint32_t, 14 * 32);
        if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(arena)) return;
        DecideOverLiteralContextModeling(
            data, wrapped_last_flush_pos, bytes, mask, params->quality,
            params->size_hint, &num_literal_contexts,
            &literal_context_map, arena);
        BROTLI_FREE(m, arena);
      }
      BrotliBuildMetaBlockGreedy(m, data, wrapped_last_flush_pos, mask,
          prev_byte, prev_byte2, literal_context_lut, num_literal_contexts,
          literal_context_map, commands, num_commands, &mb);
      if (BROTLI_IS_OOM(m)) return;
    } else {
      BrotliBuildMetaBlock(m, data, wrapped_last_flush_pos, mask, &block_params,
                           prev_byte, prev_byte2,
                           commands, num_commands,
                           literal_context_mode,
                           &mb);
      if (BROTLI_IS_OOM(m)) return;
    }
    if (params->quality >= MIN_QUALITY_FOR_OPTIMIZE_HISTOGRAMS) {
      /* The number of distance symbols effectively used for distance
         histograms. It might be less than distance alphabet size
         for "Large Window Brotli" (32-bit). */
      BrotliOptimizeHistograms(block_params.dist.alphabet_size_limit, &mb);
    }
    BrotliStoreMetaBlock(m, data, wrapped_last_flush_pos, bytes, mask,
                         prev_byte, prev_byte2,
                         is_last,
                         &block_params,
                         literal_context_mode,
                         commands, num_commands,
                         &mb,
                         storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return;
    DestroyMetaBlockSplit(m, &mb);
  }
  if (bytes + 4 < (*storage_ix >> 3)) {
    /* Restore the distance cache and last byte. */
    memcpy(dist_cache, saved_dist_cache, 4 * sizeof(dist_cache[0]));
    storage[0] = (uint8_t)last_bytes;
    storage[1] = (uint8_t)(last_bytes >> 8);
    *storage_ix = last_bytes_bits;
    BrotliStoreUncompressedMetaBlock(is_last, data,
                                     wrapped_last_flush_pos, mask,
                                     bytes, storage_ix, storage);
  }
}

static void ChooseDistanceParams(BrotliEncoderParams* params) {
  uint32_t distance_postfix_bits = 0;
  uint32_t num_direct_distance_codes = 0;

  if (params->quality >= MIN_QUALITY_FOR_NONZERO_DISTANCE_PARAMS) {
    uint32_t ndirect_msb;
    if (params->mode == BROTLI_MODE_FONT) {
      distance_postfix_bits = 1;
      num_direct_distance_codes = 12;
    } else {
      distance_postfix_bits = params->dist.distance_postfix_bits;
      num_direct_distance_codes = params->dist.num_direct_distance_codes;
    }
    ndirect_msb = (num_direct_distance_codes >> distance_postfix_bits) & 0x0F;
    if (distance_postfix_bits > BROTLI_MAX_NPOSTFIX ||
        num_direct_distance_codes > BROTLI_MAX_NDIRECT ||
        (ndirect_msb << distance_postfix_bits) != num_direct_distance_codes) {
      distance_postfix_bits = 0;
      num_direct_distance_codes = 0;
    }
  }

  BrotliInitDistanceParams(&params->dist, distance_postfix_bits,
                           num_direct_distance_codes, params->large_window);
}

static BROTLI_BOOL EnsureInitialized(BrotliEncoderState* s) {
  MemoryManager* m = &s->memory_manager_;
  if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
  if (s->is_initialized_) return BROTLI_TRUE;

  s->last_bytes_bits_ = 0;
  s->last_bytes_ = 0;
  s->flint_ = BROTLI_FLINT_DONE;
  s->remaining_metadata_bytes_ = BROTLI_UINT32_MAX;

  SanitizeParams(&s->params);
  s->params.lgblock = ComputeLgBlock(&s->params);
  ChooseDistanceParams(&s->params);

  if (s->params.stream_offset != 0) {
    s->flint_ = BROTLI_FLINT_NEEDS_2_BYTES;
    /* Poison the distance cache. -16 +- 3 is still less than zero (invalid). */
    s->dist_cache_[0] = -16;
    s->dist_cache_[1] = -16;
    s->dist_cache_[2] = -16;
    s->dist_cache_[3] = -16;
    memcpy(s->saved_dist_cache_, s->dist_cache_, sizeof(s->saved_dist_cache_));
  }

  RingBufferSetup(&s->params, &s->ringbuffer_);

  /* Initialize last byte with stream header. */
  {
    int lgwin = s->params.lgwin;
    if (s->params.quality == FAST_ONE_PASS_COMPRESSION_QUALITY ||
        s->params.quality == FAST_TWO_PASS_COMPRESSION_QUALITY) {
      lgwin = BROTLI_MAX(int, lgwin, 18);
    }
    if (s->params.stream_offset == 0) {
      EncodeWindowBits(lgwin, s->params.large_window,
                       &s->last_bytes_, &s->last_bytes_bits_);
    } else {
      /* Bigger values have the same effect, but could cause overflows. */
      s->params.stream_offset = BROTLI_MIN(size_t,
          s->params.stream_offset, BROTLI_MAX_BACKWARD_LIMIT(lgwin));
    }
  }

  if (s->params.quality == FAST_ONE_PASS_COMPRESSION_QUALITY) {
    s->one_pass_arena_ = BROTLI_ALLOC(m, BrotliOnePassArena, 1);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    InitCommandPrefixCodes(s->one_pass_arena_);
  } else if (s->params.quality == FAST_TWO_PASS_COMPRESSION_QUALITY) {
    s->two_pass_arena_ = BROTLI_ALLOC(m, BrotliTwoPassArena, 1);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
  }

  s->is_initialized_ = BROTLI_TRUE;
  return BROTLI_TRUE;
}

static void BrotliEncoderInitParams(BrotliEncoderParams* params) {
  params->mode = BROTLI_DEFAULT_MODE;
  params->large_window = BROTLI_FALSE;
  params->quality = BROTLI_DEFAULT_QUALITY;
  params->lgwin = BROTLI_DEFAULT_WINDOW;
  params->lgblock = 0;
  params->stream_offset = 0;
  params->size_hint = 0;
  params->disable_literal_context_modeling = BROTLI_FALSE;
  BrotliInitSharedEncoderDictionary(&params->dictionary);
  params->dist.distance_postfix_bits = 0;
  params->dist.num_direct_distance_codes = 0;
  params->dist.alphabet_size_max =
      BROTLI_DISTANCE_ALPHABET_SIZE(0, 0, BROTLI_MAX_DISTANCE_BITS);
  params->dist.alphabet_size_limit = params->dist.alphabet_size_max;
  params->dist.max_distance = BROTLI_MAX_DISTANCE;
}

static void BrotliEncoderCleanupParams(MemoryManager* m,
    BrotliEncoderParams* params) {
  BrotliCleanupSharedEncoderDictionary(m, &params->dictionary);
}

static void BrotliEncoderInitState(BrotliEncoderState* s) {
  BrotliEncoderInitParams(&s->params);
  s->input_pos_ = 0;
  s->num_commands_ = 0;
  s->num_literals_ = 0;
  s->last_insert_len_ = 0;
  s->last_flush_pos_ = 0;
  s->last_processed_pos_ = 0;
  s->prev_byte_ = 0;
  s->prev_byte2_ = 0;
  s->storage_size_ = 0;
  s->storage_ = 0;
  HasherInit(&s->hasher_);
  s->large_table_ = NULL;
  s->large_table_size_ = 0;
  s->one_pass_arena_ = NULL;
  s->two_pass_arena_ = NULL;
  s->command_buf_ = NULL;
  s->literal_buf_ = NULL;
  s->total_in_ = 0;
  s->next_out_ = NULL;
  s->available_out_ = 0;
  s->total_out_ = 0;
  s->stream_state_ = BROTLI_STREAM_PROCESSING;
  s->is_last_block_emitted_ = BROTLI_FALSE;
  s->is_initialized_ = BROTLI_FALSE;

  RingBufferInit(&s->ringbuffer_);

  s->commands_ = 0;
  s->cmd_alloc_size_ = 0;

  /* Initialize distance cache. */
  s->dist_cache_[0] = 4;
  s->dist_cache_[1] = 11;
  s->dist_cache_[2] = 15;
  s->dist_cache_[3] = 16;
  /* Save the state of the distance cache in case we need to restore it for
     emitting an uncompressed block. */
  memcpy(s->saved_dist_cache_, s->dist_cache_, sizeof(s->saved_dist_cache_));
}

BrotliEncoderState* BrotliEncoderCreateInstance(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  BrotliEncoderState* state = (BrotliEncoderState*)BrotliBootstrapAlloc(
      sizeof(BrotliEncoderState), alloc_func, free_func, opaque);
  if (state == NULL) {
    /* BROTLI_DUMP(); */
    return 0;
  }
  BrotliInitMemoryManager(
      &state->memory_manager_, alloc_func, free_func, opaque);
  BrotliEncoderInitState(state);
  return state;
}

#ifdef BROTLI_REPORTING
/* When BROTLI_REPORTING is defined extra reporting module have to be linked. */
void BrotliEncoderOnFinish(const BrotliEncoderState* s);
#define BROTLI_ENCODER_ON_FINISH(s) BrotliEncoderOnFinish(s);
#else
#if !defined(BROTLI_ENCODER_ON_FINISH)
#define BROTLI_ENCODER_ON_FINISH(s) (void)(s);
#endif
#endif

static void BrotliEncoderCleanupState(BrotliEncoderState* s) {
  MemoryManager* m = &s->memory_manager_;

  BROTLI_ENCODER_ON_FINISH(s);

  if (BROTLI_IS_OOM(m)) {
    BrotliWipeOutMemoryManager(m);
    return;
  }

  BROTLI_FREE(m, s->storage_);
  BROTLI_FREE(m, s->commands_);
  RingBufferFree(m, &s->ringbuffer_);
  DestroyHasher(m, &s->hasher_);
  BROTLI_FREE(m, s->large_table_);
  BROTLI_FREE(m, s->one_pass_arena_);
  BROTLI_FREE(m, s->two_pass_arena_);
  BROTLI_FREE(m, s->command_buf_);
  BROTLI_FREE(m, s->literal_buf_);
  BrotliEncoderCleanupParams(m, &s->params);
}

/* Deinitializes and frees BrotliEncoderState instance. */
void BrotliEncoderDestroyInstance(BrotliEncoderState* state) {
  if (!state) {
    return;
  } else {
    BrotliEncoderCleanupState(state);
    BrotliBootstrapFree(state, &state->memory_manager_);
  }
}

/*
   Copies the given input data to the internal ring buffer of the compressor.
   No processing of the data occurs at this time and this function can be
   called multiple times before calling WriteBrotliData() to process the
   accumulated input. At most input_block_size() bytes of input data can be
   copied to the ring buffer, otherwise the next WriteBrotliData() will fail.
 */
static void CopyInputToRingBuffer(BrotliEncoderState* s,
                                  const size_t input_size,
                                  const uint8_t* input_buffer) {
  RingBuffer* ringbuffer_ = &s->ringbuffer_;
  MemoryManager* m = &s->memory_manager_;
  RingBufferWrite(m, input_buffer, input_size, ringbuffer_);
  if (BROTLI_IS_OOM(m)) return;
  s->input_pos_ += input_size;

  /* TL;DR: If needed, initialize 7 more bytes in the ring buffer to make the
     hashing not depend on uninitialized data. This makes compression
     deterministic and it prevents uninitialized memory warnings in Valgrind.
     Even without erasing, the output would be valid (but nondeterministic).

     Background information: The compressor stores short (at most 8 bytes)
     substrings of the input already read in a hash table, and detects
     repetitions by looking up such substrings in the hash table. If it
     can find a substring, it checks whether the substring is really there
     in the ring buffer (or it's just a hash collision). Should the hash
     table become corrupt, this check makes sure that the output is
     still valid, albeit the compression ratio would be bad.

     The compressor populates the hash table from the ring buffer as it's
     reading new bytes from the input. However, at the last few indexes of
     the ring buffer, there are not enough bytes to build full-length
     substrings from. Since the hash table always contains full-length
     substrings, we erase with dummy zeros here to make sure that those
     substrings will contain zeros at the end instead of uninitialized
     data.

     Please note that erasing is not necessary (because the
     memory region is already initialized since he ring buffer
     has a `tail' that holds a copy of the beginning,) so we
     skip erasing if we have already gone around at least once in
     the ring buffer.

     Only clear during the first round of ring-buffer writes. On
     subsequent rounds data in the ring-buffer would be affected. */
  if (ringbuffer_->pos_ <= ringbuffer_->mask_) {
    /* This is the first time when the ring buffer is being written.
       We clear 7 bytes just after the bytes that have been copied from
       the input buffer.

       The ring-buffer has a "tail" that holds a copy of the beginning,
       but only once the ring buffer has been fully written once, i.e.,
       pos <= mask. For the first time, we need to write values
       in this tail (where index may be larger than mask), so that
       we have exactly defined behavior and don't read uninitialized
       memory. Due to performance reasons, hashing reads data using a
       LOAD64, which can go 7 bytes beyond the bytes written in the
       ring-buffer. */
    memset(ringbuffer_->buffer_ + ringbuffer_->pos_, 0, 7);
  }
}

/* Marks all input as processed.
   Returns true if position wrapping occurs. */
static BROTLI_BOOL UpdateLastProcessedPos(BrotliEncoderState* s) {
  uint32_t wrapped_last_processed_pos = WrapPosition(s->last_processed_pos_);
  uint32_t wrapped_input_pos = WrapPosition(s->input_pos_);
  s->last_processed_pos_ = s->input_pos_;
  return TO_BROTLI_BOOL(wrapped_input_pos < wrapped_last_processed_pos);
}

static void ExtendLastCommand(BrotliEncoderState* s, uint32_t* bytes,
                              uint32_t* wrapped_last_processed_pos) {
  Command* last_command = &s->commands_[s->num_commands_ - 1];
  const uint8_t* data = s->ringbuffer_.buffer_;
  const uint32_t mask = s->ringbuffer_.mask_;
  uint64_t max_backward_distance =
      (((uint64_t)1) << s->params.lgwin) - BROTLI_WINDOW_GAP;
  uint64_t last_copy_len = last_command->copy_len_ & 0x1FFFFFF;
  uint64_t last_processed_pos = s->last_processed_pos_ - last_copy_len;
  uint64_t max_distance = last_processed_pos < max_backward_distance ?
      last_processed_pos : max_backward_distance;
  uint64_t cmd_dist = (uint64_t)s->dist_cache_[0];
  uint32_t distance_code = CommandRestoreDistanceCode(last_command,
                                                      &s->params.dist);
  const CompoundDictionary* dict = &s->params.dictionary.compound;
  size_t compound_dictionary_size = dict->total_size;
  if (distance_code < BROTLI_NUM_DISTANCE_SHORT_CODES ||
      distance_code - (BROTLI_NUM_DISTANCE_SHORT_CODES - 1) == cmd_dist) {
    if (cmd_dist <= max_distance) {
      while (*bytes != 0 && data[*wrapped_last_processed_pos & mask] ==
             data[(*wrapped_last_processed_pos - cmd_dist) & mask]) {
        last_command->copy_len_++;
        (*bytes)--;
        (*wrapped_last_processed_pos)++;
      }
    } else {
      if ((cmd_dist - max_distance - 1) < compound_dictionary_size &&
          last_copy_len < cmd_dist - max_distance) {
        size_t address =
            compound_dictionary_size - (size_t)(cmd_dist - max_distance) +
            (size_t)last_copy_len;
        size_t br_index = 0;
        size_t br_offset;
        const uint8_t* chunk;
        size_t chunk_length;
        while (address >= dict->chunk_offsets[br_index + 1]) br_index++;
        br_offset = address - dict->chunk_offsets[br_index];
        chunk = dict->chunk_source[br_index];
        chunk_length =
            dict->chunk_offsets[br_index + 1] - dict->chunk_offsets[br_index];
        while (*bytes != 0 && data[*wrapped_last_processed_pos & mask] ==
               chunk[br_offset]) {
          last_command->copy_len_++;
          (*bytes)--;
          (*wrapped_last_processed_pos)++;
          if (++br_offset == chunk_length) {
            br_index++;
            br_offset = 0;
            if (br_index != dict->num_chunks) {
              chunk = dict->chunk_source[br_index];
              chunk_length = dict->chunk_offsets[br_index + 1] -
                  dict->chunk_offsets[br_index];
            } else {
              break;
            }
          }
        }
      }
    }
    /* The copy length is at most the metablock size, and thus expressible. */
    GetLengthCode(last_command->insert_len_,
                  (size_t)((int)(last_command->copy_len_ & 0x1FFFFFF) +
                           (int)(last_command->copy_len_ >> 25)),
                  TO_BROTLI_BOOL((last_command->dist_prefix_ & 0x3FF) == 0),
                  &last_command->cmd_prefix_);
  }
}

/*
   Processes the accumulated input data and sets |*out_size| to the length of
   the new output meta-block, or to zero if no new output meta-block has been
   created (in this case the processed input data is buffered internally).
   If |*out_size| is positive, |*output| points to the start of the output
   data. If |is_last| or |force_flush| is BROTLI_TRUE, an output meta-block is
   always created. However, until |is_last| is BROTLI_TRUE encoder may retain up
   to 7 bits of the last byte of output. Byte-alignment could be enforced by
   emitting an empty meta-data block.
   Returns BROTLI_FALSE if the size of the input data is larger than
   input_block_size().
 */
static BROTLI_BOOL EncodeData(
    BrotliEncoderState* s, const BROTLI_BOOL is_last,
    const BROTLI_BOOL force_flush, size_t* out_size, uint8_t** output) {
  const uint64_t delta = UnprocessedInputSize(s);
  uint32_t bytes = (uint32_t)delta;
  uint32_t wrapped_last_processed_pos = WrapPosition(s->last_processed_pos_);
  uint8_t* data;
  uint32_t mask;
  MemoryManager* m = &s->memory_manager_;
  ContextType literal_context_mode;
  ContextLut literal_context_lut;
  BROTLI_BOOL fast_compress =
      s->params.quality == FAST_ONE_PASS_COMPRESSION_QUALITY ||
      s->params.quality == FAST_TWO_PASS_COMPRESSION_QUALITY;

  data = s->ringbuffer_.buffer_;
  mask = s->ringbuffer_.mask_;

  if (delta == 0) {  /* No new input; still might want to flush or finish. */
    if (!data) {  /* No input has been processed so far. */
      if (is_last) {  /* Emit complete finalized stream. */
        BROTLI_DCHECK(s->last_bytes_bits_ <= 14);
        s->last_bytes_ |= (uint16_t)(3u << s->last_bytes_bits_);
        s->last_bytes_bits_ = (uint8_t)(s->last_bytes_bits_ + 2u);
        s->tiny_buf_.u8[0] = (uint8_t)s->last_bytes_;
        s->tiny_buf_.u8[1] = (uint8_t)(s->last_bytes_ >> 8);
        *output = s->tiny_buf_.u8;
        *out_size = (s->last_bytes_bits_ + 7u) >> 3u;
        return BROTLI_TRUE;
      } else {  /* No data, not last -> no-op. */
        *out_size = 0;
        return BROTLI_TRUE;
      }
    } else {
      /* Fast compress performs flush every block -> flush is no-op. */
      if (!is_last && (!force_flush || fast_compress)) {  /* Another no-op. */
        *out_size = 0;
        return BROTLI_TRUE;
      }
    }
  }
  BROTLI_DCHECK(data);

  if (s->params.quality > s->params.dictionary.max_quality) return BROTLI_FALSE;
  /* Adding more blocks after "last" block is forbidden. */
  if (s->is_last_block_emitted_) return BROTLI_FALSE;
  if (is_last) s->is_last_block_emitted_ = BROTLI_TRUE;

  if (delta > InputBlockSize(s)) {
    return BROTLI_FALSE;
  }
  if (s->params.quality == FAST_TWO_PASS_COMPRESSION_QUALITY &&
      !s->command_buf_) {
    s->command_buf_ =
        BROTLI_ALLOC(m, uint32_t, kCompressFragmentTwoPassBlockSize);
    s->literal_buf_ =
        BROTLI_ALLOC(m, uint8_t, kCompressFragmentTwoPassBlockSize);
    if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(s->command_buf_) ||
        BROTLI_IS_NULL(s->literal_buf_)) {
      return BROTLI_FALSE;
    }
  }

  if (fast_compress) {
    uint8_t* storage;
    size_t storage_ix = s->last_bytes_bits_;
    size_t table_size;
    int* table;

    storage = GetBrotliStorage(s, 2 * bytes + 503);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    storage[0] = (uint8_t)s->last_bytes_;
    storage[1] = (uint8_t)(s->last_bytes_ >> 8);
    table = GetHashTable(s, s->params.quality, bytes, &table_size);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    if (s->params.quality == FAST_ONE_PASS_COMPRESSION_QUALITY) {
      BrotliCompressFragmentFast(
          s->one_pass_arena_, &data[wrapped_last_processed_pos & mask],
          bytes, is_last,
          table, table_size,
          &storage_ix, storage);
      if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    } else {
      BrotliCompressFragmentTwoPass(
          s->two_pass_arena_, &data[wrapped_last_processed_pos & mask],
          bytes, is_last,
          s->command_buf_, s->literal_buf_,
          table, table_size,
          &storage_ix, storage);
      if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    }
    s->last_bytes_ = (uint16_t)(storage[storage_ix >> 3]);
    s->last_bytes_bits_ = storage_ix & 7u;
    UpdateLastProcessedPos(s);
    *output = &storage[0];
    *out_size = storage_ix >> 3;
    return BROTLI_TRUE;
  }

  {
    /* Theoretical max number of commands is 1 per 2 bytes. */
    size_t newsize = s->num_commands_ + bytes / 2 + 1;
    if (newsize > s->cmd_alloc_size_) {
      Command* new_commands;
      /* Reserve a bit more memory to allow merging with a next block
         without reallocation: that would impact speed. */
      newsize += (bytes / 4) + 16;
      s->cmd_alloc_size_ = newsize;
      new_commands = BROTLI_ALLOC(m, Command, newsize);
      if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(new_commands)) return BROTLI_FALSE;
      if (s->commands_) {
        memcpy(new_commands, s->commands_, sizeof(Command) * s->num_commands_);
        BROTLI_FREE(m, s->commands_);
      }
      s->commands_ = new_commands;
    }
  }

  InitOrStitchToPreviousBlock(m, &s->hasher_, data, mask, &s->params,
      wrapped_last_processed_pos, bytes, is_last);

  literal_context_mode = ChooseContextMode(
      &s->params, data, WrapPosition(s->last_flush_pos_),
      mask, (size_t)(s->input_pos_ - s->last_flush_pos_));
  literal_context_lut = BROTLI_CONTEXT_LUT(literal_context_mode);

  if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;

  if (s->num_commands_ && s->last_insert_len_ == 0) {
    ExtendLastCommand(s, &bytes, &wrapped_last_processed_pos);
  }

  if (s->params.quality == ZOPFLIFICATION_QUALITY) {
    BROTLI_DCHECK(s->params.hasher.type == 10);
    BrotliCreateZopfliBackwardReferences(m, bytes, wrapped_last_processed_pos,
        data, mask, literal_context_lut, &s->params,
        &s->hasher_, s->dist_cache_,
        &s->last_insert_len_, &s->commands_[s->num_commands_],
        &s->num_commands_, &s->num_literals_);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
  } else if (s->params.quality == HQ_ZOPFLIFICATION_QUALITY) {
    BROTLI_DCHECK(s->params.hasher.type == 10);
    BrotliCreateHqZopfliBackwardReferences(m, bytes, wrapped_last_processed_pos,
        data, mask, literal_context_lut, &s->params,
        &s->hasher_, s->dist_cache_,
        &s->last_insert_len_, &s->commands_[s->num_commands_],
        &s->num_commands_, &s->num_literals_);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
  } else {
    BrotliCreateBackwardReferences(bytes, wrapped_last_processed_pos,
        data, mask, literal_context_lut, &s->params,
        &s->hasher_, s->dist_cache_,
        &s->last_insert_len_, &s->commands_[s->num_commands_],
        &s->num_commands_, &s->num_literals_);
  }

  {
    const size_t max_length = MaxMetablockSize(&s->params);
    const size_t max_literals = max_length / 8;
    const size_t max_commands = max_length / 8;
    const size_t processed_bytes = (size_t)(s->input_pos_ - s->last_flush_pos_);
    /* If maximal possible additional block doesn't fit metablock, flush now. */
    /* TODO(eustas): Postpone decision until next block arrives? */
    const BROTLI_BOOL next_input_fits_metablock = TO_BROTLI_BOOL(
        processed_bytes + InputBlockSize(s) <= max_length);
    /* If block splitting is not used, then flush as soon as there is some
       amount of commands / literals produced. */
    const BROTLI_BOOL should_flush = TO_BROTLI_BOOL(
        s->params.quality < MIN_QUALITY_FOR_BLOCK_SPLIT &&
        s->num_literals_ + s->num_commands_ >= MAX_NUM_DELAYED_SYMBOLS);
    if (!is_last && !force_flush && !should_flush &&
        next_input_fits_metablock &&
        s->num_literals_ < max_literals &&
        s->num_commands_ < max_commands) {
      /* Merge with next input block. Everything will happen later. */
      if (UpdateLastProcessedPos(s)) {
        HasherReset(&s->hasher_);
      }
      *out_size = 0;
      return BROTLI_TRUE;
    }
  }

  /* Create the last insert-only command. */
  if (s->last_insert_len_ > 0) {
    InitInsertCommand(&s->commands_[s->num_commands_++], s->last_insert_len_);
    s->num_literals_ += s->last_insert_len_;
    s->last_insert_len_ = 0;
  }

  if (!is_last && s->input_pos_ == s->last_flush_pos_) {
    /* We have no new input data and we don't have to finish the stream, so
       nothing to do. */
    *out_size = 0;
    return BROTLI_TRUE;
  }
  BROTLI_DCHECK(s->input_pos_ >= s->last_flush_pos_);
  BROTLI_DCHECK(s->input_pos_ > s->last_flush_pos_ || is_last);
  BROTLI_DCHECK(s->input_pos_ - s->last_flush_pos_ <= 1u << 24);
  {
    const uint32_t metablock_size =
        (uint32_t)(s->input_pos_ - s->last_flush_pos_);
    uint8_t* storage = GetBrotliStorage(s, 2 * metablock_size + 503);
    size_t storage_ix = s->last_bytes_bits_;
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    storage[0] = (uint8_t)s->last_bytes_;
    storage[1] = (uint8_t)(s->last_bytes_ >> 8);
    WriteMetaBlockInternal(
        m, data, mask, s->last_flush_pos_, metablock_size, is_last,
        literal_context_mode, &s->params, s->prev_byte_, s->prev_byte2_,
        s->num_literals_, s->num_commands_, s->commands_, s->saved_dist_cache_,
        s->dist_cache_, &storage_ix, storage);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    s->last_bytes_ = (uint16_t)(storage[storage_ix >> 3]);
    s->last_bytes_bits_ = storage_ix & 7u;
    s->last_flush_pos_ = s->input_pos_;
    if (UpdateLastProcessedPos(s)) {
      HasherReset(&s->hasher_);
    }
    if (s->last_flush_pos_ > 0) {
      s->prev_byte_ = data[((uint32_t)s->last_flush_pos_ - 1) & mask];
    }
    if (s->last_flush_pos_ > 1) {
      s->prev_byte2_ = data[(uint32_t)(s->last_flush_pos_ - 2) & mask];
    }
    s->num_commands_ = 0;
    s->num_literals_ = 0;
    /* Save the state of the distance cache in case we need to restore it for
       emitting an uncompressed block. */
    memcpy(s->saved_dist_cache_, s->dist_cache_, sizeof(s->saved_dist_cache_));
    *output = &storage[0];
    *out_size = storage_ix >> 3;
    return BROTLI_TRUE;
  }
}

/* Dumps remaining output bits and metadata header to |header|.
   Returns number of produced bytes.
   REQUIRED: |header| should be 8-byte aligned and at least 16 bytes long.
   REQUIRED: |block_size| <= (1 << 24). */
static size_t WriteMetadataHeader(
    BrotliEncoderState* s, const size_t block_size, uint8_t* header) {
  size_t storage_ix;
  storage_ix = s->last_bytes_bits_;
  header[0] = (uint8_t)s->last_bytes_;
  header[1] = (uint8_t)(s->last_bytes_ >> 8);
  s->last_bytes_ = 0;
  s->last_bytes_bits_ = 0;

  BrotliWriteBits(1, 0, &storage_ix, header);
  BrotliWriteBits(2, 3, &storage_ix, header);
  BrotliWriteBits(1, 0, &storage_ix, header);
  if (block_size == 0) {
    BrotliWriteBits(2, 0, &storage_ix, header);
  } else {
    uint32_t nbits = (block_size == 1) ? 1 :
        (Log2FloorNonZero((uint32_t)block_size - 1) + 1);
    uint32_t nbytes = (nbits + 7) / 8;
    BrotliWriteBits(2, nbytes, &storage_ix, header);
    BrotliWriteBits(8 * nbytes, block_size - 1, &storage_ix, header);
  }
  return (storage_ix + 7u) >> 3;
}

size_t BrotliEncoderMaxCompressedSize(size_t input_size) {
  /* [window bits / empty metadata] + N * [uncompressed] + [last empty] */
  size_t num_large_blocks = input_size >> 14;
  size_t overhead = 2 + (4 * num_large_blocks) + 3 + 1;
  size_t result = input_size + overhead;
  if (input_size == 0) return 2;
  return (result < input_size) ? 0 : result;
}

/* Wraps data to uncompressed brotli stream with minimal window size.
   |output| should point at region with at least BrotliEncoderMaxCompressedSize
   addressable bytes.
   Returns the length of stream. */
static size_t MakeUncompressedStream(
    const uint8_t* input, size_t input_size, uint8_t* output) {
  size_t size = input_size;
  size_t result = 0;
  size_t offset = 0;
  if (input_size == 0) {
    output[0] = 6;
    return 1;
  }
  output[result++] = 0x21;  /* window bits = 10, is_last = false */
  output[result++] = 0x03;  /* empty metadata, padding */
  while (size > 0) {
    uint32_t nibbles = 0;
    uint32_t chunk_size;
    uint32_t bits;
    chunk_size = (size > (1u << 24)) ? (1u << 24) : (uint32_t)size;
    if (chunk_size > (1u << 16)) nibbles = (chunk_size > (1u << 20)) ? 2 : 1;
    bits =
        (nibbles << 1) | ((chunk_size - 1) << 3) | (1u << (19 + 4 * nibbles));
    output[result++] = (uint8_t)bits;
    output[result++] = (uint8_t)(bits >> 8);
    output[result++] = (uint8_t)(bits >> 16);
    if (nibbles == 2) output[result++] = (uint8_t)(bits >> 24);
    memcpy(&output[result], &input[offset], chunk_size);
    result += chunk_size;
    offset += chunk_size;
    size -= chunk_size;
  }
  output[result++] = 3;
  return result;
}

BROTLI_BOOL BrotliEncoderCompress(
    int quality, int lgwin, BrotliEncoderMode mode, size_t input_size,
    const uint8_t input_buffer[BROTLI_ARRAY_PARAM(input_size)],
    size_t* encoded_size,
    uint8_t encoded_buffer[BROTLI_ARRAY_PARAM(*encoded_size)]) {
  BrotliEncoderState* s;
  size_t out_size = *encoded_size;
  const uint8_t* input_start = input_buffer;
  uint8_t* output_start = encoded_buffer;
  size_t max_out_size = BrotliEncoderMaxCompressedSize(input_size);
  if (out_size == 0) {
    /* Output buffer needs at least one byte. */
    return BROTLI_FALSE;
  }
  if (input_size == 0) {
    /* Handle the special case of empty input. */
    *encoded_size = 1;
    *encoded_buffer = 6;
    return BROTLI_TRUE;
  }

  s = BrotliEncoderCreateInstance(0, 0, 0);
  if (!s) {
    return BROTLI_FALSE;
  } else {
    size_t available_in = input_size;
    const uint8_t* next_in = input_buffer;
    size_t available_out = *encoded_size;
    uint8_t* next_out = encoded_buffer;
    size_t total_out = 0;
    BROTLI_BOOL result = BROTLI_FALSE;
    /* TODO(eustas): check that parameters are sane. */
    BrotliEncoderSetParameter(s, BROTLI_PARAM_QUALITY, (uint32_t)quality);
    BrotliEncoderSetParameter(s, BROTLI_PARAM_LGWIN, (uint32_t)lgwin);
    BrotliEncoderSetParameter(s, BROTLI_PARAM_MODE, (uint32_t)mode);
    BrotliEncoderSetParameter(s, BROTLI_PARAM_SIZE_HINT, (uint32_t)input_size);
    if (lgwin > BROTLI_MAX_WINDOW_BITS) {
      BrotliEncoderSetParameter(s, BROTLI_PARAM_LARGE_WINDOW, BROTLI_TRUE);
    }
    result = BrotliEncoderCompressStream(s, BROTLI_OPERATION_FINISH,
        &available_in, &next_in, &available_out, &next_out, &total_out);
    if (!BrotliEncoderIsFinished(s)) result = 0;
    *encoded_size = total_out;
    BrotliEncoderDestroyInstance(s);
    if (!result || (max_out_size && *encoded_size > max_out_size)) {
      goto fallback;
    }
    return BROTLI_TRUE;
  }
fallback:
  *encoded_size = 0;
  if (!max_out_size) return BROTLI_FALSE;
  if (out_size >= max_out_size) {
    *encoded_size =
        MakeUncompressedStream(input_start, input_size, output_start);
    return BROTLI_TRUE;
  }
  return BROTLI_FALSE;
}

static void InjectBytePaddingBlock(BrotliEncoderState* s) {
  uint32_t seal = s->last_bytes_;
  size_t seal_bits = s->last_bytes_bits_;
  uint8_t* destination;
  s->last_bytes_ = 0;
  s->last_bytes_bits_ = 0;
  /* is_last = 0, data_nibbles = 11, reserved = 0, meta_nibbles = 00 */
  seal |= 0x6u << seal_bits;
  seal_bits += 6;
  /* If we have already created storage, then append to it.
     Storage is valid until next block is being compressed. */
  if (s->next_out_) {
    destination = s->next_out_ + s->available_out_;
  } else {
    destination = s->tiny_buf_.u8;
    s->next_out_ = destination;
  }
  destination[0] = (uint8_t)seal;
  if (seal_bits > 8) destination[1] = (uint8_t)(seal >> 8);
  if (seal_bits > 16) destination[2] = (uint8_t)(seal >> 16);
  s->available_out_ += (seal_bits + 7) >> 3;
}

/* Fills the |total_out|, if it is not NULL. */
static void SetTotalOut(BrotliEncoderState* s, size_t* total_out) {
  if (total_out) {
    /* Saturating conversion uint64_t -> size_t */
    size_t result = (size_t)-1;
    if (s->total_out_ < result) {
      result = (size_t)s->total_out_;
    }
    *total_out = result;
  }
}

/* Injects padding bits or pushes compressed data to output.
   Returns false if nothing is done. */
static BROTLI_BOOL InjectFlushOrPushOutput(BrotliEncoderState* s,
    size_t* available_out, uint8_t** next_out, size_t* total_out) {
  if (s->stream_state_ == BROTLI_STREAM_FLUSH_REQUESTED &&
      s->last_bytes_bits_ != 0) {
    InjectBytePaddingBlock(s);
    return BROTLI_TRUE;
  }

  if (s->available_out_ != 0 && *available_out != 0) {
    size_t copy_output_size =
        BROTLI_MIN(size_t, s->available_out_, *available_out);
    memcpy(*next_out, s->next_out_, copy_output_size);
    *next_out += copy_output_size;
    *available_out -= copy_output_size;
    s->next_out_ += copy_output_size;
    s->available_out_ -= copy_output_size;
    s->total_out_ += copy_output_size;
    SetTotalOut(s, total_out);
    return BROTLI_TRUE;
  }

  return BROTLI_FALSE;
}

static void CheckFlushComplete(BrotliEncoderState* s) {
  if (s->stream_state_ == BROTLI_STREAM_FLUSH_REQUESTED &&
      s->available_out_ == 0) {
    s->stream_state_ = BROTLI_STREAM_PROCESSING;
    s->next_out_ = 0;
  }
}

static BROTLI_BOOL BrotliEncoderCompressStreamFast(
    BrotliEncoderState* s, BrotliEncoderOperation op, size_t* available_in,
    const uint8_t** next_in, size_t* available_out, uint8_t** next_out,
    size_t* total_out) {
  const size_t block_size_limit = (size_t)1 << s->params.lgwin;
  const size_t buf_size = BROTLI_MIN(size_t, kCompressFragmentTwoPassBlockSize,
      BROTLI_MIN(size_t, *available_in, block_size_limit));
  uint32_t* tmp_command_buf = NULL;
  uint32_t* command_buf = NULL;
  uint8_t* tmp_literal_buf = NULL;
  uint8_t* literal_buf = NULL;
  MemoryManager* m = &s->memory_manager_;
  if (s->params.quality != FAST_ONE_PASS_COMPRESSION_QUALITY &&
      s->params.quality != FAST_TWO_PASS_COMPRESSION_QUALITY) {
    return BROTLI_FALSE;
  }
  if (s->params.quality == FAST_TWO_PASS_COMPRESSION_QUALITY) {
    if (!s->command_buf_ && buf_size == kCompressFragmentTwoPassBlockSize) {
      s->command_buf_ =
          BROTLI_ALLOC(m, uint32_t, kCompressFragmentTwoPassBlockSize);
      s->literal_buf_ =
          BROTLI_ALLOC(m, uint8_t, kCompressFragmentTwoPassBlockSize);
      if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(s->command_buf_) ||
          BROTLI_IS_NULL(s->literal_buf_)) {
        return BROTLI_FALSE;
      }
    }
    if (s->command_buf_) {
      command_buf = s->command_buf_;
      literal_buf = s->literal_buf_;
    } else {
      tmp_command_buf = BROTLI_ALLOC(m, uint32_t, buf_size);
      tmp_literal_buf = BROTLI_ALLOC(m, uint8_t, buf_size);
      if (BROTLI_IS_OOM(m) || BROTLI_IS_NULL(tmp_command_buf) ||
          BROTLI_IS_NULL(tmp_literal_buf)) {
        return BROTLI_FALSE;
      }
      command_buf = tmp_command_buf;
      literal_buf = tmp_literal_buf;
    }
  }

  while (BROTLI_TRUE) {
    if (InjectFlushOrPushOutput(s, available_out, next_out, total_out)) {
      continue;
    }

    /* Compress block only when internal output buffer is empty, stream is not
       finished, there is no pending flush request, and there is either
       additional input or pending operation. */
    if (s->available_out_ == 0 &&
        s->stream_state_ == BROTLI_STREAM_PROCESSING &&
        (*available_in != 0 || op != BROTLI_OPERATION_PROCESS)) {
      size_t block_size = BROTLI_MIN(size_t, block_size_limit, *available_in);
      BROTLI_BOOL is_last =
          (*available_in == block_size) && (op == BROTLI_OPERATION_FINISH);
      BROTLI_BOOL force_flush =
          (*available_in == block_size) && (op == BROTLI_OPERATION_FLUSH);
      size_t max_out_size = 2 * block_size + 503;
      BROTLI_BOOL inplace = BROTLI_TRUE;
      uint8_t* storage = NULL;
      size_t storage_ix = s->last_bytes_bits_;
      size_t table_size;
      int* table;

      if (force_flush && block_size == 0) {
        s->stream_state_ = BROTLI_STREAM_FLUSH_REQUESTED;
        continue;
      }
      if (max_out_size <= *available_out) {
        storage = *next_out;
      } else {
        inplace = BROTLI_FALSE;
        storage = GetBrotliStorage(s, max_out_size);
        if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
      }
      storage[0] = (uint8_t)s->last_bytes_;
      storage[1] = (uint8_t)(s->last_bytes_ >> 8);
      table = GetHashTable(s, s->params.quality, block_size, &table_size);
      if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;

      if (s->params.quality == FAST_ONE_PASS_COMPRESSION_QUALITY) {
        BrotliCompressFragmentFast(s->one_pass_arena_, *next_in, block_size,
            is_last, table, table_size, &storage_ix, storage);
        if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
      } else {
        BrotliCompressFragmentTwoPass(s->two_pass_arena_, *next_in, block_size,
            is_last, command_buf, literal_buf, table, table_size,
            &storage_ix, storage);
        if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
      }
      if (block_size != 0) {
        *next_in += block_size;
        *available_in -= block_size;
        s->total_in_ += block_size;
      }
      if (inplace) {
        size_t out_bytes = storage_ix >> 3;
        BROTLI_DCHECK(out_bytes <= *available_out);
        BROTLI_DCHECK((storage_ix & 7) == 0 || out_bytes < *available_out);
        *next_out += out_bytes;
        *available_out -= out_bytes;
        s->total_out_ += out_bytes;
        SetTotalOut(s, total_out);
      } else {
        size_t out_bytes = storage_ix >> 3;
        s->next_out_ = storage;
        s->available_out_ = out_bytes;
      }
      s->last_bytes_ = (uint16_t)(storage[storage_ix >> 3]);
      s->last_bytes_bits_ = storage_ix & 7u;

      if (force_flush) s->stream_state_ = BROTLI_STREAM_FLUSH_REQUESTED;
      if (is_last) s->stream_state_ = BROTLI_STREAM_FINISHED;
      continue;
    }
    break;
  }
  BROTLI_FREE(m, tmp_command_buf);
  BROTLI_FREE(m, tmp_literal_buf);
  CheckFlushComplete(s);
  return BROTLI_TRUE;
}

static BROTLI_BOOL ProcessMetadata(
    BrotliEncoderState* s, size_t* available_in, const uint8_t** next_in,
    size_t* available_out, uint8_t** next_out, size_t* total_out) {
  if (*available_in > (1u << 24)) return BROTLI_FALSE;
  /* Switch to metadata block workflow, if required. */
  if (s->stream_state_ == BROTLI_STREAM_PROCESSING) {
    s->remaining_metadata_bytes_ = (uint32_t)*available_in;
    s->stream_state_ = BROTLI_STREAM_METADATA_HEAD;
  }
  if (s->stream_state_ != BROTLI_STREAM_METADATA_HEAD &&
      s->stream_state_ != BROTLI_STREAM_METADATA_BODY) {
    return BROTLI_FALSE;
  }

  while (BROTLI_TRUE) {
    if (InjectFlushOrPushOutput(s, available_out, next_out, total_out)) {
      continue;
    }
    if (s->available_out_ != 0) break;

    if (s->input_pos_ != s->last_flush_pos_) {
      BROTLI_BOOL result = EncodeData(s, BROTLI_FALSE, BROTLI_TRUE,
          &s->available_out_, &s->next_out_);
      if (!result) return BROTLI_FALSE;
      continue;
    }

    if (s->stream_state_ == BROTLI_STREAM_METADATA_HEAD) {
      s->next_out_ = s->tiny_buf_.u8;
      s->available_out_ =
          WriteMetadataHeader(s, s->remaining_metadata_bytes_, s->next_out_);
      s->stream_state_ = BROTLI_STREAM_METADATA_BODY;
      continue;
    } else {
      /* Exit workflow only when there is no more input and no more output.
         Otherwise client may continue producing empty metadata blocks. */
      if (s->remaining_metadata_bytes_ == 0) {
        s->remaining_metadata_bytes_ = BROTLI_UINT32_MAX;
        s->stream_state_ = BROTLI_STREAM_PROCESSING;
        break;
      }
      if (*available_out) {
        /* Directly copy input to output. */
        uint32_t copy = (uint32_t)BROTLI_MIN(
            size_t, s->remaining_metadata_bytes_, *available_out);
        memcpy(*next_out, *next_in, copy);
        *next_in += copy;
        *available_in -= copy;
        s->total_in_ += copy;  /* not actually data input, though */
        s->remaining_metadata_bytes_ -= copy;
        *next_out += copy;
        *available_out -= copy;
      } else {
        /* This guarantees progress in "TakeOutput" workflow. */
        uint32_t copy = BROTLI_MIN(uint32_t, s->remaining_metadata_bytes_, 16);
        s->next_out_ = s->tiny_buf_.u8;
        memcpy(s->next_out_, *next_in, copy);
        *next_in += copy;
        *available_in -= copy;
        s->total_in_ += copy;  /* not actually data input, though */
        s->remaining_metadata_bytes_ -= copy;
        s->available_out_ = copy;
      }
      continue;
    }
  }

  return BROTLI_TRUE;
}

static void UpdateSizeHint(BrotliEncoderState* s, size_t available_in) {
  if (s->params.size_hint == 0) {
    uint64_t delta = UnprocessedInputSize(s);
    uint64_t tail = available_in;
    uint32_t limit = 1u << 30;
    uint32_t total;
    if ((delta >= limit) || (tail >= limit) || ((delta + tail) >= limit)) {
      total = limit;
    } else {
      total = (uint32_t)(delta + tail);
    }
    s->params.size_hint = total;
  }
}

BROTLI_BOOL BrotliEncoderCompressStream(
    BrotliEncoderState* s, BrotliEncoderOperation op, size_t* available_in,
    const uint8_t** next_in, size_t* available_out, uint8_t** next_out,
    size_t* total_out) {
  if (!EnsureInitialized(s)) return BROTLI_FALSE;

  /* Unfinished metadata block; check requirements. */
  if (s->remaining_metadata_bytes_ != BROTLI_UINT32_MAX) {
    if (*available_in != s->remaining_metadata_bytes_) return BROTLI_FALSE;
    if (op != BROTLI_OPERATION_EMIT_METADATA) return BROTLI_FALSE;
  }

  if (op == BROTLI_OPERATION_EMIT_METADATA) {
    UpdateSizeHint(s, 0);  /* First data metablock might be emitted here. */
    return ProcessMetadata(
        s, available_in, next_in, available_out, next_out, total_out);
  }

  if (s->stream_state_ == BROTLI_STREAM_METADATA_HEAD ||
      s->stream_state_ == BROTLI_STREAM_METADATA_BODY) {
    return BROTLI_FALSE;
  }

  if (s->stream_state_ != BROTLI_STREAM_PROCESSING && *available_in != 0) {
    return BROTLI_FALSE;
  }
  if (s->params.quality == FAST_ONE_PASS_COMPRESSION_QUALITY ||
      s->params.quality == FAST_TWO_PASS_COMPRESSION_QUALITY) {
    return BrotliEncoderCompressStreamFast(s, op, available_in, next_in,
        available_out, next_out, total_out);
  }
  while (BROTLI_TRUE) {
    size_t remaining_block_size = RemainingInputBlockSize(s);
    /* Shorten input to flint size. */
    if (s->flint_ >= 0 && remaining_block_size > (size_t)s->flint_) {
      remaining_block_size = (size_t)s->flint_;
    }

    if (remaining_block_size != 0 && *available_in != 0) {
      size_t copy_input_size =
          BROTLI_MIN(size_t, remaining_block_size, *available_in);
      CopyInputToRingBuffer(s, copy_input_size, *next_in);
      *next_in += copy_input_size;
      *available_in -= copy_input_size;
      s->total_in_ += copy_input_size;
      if (s->flint_ > 0) s->flint_ = (int8_t)(s->flint_ - (int)copy_input_size);
      continue;
    }

    if (InjectFlushOrPushOutput(s, available_out, next_out, total_out)) {
      /* Exit the "emit flint" workflow. */
      if (s->flint_ == BROTLI_FLINT_WAITING_FOR_FLUSHING) {
        CheckFlushComplete(s);
        if (s->stream_state_ == BROTLI_STREAM_PROCESSING) {
          s->flint_ = BROTLI_FLINT_DONE;
        }
      }
      continue;
    }

    /* Compress data only when internal output buffer is empty, stream is not
       finished and there is no pending flush request. */
    if (s->available_out_ == 0 &&
        s->stream_state_ == BROTLI_STREAM_PROCESSING) {
      if (remaining_block_size == 0 || op != BROTLI_OPERATION_PROCESS) {
        BROTLI_BOOL is_last = TO_BROTLI_BOOL(
            (*available_in == 0) && op == BROTLI_OPERATION_FINISH);
        BROTLI_BOOL force_flush = TO_BROTLI_BOOL(
            (*available_in == 0) && op == BROTLI_OPERATION_FLUSH);
        BROTLI_BOOL result;
        /* Force emitting (uncompressed) piece containing flint. */
        if (!is_last && s->flint_ == 0) {
          s->flint_ = BROTLI_FLINT_WAITING_FOR_FLUSHING;
          force_flush = BROTLI_TRUE;
        }
        UpdateSizeHint(s, *available_in);
        result = EncodeData(s, is_last, force_flush,
            &s->available_out_, &s->next_out_);
        if (!result) return BROTLI_FALSE;
        if (force_flush) s->stream_state_ = BROTLI_STREAM_FLUSH_REQUESTED;
        if (is_last) s->stream_state_ = BROTLI_STREAM_FINISHED;
        continue;
      }
    }
    break;
  }
  CheckFlushComplete(s);
  return BROTLI_TRUE;
}

BROTLI_BOOL BrotliEncoderIsFinished(BrotliEncoderState* s) {
  return TO_BROTLI_BOOL(s->stream_state_ == BROTLI_STREAM_FINISHED &&
      !BrotliEncoderHasMoreOutput(s));
}

BROTLI_BOOL BrotliEncoderHasMoreOutput(BrotliEncoderState* s) {
  return TO_BROTLI_BOOL(s->available_out_ != 0);
}

const uint8_t* BrotliEncoderTakeOutput(BrotliEncoderState* s, size_t* size) {
  size_t consumed_size = s->available_out_;
  uint8_t* result = s->next_out_;
  if (*size) {
    consumed_size = BROTLI_MIN(size_t, *size, s->available_out_);
  }
  if (consumed_size) {
    s->next_out_ += consumed_size;
    s->available_out_ -= consumed_size;
    s->total_out_ += consumed_size;
    CheckFlushComplete(s);
    *size = consumed_size;
  } else {
    *size = 0;
    result = 0;
  }
  return result;
}

uint32_t BrotliEncoderVersion(void) {
  return BROTLI_VERSION;
}

BrotliEncoderPreparedDictionary* BrotliEncoderPrepareDictionary(
    BrotliSharedDictionaryType type, size_t size,
    const uint8_t data[BROTLI_ARRAY_PARAM(size)], int quality,
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  ManagedDictionary* managed_dictionary = NULL;
  BROTLI_BOOL type_is_known = BROTLI_FALSE;
  type_is_known |= (type == BROTLI_SHARED_DICTIONARY_RAW);
#if defined(BROTLI_EXPERIMENTAL)
  type_is_known |= (type == BROTLI_SHARED_DICTIONARY_SERIALIZED);
#endif  /* BROTLI_EXPERIMENTAL */
  if (!type_is_known) {
    return NULL;
  }
  managed_dictionary =
      BrotliCreateManagedDictionary(alloc_func, free_func, opaque);
  if (managed_dictionary == NULL) {
    return NULL;
  }
  if (type == BROTLI_SHARED_DICTIONARY_RAW) {
    managed_dictionary->dictionary = (uint32_t*)CreatePreparedDictionary(
        &managed_dictionary->memory_manager_, data, size);
  }
#if defined(BROTLI_EXPERIMENTAL)
  if (type == BROTLI_SHARED_DICTIONARY_SERIALIZED) {
    SharedEncoderDictionary* dict = (SharedEncoderDictionary*)BrotliAllocate(
        &managed_dictionary->memory_manager_, sizeof(SharedEncoderDictionary));
    managed_dictionary->dictionary = (uint32_t*)dict;
    if (dict != NULL) {
      BROTLI_BOOL ok = BrotliInitCustomSharedEncoderDictionary(
          &managed_dictionary->memory_manager_, data, size, quality, dict);
      if (!ok) {
        BrotliFree(&managed_dictionary->memory_manager_, dict);
        managed_dictionary->dictionary = NULL;
      }
    }
  }
#else  /* BROTLI_EXPERIMENTAL */
  (void)quality;
#endif  /* BROTLI_EXPERIMENTAL */
  if (managed_dictionary->dictionary == NULL) {
    BrotliDestroyManagedDictionary(managed_dictionary);
    return NULL;
  }
  return (BrotliEncoderPreparedDictionary*)managed_dictionary;
}

void BrotliEncoderDestroyPreparedDictionary(
    BrotliEncoderPreparedDictionary* dictionary) {
  ManagedDictionary* dict = (ManagedDictionary*)dictionary;
  if (!dictionary) return;
  /* First field of dictionary structs. */
  /* Only managed dictionaries are eligible for destruction by this method. */
  if (dict->magic != kManagedDictionaryMagic) {
    return;
  }
  if (dict->dictionary == NULL) {
    /* This should never ever happen. */
  } else if (*dict->dictionary == kLeanPreparedDictionaryMagic) {
    DestroyPreparedDictionary(
        &dict->memory_manager_, (PreparedDictionary*)dict->dictionary);
  } else if (*dict->dictionary == kSharedDictionaryMagic) {
    BrotliCleanupSharedEncoderDictionary(&dict->memory_manager_,
        (SharedEncoderDictionary*)dict->dictionary);
    BrotliFree(&dict->memory_manager_, dict->dictionary);
  } else {
    /* There is also kPreparedDictionaryMagic, but such instances should be
     * constructed and destroyed by different means. */
  }
  dict->dictionary = NULL;
  BrotliDestroyManagedDictionary(dict);
}

BROTLI_BOOL BrotliEncoderAttachPreparedDictionary(BrotliEncoderState* state,
    const BrotliEncoderPreparedDictionary* dictionary) {
  /* First field of dictionary structs */
  const BrotliEncoderPreparedDictionary* dict = dictionary;
  uint32_t magic = *((const uint32_t*)dict);
  SharedEncoderDictionary* current = NULL;
  if (magic == kManagedDictionaryMagic) {
    /* Unwrap managed dictionary. */
    ManagedDictionary* managed_dictionary = (ManagedDictionary*)dict;
    magic = *managed_dictionary->dictionary;
    dict = (BrotliEncoderPreparedDictionary*)managed_dictionary->dictionary;
  }
  current = &state->params.dictionary;
  if (magic == kPreparedDictionaryMagic ||
      magic == kLeanPreparedDictionaryMagic) {
    const PreparedDictionary* prepared = (const PreparedDictionary*)dict;
    if (!AttachPreparedDictionary(&current->compound, prepared)) {
      return BROTLI_FALSE;
    }
  } else if (magic == kSharedDictionaryMagic) {
    const SharedEncoderDictionary* attached =
        (const SharedEncoderDictionary*)dict;
    BROTLI_BOOL was_default = !current->contextual.context_based &&
        current->contextual.num_dictionaries == 1 &&
        current->contextual.dict[0]->hash_table_words ==
        kStaticDictionaryHashWords &&
        current->contextual.dict[0]->hash_table_lengths ==
        kStaticDictionaryHashLengths;
    BROTLI_BOOL new_default = !attached->contextual.context_based &&
        attached->contextual.num_dictionaries == 1 &&
        attached->contextual.dict[0]->hash_table_words ==
        kStaticDictionaryHashWords &&
        attached->contextual.dict[0]->hash_table_lengths ==
        kStaticDictionaryHashLengths;
    size_t i;
    if (state->is_initialized_) return BROTLI_FALSE;
    current->max_quality =
        BROTLI_MIN(int, current->max_quality, attached->max_quality);
    for (i = 0; i < attached->compound.num_chunks; i++) {
      if (!AttachPreparedDictionary(&current->compound,
          attached->compound.chunks[i])) {
        return BROTLI_FALSE;
      }
    }
    if (!new_default) {
      if (!was_default) return BROTLI_FALSE;
      /* Copy by value, but then set num_instances_ to 0 because their memory
      is managed by attached, not by current */
      current->contextual = attached->contextual;
      current->contextual.num_instances_ = 0;
    }
  } else {
    return BROTLI_FALSE;
  }
  return BROTLI_TRUE;
}

size_t BrotliEncoderEstimatePeakMemoryUsage(int quality, int lgwin,
                                            size_t input_size) {
  BrotliEncoderParams params;
  size_t memory_manager_slots = BROTLI_ENCODER_MEMORY_MANAGER_SLOTS;
  size_t memory_manager_size = memory_manager_slots * sizeof(void*);
  BrotliEncoderInitParams(&params);
  params.quality = quality;
  params.lgwin = lgwin;
  params.size_hint = input_size;
  params.large_window = lgwin > BROTLI_MAX_WINDOW_BITS;
  SanitizeParams(&params);
  params.lgblock = ComputeLgBlock(&params);
  ChooseHasher(&params, &params.hasher);
  if (params.quality == FAST_ONE_PASS_COMPRESSION_QUALITY ||
      params.quality == FAST_TWO_PASS_COMPRESSION_QUALITY) {
    size_t state_size = sizeof(BrotliEncoderState);
    size_t block_size = BROTLI_MIN(size_t, input_size, (1ul << params.lgwin));
    size_t hash_table_size =
        HashTableSize(MaxHashTableSize(params.quality), block_size);
    size_t hash_size =
        (hash_table_size < (1u << 10)) ? 0 : sizeof(int) * hash_table_size;
    size_t cmdbuf_size = params.quality == FAST_TWO_PASS_COMPRESSION_QUALITY ?
        5 * BROTLI_MIN(size_t, block_size, 1ul << 17) : 0;
    if (params.quality == FAST_ONE_PASS_COMPRESSION_QUALITY) {
      state_size += sizeof(BrotliOnePassArena);
    } else {
      state_size += sizeof(BrotliTwoPassArena);
    }
    return hash_size + cmdbuf_size + state_size;
  } else {
    size_t short_ringbuffer_size = (size_t)1 << params.lgblock;
    int ringbuffer_bits = ComputeRbBits(&params);
    size_t ringbuffer_size = input_size < short_ringbuffer_size ?
        input_size : (1u << ringbuffer_bits) + short_ringbuffer_size;
    size_t hash_size[4] = {0};
    size_t metablock_size =
        BROTLI_MIN(size_t, input_size, MaxMetablockSize(&params));
    size_t inputblock_size =
        BROTLI_MIN(size_t, input_size, (size_t)1 << params.lgblock);
    size_t cmdbuf_size = metablock_size * 2 + inputblock_size * 6;
    size_t outbuf_size = metablock_size * 2 + 503;
    size_t histogram_size = 0;
    HasherSize(&params, BROTLI_TRUE, input_size, hash_size);
    if (params.quality < MIN_QUALITY_FOR_BLOCK_SPLIT) {
      cmdbuf_size = BROTLI_MIN(size_t, cmdbuf_size,
          MAX_NUM_DELAYED_SYMBOLS * sizeof(Command) + inputblock_size * 12);
    }
    if (params.quality >= MIN_QUALITY_FOR_HQ_BLOCK_SPLITTING) {
      /* Only a very rough estimation, based on enwik8. */
      histogram_size = 200 << 20;
    } else if (params.quality >= MIN_QUALITY_FOR_BLOCK_SPLIT) {
      size_t literal_histograms =
          BROTLI_MIN(size_t, metablock_size / 6144, 256);
      size_t command_histograms =
          BROTLI_MIN(size_t, metablock_size / 6144, 256);
      size_t distance_histograms =
          BROTLI_MIN(size_t, metablock_size / 6144, 256);
      histogram_size = literal_histograms * sizeof(HistogramLiteral) +
                       command_histograms * sizeof(HistogramCommand) +
                       distance_histograms * sizeof(HistogramDistance);
    }
    return (memory_manager_size + ringbuffer_size +
            hash_size[0] + hash_size[1] + hash_size[2] + hash_size[3] +
            cmdbuf_size +
            outbuf_size +
            histogram_size);
  }
}
size_t BrotliEncoderGetPreparedDictionarySize(
    const BrotliEncoderPreparedDictionary* prepared_dictionary) {
  /* First field of dictionary structs */
  const BrotliEncoderPreparedDictionary* prepared = prepared_dictionary;
  uint32_t magic = *((const uint32_t*)prepared);
  size_t overhead = 0;
  if (magic == kManagedDictionaryMagic) {
    const ManagedDictionary* managed = (const ManagedDictionary*)prepared;
    overhead = sizeof(ManagedDictionary);
    magic = *managed->dictionary;
    prepared = (const BrotliEncoderPreparedDictionary*)managed->dictionary;
  }

  if (magic == kPreparedDictionaryMagic) {
    const PreparedDictionary* dictionary =
        (const PreparedDictionary*)prepared;
    /* Keep in sync with step 3 of CreatePreparedDictionary */
    return sizeof(PreparedDictionary) + dictionary->source_size +
        (sizeof(uint32_t) << dictionary->slot_bits) +
        (sizeof(uint16_t) << dictionary->bucket_bits) +
        (sizeof(uint32_t) * dictionary->num_items) + overhead;
  } else if (magic == kLeanPreparedDictionaryMagic) {
    const PreparedDictionary* dictionary =
        (const PreparedDictionary*)prepared;
    /* Keep in sync with step 3 of CreatePreparedDictionary */
    return sizeof(PreparedDictionary) + sizeof(uint8_t*) +
        (sizeof(uint32_t) << dictionary->slot_bits) +
        (sizeof(uint16_t) << dictionary->bucket_bits) +
        (sizeof(uint32_t) * dictionary->num_items) + overhead;
  } else if (magic == kSharedDictionaryMagic) {
    const SharedEncoderDictionary* dictionary =
        (const SharedEncoderDictionary*)prepared;
    const CompoundDictionary* compound = &dictionary->compound;
    const ContextualEncoderDictionary* contextual = &dictionary->contextual;
    size_t result = sizeof(*dictionary);
    size_t i;
    size_t num_instances;
    const BrotliEncoderDictionary* instances;
    for (i = 0; i < compound->num_prepared_instances_; i++) {
      size_t size = BrotliEncoderGetPreparedDictionarySize(
          (const BrotliEncoderPreparedDictionary*)
          compound->prepared_instances_[i]);
      if (!size) return 0;  /* error */
      result += size;
    }
    if (contextual->context_based) {
      num_instances = contextual->num_instances_;
      instances = contextual->instances_;
      result += sizeof(*instances) * num_instances;
    } else {
      num_instances = 1;
      instances = &contextual->instance_;
    }
    for (i = 0; i < num_instances; i++) {
      const BrotliEncoderDictionary* dict = &instances[i];
      result += dict->trie.pool_capacity * sizeof(BrotliTrieNode);
      if (dict->hash_table_data_words_) {
        result += sizeof(kStaticDictionaryHashWords);
      }
      if (dict->hash_table_data_lengths_) {
        result += sizeof(kStaticDictionaryHashLengths);
      }
      if (dict->buckets_data_) {
        result += sizeof(*dict->buckets_data_) * dict->buckets_alloc_size_;
      }
      if (dict->dict_words_data_) {
        result += sizeof(*dict->dict_words) * dict->dict_words_alloc_size_;
      }
      if (dict->words_instance_) {
        result += sizeof(*dict->words_instance_);
        /* data_size not added here: it is never allocated by the
           SharedEncoderDictionary, instead it always points to the file
           already loaded in memory. So if the caller wants to include
           this memory as well, add the size of the loaded dictionary
           file to this. */
      }
    }
    return result + overhead;
  }
  return 0;  /* error */
}

#if defined(BROTLI_TEST)
size_t MakeUncompressedStreamForTest(const uint8_t*, size_t, uint8_t*);
size_t MakeUncompressedStreamForTest(
    const uint8_t* input, size_t input_size, uint8_t* output) {
  return MakeUncompressedStream(input, input_size, output);
}
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
