/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include "encoder_dict.h"

#include <stdlib.h>  /* malloc, free */

#include "../common/dictionary.h"
#include "../common/platform.h"
#include "../common/shared_dictionary_internal.h"
#include "../common/transform.h"
#include "compound_dictionary.h"
#include "dictionary_hash.h"
#include "memory.h"
#include "quality.h"
#include "hash.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define NUM_HASH_BITS 15u
#define NUM_HASH_BUCKETS (1u << NUM_HASH_BITS)

static void BrotliTrieInit(BrotliTrie* trie) {
  trie->pool_capacity = 0;
  trie->pool_size = 0;
  trie->pool = 0;

  /* Set up the root node */
  trie->root.single = 0;
  trie->root.len_ = 0;
  trie->root.idx_ = 0;
  trie->root.sub = 0;
}

static void BrotliTrieFree(MemoryManager* m, BrotliTrie* trie) {
  BrotliFree(m, trie->pool);
}

/* Initializes to RFC 7932 static dictionary / transforms. */
static void InitEncoderDictionary(BrotliEncoderDictionary* dict) {
  dict->words = BrotliGetDictionary();
  dict->num_transforms = (uint32_t)BrotliGetTransforms()->num_transforms;

  dict->hash_table_words = kStaticDictionaryHashWords;
  dict->hash_table_lengths = kStaticDictionaryHashLengths;
  dict->buckets = kStaticDictionaryBuckets;
  dict->dict_words = kStaticDictionaryWords;

  dict->cutoffTransformsCount = kCutoffTransformsCount;
  dict->cutoffTransforms = kCutoffTransforms;

  dict->parent = 0;

  dict->hash_table_data_words_ = 0;
  dict->hash_table_data_lengths_ = 0;
  dict->buckets_alloc_size_ = 0;
  dict->buckets_data_ = 0;
  dict->dict_words_alloc_size_ = 0;
  dict->dict_words_data_ = 0;
  dict->words_instance_ = 0;
  dict->has_words_heavy = BROTLI_FALSE;
  BrotliTrieInit(&dict->trie);
}

static void BrotliDestroyEncoderDictionary(MemoryManager* m,
    BrotliEncoderDictionary* dict) {
  BrotliFree(m, dict->hash_table_data_words_);
  BrotliFree(m, dict->hash_table_data_lengths_);
  BrotliFree(m, dict->buckets_data_);
  BrotliFree(m, dict->dict_words_data_);
  BrotliFree(m, dict->words_instance_);
  BrotliTrieFree(m, &dict->trie);
}

#if defined(BROTLI_EXPERIMENTAL)
/* Word length must be at least 4 bytes */
static uint32_t Hash(const uint8_t* data, int bits) {
  uint32_t h = BROTLI_UNALIGNED_LOAD32LE(data) * kHashMul32;
  /* The higher bits contain more mixture from the multiplication,
     so we take our results from there. */
  return h >> (32 - bits);
}

/* Theoretical max possible word size after transform */
#define kTransformedBufferSize \
    (256 + 256 + SHARED_BROTLI_MAX_DICTIONARY_WORD_LENGTH)

/* To be safe buffer must have at least kTransformedBufferSize */
static void TransformedDictionaryWord(uint32_t word_idx, int len, int transform,
    const BrotliTransforms* transforms,
    const BrotliEncoderDictionary* dict,
    uint8_t* buffer, size_t* size) {
  const uint8_t* dict_word = &dict->words->data[
      dict->words->offsets_by_length[len] + (uint32_t)len * word_idx];
  *size = (size_t)BrotliTransformDictionaryWord(buffer, dict_word, len,
      transforms, transform);
}

static DictWord MakeDictWord(uint8_t len, uint8_t transform, uint16_t idx) {
  DictWord result;
  result.len = len;
  result.transform = transform;
  result.idx = idx;
  return result;
}

static uint32_t BrotliTrieAlloc(MemoryManager* m, size_t num, BrotliTrie* trie,
                                BrotliTrieNode** keep) {
  uint32_t result;
  uint32_t keep_index = 0;
  if (keep && *keep != &trie->root) {
    /* Optional node to keep, since address may change after re-allocating */
    keep_index = (uint32_t)(*keep - trie->pool);
  }
  if (trie->pool_size == 0) {
    /* Have a dummy node in the front. We do not want the result to be 0, it
    must be at least 1, 0 represents "null pointer" */
    trie->pool_size = 1;
  }
  BROTLI_ENSURE_CAPACITY(m, BrotliTrieNode, trie->pool, trie->pool_capacity,
                         trie->pool_size + num);
  if (BROTLI_IS_OOM(m)) return 0;
  /* Init the new nodes to empty */
  memset(trie->pool + trie->pool_size, 0, sizeof(*trie->pool) * num);
  result = (uint32_t)trie->pool_size;
  trie->pool_size += num;
  if (keep && *keep != &trie->root) {
    *keep = trie->pool + keep_index;
  }
  return result;
}

