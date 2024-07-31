/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Brotli state for partial streaming decoding. */

#ifndef BROTLI_DEC_STATE_H_
#define BROTLI_DEC_STATE_H_

#include <brotli/decode.h>
#include <brotli/shared_dictionary.h>
#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/dictionary.h"
#include "../common/platform.h"
#include "../common/transform.h"
#include "bit_reader.h"
#include "huffman.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Graphviz diagram that describes state transitions:

digraph States {
  graph [compound=true]
  concentrate=true
  node [shape="box"]

  UNINITED -> {LARGE_WINDOW_BITS -> INITIALIZE}
  subgraph cluster_metablock_workflow {
    style="rounded"
    label=< <B>METABLOCK CYCLE</B> >
    METABLOCK_BEGIN -> METABLOCK_HEADER
    METABLOCK_HEADER:sw -> METADATA
    METABLOCK_HEADER:s -> UNCOMPRESSED
    METABLOCK_HEADER:se -> METABLOCK_DONE:ne
    METADATA:s -> METABLOCK_DONE:w
    UNCOMPRESSED:s -> METABLOCK_DONE:n
    METABLOCK_DONE:e -> METABLOCK_BEGIN:e [constraint="false"]
  }
  INITIALIZE -> METABLOCK_BEGIN
  METABLOCK_DONE -> DONE

  subgraph cluster_compressed_metablock {
    style="rounded"
    label=< <B>COMPRESSED METABLOCK</B> >

    subgraph cluster_command {
      style="rounded"
      label=< <B>HOT LOOP</B> >

      _METABLOCK_DONE_PORT_ [shape=point style=invis]

      {
        // Set different shape for nodes returning from "compressed metablock".
        node [shape=invhouse]; CMD_INNER CMD_POST_DECODE_LITERALS;
        CMD_POST_WRAP_COPY; CMD_INNER_WRITE; CMD_POST_WRITE_1;
      }

      CMD_BEGIN -> CMD_INNER -> CMD_POST_DECODE_LITERALS -> CMD_POST_WRAP_COPY

      // IO ("write") nodes are not in the hot loop!
      CMD_INNER_WRITE [style=dashed]
      CMD_INNER -> CMD_INNER_WRITE
      CMD_POST_WRITE_1 [style=dashed]
      CMD_POST_DECODE_LITERALS -> CMD_POST_WRITE_1
      CMD_POST_WRITE_2 [style=dashed]
      CMD_POST_WRAP_COPY -> CMD_POST_WRITE_2

      CMD_POST_WRITE_1 -> CMD_BEGIN:s [constraint="false"]
      CMD_INNER_WRITE -> {CMD_INNER CMD_POST_DECODE_LITERALS}
          [constraint="false"]
      CMD_BEGIN:ne -> CMD_POST_DECODE_LITERALS [constraint="false"]
      CMD_POST_WRAP_COPY -> CMD_BEGIN [constraint="false"]
      CMD_POST_DECODE_LITERALS -> CMD_BEGIN:ne [constraint="false"]
      CMD_POST_WRITE_2 -> CMD_POST_WRAP_COPY [constraint="false"]
      {rank=same; CMD_BEGIN; CMD_INNER; CMD_POST_DECODE_LITERALS;
          CMD_POST_WRAP_COPY}
      {rank=same; CMD_INNER_WRITE; CMD_POST_WRITE_1; CMD_POST_WRITE_2}

      {CMD_INNER CMD_POST_DECODE_LITERALS CMD_POST_WRAP_COPY} ->
          _METABLOCK_DONE_PORT_ [style=invis]
      {CMD_INNER_WRITE CMD_POST_WRITE_1} -> _METABLOCK_DONE_PORT_
          [constraint="false" style=invis]
    }

    BEFORE_COMPRESSED_METABLOCK_HEADER:s -> HUFFMAN_CODE_0:n
    HUFFMAN_CODE_0 -> HUFFMAN_CODE_1 -> HUFFMAN_CODE_2 -> HUFFMAN_CODE_3
    HUFFMAN_CODE_0 -> METABLOCK_HEADER_2 -> CONTEXT_MODES -> CONTEXT_MAP_1
    CONTEXT_MAP_1 -> CONTEXT_MAP_2 -> TREE_GROUP
    TREE_GROUP -> BEFORE_COMPRESSED_METABLOCK_BODY:e
    BEFORE_COMPRESSED_METABLOCK_BODY:s -> CMD_BEGIN:n

    HUFFMAN_CODE_3:e -> HUFFMAN_CODE_0:ne [constraint="false"]
    {rank=same; HUFFMAN_CODE_0; HUFFMAN_CODE_1; HUFFMAN_CODE_2; HUFFMAN_CODE_3}
    {rank=same; METABLOCK_HEADER_2; CONTEXT_MODES; CONTEXT_MAP_1; CONTEXT_MAP_2;
        TREE_GROUP}
  }
  METABLOCK_HEADER:e -> BEFORE_COMPRESSED_METABLOCK_HEADER:n

  _METABLOCK_DONE_PORT_ -> METABLOCK_DONE:se
      [constraint="false" ltail=cluster_command]

  UNINITED [shape=Mdiamond];
  DONE [shape=Msquare];
}


 */

