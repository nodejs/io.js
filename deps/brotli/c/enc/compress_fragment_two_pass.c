/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Function for fast encoding of an input fragment, independently from the input
   history. This function uses two-pass processing: in the first pass we save
   the found backward matches and literal bytes into a buffer, and in the
   second pass we emit them into the bit stream using prefix codes built based
   on the actual command and literal byte histograms. */

#include "compress_fragment_two_pass.h"

#include <string.h>  /* memcmp, memcpy, memset */

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/platform.h"
#include "bit_cost.h"
#include "brotli_bit_stream.h"
#include "entropy_encode.h"
#include "fast_log.h"
#include "find_match_length.h"
#include "write_bits.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define MAX_DISTANCE (long)BROTLI_MAX_BACKWARD_LIMIT(18)

/* kHashMul32 multiplier has these properties:
   * The multiplier must be odd. Otherwise we may lose the highest bit.
   * No long streaks of ones or zeros.
   * There is no effort to ensure that it is a prime, the oddity is enough
     for this use.
   * The number has been tuned heuristically against compression benchmarks. */
static const uint32_t kHashMul32 = 0x1E35A7BD;

static BROTLI_INLINE uint32_t Hash(const uint8_t* p,
    size_t shift, size_t length) {
  const uint64_t h =
      (BROTLI_UNALIGNED_LOAD64LE(p) << ((8 - length) * 8)) * kHashMul32;
  return (uint32_t)(h >> shift);
}

static BROTLI_INLINE uint32_t HashBytesAtOffset(uint64_t v, size_t offset,
    size_t shift, size_t length) {
  BROTLI_DCHECK(offset <= 8 - length);
  {
    const uint64_t h = ((v >> (8 * offset)) << ((8 - length) * 8)) * kHashMul32;
    return (uint32_t)(h >> shift);
  }
}

static BROTLI_INLINE BROTLI_BOOL IsMatch(const uint8_t* p1, const uint8_t* p2,
    size_t length) {
  if (BrotliUnalignedRead32(p1) == BrotliUnalignedRead32(p2)) {
    if (length == 4) return BROTLI_TRUE;
    return TO_BROTLI_BOOL(p1[4] == p2[4] && p1[5] == p2[5]);
  }
  return BROTLI_FALSE;
}

/* Builds a command and distance prefix code (each 64 symbols) into "depth" and
   "bits" based on "histogram" and stores it into the bit stream. */
static void BuildAndStoreCommandPrefixCode(BrotliTwoPassArena* s,
                                           size_t* storage_ix,
                                           uint8_t* storage) {
  /* Tree size for building a tree over 64 symbols is 2 * 64 + 1. */
  /* TODO(eustas): initialize once. */
  memset(s->tmp_depth, 0, sizeof(s->tmp_depth));
  BrotliCreateHuffmanTree(s->cmd_histo, 64, 15, s->tmp_tree, s->cmd_depth);
  BrotliCreateHuffmanTree(&s->cmd_histo[64], 64, 14, s->tmp_tree,
                          &s->cmd_depth[64]);
  /* We have to jump through a few hoops here in order to compute
     the command bits because the symbols are in a different order than in
     the full alphabet. This looks complicated, but having the symbols
     in this order in the command bits saves a few branches in the Emit*
     functions. */
  memcpy(s->tmp_depth, s->cmd_depth + 24, 24);
  memcpy(s->tmp_depth + 24, s->cmd_depth, 8);
  memcpy(s->tmp_depth + 32, s->cmd_depth + 48, 8);
  memcpy(s->tmp_depth + 40, s->cmd_depth + 8, 8);
  memcpy(s->tmp_depth + 48, s->cmd_depth + 56, 8);
  memcpy(s->tmp_depth + 56, s->cmd_depth + 16, 8);
  BrotliConvertBitDepthsToSymbols(s->tmp_depth, 64, s->tmp_bits);
  memcpy(s->cmd_bits, s->tmp_bits + 24, 16);
  memcpy(s->cmd_bits + 8, s->tmp_bits + 40, 16);
  memcpy(s->cmd_bits + 16, s->tmp_bits + 56, 16);
  memcpy(s->cmd_bits + 24, s->tmp_bits, 48);
  memcpy(s->cmd_bits + 48, s->tmp_bits + 32, 16);
  memcpy(s->cmd_bits + 56, s->tmp_bits + 48, 16);
  BrotliConvertBitDepthsToSymbols(&s->cmd_depth[64], 64, &s->cmd_bits[64]);
  {
    /* Create the bit length array for the full command alphabet. */
    size_t i;
    memset(s->tmp_depth, 0, 64); /* only 64 first values were used */
    memcpy(s->tmp_depth, s->cmd_depth + 24, 8);
    memcpy(s->tmp_depth + 64, s->cmd_depth + 32, 8);
    memcpy(s->tmp_depth + 128, s->cmd_depth + 40, 8);
    memcpy(s->tmp_depth + 192, s->cmd_depth + 48, 8);
    memcpy(s->tmp_depth + 384, s->cmd_depth + 56, 8);
    for (i = 0; i < 8; ++i) {
      s->tmp_depth[128 + 8 * i] = s->cmd_depth[i];
      s->tmp_depth[256 + 8 * i] = s->cmd_depth[8 + i];
      s->tmp_depth[448 + 8 * i] = s->cmd_depth[16 + i];
    }
    BrotliStoreHuffmanTree(s->tmp_depth, BROTLI_NUM_COMMAND_SYMBOLS,
                           s->tmp_tree, storage_ix, storage);
  }
  BrotliStoreHuffmanTree(&s->cmd_depth[64], 64, s->tmp_tree, storage_ix,
                         storage);
}