/**
 * len and idx: payload for last node
 * word, size: the string
 * index: position in the string
 */
static BROTLI_BOOL BrotliTrieNodeAdd(MemoryManager* m, uint8_t len,
    uint32_t idx, const uint8_t* word, size_t size, int index,
    BrotliTrieNode* node, BrotliTrie* trie) {
  BrotliTrieNode* child = 0;
  uint8_t c;
  if ((size_t)index == size) {
    if (!node->len_ || idx < node->idx_) {
      node->len_ = len;
      node->idx_ = idx;
    }
    return BROTLI_TRUE;
  }
  c = word[index];
  if (node->single && c != node->c) {
    BrotliTrieNode old = trie->pool[node->sub];
    uint32_t new_nodes = BrotliTrieAlloc(m, 32, trie, &node);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    node->single = 0;
    node->sub = new_nodes;
    trie->pool[node->sub + (node->c >> 4)].sub = new_nodes + 16;
    trie->pool[trie->pool[node->sub + (node->c >> 4)].sub + (node->c & 15)] =
        old;
  }
  if (!node->sub) {
    uint32_t new_node = BrotliTrieAlloc(m, 1, trie, &node);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    node->single = 1;
    node->c = c;
    node->sub = new_node;
  }
  if (node->single) {
    child = &trie->pool[node->sub];
  } else {
    if (!trie->pool[node->sub + (c >> 4)].sub) {
      uint32_t new_nodes = BrotliTrieAlloc(m, 16, trie, &node);
      if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
      trie->pool[node->sub + (c >> 4)].sub = new_nodes;
    }
    child = &trie->pool[trie->pool[node->sub + (c >> 4)].sub + (c & 15)];
  }
  return BrotliTrieNodeAdd(m, len, idx, word, size, index + 1, child, trie);
}

static BROTLI_BOOL BrotliTrieAdd(MemoryManager* m, uint8_t len, uint32_t idx,
                          const uint8_t* word, size_t size, BrotliTrie* trie) {
  return BrotliTrieNodeAdd(m, len, idx, word, size, 0, &trie->root, trie);
}

const BrotliTrieNode* BrotliTrieSub(const BrotliTrie* trie,
                                    const BrotliTrieNode* node, uint8_t c) {
  BrotliTrieNode* temp_node;
  if (node->single) {
    if (node->c == c) return &trie->pool[node->sub];
    return 0;
  }
  if (!node->sub) return 0;
  temp_node = &trie->pool[node->sub + (c >> 4)];
  if (!temp_node->sub) return 0;
  return &trie->pool[temp_node->sub + (c & 15)];
}

static const BrotliTrieNode* BrotliTrieFind(const BrotliTrie* trie,
                                            const uint8_t* word, size_t size) {
  const BrotliTrieNode* node = &trie->root;
  size_t i;
  for (i = 0; i < size; i++) {
    node = BrotliTrieSub(trie, node, word[i]);
    if (!node) return 0;
  }
  return node;
}