typedef enum {
  BROTLI_STATE_UNINITED,
  BROTLI_STATE_LARGE_WINDOW_BITS,
  BROTLI_STATE_INITIALIZE,
  BROTLI_STATE_METABLOCK_BEGIN,
  BROTLI_STATE_METABLOCK_HEADER,
  BROTLI_STATE_METABLOCK_HEADER_2,
  BROTLI_STATE_CONTEXT_MODES,
  BROTLI_STATE_COMMAND_BEGIN,
  BROTLI_STATE_COMMAND_INNER,
  BROTLI_STATE_COMMAND_POST_DECODE_LITERALS,
  BROTLI_STATE_COMMAND_POST_WRAP_COPY,
  BROTLI_STATE_UNCOMPRESSED,
  BROTLI_STATE_METADATA,
  BROTLI_STATE_COMMAND_INNER_WRITE,
  BROTLI_STATE_METABLOCK_DONE,
  BROTLI_STATE_COMMAND_POST_WRITE_1,
  BROTLI_STATE_COMMAND_POST_WRITE_2,
  BROTLI_STATE_BEFORE_COMPRESSED_METABLOCK_HEADER,
  BROTLI_STATE_HUFFMAN_CODE_0,
  BROTLI_STATE_HUFFMAN_CODE_1,
  BROTLI_STATE_HUFFMAN_CODE_2,
  BROTLI_STATE_HUFFMAN_CODE_3,
  BROTLI_STATE_CONTEXT_MAP_1,
  BROTLI_STATE_CONTEXT_MAP_2,
  BROTLI_STATE_TREE_GROUP,
  BROTLI_STATE_BEFORE_COMPRESSED_METABLOCK_BODY,
  BROTLI_STATE_DONE
} BrotliRunningState;

typedef enum {
  BROTLI_STATE_METABLOCK_HEADER_NONE,
  BROTLI_STATE_METABLOCK_HEADER_EMPTY,
  BROTLI_STATE_METABLOCK_HEADER_NIBBLES,
  BROTLI_STATE_METABLOCK_HEADER_SIZE,
  BROTLI_STATE_METABLOCK_HEADER_UNCOMPRESSED,
  BROTLI_STATE_METABLOCK_HEADER_RESERVED,
  BROTLI_STATE_METABLOCK_HEADER_BYTES,
  BROTLI_STATE_METABLOCK_HEADER_METADATA
} BrotliRunningMetablockHeaderState;

typedef enum {
  BROTLI_STATE_UNCOMPRESSED_NONE,
  BROTLI_STATE_UNCOMPRESSED_WRITE
} BrotliRunningUncompressedState;

typedef enum {
  BROTLI_STATE_TREE_GROUP_NONE,
  BROTLI_STATE_TREE_GROUP_LOOP
} BrotliRunningTreeGroupState;

typedef enum {
  BROTLI_STATE_CONTEXT_MAP_NONE,
  BROTLI_STATE_CONTEXT_MAP_READ_PREFIX,
  BROTLI_STATE_CONTEXT_MAP_HUFFMAN,
  BROTLI_STATE_CONTEXT_MAP_DECODE,
  BROTLI_STATE_CONTEXT_MAP_TRANSFORM
} BrotliRunningContextMapState;

