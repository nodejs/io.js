/* Copyright 2017 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Shared Dictionary definition and utilities. */

#include <brotli/shared_dictionary.h>

#include <memory.h>
#include <stdlib.h>  /* malloc, free */
#include <stdio.h>

#include "dictionary.h"
#include "platform.h"
#include "shared_dictionary_internal.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#if defined(BROTLI_EXPERIMENTAL)

#define BROTLI_NUM_ENCODED_LENGTHS (SHARED_BROTLI_MAX_DICTIONARY_WORD_LENGTH \
    - SHARED_BROTLI_MIN_DICTIONARY_WORD_LENGTH + 1)

/* Max allowed by spec */
#define BROTLI_MAX_SIZE_BITS 15u

/* Returns BROTLI_TRUE on success, BROTLI_FALSE on failure. */
static BROTLI_BOOL ReadBool(const uint8_t* encoded, size_t size, size_t* pos,
    BROTLI_BOOL* result) {
  uint8_t value;
  size_t position = *pos;
  if (position >= size) return BROTLI_FALSE;  /* past file end */
  value = encoded[position++];
  if (value > 1) return BROTLI_FALSE;  /* invalid bool */
  *result = TO_BROTLI_BOOL(value);
  *pos = position;
  return BROTLI_TRUE;  /* success */
}

/* Returns BROTLI_TRUE on success, BROTLI_FALSE on failure. */
static BROTLI_BOOL ReadUint8(const uint8_t* encoded, size_t size, size_t* pos,
    uint8_t* result) {
  size_t position = *pos;
  if (position + sizeof(uint8_t) > size) return BROTLI_FALSE;
  *result = encoded[position++];
  *pos = position;
  return BROTLI_TRUE;
}

/* Returns BROTLI_TRUE on success, BROTLI_FALSE on failure. */
static BROTLI_BOOL ReadUint16(const uint8_t* encoded, size_t size, size_t* pos,
    uint16_t* result) {
  size_t position = *pos;
  if (position + sizeof(uint16_t) > size) return BROTLI_FALSE;
  *result = BROTLI_UNALIGNED_LOAD16LE(&encoded[position]);
  position += 2;
  *pos = position;
  return BROTLI_TRUE;
}

/* Reads a varint into a uint32_t, and returns error if it's too large */
/* Returns BROTLI_TRUE on success, BROTLI_FALSE on failure. */
static BROTLI_BOOL ReadVarint32(const uint8_t* encoded, size_t size,
    size_t* pos, uint32_t* result) {
  int num = 0;
  uint8_t byte;
  *result = 0;
  for (;;) {
    if (*pos >= size) return BROTLI_FALSE;
    byte = encoded[(*pos)++];
    if (num == 4 && byte > 15) return BROTLI_FALSE;
    *result |= (uint32_t)(byte & 127) << (num * 7);
    if (byte < 128) return BROTLI_TRUE;
    num++;
  }
}

/* Returns the total length of word list. */
static size_t BrotliSizeBitsToOffsets(const uint8_t* size_bits_by_length,
    uint32_t* offsets_by_length) {
  uint32_t pos = 0;
  uint32_t i;
  for (i = 0; i <= SHARED_BROTLI_MAX_DICTIONARY_WORD_LENGTH; i++) {
    offsets_by_length[i] = pos;
    if (size_bits_by_length[i] != 0) {
      pos += i << size_bits_by_length[i];
    }
  }
  return pos;
}

static BROTLI_BOOL ParseWordList(size_t size, const uint8_t* encoded,
    size_t* pos, BrotliDictionary* out) {
  size_t offset;
  size_t i;
  size_t position = *pos;
  if (position + BROTLI_NUM_ENCODED_LENGTHS > size) {
    return BROTLI_FALSE;
  }

  memset(out->size_bits_by_length, 0, SHARED_BROTLI_MIN_DICTIONARY_WORD_LENGTH);
  memcpy(out->size_bits_by_length + SHARED_BROTLI_MIN_DICTIONARY_WORD_LENGTH,
      &encoded[position], BROTLI_NUM_ENCODED_LENGTHS);
  for (i = SHARED_BROTLI_MIN_DICTIONARY_WORD_LENGTH;
      i <= SHARED_BROTLI_MAX_DICTIONARY_WORD_LENGTH; i++) {
    if (out->size_bits_by_length[i] > BROTLI_MAX_SIZE_BITS) {
      return BROTLI_FALSE;
    }
  }
  position += BROTLI_NUM_ENCODED_LENGTHS;
  offset = BrotliSizeBitsToOffsets(
      out->size_bits_by_length, out->offsets_by_length);

  out->data = &encoded[position];
  out->data_size = offset;
  position += offset;
  if (position > size) return BROTLI_FALSE;
  *pos = position;
  return BROTLI_TRUE;
}