static BROTLI_INLINE void EmitInsertLen(
    uint32_t insertlen, uint32_t** commands) {
  if (insertlen < 6) {
    **commands = insertlen;
  } else if (insertlen < 130) {
    const uint32_t tail = insertlen - 2;
    const uint32_t nbits = Log2FloorNonZero(tail) - 1u;
    const uint32_t prefix = tail >> nbits;
    const uint32_t inscode = (nbits << 1) + prefix + 2;
    const uint32_t extra = tail - (prefix << nbits);
    **commands = inscode | (extra << 8);
  } else if (insertlen < 2114) {
    const uint32_t tail = insertlen - 66;
    const uint32_t nbits = Log2FloorNonZero(tail);
    const uint32_t code = nbits + 10;
    const uint32_t extra = tail - (1u << nbits);
    **commands = code | (extra << 8);
  } else if (insertlen < 6210) {
    const uint32_t extra = insertlen - 2114;
    **commands = 21 | (extra << 8);
  } else if (insertlen < 22594) {
    const uint32_t extra = insertlen - 6210;
    **commands = 22 | (extra << 8);
  } else {
    const uint32_t extra = insertlen - 22594;
    **commands = 23 | (extra << 8);
  }
  ++(*commands);
}

static BROTLI_INLINE void EmitCopyLen(size_t copylen, uint32_t** commands) {
  if (copylen < 10) {
    **commands = (uint32_t)(copylen + 38);
  } else if (copylen < 134) {
    const size_t tail = copylen - 6;
    const size_t nbits = Log2FloorNonZero(tail) - 1;
    const size_t prefix = tail >> nbits;
    const size_t code = (nbits << 1) + prefix + 44;
    const size_t extra = tail - (prefix << nbits);
    **commands = (uint32_t)(code | (extra << 8));
  } else if (copylen < 2118) {
    const size_t tail = copylen - 70;
    const size_t nbits = Log2FloorNonZero(tail);
    const size_t code = nbits + 52;
    const size_t extra = tail - ((size_t)1 << nbits);
    **commands = (uint32_t)(code | (extra << 8));
  } else {
    const size_t extra = copylen - 2118;
    **commands = (uint32_t)(63 | (extra << 8));
  }
  ++(*commands);
}