static BROTLI_BOOL BuildDictionaryLut(MemoryManager* m,
    const BrotliTransforms* transforms,
    BrotliEncoderDictionary* dict) {
  uint32_t i;
  DictWord* dict_words;
  uint16_t* buckets;
  DictWord** words_by_hash;
  size_t* words_by_hash_size;
  size_t* words_by_hash_capacity;
  BrotliTrie dedup;
  uint8_t word[kTransformedBufferSize];
  size_t word_size;
  size_t total = 0;
  uint8_t l;
  uint16_t idx;

  BrotliTrieInit(&dedup);

  words_by_hash = (DictWord**)BrotliAllocate(m,
      sizeof(*words_by_hash) * NUM_HASH_BUCKETS);
  words_by_hash_size = (size_t*)BrotliAllocate(m,
      sizeof(*words_by_hash_size) * NUM_HASH_BUCKETS);
  words_by_hash_capacity = (size_t*)BrotliAllocate(m,
      sizeof(*words_by_hash_capacity) * NUM_HASH_BUCKETS);
  if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
  memset(words_by_hash, 0, sizeof(*words_by_hash) * NUM_HASH_BUCKETS);
  memset(words_by_hash_size, 0, sizeof(*words_by_hash_size) * NUM_HASH_BUCKETS);
  memset(words_by_hash_capacity, 0,
         sizeof(*words_by_hash_capacity) * NUM_HASH_BUCKETS);

  if (transforms->num_transforms > 0) {
    for (l = SHARED_BROTLI_MIN_DICTIONARY_WORD_LENGTH;
        l <= SHARED_BROTLI_MAX_DICTIONARY_WORD_LENGTH; ++l) {
      uint16_t n = dict->words->size_bits_by_length[l] ?
          (uint16_t)(1 << dict->words->size_bits_by_length[l]) : 0u;
      for (idx = 0; idx < n; ++idx) {
        uint32_t key;
        /* First transform (usually identity) */
        TransformedDictionaryWord(idx, l, 0, transforms, dict, word,
                                  &word_size);
        /* Cannot hash words smaller than 4 bytes */
        if (word_size < 4) {
          /* Break instead of continue, all next words of this length will have
             same length after transform */
          break;
        }
        if (!BrotliTrieAdd(m, 0, idx, word, word_size, &dedup)) {
          return BROTLI_FALSE;
        }
        key = Hash(word, NUM_HASH_BITS);
        BROTLI_ENSURE_CAPACITY_APPEND(m, DictWord, words_by_hash[key],
            words_by_hash_capacity[key], words_by_hash_size[key],
            MakeDictWord(l, 0, idx));
        ++total;
      }
    }
  }

  /* These LUT transforms only supported if no custom transforms. This is
     ok, we will use the heavy trie instead. */
  if (transforms == BrotliGetTransforms()) {
    for (l = SHARED_BROTLI_MIN_DICTIONARY_WORD_LENGTH;
        l <= SHARED_BROTLI_MAX_DICTIONARY_WORD_LENGTH; ++l) {
      uint16_t n = dict->words->size_bits_by_length[l] ?
          (uint16_t)(1 << dict->words->size_bits_by_length[l]) : 0u;
      for (idx = 0; idx < n; ++idx) {
        int k;
        BROTLI_BOOL is_ascii = BROTLI_TRUE;
        size_t offset = dict->words->offsets_by_length[l] + (size_t)l * idx;
        const uint8_t* data = &dict->words->data[offset];
        for (k = 0; k < l; ++k) {
          if (data[k] >= 128) is_ascii = BROTLI_FALSE;
        }
        if (data[0] < 128) {
          int transform = 9;  /* {empty, uppercase first, empty} */
          uint32_t ix = idx + (uint32_t)transform * n;
          const BrotliTrieNode* it;
          TransformedDictionaryWord(idx, l, transform, transforms,
                                   dict, word, &word_size);
          it = BrotliTrieFind(&dedup, word, word_size);
          if (!it || it->idx_ > ix) {
            uint32_t key = Hash(word, NUM_HASH_BITS);
            if (!BrotliTrieAdd(m, 0, ix, word, word_size, &dedup)) {
              return BROTLI_FALSE;
            }
            BROTLI_ENSURE_CAPACITY_APPEND(m, DictWord, words_by_hash[key],
                words_by_hash_capacity[key], words_by_hash_size[key],
                MakeDictWord(l, BROTLI_TRANSFORM_UPPERCASE_FIRST, idx));
            ++total;
          }
        }
        if (is_ascii) {
          int transform = 44;  /* {empty, uppercase all, empty} */
          uint32_t ix = idx + (uint32_t)transform * n;
          const BrotliTrieNode* it;
          TransformedDictionaryWord(idx, l, transform, transforms,
                                    dict, word, &word_size);
          it = BrotliTrieFind(&dedup, word, word_size);
          if (!it || it->idx_ > ix) {
            uint32_t key = Hash(word, NUM_HASH_BITS);
            if (!BrotliTrieAdd(m, 0, ix, word, word_size, &dedup)) {
              return BROTLI_FALSE;
            }
            BROTLI_ENSURE_CAPACITY_APPEND(m, DictWord, words_by_hash[key],
                words_by_hash_capacity[key], words_by_hash_size[key],
                MakeDictWord(l, BROTLI_TRANSFORM_UPPERCASE_ALL, idx));
            ++total;
          }
        }
      }
    }
  }

  dict_words = (DictWord*)BrotliAllocate(m,
      sizeof(*dict->dict_words) * (total + 1));
  buckets = (uint16_t*)BrotliAllocate(m,
      sizeof(*dict->buckets) * NUM_HASH_BUCKETS);
  if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
  dict->dict_words_alloc_size_ = total + 1;
  dict->dict_words = dict->dict_words_data_ = dict_words;
  dict->buckets_alloc_size_ = NUM_HASH_BUCKETS;
  dict->buckets = dict->buckets_data_ = buckets;

  /* Unused; makes offsets start from 1. */
  dict_words[0] = MakeDictWord(0, 0, 0);
  total = 1;
  for (i = 0; i < NUM_HASH_BUCKETS; ++i) {
    size_t num_words = words_by_hash_size[i];
    if (num_words > 0) {
      buckets[i] = (uint16_t)(total);
      memcpy(&dict_words[total], &words_by_hash[i][0],
          sizeof(dict_words[0]) * num_words);
      total += num_words;
      dict_words[total - 1].len |= 0x80;
    } else {
      buckets[i] = 0;
    }
  }

  for (i = 0; i < NUM_HASH_BUCKETS; ++i) {
    BrotliFree(m, words_by_hash[i]);
  }
  BrotliFree(m, words_by_hash);
  BrotliFree(m, words_by_hash_size);
  BrotliFree(m, words_by_hash_capacity);
  BrotliTrieFree(m, &dedup);

  return BROTLI_TRUE;
}