typedef enum {
  BROTLI_STATE_HUFFMAN_NONE,
  BROTLI_STATE_HUFFMAN_SIMPLE_SIZE,
  BROTLI_STATE_HUFFMAN_SIMPLE_READ,
  BROTLI_STATE_HUFFMAN_SIMPLE_BUILD,
  BROTLI_STATE_HUFFMAN_COMPLEX,
  BROTLI_STATE_HUFFMAN_LENGTH_SYMBOLS
} BrotliRunningHuffmanState;

typedef enum {
  BROTLI_STATE_DECODE_UINT8_NONE,
  BROTLI_STATE_DECODE_UINT8_SHORT,
  BROTLI_STATE_DECODE_UINT8_LONG
} BrotliRunningDecodeUint8State;

typedef enum {
  BROTLI_STATE_READ_BLOCK_LENGTH_NONE,
  BROTLI_STATE_READ_BLOCK_LENGTH_SUFFIX
} BrotliRunningReadBlockLengthState;

/* BrotliDecoderState addon, used for Compound Dictionary functionality. */
typedef struct BrotliDecoderCompoundDictionary {
  int num_chunks;
  int total_size;
  int br_index;
  int br_offset;
  int br_length;
  int br_copied;
  const uint8_t* chunks[16];
  int chunk_offsets[16];
  int block_bits;
  uint8_t block_map[256];
} BrotliDecoderCompoundDictionary;

typedef struct BrotliMetablockHeaderArena {
  BrotliRunningTreeGroupState substate_tree_group;
  BrotliRunningContextMapState substate_context_map;
  BrotliRunningHuffmanState substate_huffman;

  brotli_reg_t sub_loop_counter;

  brotli_reg_t repeat_code_len;
  brotli_reg_t prev_code_len;

  /* For ReadHuffmanCode. */
  brotli_reg_t symbol;
  brotli_reg_t repeat;
  brotli_reg_t space;

  /* Huffman table for "histograms". */
  HuffmanCode table[32];
  /* List of heads of symbol chains. */
  uint16_t* symbol_lists;
  /* Storage from symbol_lists. */
  uint16_t symbols_lists_array[BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1 +
                               BROTLI_NUM_COMMAND_SYMBOLS];
  /* Tails of symbol chains. */
  int next_symbol[32];
  uint8_t code_length_code_lengths[BROTLI_CODE_LENGTH_CODES];
  /* Population counts for the code lengths. */
  uint16_t code_length_histo[16];
  /* TODO(eustas): +2 bytes padding */

  /* For HuffmanTreeGroupDecode. */
  int htree_index;
  HuffmanCode* next;

  /* For DecodeContextMap. */
  brotli_reg_t context_index;
  brotli_reg_t max_run_length_prefix;
  brotli_reg_t code;
  HuffmanCode context_map_table[BROTLI_HUFFMAN_MAX_SIZE_272];
} BrotliMetablockHeaderArena;

typedef struct BrotliMetablockBodyArena {
  uint8_t dist_extra_bits[544];
  brotli_reg_t dist_offset[544];
} BrotliMetablockBodyArena;

struct BrotliDecoderStateStruct {
  BrotliRunningState state;

  /* This counter is reused for several disjoint loops. */
  int loop_counter;

  BrotliBitReader br;

  brotli_alloc_func alloc_func;
  brotli_free_func free_func;
  void* memory_manager_opaque;

  /* Temporary storage for remaining input. Brotli stream format is designed in
     a way, that 64 bits are enough to make progress in decoding. */
  union {
    uint64_t u64;
    uint8_t u8[8];
  } buffer;
  brotli_reg_t buffer_length;

  int pos;
  int max_backward_distance;
  int max_distance;
  int ringbuffer_size;
  int ringbuffer_mask;
  int dist_rb_idx;
  int dist_rb[4];
  int error_code;
  int meta_block_remaining_len;

  uint8_t* ringbuffer;
  uint8_t* ringbuffer_end;
  HuffmanCode* htree_command;
  const uint8_t* context_lookup;
  uint8_t* context_map_slice;
  uint8_t* dist_context_map_slice;