/* Computes the cutOffTransforms of a BrotliTransforms which already has the
   transforms data correctly filled in. */
static void ComputeCutoffTransforms(BrotliTransforms* transforms) {
  uint32_t i;
  for (i = 0; i < BROTLI_TRANSFORMS_MAX_CUT_OFF + 1; i++) {
    transforms->cutOffTransforms[i] = -1;
  }
  for (i = 0; i < transforms->num_transforms; i++) {
    const uint8_t* prefix = BROTLI_TRANSFORM_PREFIX(transforms, i);
    uint8_t type = BROTLI_TRANSFORM_TYPE(transforms, i);
    const uint8_t* suffix = BROTLI_TRANSFORM_SUFFIX(transforms, i);
    if (type <= BROTLI_TRANSFORM_OMIT_LAST_9 && *prefix == 0 && *suffix == 0 &&
        transforms->cutOffTransforms[type] == -1) {
      transforms->cutOffTransforms[type] = (int16_t)i;
    }
  }
}

static BROTLI_BOOL ParsePrefixSuffixTable(size_t size, const uint8_t* encoded,
    size_t* pos, BrotliTransforms* out, uint16_t* out_table,
    size_t* out_table_size) {
  size_t position = *pos;
  size_t offset = 0;
  size_t stringlet_count = 0;  /* NUM_PREFIX_SUFFIX */
  size_t data_length = 0;

  /* PREFIX_SUFFIX_LENGTH */
  if (!ReadUint16(encoded, size, &position, &out->prefix_suffix_size)) {
    return BROTLI_FALSE;
  }
  data_length = out->prefix_suffix_size;

  /* Must at least have space for null terminator. */
  if (data_length < 1) return BROTLI_FALSE;
  out->prefix_suffix = &encoded[position];
  if (position + data_length >= size) return BROTLI_FALSE;
  while (BROTLI_TRUE) {
    /* STRING_LENGTH */
    size_t stringlet_len = encoded[position + offset];
    out_table[stringlet_count] = (uint16_t)offset;
    stringlet_count++;
    offset++;
    if (stringlet_len == 0) {
      if (offset == data_length) {
        break;
      } else {
        return BROTLI_FALSE;
      }
    }
    if (stringlet_count > 255) return BROTLI_FALSE;
    offset += stringlet_len;
    if (offset >= data_length) return BROTLI_FALSE;
  }

  position += data_length;
  *pos = position;
  *out_table_size = (uint16_t)stringlet_count;
  return BROTLI_TRUE;
}

static BROTLI_BOOL ParseTransformsList(size_t size, const uint8_t* encoded,
    size_t* pos, BrotliTransforms* out, uint16_t* prefix_suffix_table,
    size_t* prefix_suffix_count) {
  uint32_t i;
  BROTLI_BOOL has_params = BROTLI_FALSE;
  BROTLI_BOOL prefix_suffix_ok = BROTLI_FALSE;
  size_t position = *pos;
  size_t stringlet_cnt = 0;
  if (position >= size) return BROTLI_FALSE;

  prefix_suffix_ok = ParsePrefixSuffixTable(
      size, encoded, &position, out, prefix_suffix_table, &stringlet_cnt);
  if (!prefix_suffix_ok) return BROTLI_FALSE;
  out->prefix_suffix_map = prefix_suffix_table;
  *prefix_suffix_count = stringlet_cnt;

  out->num_transforms = encoded[position++];
  out->transforms = &encoded[position];
  position += (size_t)out->num_transforms * 3;
  if (position > size) return BROTLI_FALSE;
  /* Check for errors and read extra parameters. */
  for (i = 0; i < out->num_transforms; i++) {
    uint8_t prefix_id = BROTLI_TRANSFORM_PREFIX_ID(out, i);
    uint8_t type = BROTLI_TRANSFORM_TYPE(out, i);
    uint8_t suffix_id = BROTLI_TRANSFORM_SUFFIX_ID(out, i);
    if (prefix_id >= stringlet_cnt) return BROTLI_FALSE;
    if (type >= BROTLI_NUM_TRANSFORM_TYPES) return BROTLI_FALSE;
    if (suffix_id >= stringlet_cnt) return BROTLI_FALSE;
    if (type == BROTLI_TRANSFORM_SHIFT_FIRST ||
        type == BROTLI_TRANSFORM_SHIFT_ALL) {
      has_params = BROTLI_TRUE;
    }
  }
  if (has_params) {
    out->params = &encoded[position];
    position += (size_t)out->num_transforms * 2;
    if (position > size) return BROTLI_FALSE;
    for (i = 0; i < out->num_transforms; i++) {
      uint8_t type = BROTLI_TRANSFORM_TYPE(out, i);
      if (type != BROTLI_TRANSFORM_SHIFT_FIRST &&
          type != BROTLI_TRANSFORM_SHIFT_ALL) {
        if (out->params[i * 2] != 0 || out->params[i * 2 + 1] != 0) {
          return BROTLI_FALSE;
        }
      }
    }
  } else {
    out->params = NULL;
  }
  ComputeCutoffTransforms(out);
  *pos = position;
  return BROTLI_TRUE;
}