static void BuildDictionaryHashTable(uint16_t* hash_table_words,
    uint8_t* hash_table_lengths, const BrotliDictionary* dict) {
  int j, len;
  /* The order of the loops is such that in case of collision, words with
     shorter length are preferred, and in case of same length, words with
     smaller index. There is only a single word per bucket. */
  /* TODO(lode): consider adding optional user-supplied frequency_map to use
     for preferred words instead, this can make the encoder better for
     quality 9 and below without affecting the decoder */
  memset(hash_table_words, 0, sizeof(kStaticDictionaryHashWords));
  memset(hash_table_lengths, 0, sizeof(kStaticDictionaryHashLengths));
  for (len = SHARED_BROTLI_MAX_DICTIONARY_WORD_LENGTH;
      len >= SHARED_BROTLI_MIN_DICTIONARY_WORD_LENGTH; --len) {
    const size_t num_words = dict->size_bits_by_length[len] ?
        (1u << dict->size_bits_by_length[len]) : 0;
    for (j = (int)num_words - 1; j >= 0; --j) {
      size_t offset = dict->offsets_by_length[len] +
          (size_t)len * (size_t)j;
      const uint8_t* word = &dict->data[offset];
      const uint32_t key = Hash(word, 14);
      int idx = (int)(key << 1) + (len < 8 ? 1 : 0);
      BROTLI_DCHECK(idx < (int)NUM_HASH_BUCKETS);
      hash_table_words[idx] = (uint16_t)j;
      hash_table_lengths[idx] = (uint8_t)len;
    }
  }
}

static BROTLI_BOOL GenerateWordsHeavy(MemoryManager* m,
    const BrotliTransforms* transforms,
    BrotliEncoderDictionary* dict) {
  int i, j, l;
  for (j = (int)transforms->num_transforms - 1; j >= 0 ; --j) {
    for (l = 0; l < 32; l++) {
      int num = (int)((1u << dict->words->size_bits_by_length[l]) & ~1u);
      for (i = 0; i < num; i++) {
        uint8_t transformed[kTransformedBufferSize];
        size_t size;
        TransformedDictionaryWord(
            (uint32_t)i, l, j, transforms, dict, transformed, &size);
        if (size < 4) continue;
        if (!BrotliTrieAdd(m, (uint8_t)l, (uint32_t)(i + num * j),
            transformed, size, &dict->trie)) {
          return BROTLI_FALSE;
        }
      }
    }
  }
  return BROTLI_TRUE;
}

/* Computes cutoffTransformsCount (in count) and cutoffTransforms (in data) for
   the custom transforms, where possible within the limits of the
   cutoffTransforms encoding. The fast encoder uses this to do fast lookup for
   transforms that remove the N last characters (OmitLast). */
