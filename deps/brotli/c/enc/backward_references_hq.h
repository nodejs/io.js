/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Function to find backward reference copies. */

#ifndef BROTLI_ENC_BACKWARD_REFERENCES_HQ_H_
#define BROTLI_ENC_BACKWARD_REFERENCES_HQ_H_

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/context.h"
#include "../common/dictionary.h"
#include "../common/platform.h"
#include "command.h"
#include "hash.h"
#include "memory.h"
#include "quality.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

BROTLI_INTERNAL void BrotliCreateZopfliBackwardReferences(MemoryManager* m,
    size_t num_bytes,
    size_t position, const uint8_t* ringbuffer, size_t ringbuffer_mask,
    ContextLut literal_context_lut, const BrotliEncoderParams* params,
    Hasher* hasher, int* dist_cache, size_t* last_insert_len,
    Command* commands, size_t* num_commands, size_t* num_literals);

BROTLI_INTERNAL void BrotliCreateHqZopfliBackwardReferences(MemoryManager* m,
    size_t num_bytes,
    size_t position, const uint8_t* ringbuffer, size_t ringbuffer_mask,
    ContextLut literal_context_lut, const BrotliEncoderParams* params,
    Hasher* hasher, int* dist_cache, size_t* last_insert_len,
    Command* commands, size_t* num_commands, size_t* num_literals);

typedef struct ZopfliNode {
  /* Best length to get up to this byte (not including this byte itself)
     highest 7 bit is used to reconstruct the length code. */
  uint32_t length;
  /* Distance associated with the length. */
  uint32_t distance;
  /* Number of literal inserts before this copy; highest 5 bits contain
     distance short code + 1 (or zero if no short code). */
  uint32_t dcode_insert_length;

  /* This union holds information used by dynamic-programming. During forward
     pass |cost| it used to store the goal function. When node is processed its
     |cost| is invalidated in favor of |shortcut|. On path back-tracing pass
     |next| is assigned the offset to next node on the path. */
  union {
    /* Smallest cost to get to this byte from the beginning, as found so far. */
    float cost;
    /* Offset to the next node on the path. Equals to command_length() of the
       next node on the path. For last node equals to BROTLI_UINT32_MAX */
    uint32_t next;
    /* Node position that provides next distance for distance cache. */
    uint32_t shortcut;
  } u;
} ZopfliNode;

BROTLI_INTERNAL void BrotliInitZopfliNodes(ZopfliNode* array, size_t length);

/* Computes the shortest path of commands from position to at most
   position + num_bytes.

   On return, path->size() is the number of commands found and path[i] is the
   length of the i-th command (copy length plus insert length).
   Note that the sum of the lengths of all commands can be less than num_bytes.

   On return, the nodes[0..num_bytes] array will have the following
   "ZopfliNode array invariant":
   For each i in [1..num_bytes], if nodes[i].cost < kInfinity, then
     (1) nodes[i].copy_length() >= 2
     (2) nodes[i].command_length() <= i and
     (3) nodes[i - nodes[i].command_length()].cost < kInfinity */
BROTLI_INTERNAL size_t BrotliZopfliComputeShortestPath(
    MemoryManager* m, size_t num_bytes,
    size_t position, const uint8_t* ringbuffer, size_t ringbuffer_mask,
    ContextLut literal_context_lut, const BrotliEncoderParams* params,
    const int* dist_cache, Hasher* hasher, ZopfliNode* nodes);

BROTLI_INTERNAL void BrotliZopfliCreateCommands(
    const size_t num_bytes, const size_t block_start, const ZopfliNode* nodes,
    int* dist_cache, size_t* last_insert_len, const BrotliEncoderParams* params,
    Command* commands, size_t* num_literals);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_BACKWARD_REFERENCES_HQ_H_ */