  /* This ring buffer holds a few past copy distances that will be used by
     some special distance codes. */
  HuffmanTreeGroup literal_hgroup;
  HuffmanTreeGroup insert_copy_hgroup;
  HuffmanTreeGroup distance_hgroup;
  HuffmanCode* block_type_trees;
  HuffmanCode* block_len_trees;
  /* This is true if the literal context map histogram type always matches the
     block type. It is then not needed to keep the context (faster decoding). */
  int trivial_literal_context;
  /* Distance context is actual after command is decoded and before distance is
     computed. After distance computation it is used as a temporary variable. */
  int distance_context;
  brotli_reg_t block_length[3];
  brotli_reg_t block_length_index;
  brotli_reg_t num_block_types[3];
  brotli_reg_t block_type_rb[6];
  brotli_reg_t distance_postfix_bits;
  brotli_reg_t num_direct_distance_codes;
  brotli_reg_t num_dist_htrees;
  uint8_t* dist_context_map;
  HuffmanCode* literal_htree;

  /* For partial write operations. */
  size_t rb_roundtrips;  /* how many times we went around the ring-buffer */
  size_t partial_pos_out;  /* how much output to the user in total */

  /* For InverseMoveToFrontTransform. */
  brotli_reg_t mtf_upper_bound;
  uint32_t mtf[64 + 1];

  int copy_length;
  int distance_code;

  uint8_t dist_htree_index;
  /* TODO(eustas): +3 bytes padding */

  /* Less used attributes are at the end of this struct. */

  brotli_decoder_metadata_start_func metadata_start_func;
  brotli_decoder_metadata_chunk_func metadata_chunk_func;
  void* metadata_callback_opaque;

  /* For reporting. */
  uint64_t used_input;  /* how many bytes of input are consumed */

  /* States inside function calls. */
  BrotliRunningMetablockHeaderState substate_metablock_header;
  BrotliRunningUncompressedState substate_uncompressed;
  BrotliRunningDecodeUint8State substate_decode_uint8;
  BrotliRunningReadBlockLengthState substate_read_block_length;

  int new_ringbuffer_size;
  /* TODO(eustas): +4 bytes padding */

  unsigned int is_last_metablock : 1;
  unsigned int is_uncompressed : 1;
  unsigned int is_metadata : 1;
  unsigned int should_wrap_ringbuffer : 1;
  unsigned int canny_ringbuffer_allocation : 1;
  unsigned int large_window : 1;
  unsigned int window_bits : 6;
  unsigned int size_nibbles : 8;
  /* TODO(eustas): +12 bits padding */

  brotli_reg_t num_literal_htrees;
  uint8_t* context_map;
  uint8_t* context_modes;

  BrotliSharedDictionary* dictionary;
  BrotliDecoderCompoundDictionary* compound_dictionary;

  uint32_t trivial_literal_contexts[8];  /* 256 bits */

  union {
    BrotliMetablockHeaderArena header;
    BrotliMetablockBodyArena body;
  } arena;
};

typedef struct BrotliDecoderStateStruct BrotliDecoderStateInternal;
#define BrotliDecoderState BrotliDecoderStateInternal

BROTLI_INTERNAL BROTLI_BOOL BrotliDecoderStateInit(BrotliDecoderState* s,
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque);
BROTLI_INTERNAL void BrotliDecoderStateCleanup(BrotliDecoderState* s);
BROTLI_INTERNAL void BrotliDecoderStateMetablockBegin(BrotliDecoderState* s);
BROTLI_INTERNAL void BrotliDecoderStateCleanupAfterMetablock(
    BrotliDecoderState* s);
BROTLI_INTERNAL BROTLI_BOOL BrotliDecoderHuffmanTreeGroupInit(
    BrotliDecoderState* s, HuffmanTreeGroup* group,
    brotli_reg_t alphabet_size_max, brotli_reg_t alphabet_size_limit,
    brotli_reg_t ntrees);

#define BROTLI_DECODER_ALLOC(S, L) S->alloc_func(S->memory_manager_opaque, L)

#define BROTLI_DECODER_FREE(S, X) {          \
  S->free_func(S->memory_manager_opaque, X); \
  X = NULL;                                  \
}

/* Literal/Command/Distance block size maximum; same as maximum metablock size;
   used as block size when there is no block switching. */
#define BROTLI_BLOCK_SIZE_CAP (1U << 24)

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_DEC_STATE_H_ */