static void ComputeCutoffTransforms(
    const BrotliTransforms* transforms,
    uint32_t* count, uint64_t* data) {
  int i;
  /* The encoding in a 64-bit integer of transform N in the data is: (N << 2) +
     ((cutoffTransforms >> (N * 6)) & 0x3F), so for example the identity
     transform code must be 0-63, for N=1 the transform code must be 4-67, ...,
     for N=9 it must be 36-99.
     TODO(lode): consider a simple flexible uint8_t[10] instead of the uint64_t
     for the cutoff transforms, so that shared dictionaries can have the
     OmitLast transforms anywhere without loss. */
  *count = 0;
  *data = 0;
  for (i = 0; i < BROTLI_TRANSFORMS_MAX_CUT_OFF + 1; i++) {
    int idx = transforms->cutOffTransforms[i];
    if (idx == -1) break;  /* Not found */
    if (idx < (i << 2)) break;  /* Too small for the encoding */
    if (idx >= (i << 2) + 64) break;  /* Too large for the encoding */
    (*count)++;
    *data |= (uint64_t)(((uint64_t)idx -
        ((uint64_t)i << 2u)) << ((uint64_t)i * 6u));
  }
}

static BROTLI_BOOL ComputeDictionary(MemoryManager* m, int quality,
    const BrotliTransforms* transforms,
    BrotliEncoderDictionary* current) {
  int default_words = current->words == BrotliGetDictionary();
  int default_transforms = transforms == BrotliGetTransforms();

  if (default_words && default_transforms) {
    /* hashes are already set to Brotli defaults */
    return BROTLI_TRUE;
  }

  current->hash_table_data_words_ = (uint16_t*)BrotliAllocate(
      m, sizeof(kStaticDictionaryHashWords));
  current->hash_table_data_lengths_ = (uint8_t*)BrotliAllocate(
      m, sizeof(kStaticDictionaryHashLengths));
  if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
  current->hash_table_words = current->hash_table_data_words_;
  current->hash_table_lengths = current->hash_table_data_lengths_;

  BuildDictionaryHashTable(current->hash_table_data_words_,
      current->hash_table_data_lengths_, current->words);

  ComputeCutoffTransforms(transforms,
      &current->cutoffTransformsCount, &current->cutoffTransforms);

  /* Only compute the data for slow encoder if the requested quality is high
     enough to need it */
  if (quality >= ZOPFLIFICATION_QUALITY) {
    if (!BuildDictionaryLut(m, transforms, current)) return BROTLI_FALSE;

    /* For the built-in Brotli transforms, there is a hard-coded function to
       handle all transforms, but for custom transforms, we use the following
       large hammer instead */
    current->has_words_heavy = !default_transforms;
    if (current->has_words_heavy) {
      if (!GenerateWordsHeavy(m, transforms, current)) return BROTLI_FALSE;
    }
  }

  return BROTLI_TRUE;
}
#endif  /* BROTLI_EXPERIMENTAL */

void BrotliInitSharedEncoderDictionary(SharedEncoderDictionary* dict) {
  dict->magic = kSharedDictionaryMagic;

  dict->compound.num_chunks = 0;
  dict->compound.total_size = 0;
  dict->compound.chunk_offsets[0] = 0;
  dict->compound.num_prepared_instances_ = 0;

  dict->contextual.context_based = 0;
  dict->contextual.num_dictionaries = 1;
  dict->contextual.instances_ = 0;
  dict->contextual.num_instances_ = 1;  /* The instance_ field */
  dict->contextual.dict[0] = &dict->contextual.instance_;
  InitEncoderDictionary(&dict->contextual.instance_);
  dict->contextual.instance_.parent = &dict->contextual;

  dict->max_quality = BROTLI_MAX_QUALITY;
}

#if defined(BROTLI_EXPERIMENTAL)
/* TODO(eustas): make sure that tooling will warn user if not all the cutoff
   transforms are available (for low-quality encoder). */
