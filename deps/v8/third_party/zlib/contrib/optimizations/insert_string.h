/* insert_string.h
 *
 * Copyright 2019 The Chromium Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 */

#ifndef INSERT_STRING_H
#define INSERT_STRING_H

#ifndef INLINE
#if defined(_MSC_VER) && !defined(__clang__)
#define INLINE __inline
#else
#define INLINE inline
#endif
#endif

#include <stdint.h>

/**
 * Some applications need to match zlib DEFLATE output exactly [3]. Use the
 * canonical zlib Rabin-Karp rolling hash [1,2] in that case.
 *
 *  [1] For a description of the Rabin and Karp algorithm, see "Algorithms"
 *      book by R. Sedgewick, Addison-Wesley, p252.
 *  [2] https://www.euccas.me/zlib/#zlib_rabin_karp and also "rolling hash"
 *      https://en.wikipedia.org/wiki/Rolling_hash
 *  [3] crbug.com/1316541 AOSP incremental client APK package OTA upgrades.
 */
#ifdef CHROMIUM_ZLIB_NO_CASTAGNOLI
#define USE_ZLIB_RABIN_KARP_ROLLING_HASH
#endif

/* ===========================================================================
 * Update a hash value with the given input byte (Rabin-Karp rolling hash).
 * IN  assertion: all calls to UPDATE_HASH are made with consecutive input
 *    characters, so that a running hash key can be computed from the previous
 *    key instead of complete recalculation each time.
 */
#define UPDATE_HASH(s, h, c) (h = (((h) << s->hash_shift) ^ (c)) & s->hash_mask)

/* ===========================================================================
 * Insert string str in the dictionary and set match_head to the previous head
 * of the hash chain (the most recent string with same hash key). Return
 * the previous length of the hash chain.
 * If this file is compiled with -DFASTEST, the compression level is forced
 * to 1, and no hash chains are maintained.
 * IN  assertion: all calls to INSERT_STRING are made with consecutive input
 *    characters and the first MIN_MATCH bytes of str are valid (except for
 *    the last MIN_MATCH-1 bytes of the input file).
 */
local INLINE Pos insert_string(deflate_state* const s, const Pos str) {
  Pos ret;
/* insert_string dictionary insertion: ANZAC++ hasher
 * significantly improves data compression speed.
 *
 * Note: the generated compressed output is a valid DEFLATE stream, but will
 * differ from canonical zlib output.
 */
#if defined(USE_ZLIB_RABIN_KARP_ROLLING_HASH)
  UPDATE_HASH(s, s->ins_h, s->window[(str) + (MIN_MATCH - 1)]);
#else
  uint32_t value;
  // Validated for little endian archs (i.e. x86, Arm). YMMV for big endian.
  zmemcpy(&value, &s->window[str], sizeof(value));
  s->ins_h = ((value * 66521 + 66521) >> 16) & s->hash_mask;
#endif

#ifdef FASTEST
  ret = s->head[s->ins_h];
#else
  ret = s->prev[str & s->w_mask] = s->head[s->ins_h];
#endif
  s->head[s->ins_h] = str;

  return ret;
}

#endif /* INSERT_STRING_H */
