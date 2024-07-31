/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#ifndef BROTLI_ENC_PREPARED_DICTIONARY_H_
#define BROTLI_ENC_PREPARED_DICTIONARY_H_

#include <brotli/shared_dictionary.h>
#include <brotli/types.h>

#include "../common/platform.h"
#include "../common/constants.h"
#include "memory.h"

/* "Fat" prepared dictionary, could be cooked outside of C implementation,
 * e.g. on Java side. LZ77 data is copied inside PreparedDictionary struct. */
static const uint32_t kPreparedDictionaryMagic = 0xDEBCEDE0;

static const uint32_t kSharedDictionaryMagic = 0xDEBCEDE1;

static const uint32_t kManagedDictionaryMagic = 0xDEBCEDE2;

/* "Lean" prepared dictionary. LZ77 data is referenced. It is the responsibility
 * of caller of "prepare dictionary" to keep the LZ77 data while prepared
 * dictionary is in use. */
static const uint32_t kLeanPreparedDictionaryMagic = 0xDEBCEDE3;

static const uint64_t kPreparedDictionaryHashMul64Long =
    BROTLI_MAKE_UINT64_T(0x1FE35A7Bu, 0xD3579BD3u);

typedef struct PreparedDictionary {
  uint32_t magic;
  uint32_t num_items;
  uint32_t source_size;
  uint32_t hash_bits;
  uint32_t bucket_bits;
  uint32_t slot_bits;

  /* --- Dynamic size members --- */

  /* uint32_t slot_offsets[1 << slot_bits]; */
  /* uint16_t heads[1 << bucket_bits]; */
  /* uint32_t items[variable]; */

  /* [maybe] uint8_t* source_ref, depending on magic. */
  /* [maybe] uint8_t source[source_size], depending on magic. */
} PreparedDictionary;

BROTLI_INTERNAL PreparedDictionary* CreatePreparedDictionary(MemoryManager* m,
    const uint8_t* source, size_t source_size);

BROTLI_INTERNAL void DestroyPreparedDictionary(MemoryManager* m,
    PreparedDictionary* dictionary);

typedef struct CompoundDictionary {
  /* LZ77 prefix, compound dictionary */
  size_t num_chunks;
  size_t total_size;
  /* Client instances. */
  const PreparedDictionary* chunks[SHARED_BROTLI_MAX_COMPOUND_DICTS + 1];
  const uint8_t* chunk_source[SHARED_BROTLI_MAX_COMPOUND_DICTS + 1];
  size_t chunk_offsets[SHARED_BROTLI_MAX_COMPOUND_DICTS + 1];

  size_t num_prepared_instances_;
  /* Owned instances. */
  PreparedDictionary* prepared_instances_[SHARED_BROTLI_MAX_COMPOUND_DICTS + 1];
} CompoundDictionary;

BROTLI_INTERNAL BROTLI_BOOL AttachPreparedDictionary(
    CompoundDictionary* compound, const PreparedDictionary* dictionary);

#endif /* BROTLI_ENC_PREPARED_DICTIONARY */