static BROTLI_BOOL DryParseDictionary(const uint8_t* encoded,
    size_t size, uint32_t* num_prefix, BROTLI_BOOL* is_custom_static_dict) {
  size_t pos = 0;
  uint32_t chunk_size = 0;
  uint8_t num_word_lists;
  uint8_t num_transform_lists;
  *is_custom_static_dict = BROTLI_FALSE;
  *num_prefix = 0;

  /* Skip magic header bytes. */
  pos += 2;

  /* LZ77_DICTIONARY_LENGTH */
  if (!ReadVarint32(encoded, size, &pos, &chunk_size)) return BROTLI_FALSE;
  if (chunk_size != 0) {
    /* This limitation is not specified but the 32-bit Brotli decoder for now */
    if (chunk_size > 1073741823) return BROTLI_FALSE;
    *num_prefix = 1;
    if (pos + chunk_size > size) return BROTLI_FALSE;
    pos += chunk_size;
  }

  if (!ReadUint8(encoded, size, &pos, &num_word_lists)) {
    return BROTLI_FALSE;
  }
  if (!ReadUint8(encoded, size, &pos, &num_transform_lists)) {
    return BROTLI_FALSE;
  }

  if (num_word_lists > 0 || num_transform_lists > 0) {
    *is_custom_static_dict = BROTLI_TRUE;
  }

  return BROTLI_TRUE;
}

