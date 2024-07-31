/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Class to model the static dictionary. */

#ifndef BROTLI_ENC_STATIC_DICT_H_
#define BROTLI_ENC_STATIC_DICT_H_

#include <brotli/types.h>

#include "../common/dictionary.h"
#include "../common/platform.h"
#include "encoder_dict.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN 37
static const uint32_t kInvalidMatch = 0xFFFFFFF;

/* Matches data against static dictionary words, and for each length l,
   for which a match is found, updates matches[l] to be the minimum possible
     (distance << 5) + len_code.
   Returns 1 if matches have been found, otherwise 0.
   Prerequisites:
     matches array is at least BROTLI_MAX_STATIC_DICTIONARY_MATCH_LEN + 1 long
     all elements are initialized to kInvalidMatch */
BROTLI_INTERNAL BROTLI_BOOL BrotliFindAllStaticDictionaryMatches(
    const BrotliEncoderDictionary* dictionary,
    const uint8_t* data, size_t min_length, size_t max_length,
    uint32_t* matches);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_STATIC_DICT_H_ */