static BROTLI_INLINE void EmitCopyLenLastDistance(
    size_t copylen, uint32_t** commands) {
  if (copylen < 12) {
    **commands = (uint32_t)(copylen + 20);
    ++(*commands);
  } else if (copylen < 72) {
    const size_t tail = copylen - 8;
    const size_t nbits = Log2FloorNonZero(tail) - 1;
    const size_t prefix = tail >> nbits;
    const size_t code = (nbits << 1) + prefix + 28;
    const size_t extra = tail - (prefix << nbits);
    **commands = (uint32_t)(code | (extra << 8));
    ++(*commands);
  } else if (copylen < 136) {
    const size_t tail = copylen - 8;
    const size_t code = (tail >> 5) + 54;
    const size_t extra = tail & 31;
    **commands = (uint32_t)(code | (extra << 8));
    ++(*commands);
    **commands = 64;
    ++(*commands);
  } else if (copylen < 2120) {
    const size_t tail = copylen - 72;
    const size_t nbits = Log2FloorNonZero(tail);
    const size_t code = nbits + 52;
    const size_t extra = tail - ((size_t)1 << nbits);
    **commands = (uint32_t)(code | (extra << 8));
    ++(*commands);
    **commands = 64;
    ++(*commands);
  } else {
    const size_t extra = copylen - 2120;
    **commands = (uint32_t)(63 | (extra << 8));
    ++(*commands);
    **commands = 64;
    ++(*commands);
  }
}

static BROTLI_INLINE void EmitDistance(uint32_t distance, uint32_t** commands) {
  uint32_t d = distance + 3;
  uint32_t nbits = Log2FloorNonZero(d) - 1;
  const uint32_t prefix = (d >> nbits) & 1;
  const uint32_t offset = (2 + prefix) << nbits;
  const uint32_t distcode = 2 * (nbits - 1) + prefix + 80;
  uint32_t extra = d - offset;
  **commands = distcode | (extra << 8);
  ++(*commands);
}

/* REQUIRES: len <= 1 << 24. */
static void BrotliStoreMetaBlockHeader(
    size_t len, BROTLI_BOOL is_uncompressed, size_t* storage_ix,
    uint8_t* storage) {
  size_t nibbles = 6;
  /* ISLAST */
  BrotliWriteBits(1, 0, storage_ix, storage);
  if (len <= (1U << 16)) {
    nibbles = 4;
  } else if (len <= (1U << 20)) {
    nibbles = 5;
  }
  BrotliWriteBits(2, nibbles - 4, storage_ix, storage);
  BrotliWriteBits(nibbles * 4, len - 1, storage_ix, storage);
  /* ISUNCOMPRESSED */
  BrotliWriteBits(1, (uint64_t)is_uncompressed, storage_ix, storage);
}