static BROTLI_BOOL ParseDictionary(const uint8_t* encoded, size_t size,
    BrotliSharedDictionary* dict) {
  uint32_t i;
  size_t pos = 0;
  uint32_t chunk_size = 0;
  size_t total_prefix_suffix_count = 0;
  size_t trasform_list_start[SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS];
  uint16_t temporary_prefix_suffix_table[256];

  /* Skip magic header bytes. */
  pos += 2;

  /* LZ77_DICTIONARY_LENGTH */
  if (!ReadVarint32(encoded, size, &pos, &chunk_size)) return BROTLI_FALSE;
  if (chunk_size != 0) {
    if (pos + chunk_size > size) return BROTLI_FALSE;
    dict->prefix_size[dict->num_prefix] = chunk_size;
    dict->prefix[dict->num_prefix] = &encoded[pos];
    dict->num_prefix++;
    /* LZ77_DICTIONARY_LENGTH bytes. */
    pos += chunk_size;
  }

  /* NUM_WORD_LISTS */
  if (!ReadUint8(encoded, size, &pos, &dict->num_word_lists)) {
    return BROTLI_FALSE;
  }
  if (dict->num_word_lists > SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS) {
    return BROTLI_FALSE;
  }

  if (dict->num_word_lists != 0) {
    dict->words_instances = (BrotliDictionary*)dict->alloc_func(
        dict->memory_manager_opaque,
        dict->num_word_lists * sizeof(*dict->words_instances));
    if (!dict->words_instances) return BROTLI_FALSE;  /* OOM */
  }
  for (i = 0; i < dict->num_word_lists; i++) {
    if (!ParseWordList(size, encoded, &pos, &dict->words_instances[i])) {
      return BROTLI_FALSE;
    }
  }

  /* NUM_TRANSFORM_LISTS */
  if (!ReadUint8(encoded, size, &pos, &dict->num_transform_lists)) {
    return BROTLI_FALSE;
  }
  if (dict->num_transform_lists > SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS) {
    return BROTLI_FALSE;
  }

  if (dict->num_transform_lists != 0) {
    dict->transforms_instances = (BrotliTransforms*)dict->alloc_func(
        dict->memory_manager_opaque,
        dict->num_transform_lists * sizeof(*dict->transforms_instances));
    if (!dict->transforms_instances) return BROTLI_FALSE;  /* OOM */
  }
  for (i = 0; i < dict->num_transform_lists; i++) {
    BROTLI_BOOL ok = BROTLI_FALSE;
    size_t prefix_suffix_count = 0;
    trasform_list_start[i] = pos;
    dict->transforms_instances[i].prefix_suffix_map =
        temporary_prefix_suffix_table;
    ok = ParseTransformsList(
        size, encoded, &pos, &dict->transforms_instances[i],
        temporary_prefix_suffix_table, &prefix_suffix_count);
    if (!ok) return BROTLI_FALSE;
    total_prefix_suffix_count += prefix_suffix_count;
  }
  if (total_prefix_suffix_count != 0) {
    dict->prefix_suffix_maps = (uint16_t*)dict->alloc_func(
        dict->memory_manager_opaque,
        total_prefix_suffix_count * sizeof(*dict->prefix_suffix_maps));
    if (!dict->prefix_suffix_maps) return BROTLI_FALSE;  /* OOM */
  }
  total_prefix_suffix_count = 0;
  for (i = 0; i < dict->num_transform_lists; i++) {
    size_t prefix_suffix_count = 0;
    size_t position = trasform_list_start[i];
    uint16_t* prefix_suffix_map =
      &dict->prefix_suffix_maps[total_prefix_suffix_count];
    BROTLI_BOOL ok = ParsePrefixSuffixTable(
        size, encoded, &position, &dict->transforms_instances[i],
        prefix_suffix_map, &prefix_suffix_count);
    if (!ok) return BROTLI_FALSE;
    dict->transforms_instances[i].prefix_suffix_map = prefix_suffix_map;
    total_prefix_suffix_count += prefix_suffix_count;
  }

  if (dict->num_word_lists != 0 || dict->num_transform_lists != 0) {
    if (!ReadUint8(encoded, size, &pos, &dict->num_dictionaries)) {
      return BROTLI_FALSE;
    }
    if (dict->num_dictionaries == 0 ||
        dict->num_dictionaries > SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS) {
      return BROTLI_FALSE;
    }
    for (i = 0; i < dict->num_dictionaries; i++) {
      uint8_t words_index;
      uint8_t transforms_index;
      if (!ReadUint8(encoded, size, &pos, &words_index)) {
        return BROTLI_FALSE;
      }
      if (words_index > dict->num_word_lists) return BROTLI_FALSE;
      if (!ReadUint8(encoded, size, &pos, &transforms_index)) {
        return BROTLI_FALSE;
      }
      if (transforms_index > dict->num_transform_lists) return BROTLI_FALSE;
      dict->words[i] = words_index == dict->num_word_lists ?
          BrotliGetDictionary() : &dict->words_instances[words_index];
      dict->transforms[i] = transforms_index == dict->num_transform_lists ?
          BrotliGetTransforms(): &dict->transforms_instances[transforms_index];
    }
    /* CONTEXT_ENABLED */
    if (!ReadBool(encoded, size, &pos, &dict->context_based)) {
      return BROTLI_FALSE;
    }

    /* CONTEXT_MAP */
    if (dict->context_based) {
      for (i = 0; i < SHARED_BROTLI_NUM_DICTIONARY_CONTEXTS; i++) {
        if (!ReadUint8(encoded, size, &pos, &dict->context_map[i])) {
          return BROTLI_FALSE;
        }
        if (dict->context_map[i] >= dict->num_dictionaries) {
          return BROTLI_FALSE;
        }
      }
    }
  } else {
    dict->context_based = BROTLI_FALSE;
    dict->num_dictionaries = 1;
    dict->words[0] = BrotliGetDictionary();
    dict->transforms[0] = BrotliGetTransforms();
  }

  return BROTLI_TRUE;
}

