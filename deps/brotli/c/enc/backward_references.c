/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Function to find backward reference copies. */

#include "backward_references.h"

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/dictionary.h"
#include "../common/platform.h"
#include "command.h"
#include "compound_dictionary.h"
#include "dictionary_hash.h"
#include "encoder_dict.h"
#include "memory.h"
#include "quality.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static BROTLI_INLINE size_t ComputeDistanceCode(size_t distance,
                                                size_t max_distance,
                                                const int* dist_cache) {
  if (distance <= max_distance) {
    size_t distance_plus_3 = distance + 3;
    size_t offset0 = distance_plus_3 - (size_t)dist_cache[0];
    size_t offset1 = distance_plus_3 - (size_t)dist_cache[1];
    if (distance == (size_t)dist_cache[0]) {
      return 0;
    } else if (distance == (size_t)dist_cache[1]) {
      return 1;
    } else if (offset0 < 7) {
      return (0x9750468 >> (4 * offset0)) & 0xF;
    } else if (offset1 < 7) {
      return (0xFDB1ACE >> (4 * offset1)) & 0xF;
    } else if (distance == (size_t)dist_cache[2]) {
      return 2;
    } else if (distance == (size_t)dist_cache[3]) {
      return 3;
    }
  }
  return distance + BROTLI_NUM_DISTANCE_SHORT_CODES - 1;
}

#define EXPAND_CAT(a, b) CAT(a, b)
#define CAT(a, b) a ## b
#define FN(X) EXPAND_CAT(X, HASHER())
#define EXPORT_FN(X) EXPAND_CAT(X, EXPAND_CAT(PREFIX(), HASHER()))

#define PREFIX() N
#define ENABLE_COMPOUND_DICTIONARY 0

#define HASHER() H2
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H3
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H4
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H5
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H6
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H40
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H41
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H42
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H54
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H35
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H55
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#define HASHER() H65
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#undef ENABLE_COMPOUND_DICTIONARY
#undef PREFIX
#define PREFIX() D
#define ENABLE_COMPOUND_DICTIONARY 1

#define HASHER() H5
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER
#define HASHER() H6
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER
#define HASHER() H40
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER
#define HASHER() H41
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER
#define HASHER() H42
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER
#define HASHER() H55
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER
#define HASHER() H65
/* NOLINTNEXTLINE(build/include) */
#include "backward_references_inc.h"
#undef HASHER

#undef ENABLE_COMPOUND_DICTIONARY
#undef PREFIX

#undef EXPORT_FN
#undef FN
#undef CAT
#undef EXPAND_CAT

void BrotliCreateBackwardReferences(size_t num_bytes,
    size_t position, const uint8_t* ringbuffer, size_t ringbuffer_mask,
    ContextLut literal_context_lut, const BrotliEncoderParams* params,
    Hasher* hasher, int* dist_cache, size_t* last_insert_len,
    Command* commands, size_t* num_commands, size_t* num_literals) {
  if (params->dictionary.compound.num_chunks != 0) {
    switch (params->hasher.type) {
#define CASE_(N)                                                    \
      case N:                                                       \
        CreateBackwardReferencesDH ## N(num_bytes,                  \
            position, ringbuffer, ringbuffer_mask,                  \
            literal_context_lut, params, hasher, dist_cache,        \
            last_insert_len, commands, num_commands, num_literals); \
        return;
      CASE_(5)
      CASE_(6)
      CASE_(40)
      CASE_(41)
      CASE_(42)
      CASE_(55)
      CASE_(65)
#undef CASE_
      default:
        BROTLI_DCHECK(false);
        break;
    }
  }

  switch (params->hasher.type) {
#define CASE_(N)                                                  \
    case N:                                                       \
      CreateBackwardReferencesNH ## N(num_bytes,                  \
          position, ringbuffer, ringbuffer_mask,                  \
          literal_context_lut, params, hasher, dist_cache,        \
          last_insert_len, commands, num_commands, num_literals); \
      return;
    FOR_GENERIC_HASHERS(CASE_)
#undef CASE_
    default:
      BROTLI_DCHECK(false);
      break;
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