static BROTLI_INLINE void CreateCommands(const uint8_t* input,
    size_t block_size, size_t input_size, const uint8_t* base_ip, int* table,
    size_t table_bits, size_t min_match,
    uint8_t** literals, uint32_t** commands) {
  /* "ip" is the input pointer. */
  const uint8_t* ip = input;
  const size_t shift = 64u - table_bits;
  const uint8_t* ip_end = input + block_size;
  /* "next_emit" is a pointer to the first byte that is not covered by a
     previous copy. Bytes between "next_emit" and the start of the next copy or
     the end of the input will be emitted as literal bytes. */
  const uint8_t* next_emit = input;

  int last_distance = -1;
  const size_t kInputMarginBytes = BROTLI_WINDOW_GAP;

  if (BROTLI_PREDICT_TRUE(block_size >= kInputMarginBytes)) {
    /* For the last block, we need to keep a 16 bytes margin so that we can be
       sure that all distances are at most window size - 16.
       For all other blocks, we only need to keep a margin of 5 bytes so that
       we don't go over the block size with a copy. */
    const size_t len_limit = BROTLI_MIN(size_t, block_size - min_match,
                                        input_size - kInputMarginBytes);
    const uint8_t* ip_limit = input + len_limit;

    uint32_t next_hash;
    for (next_hash = Hash(++ip, shift, min_match); ; ) {
      /* Step 1: Scan forward in the input looking for a 6-byte-long match.
         If we get close to exhausting the input then goto emit_remainder.

         Heuristic match skipping: If 32 bytes are scanned with no matches
         found, start looking only at every other byte. If 32 more bytes are
         scanned, look at every third byte, etc.. When a match is found,
         immediately go back to looking at every byte. This is a small loss
         (~5% performance, ~0.1% density) for compressible data due to more
         bookkeeping, but for non-compressible data (such as JPEG) it's a huge
         win since the compressor quickly "realizes" the data is incompressible
         and doesn't bother looking for matches everywhere.

         The "skip" variable keeps track of how many bytes there are since the
         last match; dividing it by 32 (ie. right-shifting by five) gives the
         number of bytes to move ahead for each iteration. */
      uint32_t skip = 32;

      const uint8_t* next_ip = ip;
      const uint8_t* candidate;

      BROTLI_DCHECK(next_emit < ip);
trawl:
      do {
        uint32_t hash = next_hash;
        uint32_t bytes_between_hash_lookups = skip++ >> 5;
        ip = next_ip;
        BROTLI_DCHECK(hash == Hash(ip, shift, min_match));
        next_ip = ip + bytes_between_hash_lookups;
        if (BROTLI_PREDICT_FALSE(next_ip > ip_limit)) {
          goto emit_remainder;
        }
        next_hash = Hash(next_ip, shift, min_match);
        candidate = ip - last_distance;
        if (IsMatch(ip, candidate, min_match)) {
          if (BROTLI_PREDICT_TRUE(candidate < ip)) {
            table[hash] = (int)(ip - base_ip);
            break;
          }
        }
        candidate = base_ip + table[hash];
        BROTLI_DCHECK(candidate >= base_ip);
        BROTLI_DCHECK(candidate < ip);

        table[hash] = (int)(ip - base_ip);
      } while (BROTLI_PREDICT_TRUE(!IsMatch(ip, candidate, min_match)));

      /* Check copy distance. If candidate is not feasible, continue search.
         Checking is done outside of hot loop to reduce overhead. */
      if (ip - candidate > MAX_DISTANCE) goto trawl;

      /* Step 2: Emit the found match together with the literal bytes from
         "next_emit", and then see if we can find a next match immediately
         afterwards. Repeat until we find no match for the input
         without emitting some literal bytes. */

      {
        /* We have a 6-byte match at ip, and we need to emit bytes in
           [next_emit, ip). */
        const uint8_t* base = ip;
        size_t matched = min_match + FindMatchLengthWithLimit(
            candidate + min_match, ip + min_match,
            (size_t)(ip_end - ip) - min_match);
        int distance = (int)(base - candidate);  /* > 0 */
        int insert = (int)(base - next_emit);
        ip += matched;
        BROTLI_DCHECK(0 == memcmp(base, candidate, matched));
        EmitInsertLen((uint32_t)insert, commands);
        BROTLI_LOG(("[CompressFragment] pos = %d insert = %d copy = %d\n",
                    (int)(next_emit - base_ip), insert, 2));
        memcpy(*literals, next_emit, (size_t)insert);
        *literals += insert;
        if (distance == last_distance) {
          **commands = 64;
          ++(*commands);
        } else {
          EmitDistance((uint32_t)distance, commands);
          last_distance = distance;
        }
        EmitCopyLenLastDistance(matched, commands);
        BROTLI_LOG(("[CompressFragment] pos = %d distance = %d\n"
                    "[CompressFragment] pos = %d insert = %d copy = %d\n"
                    "[CompressFragment] pos = %d distance = %d\n",
                    (int)(base - base_ip), (int)distance,
                    (int)(base - base_ip) + 2, 0, (int)matched - 2,
                    (int)(base - base_ip) + 2, (int)distance));

        next_emit = ip;
        if (BROTLI_PREDICT_FALSE(ip >= ip_limit)) {
          goto emit_remainder;
        }
        {
          /* We could immediately start working at ip now, but to improve
             compression we first update "table" with the hashes of some
             positions within the last copy. */
          uint64_t input_bytes;
          uint32_t cur_hash;
          uint32_t prev_hash;
          if (min_match == 4) {
            input_bytes = BROTLI_UNALIGNED_LOAD64LE(ip - 3);
            cur_hash = HashBytesAtOffset(input_bytes, 3, shift, min_match);
            prev_hash = HashBytesAtOffset(input_bytes, 0, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 3);
            prev_hash = HashBytesAtOffset(input_bytes, 1, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 2);
            prev_hash = HashBytesAtOffset(input_bytes, 0, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 1);
          } else {
            input_bytes = BROTLI_UNALIGNED_LOAD64LE(ip - 5);
            prev_hash = HashBytesAtOffset(input_bytes, 0, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 5);
            prev_hash = HashBytesAtOffset(input_bytes, 1, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 4);
            prev_hash = HashBytesAtOffset(input_bytes, 2, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 3);
            input_bytes = BROTLI_UNALIGNED_LOAD64LE(ip - 2);
            cur_hash = HashBytesAtOffset(input_bytes, 2, shift, min_match);
            prev_hash = HashBytesAtOffset(input_bytes, 0, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 2);
            prev_hash = HashBytesAtOffset(input_bytes, 1, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 1);
          }

          candidate = base_ip + table[cur_hash];
          table[cur_hash] = (int)(ip - base_ip);
        }
      }

      while (ip - candidate <= MAX_DISTANCE &&
          IsMatch(ip, candidate, min_match)) {
        /* We have a 6-byte match at ip, and no need to emit any
           literal bytes prior to ip. */
        const uint8_t* base = ip;
        size_t matched = min_match + FindMatchLengthWithLimit(
            candidate + min_match, ip + min_match,
            (size_t)(ip_end - ip) - min_match);
        ip += matched;
        last_distance = (int)(base - candidate);  /* > 0 */
        BROTLI_DCHECK(0 == memcmp(base, candidate, matched));
        EmitCopyLen(matched, commands);
        EmitDistance((uint32_t)last_distance, commands);
        BROTLI_LOG(("[CompressFragment] pos = %d insert = %d copy = %d\n"
                    "[CompressFragment] pos = %d distance = %d\n",
                    (int)(base - base_ip), 0, (int)matched,
                    (int)(base - base_ip), (int)last_distance));

        next_emit = ip;
        if (BROTLI_PREDICT_FALSE(ip >= ip_limit)) {
          goto emit_remainder;
        }
        {
          /* We could immediately start working at ip now, but to improve
             compression we first update "table" with the hashes of some
             positions within the last copy. */
          uint64_t input_bytes;
          uint32_t cur_hash;
          uint32_t prev_hash;
          if (min_match == 4) {
            input_bytes = BROTLI_UNALIGNED_LOAD64LE(ip - 3);
            cur_hash = HashBytesAtOffset(input_bytes, 3, shift, min_match);
            prev_hash = HashBytesAtOffset(input_bytes, 0, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 3);
            prev_hash = HashBytesAtOffset(input_bytes, 1, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 2);
            prev_hash = HashBytesAtOffset(input_bytes, 2, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 1);
          } else {
            input_bytes = BROTLI_UNALIGNED_LOAD64LE(ip - 5);
            prev_hash = HashBytesAtOffset(input_bytes, 0, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 5);
            prev_hash = HashBytesAtOffset(input_bytes, 1, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 4);
            prev_hash = HashBytesAtOffset(input_bytes, 2, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 3);
            input_bytes = BROTLI_UNALIGNED_LOAD64LE(ip - 2);
            cur_hash = HashBytesAtOffset(input_bytes, 2, shift, min_match);
            prev_hash = HashBytesAtOffset(input_bytes, 0, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 2);
            prev_hash = HashBytesAtOffset(input_bytes, 1, shift, min_match);
            table[prev_hash] = (int)(ip - base_ip - 1);
          }

          candidate = base_ip + table[cur_hash];
          table[cur_hash] = (int)(ip - base_ip);
        }
      }

      next_hash = Hash(++ip, shift, min_match);
    }
  }

emit_remainder:
  BROTLI_DCHECK(next_emit <= ip_end);
  /* Emit the remaining bytes as literals. */
  if (next_emit < ip_end) {
    const uint32_t insert = (uint32_t)(ip_end - next_emit);
    EmitInsertLen(insert, commands);
    BROTLI_LOG(("[CompressFragment] pos = %d insert = %d copy = %d\n",
                (int)(next_emit - base_ip), insert, 2));
    memcpy(*literals, next_emit, insert);
    *literals += insert;
  }
}

