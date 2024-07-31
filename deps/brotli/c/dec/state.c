/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "state.h"

#include <stdlib.h>  /* free, malloc */

#include <brotli/types.h>

#include "../common/dictionary.h"
#include "huffman.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

BROTLI_BOOL BrotliDecoderStateInit(BrotliDecoderState* s,
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  if (!alloc_func) {
    s->alloc_func = BrotliDefaultAllocFunc;
    s->free_func = BrotliDefaultFreeFunc;
    s->memory_manager_opaque = 0;
  } else {
    s->alloc_func = alloc_func;
    s->free_func = free_func;
    s->memory_manager_opaque = opaque;
  }

  s->error_code = 0; /* BROTLI_DECODER_NO_ERROR */

  BrotliInitBitReader(&s->br);
  s->state = BROTLI_STATE_UNINITED;
  s->large_window = 0;
  s->substate_metablock_header = BROTLI_STATE_METABLOCK_HEADER_NONE;
  s->substate_uncompressed = BROTLI_STATE_UNCOMPRESSED_NONE;
  s->substate_decode_uint8 = BROTLI_STATE_DECODE_UINT8_NONE;
  s->substate_read_block_length = BROTLI_STATE_READ_BLOCK_LENGTH_NONE;

  s->buffer_length = 0;
  s->loop_counter = 0;
  s->pos = 0;
  s->rb_roundtrips = 0;
  s->partial_pos_out = 0;
  s->used_input = 0;

  s->block_type_trees = NULL;
  s->block_len_trees = NULL;
  s->ringbuffer = NULL;
  s->ringbuffer_size = 0;
  s->new_ringbuffer_size = 0;
  s->ringbuffer_mask = 0;

  s->context_map = NULL;
  s->context_modes = NULL;
  s->dist_context_map = NULL;
  s->context_map_slice = NULL;
  s->dist_context_map_slice = NULL;

  s->literal_hgroup.codes = NULL;
  s->literal_hgroup.htrees = NULL;
  s->insert_copy_hgroup.codes = NULL;
  s->insert_copy_hgroup.htrees = NULL;
  s->distance_hgroup.codes = NULL;
  s->distance_hgroup.htrees = NULL;

  s->is_last_metablock = 0;
  s->is_uncompressed = 0;
  s->is_metadata = 0;
  s->should_wrap_ringbuffer = 0;
  s->canny_ringbuffer_allocation = 1;

  s->window_bits = 0;
  s->max_distance = 0;
  s->dist_rb[0] = 16;
  s->dist_rb[1] = 15;
  s->dist_rb[2] = 11;
  s->dist_rb[3] = 4;
  s->dist_rb_idx = 0;
  s->block_type_trees = NULL;
  s->block_len_trees = NULL;

  s->mtf_upper_bound = 63;

  s->compound_dictionary = NULL;
  s->dictionary =
      BrotliSharedDictionaryCreateInstance(alloc_func, free_func, opaque);
  if (!s->dictionary) return BROTLI_FALSE;

  s->metadata_start_func = NULL;
  s->metadata_chunk_func = NULL;
  s->metadata_callback_opaque = 0;

  return BROTLI_TRUE;
}

void BrotliDecoderStateMetablockBegin(BrotliDecoderState* s) {
  s->meta_block_remaining_len = 0;
  s->block_length[0] = BROTLI_BLOCK_SIZE_CAP;
  s->block_length[1] = BROTLI_BLOCK_SIZE_CAP;
  s->block_length[2] = BROTLI_BLOCK_SIZE_CAP;
  s->num_block_types[0] = 1;
  s->num_block_types[1] = 1;
  s->num_block_types[2] = 1;
  s->block_type_rb[0] = 1;
  s->block_type_rb[1] = 0;
  s->block_type_rb[2] = 1;
  s->block_type_rb[3] = 0;
  s->block_type_rb[4] = 1;
  s->block_type_rb[5] = 0;
  s->context_map = NULL;
  s->context_modes = NULL;
  s->dist_context_map = NULL;
  s->context_map_slice = NULL;
  s->literal_htree = NULL;
  s->dist_context_map_slice = NULL;
  s->dist_htree_index = 0;
  s->context_lookup = NULL;
  s->literal_hgroup.codes = NULL;
  s->literal_hgroup.htrees = NULL;
  s->insert_copy_hgroup.codes = NULL;
  s->insert_copy_hgroup.htrees = NULL;
  s->distance_hgroup.codes = NULL;
  s->distance_hgroup.htrees = NULL;
}

void BrotliDecoderStateCleanupAfterMetablock(BrotliDecoderState* s) {
  BROTLI_DECODER_FREE(s, s->context_modes);
  BROTLI_DECODER_FREE(s, s->context_map);
  BROTLI_DECODER_FREE(s, s->dist_context_map);
  BROTLI_DECODER_FREE(s, s->literal_hgroup.htrees);
  BROTLI_DECODER_FREE(s, s->insert_copy_hgroup.htrees);
  BROTLI_DECODER_FREE(s, s->distance_hgroup.htrees);
}

#ifdef BROTLI_REPORTING
/* When BROTLI_REPORTING is defined extra reporting module have to be linked. */
void BrotliDecoderOnFinish(const BrotliDecoderState* s);
#define BROTLI_DECODER_ON_FINISH(s) BrotliDecoderOnFinish(s);
#else
#if !defined(BROTLI_DECODER_ON_FINISH)
#define BROTLI_DECODER_ON_FINISH(s) (void)(s);
#endif
#endif

void BrotliDecoderStateCleanup(BrotliDecoderState* s) {
  BrotliDecoderStateCleanupAfterMetablock(s);

  BROTLI_DECODER_ON_FINISH(s);

  BROTLI_DECODER_FREE(s, s->compound_dictionary);
  BrotliSharedDictionaryDestroyInstance(s->dictionary);
  s->dictionary = NULL;
  BROTLI_DECODER_FREE(s, s->ringbuffer);
  BROTLI_DECODER_FREE(s, s->block_type_trees);
}

BROTLI_BOOL BrotliDecoderHuffmanTreeGroupInit(BrotliDecoderState* s,
    HuffmanTreeGroup* group, brotli_reg_t alphabet_size_max,
    brotli_reg_t alphabet_size_limit, brotli_reg_t ntrees) {
  /* 376 = 256 (1-st level table) + 4 + 7 + 15 + 31 + 63 (2-nd level mix-tables)
     This number is discovered "unlimited" "enough" calculator; it is actually
     a wee bigger than required in several cases (especially for alphabets with
     less than 16 symbols). */
  const size_t max_table_size = alphabet_size_limit + 376;
  const size_t code_size = sizeof(HuffmanCode) * ntrees * max_table_size;
  const size_t htree_size = sizeof(HuffmanCode*) * ntrees;
  /* Pointer alignment is, hopefully, wider than sizeof(HuffmanCode). */
  HuffmanCode** p = (HuffmanCode**)BROTLI_DECODER_ALLOC(s,
      code_size + htree_size);
  group->alphabet_size_max = (uint16_t)alphabet_size_max;
  group->alphabet_size_limit = (uint16_t)alphabet_size_limit;
  group->num_htrees = (uint16_t)ntrees;
  group->htrees = p;
  group->codes = (HuffmanCode*)(&p[ntrees]);
  return !!p;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
