/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Function to find backward reference copies. */

#ifndef BROTLI_ENC_BACKWARD_REFERENCES_H_
#define BROTLI_ENC_BACKWARD_REFERENCES_H_

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/context.h"
#include "../common/dictionary.h"
#include "../common/platform.h"
#include "command.h"
#include "hash.h"
#include "quality.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* "commands" points to the next output command to write to, "*num_commands" is
   initially the total amount of commands output by previous
   CreateBackwardReferences calls, and must be incremented by the amount written
   by this call. */
BROTLI_INTERNAL void BrotliCreateBackwardReferences(size_t num_bytes,
    size_t position, const uint8_t* ringbuffer, size_t ringbuffer_mask,
    ContextLut literal_context_lut, const BrotliEncoderParams* params,
    Hasher* hasher, int* dist_cache, size_t* last_insert_len,
    Command* commands, size_t* num_commands, size_t* num_literals);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_BACKWARD_REFERENCES_H_ */