static void StoreCommands(BrotliTwoPassArena* s,
                          const uint8_t* literals, const size_t num_literals,
                          const uint32_t* commands, const size_t num_commands,
                          size_t* storage_ix, uint8_t* storage) {
  static const uint32_t kNumExtraBits[128] = {
      0,  0,  0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,
      6,  7,  8,  9,  10, 12, 14, 24, 0,  0,  0,  0,  0,  0,  0,  0,
      1,  1,  2,  2,  3,  3,  4,  4,  0,  0,  0,  0,  0,  0,  0,  0,
      1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  7,  8,  9,  10, 24,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,
      9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
      17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24,
  };
  static const uint32_t kInsertOffset[24] = {
      0,  1,  2,  3,  4,   5,   6,   8,   10,   14,   18,   26,
      34, 50, 66, 98, 130, 194, 322, 578, 1090, 2114, 6210, 22594,
  };

  size_t i;
  memset(s->lit_histo, 0, sizeof(s->lit_histo));
  /* TODO(eustas): is that necessary? */
  memset(s->cmd_depth, 0, sizeof(s->cmd_depth));
  /* TODO(eustas): is that necessary? */
  memset(s->cmd_bits, 0, sizeof(s->cmd_bits));
  memset(s->cmd_histo, 0, sizeof(s->cmd_histo));
  for (i = 0; i < num_literals; ++i) {
    ++s->lit_histo[literals[i]];
  }
  BrotliBuildAndStoreHuffmanTreeFast(s->tmp_tree, s->lit_histo, num_literals,
                                     /* max_bits = */ 8, s->lit_depth,
                                     s->lit_bits, storage_ix, storage);

  for (i = 0; i < num_commands; ++i) {
    const uint32_t code = commands[i] & 0xFF;
    BROTLI_DCHECK(code < 128);
    ++s->cmd_histo[code];
  }
  s->cmd_histo[1] += 1;
  s->cmd_histo[2] += 1;
  s->cmd_histo[64] += 1;
  s->cmd_histo[84] += 1;
  BuildAndStoreCommandPrefixCode(s, storage_ix, storage);

  for (i = 0; i < num_commands; ++i) {
    const uint32_t cmd = commands[i];
    const uint32_t code = cmd & 0xFF;
    const uint32_t extra = cmd >> 8;
    BROTLI_DCHECK(code < 128);
    BrotliWriteBits(s->cmd_depth[code], s->cmd_bits[code], storage_ix, storage);
    BrotliWriteBits(kNumExtraBits[code], extra, storage_ix, storage);
    if (code < 24) {
      const uint32_t insert = kInsertOffset[code] + extra;
      uint32_t j;
      for (j = 0; j < insert; ++j) {
        const uint8_t lit = *literals;
        BrotliWriteBits(s->lit_depth[lit], s->lit_bits[lit], storage_ix,
                        storage);
        ++literals;
      }
    }
  }
}

