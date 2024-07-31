/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#ifndef BROTLI_ENC_ENCODER_DICT_H_
#define BROTLI_ENC_ENCODER_DICT_H_

#include <brotli/shared_dictionary.h>
#include <brotli/types.h>

#include "../common/dictionary.h"
#include "../common/platform.h"
#include "compound_dictionary.h"
#include "memory.h"
#include "static_dict_lut.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*
Dictionary hierarchy for Encoder:
-SharedEncoderDictionary
--CompoundDictionary
---PreparedDictionary [up to 15x]
   = prefix dictionary with precomputed hashes
--ContextualEncoderDictionary
---BrotliEncoderDictionary [up to 64x]
   = for each context, precomputed static dictionary with words + transforms

Dictionary hiearchy from common: similar, but without precomputed hashes
-BrotliSharedDictionary
--BrotliDictionary [up to 64x]
--BrotliTransforms [up to 64x]
--const uint8_t* prefix [up to 15x]: compound dictionaries
*/

typedef struct BrotliTrieNode {
  uint8_t single;  /* if 1, sub is a single node for c instead of 256 */
  uint8_t c;
  uint8_t len_;  /* untransformed length */
  uint32_t idx_;  /* word index + num words * transform index */
  uint32_t sub;  /* index of sub node(s) in the pool */
} BrotliTrieNode;

typedef struct BrotliTrie {
  BrotliTrieNode* pool;
  size_t pool_capacity;
  size_t pool_size;
  BrotliTrieNode root;
} BrotliTrie;

#if defined(BROTLI_EXPERIMENTAL)
BROTLI_INTERNAL const BrotliTrieNode* BrotliTrieSub(const BrotliTrie* trie,
    const BrotliTrieNode* node, uint8_t c);
#endif  /* BROTLI_EXPERIMENTAL */

/* Dictionary data (words and transforms) for 1 possible context */
typedef struct BrotliEncoderDictionary {
  const BrotliDictionary* words;
  uint32_t num_transforms;

  /* cut off for fast encoder */
  uint32_t cutoffTransformsCount;
  uint64_t cutoffTransforms;

  /* from dictionary_hash.h, for fast encoder */
  const uint16_t* hash_table_words;
  const uint8_t* hash_table_lengths;

  /* from static_dict_lut.h, for slow encoder */
  const uint16_t* buckets;
  const DictWord* dict_words;
  /* Heavy version, for use by slow encoder when there are custom transforms.
     Contains every possible transformed dictionary word in a trie. It encodes
     about as fast as the non-heavy encoder but consumes a lot of memory and
     takes time to build. */
  BrotliTrie trie;
  BROTLI_BOOL has_words_heavy;

  /* Reference to other dictionaries. */
  const struct ContextualEncoderDictionary* parent;

  /* Allocated memory, used only when not using the Brotli defaults */
  uint16_t* hash_table_data_words_;
  uint8_t* hash_table_data_lengths_;
  size_t buckets_alloc_size_;
  uint16_t* buckets_data_;
  size_t dict_words_alloc_size_;
  DictWord* dict_words_data_;
  BrotliDictionary* words_instance_;
} BrotliEncoderDictionary;

/* Dictionary data for all 64 contexts */
typedef struct ContextualEncoderDictionary {
  BROTLI_BOOL context_based;
  uint8_t num_dictionaries;
  uint8_t context_map[SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS];
  const BrotliEncoderDictionary* dict[SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS];

  /* If num_instances_ is 1, instance_ is used, else dynamic allocation with
     instances_ is used. */
  size_t num_instances_;
  BrotliEncoderDictionary instance_;
  BrotliEncoderDictionary* instances_;
} ContextualEncoderDictionary;

typedef struct SharedEncoderDictionary {
  /* Magic value to distinguish this struct from PreparedDictionary for
     certain external usages. */
  uint32_t magic;

  /* LZ77 prefix, compound dictionary */
  CompoundDictionary compound;

  /* Custom static dictionary (optionally context-based) */
  ContextualEncoderDictionary contextual;

  /* The maximum quality the dictionary was computed for */
  int max_quality;
} SharedEncoderDictionary;

typedef struct ManagedDictionary {
  uint32_t magic;
  MemoryManager memory_manager_;
  uint32_t* dictionary;
} ManagedDictionary;

/* Initializes to the brotli built-in dictionary */
BROTLI_INTERNAL void BrotliInitSharedEncoderDictionary(
    SharedEncoderDictionary* dict);

#if defined(BROTLI_EXPERIMENTAL)
/* Initializes to shared dictionary that will be parsed from
   encoded_dict. Requires that you keep the encoded_dict buffer
   around, parts of data will point to it. */
BROTLI_INTERNAL BROTLI_BOOL BrotliInitCustomSharedEncoderDictionary(
    MemoryManager* m, const uint8_t* encoded_dict, size_t size,
    int quality, SharedEncoderDictionary* dict);
#endif  /* BROTLI_EXPERIMENTAL */

BROTLI_INTERNAL void BrotliCleanupSharedEncoderDictionary(
    MemoryManager* m, SharedEncoderDictionary* dict);

BROTLI_INTERNAL ManagedDictionary* BrotliCreateManagedDictionary(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque);

BROTLI_INTERNAL void BrotliDestroyManagedDictionary(
    ManagedDictionary* dictionary);

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif

#endif  /* BROTLI_ENC_ENCODER_DICT_H_ */