/* Decodes shared dictionary and verifies correctness.
   Returns BROTLI_TRUE if dictionary is valid, BROTLI_FALSE otherwise.
   The BrotliSharedDictionary must already have been initialized. If the
   BrotliSharedDictionary already contains data, compound dictionaries
   will be appended, but an error will be returned if it already has
   custom words or transforms.
   TODO(lode): link to RFC for shared brotli once published. */
static BROTLI_BOOL DecodeSharedDictionary(
    const uint8_t* encoded, size_t size, BrotliSharedDictionary* dict) {
  uint32_t num_prefix = 0;
  BROTLI_BOOL is_custom_static_dict = BROTLI_FALSE;
  BROTLI_BOOL has_custom_static_dict =
      dict->num_word_lists > 0 || dict->num_transform_lists > 0;

  /* Check magic header bytes. */
  if (size < 2) return BROTLI_FALSE;
  if (encoded[0] != 0x91 || encoded[1] != 0) return BROTLI_FALSE;

  if (!DryParseDictionary(encoded, size, &num_prefix, &is_custom_static_dict)) {
    return BROTLI_FALSE;
  }

  if (num_prefix + dict->num_prefix > SHARED_BROTLI_MAX_COMPOUND_DICTS) {
    return BROTLI_FALSE;
  }

  /* Cannot combine different static dictionaries, only prefix dictionaries */
  if (has_custom_static_dict && is_custom_static_dict) return BROTLI_FALSE;

  return ParseDictionary(encoded, size, dict);
}

#endif  /* BROTLI_EXPERIMENTAL */

void BrotliSharedDictionaryDestroyInstance(
    BrotliSharedDictionary* dict) {
  if (!dict) {
    return;
  } else {
    brotli_free_func free_func = dict->free_func;
    void* opaque = dict->memory_manager_opaque;
    /* Cleanup. */
    free_func(opaque, dict->words_instances);
    free_func(opaque, dict->transforms_instances);
    free_func(opaque, dict->prefix_suffix_maps);
    /* Self-destruction. */
    free_func(opaque, dict);
  }
}

BROTLI_BOOL BrotliSharedDictionaryAttach(
    BrotliSharedDictionary* dict, BrotliSharedDictionaryType type,
    size_t data_size, const uint8_t data[BROTLI_ARRAY_PARAM(data_size)]) {
  if (!dict) {
    return BROTLI_FALSE;
  }
#if defined(BROTLI_EXPERIMENTAL)
  if (type == BROTLI_SHARED_DICTIONARY_SERIALIZED) {
    return DecodeSharedDictionary(data, data_size, dict);
  }
#endif  /* BROTLI_EXPERIMENTAL */
  if (type == BROTLI_SHARED_DICTIONARY_RAW) {
    if (dict->num_prefix >= SHARED_BROTLI_MAX_COMPOUND_DICTS) {
      return BROTLI_FALSE;
    }
    dict->prefix_size[dict->num_prefix] = data_size;
    dict->prefix[dict->num_prefix] = data;
    dict->num_prefix++;
    return BROTLI_TRUE;
  }
  return BROTLI_FALSE;
}

BrotliSharedDictionary* BrotliSharedDictionaryCreateInstance(
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  BrotliSharedDictionary* dict = 0;
  if (!alloc_func && !free_func) {
    dict = (BrotliSharedDictionary*)malloc(sizeof(BrotliSharedDictionary));
  } else if (alloc_func && free_func) {
    dict = (BrotliSharedDictionary*)alloc_func(
        opaque, sizeof(BrotliSharedDictionary));
  }
  if (dict == 0) {
    return 0;
  }

  /* TODO(eustas): explicitly initialize all the fields? */
  memset(dict, 0, sizeof(BrotliSharedDictionary));

  dict->context_based = BROTLI_FALSE;
  dict->num_dictionaries = 1;
  dict->num_word_lists = 0;
  dict->num_transform_lists = 0;

  dict->words[0] = BrotliGetDictionary();
  dict->transforms[0] = BrotliGetTransforms();

  dict->alloc_func = alloc_func ? alloc_func : BrotliDefaultAllocFunc;
  dict->free_func = free_func ? free_func : BrotliDefaultFreeFunc;
  dict->memory_manager_opaque = opaque;

  return dict;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
