/* Copyright 2022 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Encoder state. */

#ifndef BROTLI_ENC_STATE_H_
#define BROTLI_ENC_STATE_H_

#include <brotli/types.h>

#include "command.h"
#include "compress_fragment.h"
#include "compress_fragment_two_pass.h"
#include "hash.h"
#include "memory.h"
#include "params.h"
#include "ringbuffer.h"

typedef enum BrotliEncoderStreamState {
  /* Default state. */
  BROTLI_STREAM_PROCESSING = 0,
  /* Intermediate state; after next block is emitted, byte-padding should be
     performed before getting back to default state. */
  BROTLI_STREAM_FLUSH_REQUESTED = 1,
  /* Last metablock was produced; no more input is acceptable. */
  BROTLI_STREAM_FINISHED = 2,
  /* Flushing compressed block and writing meta-data block header. */
  BROTLI_STREAM_METADATA_HEAD = 3,
  /* Writing metadata block body. */
  BROTLI_STREAM_METADATA_BODY = 4
} BrotliEncoderStreamState;

typedef enum BrotliEncoderFlintState {
  BROTLI_FLINT_NEEDS_2_BYTES = 2,
  BROTLI_FLINT_NEEDS_1_BYTE = 1,
  BROTLI_FLINT_WAITING_FOR_PROCESSING = 0,
  BROTLI_FLINT_WAITING_FOR_FLUSHING = -1,
  BROTLI_FLINT_DONE = -2
} BrotliEncoderFlintState;

typedef struct BrotliEncoderStateStruct {
  BrotliEncoderParams params;

  MemoryManager memory_manager_;

  uint64_t input_pos_;
  RingBuffer ringbuffer_;
  size_t cmd_alloc_size_;
  Command* commands_;
  size_t num_commands_;
  size_t num_literals_;
  size_t last_insert_len_;
  uint64_t last_flush_pos_;
  uint64_t last_processed_pos_;
  int dist_cache_[BROTLI_NUM_DISTANCE_SHORT_CODES];
  int saved_dist_cache_[4];
  uint16_t last_bytes_;
  uint8_t last_bytes_bits_;
  /* "Flint" is a tiny uncompressed block emitted before the continuation
     block to unwire literal context from previous data. Despite being int8_t,
     field is actually BrotliEncoderFlintState enum. */
  int8_t flint_;
  uint8_t prev_byte_;
  uint8_t prev_byte2_;
  size_t storage_size_;
  uint8_t* storage_;

  Hasher hasher_;

  /* Hash table for FAST_ONE_PASS_COMPRESSION_QUALITY mode. */
  int small_table_[1 << 10];  /* 4KiB */
  int* large_table_;          /* Allocated only when needed */
  size_t large_table_size_;

  BrotliOnePassArena* one_pass_arena_;
  BrotliTwoPassArena* two_pass_arena_;

  /* Command and literal buffers for FAST_TWO_PASS_COMPRESSION_QUALITY. */
  uint32_t* command_buf_;
  uint8_t* literal_buf_;

  uint64_t total_in_;
  uint8_t* next_out_;
  size_t available_out_;
  uint64_t total_out_;
  /* Temporary buffer for padding flush bits or metadata block header / body. */
  union {
    uint64_t u64[2];
    uint8_t u8[16];
  } tiny_buf_;
  uint32_t remaining_metadata_bytes_;
  BrotliEncoderStreamState stream_state_;

  BROTLI_BOOL is_last_block_emitted_;
  BROTLI_BOOL is_initialized_;
} BrotliEncoderStateStruct;

typedef struct BrotliEncoderStateStruct BrotliEncoderStateInternal;
#define BrotliEncoderState BrotliEncoderStateInternal

#endif  // BROTLI_ENC_STATE_H_