static BROTLI_BOOL InitCustomSharedEncoderDictionary(
    MemoryManager* m, const BrotliSharedDictionary* decoded_dict,
    int quality, SharedEncoderDictionary* dict) {
  ContextualEncoderDictionary* contextual;
  CompoundDictionary* compound;
  BrotliEncoderDictionary* instances;
  int i;
  BrotliInitSharedEncoderDictionary(dict);

  contextual = &dict->contextual;
  compound = &dict->compound;

  for (i = 0; i < (int)decoded_dict->num_prefix; i++) {
    PreparedDictionary* prepared = CreatePreparedDictionary(m,
        decoded_dict->prefix[i], decoded_dict->prefix_size[i]);
    AttachPreparedDictionary(compound, prepared);
    /* remember for cleanup */
    compound->prepared_instances_[
        compound->num_prepared_instances_++] = prepared;
  }

  dict->max_quality = quality;
  contextual->context_based = decoded_dict->context_based;
  if (decoded_dict->context_based) {
    memcpy(contextual->context_map, decoded_dict->context_map,
        SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS);
  }

  contextual->num_dictionaries = decoded_dict->num_dictionaries;
  contextual->num_instances_ = decoded_dict->num_dictionaries;
  if (contextual->num_instances_ == 1) {
    instances = &contextual->instance_;
  } else {
    contextual->instances_ = (BrotliEncoderDictionary*)
        BrotliAllocate(m, sizeof(*contextual->instances_) *
        contextual->num_instances_);
    if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
    instances = contextual->instances_;
  }
  for (i = 0; i < (int)contextual->num_instances_; i++) {
    BrotliEncoderDictionary* current = &instances[i];
    InitEncoderDictionary(current);
    current->parent = &dict->contextual;
    if (decoded_dict->words[i] == BrotliGetDictionary()) {
      current->words = BrotliGetDictionary();
    } else {
      current->words_instance_ = (BrotliDictionary*)BrotliAllocate(
          m, sizeof(BrotliDictionary));
      if (BROTLI_IS_OOM(m)) return BROTLI_FALSE;
      *current->words_instance_ = *decoded_dict->words[i];
      current->words = current->words_instance_;
    }
    current->num_transforms =
        (uint32_t)decoded_dict->transforms[i]->num_transforms;
    if (!ComputeDictionary(
        m, quality, decoded_dict->transforms[i], current)) {
      return BROTLI_FALSE;
    }

    contextual->dict[i] = current;
  }

  return BROTLI_TRUE;  /* success */
}

BROTLI_BOOL BrotliInitCustomSharedEncoderDictionary(
    MemoryManager* m, const uint8_t* encoded_dict, size_t size,
    int quality, SharedEncoderDictionary* dict) {
  BROTLI_BOOL success = BROTLI_FALSE;
  BrotliSharedDictionary* decoded_dict = BrotliSharedDictionaryCreateInstance(
      m->alloc_func, m->free_func, m->opaque);
  if (!decoded_dict) {  /* OOM */
    return BROTLI_FALSE;
  }
  success = BrotliSharedDictionaryAttach(
      decoded_dict, BROTLI_SHARED_DICTIONARY_SERIALIZED, size, encoded_dict);
  if (success) {
    success = InitCustomSharedEncoderDictionary(m,
        decoded_dict, quality, dict);
  }
  BrotliSharedDictionaryDestroyInstance(decoded_dict);
  return success;
}
#endif  /* BROTLI_EXPERIMENTAL */

void BrotliCleanupSharedEncoderDictionary(MemoryManager* m,
                                          SharedEncoderDictionary* dict) {
  size_t i;
  for (i = 0; i < dict->compound.num_prepared_instances_; i++) {
    DestroyPreparedDictionary(m,
        (PreparedDictionary*)dict->compound.prepared_instances_[i]);
  }
  if (dict->contextual.num_instances_ == 1) {
    BrotliDestroyEncoderDictionary(m, &dict->contextual.instance_);
  } else if (dict->contextual.num_instances_ > 1) {
    for (i = 0; i < dict->contextual.num_instances_; i++) {
      BrotliDestroyEncoderDictionary(m, &dict->contextual.instances_[i]);
    }
    BrotliFree(m, dict->contextual.instances_);
  }
}

ManagedDictionary* BrotliCreateManagedDictionary(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  ManagedDictionary* result = (ManagedDictionary*)BrotliBootstrapAlloc(
      sizeof(ManagedDictionary), alloc_func, free_func, opaque);
  if (result == NULL) return NULL;

  result->magic = kManagedDictionaryMagic;
  BrotliInitMemoryManager(
      &result->memory_manager_, alloc_func, free_func, opaque);
  result->dictionary = NULL;

  return result;
}

void BrotliDestroyManagedDictionary(ManagedDictionary* dictionary) {
  if (!dictionary) return;
  BrotliBootstrapFree(dictionary, &dictionary->memory_manager_);
}

/* Escalate internal functions visibility; for testing purposes only. */
#if defined(BROTLI_TEST)
void InitEncoderDictionaryForTest(BrotliEncoderDictionary*);
void InitEncoderDictionaryForTest(BrotliEncoderDictionary* d) {
  InitEncoderDictionary(d);
}
#endif

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