/* Acceptable loss for uncompressible speedup is 2% */
#define MIN_RATIO 0.98
#define SAMPLE_RATE 43

static BROTLI_BOOL ShouldCompress(BrotliTwoPassArena* s,
    const uint8_t* input, size_t input_size, size_t num_literals) {
  double corpus_size = (double)input_size;
  if ((double)num_literals < MIN_RATIO * corpus_size) {
    return BROTLI_TRUE;
  } else {
    const double max_total_bit_cost = corpus_size * 8 * MIN_RATIO / SAMPLE_RATE;
    size_t i;
    memset(s->lit_histo, 0, sizeof(s->lit_histo));
    for (i = 0; i < input_size; i += SAMPLE_RATE) {
      ++s->lit_histo[input[i]];
    }
    return TO_BROTLI_BOOL(BitsEntropy(s->lit_histo, 256) < max_total_bit_cost);
  }
}

static void RewindBitPosition(const size_t new_storage_ix,
                              size_t* storage_ix, uint8_t* storage) {
  const size_t bitpos = new_storage_ix & 7;
  const size_t mask = (1u << bitpos) - 1;
  storage[new_storage_ix >> 3] &= (uint8_t)mask;
  *storage_ix = new_storage_ix;
}

static void EmitUncompressedMetaBlock(const uint8_t* input, size_t input_size,
                                      size_t* storage_ix, uint8_t* storage) {
  BrotliStoreMetaBlockHeader(input_size, 1, storage_ix, storage);
  *storage_ix = (*storage_ix + 7u) & ~7u;
  memcpy(&storage[*storage_ix >> 3], input, input_size);
  *storage_ix += input_size << 3;
  storage[*storage_ix >> 3] = 0;
}

static BROTLI_INLINE void BrotliCompressFragmentTwoPassImpl(
    BrotliTwoPassArena* s, const uint8_t* input, size_t input_size,
    BROTLI_BOOL is_last, uint32_t* command_buf, uint8_t* literal_buf,
    int* table, size_t table_bits, size_t min_match,
    size_t* storage_ix, uint8_t* storage) {
  /* Save the start of the first block for position and distance computations.
  */
  const uint8_t* base_ip = input;
  BROTLI_UNUSED(is_last);

  while (input_size > 0) {
    size_t block_size =
        BROTLI_MIN(size_t, input_size, kCompressFragmentTwoPassBlockSize);
    uint32_t* commands = command_buf;
    uint8_t* literals = literal_buf;
    size_t num_literals;
    CreateCommands(input, block_size, input_size, base_ip, table,
                   table_bits, min_match, &literals, &commands);
    num_literals = (size_t)(literals - literal_buf);
    if (ShouldCompress(s, input, block_size, num_literals)) {
      const size_t num_commands = (size_t)(commands - command_buf);
      BrotliStoreMetaBlockHeader(block_size, 0, storage_ix, storage);
      /* No block splits, no contexts. */
      BrotliWriteBits(13, 0, storage_ix, storage);
      StoreCommands(s, literal_buf, num_literals, command_buf, num_commands,
                    storage_ix, storage);
    } else {
      /* Since we did not find many backward references and the entropy of
         the data is close to 8 bits, we can simply emit an uncompressed block.
         This makes compression speed of uncompressible data about 3x faster. */
      EmitUncompressedMetaBlock(input, block_size, storage_ix, storage);
    }
    input += block_size;
    input_size -= block_size;
  }
}

#define FOR_TABLE_BITS_(X) \
  X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17)

#define BAKE_METHOD_PARAM_(B)                                                  \
static BROTLI_NOINLINE void BrotliCompressFragmentTwoPassImpl ## B(            \
    BrotliTwoPassArena* s, const uint8_t* input, size_t input_size,            \
    BROTLI_BOOL is_last, uint32_t* command_buf, uint8_t* literal_buf,          \
    int* table, size_t* storage_ix, uint8_t* storage) {                        \
  size_t min_match = (B <= 15) ? 4 : 6;                                        \
  BrotliCompressFragmentTwoPassImpl(s, input, input_size, is_last, command_buf,\
      literal_buf, table, B, min_match, storage_ix, storage);                  \
}
FOR_TABLE_BITS_(BAKE_METHOD_PARAM_)
#undef BAKE_METHOD_PARAM_

void BrotliCompressFragmentTwoPass(
    BrotliTwoPassArena* s, const uint8_t* input, size_t input_size,
    BROTLI_BOOL is_last, uint32_t* command_buf, uint8_t* literal_buf,
    int* table, size_t table_size, size_t* storage_ix, uint8_t* storage) {
  const size_t initial_storage_ix = *storage_ix;
  const size_t table_bits = Log2FloorNonZero(table_size);
  switch (table_bits) {
#define CASE_(B)                                      \
    case B:                                           \
      BrotliCompressFragmentTwoPassImpl ## B(         \
          s, input, input_size, is_last, command_buf, \
          literal_buf, table, storage_ix, storage);   \
      break;
    FOR_TABLE_BITS_(CASE_)
#undef CASE_
    default: BROTLI_DCHECK(0); break;
  }

  /* If output is larger than single uncompressed block, rewrite it. */
  if (*storage_ix - initial_storage_ix > 31 + (input_size << 3)) {
    RewindBitPosition(initial_storage_ix, storage_ix, storage);
    EmitUncompressedMetaBlock(input, input_size, storage_ix, storage);
  }

  if (is_last) {
    BrotliWriteBits(1, 1, storage_ix, storage);  /* islast */
    BrotliWriteBits(1, 1, storage_ix, storage);  /* isempty */
    *storage_ix = (*storage_ix + 7u) & ~7u;
  }
}

#undef FOR_TABLE_BITS_

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
