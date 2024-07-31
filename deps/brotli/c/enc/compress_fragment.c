/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Function for fast encoding of an input fragment, independently from the input
   history. This function uses one-pass processing: when we find a backward
   match, we immediately emit the corresponding command and literal codes to
   the bit stream.

   Adapted from the CompressFragment() function in
   https://github.com/google/snappy/blob/master/snappy.cc */

#include "compress_fragment.h"

#include <string.h>  /* memcmp, memcpy, memset */

#include <brotli/types.h>

#include "../common/platform.h"
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

static BROTLI_INLINE uint32_t Hash(const uint8_t* p, size_t shift) {
  const uint64_t h = (BROTLI_UNALIGNED_LOAD64LE(p) << 24) * kHashMul32;
  return (uint32_t)(h >> shift);
}

static BROTLI_INLINE uint32_t HashBytesAtOffset(
    uint64_t v, int offset, size_t shift) {
  BROTLI_DCHECK(offset >= 0);
  BROTLI_DCHECK(offset <= 3);
  {
    const uint64_t h = ((v >> (8 * offset)) << 24) * kHashMul32;
    return (uint32_t)(h >> shift);
  }
}

static BROTLI_INLINE BROTLI_BOOL IsMatch(const uint8_t* p1, const uint8_t* p2) {
  return TO_BROTLI_BOOL(
      BrotliUnalignedRead32(p1) == BrotliUnalignedRead32(p2) &&
      p1[4] == p2[4]);
}

/* Builds a literal prefix code into "depths" and "bits" based on the statistics
   of the "input" string and stores it into the bit stream.
   Note that the prefix code here is built from the pre-LZ77 input, therefore
   we can only approximate the statistics of the actual literal stream.
   Moreover, for long inputs we build a histogram from a sample of the input
   and thus have to assign a non-zero depth for each literal.
   Returns estimated compression ratio millibytes/char for encoding given input
   with generated code. */
static size_t BuildAndStoreLiteralPrefixCode(BrotliOnePassArena* s,
                                             const uint8_t* input,
                                             const size_t input_size,
                                             uint8_t depths[256],
                                             uint16_t bits[256],
                                             size_t* storage_ix,
                                             uint8_t* storage) {
  uint32_t* BROTLI_RESTRICT const histogram = s->histogram;
  size_t histogram_total;
  size_t i;
  memset(histogram, 0, sizeof(s->histogram));

  if (input_size < (1 << 15)) {
    for (i = 0; i < input_size; ++i) {
      ++histogram[input[i]];
    }
    histogram_total = input_size;
    for (i = 0; i < 256; ++i) {
      /* We weigh the first 11 samples with weight 3 to account for the
         balancing effect of the LZ77 phase on the histogram. */
      const uint32_t adjust = 2 * BROTLI_MIN(uint32_t, histogram[i], 11u);
      histogram[i] += adjust;
      histogram_total += adjust;
    }
  } else {
    static const size_t kSampleRate = 29;
    for (i = 0; i < input_size; i += kSampleRate) {
      ++histogram[input[i]];
    }
    histogram_total = (input_size + kSampleRate - 1) / kSampleRate;
    for (i = 0; i < 256; ++i) {
      /* We add 1 to each population count to avoid 0 bit depths (since this is
         only a sample and we don't know if the symbol appears or not), and we
         weigh the first 11 samples with weight 3 to account for the balancing
         effect of the LZ77 phase on the histogram (more frequent symbols are
         more likely to be in backward references instead as literals). */
      const uint32_t adjust = 1 + 2 * BROTLI_MIN(uint32_t, histogram[i], 11u);
      histogram[i] += adjust;
      histogram_total += adjust;
    }
  }
  BrotliBuildAndStoreHuffmanTreeFast(s->tree, histogram, histogram_total,
                                     /* max_bits = */ 8,
                                     depths, bits, storage_ix, storage);
  {
    size_t literal_ratio = 0;
    for (i = 0; i < 256; ++i) {
      if (histogram[i]) literal_ratio += histogram[i] * depths[i];
    }
    /* Estimated encoding ratio, millibytes per symbol. */
    return (literal_ratio * 125) / histogram_total;
  }
}

/* Builds a command and distance prefix code (each 64 symbols) into "depth" and
   "bits" based on "histogram" and stores it into the bit stream. */
static void BuildAndStoreCommandPrefixCode(BrotliOnePassArena* s,
    size_t* storage_ix, uint8_t* storage) {
  const uint32_t* const histogram = s->cmd_histo;
  uint8_t* const depth = s->cmd_depth;
  uint16_t* const bits = s->cmd_bits;
  uint8_t* BROTLI_RESTRICT const tmp_depth = s->tmp_depth;
  uint16_t* BROTLI_RESTRICT const tmp_bits = s->tmp_bits;
  /* TODO(eustas): do only once on initialization. */
  memset(tmp_depth, 0, BROTLI_NUM_COMMAND_SYMBOLS);

  BrotliCreateHuffmanTree(histogram, 64, 15, s->tree, depth);
  BrotliCreateHuffmanTree(&histogram[64], 64, 14, s->tree, &depth[64]);
  /* We have to jump through a few hoops here in order to compute
     the command bits because the symbols are in a different order than in
     the full alphabet. This looks complicated, but having the symbols
     in this order in the command bits saves a few branches in the Emit*
     functions. */
  memcpy(tmp_depth, depth, 24);
  memcpy(tmp_depth + 24, depth + 40, 8);
  memcpy(tmp_depth + 32, depth + 24, 8);
  memcpy(tmp_depth + 40, depth + 48, 8);
  memcpy(tmp_depth + 48, depth + 32, 8);
  memcpy(tmp_depth + 56, depth + 56, 8);
  BrotliConvertBitDepthsToSymbols(tmp_depth, 64, tmp_bits);
  memcpy(bits, tmp_bits, 48);
  memcpy(bits + 24, tmp_bits + 32, 16);
  memcpy(bits + 32, tmp_bits + 48, 16);
  memcpy(bits + 40, tmp_bits + 24, 16);
  memcpy(bits + 48, tmp_bits + 40, 16);
  memcpy(bits + 56, tmp_bits + 56, 16);
  BrotliConvertBitDepthsToSymbols(&depth[64], 64, &bits[64]);
  {
    /* Create the bit length array for the full command alphabet. */
    size_t i;
    memset(tmp_depth, 0, 64);  /* only 64 first values were used */
    memcpy(tmp_depth, depth, 8);
    memcpy(tmp_depth + 64, depth + 8, 8);
    memcpy(tmp_depth + 128, depth + 16, 8);
    memcpy(tmp_depth + 192, depth + 24, 8);
    memcpy(tmp_depth + 384, depth + 32, 8);
    for (i = 0; i < 8; ++i) {
      tmp_depth[128 + 8 * i] = depth[40 + i];
      tmp_depth[256 + 8 * i] = depth[48 + i];
      tmp_depth[448 + 8 * i] = depth[56 + i];
    }
    /* TODO(eustas): could/should full-length machinery be avoided? */
    BrotliStoreHuffmanTree(
        tmp_depth, BROTLI_NUM_COMMAND_SYMBOLS, s->tree, storage_ix, storage);
  }
  BrotliStoreHuffmanTree(&depth[64], 64, s->tree, storage_ix, storage);
}

/* REQUIRES: insertlen < 6210 */
static BROTLI_INLINE void EmitInsertLen(size_t insertlen,
                                        const uint8_t depth[128],
                                        const uint16_t bits[128],
                                        uint32_t histo[128],
                                        size_t* storage_ix,
                                        uint8_t* storage) {
  if (insertlen < 6) {
    const size_t code = insertlen + 40;
    BrotliWriteBits(depth[code], bits[code], storage_ix, storage);
    ++histo[code];
  } else if (insertlen < 130) {
    const size_t tail = insertlen - 2;
    const uint32_t nbits = Log2FloorNonZero(tail) - 1u;
    const size_t prefix = tail >> nbits;
    const size_t inscode = (nbits << 1) + prefix + 42;
    BrotliWriteBits(depth[inscode], bits[inscode], storage_ix, storage);
    BrotliWriteBits(nbits, tail - (prefix << nbits), storage_ix, storage);
    ++histo[inscode];
  } else if (insertlen < 2114) {
    const size_t tail = insertlen - 66;
    const uint32_t nbits = Log2FloorNonZero(tail);
    const size_t code = nbits + 50;
    BrotliWriteBits(depth[code], bits[code], storage_ix, storage);
    BrotliWriteBits(nbits, tail - ((size_t)1 << nbits), storage_ix, storage);
    ++histo[code];
  } else {
    BrotliWriteBits(depth[61], bits[61], storage_ix, storage);
    BrotliWriteBits(12, insertlen - 2114, storage_ix, storage);
    ++histo[61];
  }
}

static BROTLI_INLINE void EmitLongInsertLen(size_t insertlen,
                                            const uint8_t depth[128],
                                            const uint16_t bits[128],
                                            uint32_t histo[128],
                                            size_t* storage_ix,
                                            uint8_t* storage) {
  if (insertlen < 22594) {
    BrotliWriteBits(depth[62], bits[62], storage_ix, storage);
    BrotliWriteBits(14, insertlen - 6210, storage_ix, storage);
    ++histo[62];
  } else {
    BrotliWriteBits(depth[63], bits[63], storage_ix, storage);
    BrotliWriteBits(24, insertlen - 22594, storage_ix, storage);
    ++histo[63];
  }
}

static BROTLI_INLINE void EmitCopyLen(size_t copylen,
                                      const uint8_t depth[128],
                                      const uint16_t bits[128],
                                      uint32_t histo[128],
                                      size_t* storage_ix,
                                      uint8_t* storage) {
  if (copylen < 10) {
    BrotliWriteBits(
        depth[copylen + 14], bits[copylen + 14], storage_ix, storage);
    ++histo[copylen + 14];
  } else if (copylen < 134) {
    const size_t tail = copylen - 6;
    const uint32_t nbits = Log2FloorNonZero(tail) - 1u;
    const size_t prefix = tail >> nbits;
    const size_t code = (nbits << 1) + prefix + 20;
    BrotliWriteBits(depth[code], bits[code], storage_ix, storage);
    BrotliWriteBits(nbits, tail - (prefix << nbits), storage_ix, storage);
    ++histo[code];
  } else if (copylen < 2118) {
    const size_t tail = copylen - 70;
    const uint32_t nbits = Log2FloorNonZero(tail);
    const size_t code = nbits + 28;
    BrotliWriteBits(depth[code], bits[code], storage_ix, storage);
    BrotliWriteBits(nbits, tail - ((size_t)1 << nbits), storage_ix, storage);
    ++histo[code];
  } else {
    BrotliWriteBits(depth[39], bits[39], storage_ix, storage);
    BrotliWriteBits(24, copylen - 2118, storage_ix, storage);
    ++histo[39];
  }
}

static BROTLI_INLINE void EmitCopyLenLastDistance(size_t copylen,
                                                  const uint8_t depth[128],
                                                  const uint16_t bits[128],
                                                  uint32_t histo[128],
                                                  size_t* storage_ix,
                                                  uint8_t* storage) {
  if (copylen < 12) {
    BrotliWriteBits(depth[copylen - 4], bits[copylen - 4], storage_ix, storage);
    ++histo[copylen - 4];
  } else if (copylen < 72) {
    const size_t tail = copylen - 8;
    const uint32_t nbits = Log2FloorNonZero(tail) - 1;
    const size_t prefix = tail >> nbits;
    const size_t code = (nbits << 1) + prefix + 4;
    BrotliWriteBits(depth[code], bits[code], storage_ix, storage);
    BrotliWriteBits(nbits, tail - (prefix << nbits), storage_ix, storage);
    ++histo[code];
  } else if (copylen < 136) {
    const size_t tail = copylen - 8;
    const size_t code = (tail >> 5) + 30;
    BrotliWriteBits(depth[code], bits[code], storage_ix, storage);
    BrotliWriteBits(5, tail & 31, storage_ix, storage);
    BrotliWriteBits(depth[64], bits[64], storage_ix, storage);
    ++histo[code];
    ++histo[64];
  } else if (copylen < 2120) {
    const size_t tail = copylen - 72;
    const uint32_t nbits = Log2FloorNonZero(tail);
    const size_t code = nbits + 28;
    BrotliWriteBits(depth[code], bits[code], storage_ix, storage);
    BrotliWriteBits(nbits, tail - ((size_t)1 << nbits), storage_ix, storage);
    BrotliWriteBits(depth[64], bits[64], storage_ix, storage);
    ++histo[code];
    ++histo[64];
  } else {
    BrotliWriteBits(depth[39], bits[39], storage_ix, storage);
    BrotliWriteBits(24, copylen - 2120, storage_ix, storage);
    BrotliWriteBits(depth[64], bits[64], storage_ix, storage);
    ++histo[39];
    ++histo[64];
  }
}

static BROTLI_INLINE void EmitDistance(size_t distance,
                                       const uint8_t depth[128],
                                       const uint16_t bits[128],
                                       uint32_t histo[128],
                                       size_t* storage_ix, uint8_t* storage) {
  const size_t d = distance + 3;
  const uint32_t nbits = Log2FloorNonZero(d) - 1u;
  const size_t prefix = (d >> nbits) & 1;
  const size_t offset = (2 + prefix) << nbits;
  const size_t distcode = 2 * (nbits - 1) + prefix + 80;
  BrotliWriteBits(depth[distcode], bits[distcode], storage_ix, storage);
  BrotliWriteBits(nbits, d - offset, storage_ix, storage);
  ++histo[distcode];
}

static BROTLI_INLINE void EmitLiterals(const uint8_t* input, const size_t len,
                                       const uint8_t depth[256],
                                       const uint16_t bits[256],
                                       size_t* storage_ix, uint8_t* storage) {
  size_t j;
  for (j = 0; j < len; j++) {
    const uint8_t lit = input[j];
    BrotliWriteBits(depth[lit], bits[lit], storage_ix, storage);
  }
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

static void UpdateBits(size_t n_bits, uint32_t bits, size_t pos,
    uint8_t* array) {
  while (n_bits > 0) {
    size_t byte_pos = pos >> 3;
    size_t n_unchanged_bits = pos & 7;
    size_t n_changed_bits = BROTLI_MIN(size_t, n_bits, 8 - n_unchanged_bits);
    size_t total_bits = n_unchanged_bits + n_changed_bits;
    uint32_t mask =
        (~((1u << total_bits) - 1u)) | ((1u << n_unchanged_bits) - 1u);
    uint32_t unchanged_bits = array[byte_pos] & mask;
    uint32_t changed_bits = bits & ((1u << n_changed_bits) - 1u);
    array[byte_pos] =
        (uint8_t)((changed_bits << n_unchanged_bits) | unchanged_bits);
    n_bits -= n_changed_bits;
    bits >>= n_changed_bits;
    pos += n_changed_bits;
  }
}

static void RewindBitPosition(const size_t new_storage_ix,
                              size_t* storage_ix, uint8_t* storage) {
  const size_t bitpos = new_storage_ix & 7;
  const size_t mask = (1u << bitpos) - 1;
  storage[new_storage_ix >> 3] &= (uint8_t)mask;
  *storage_ix = new_storage_ix;
}

static BROTLI_BOOL ShouldMergeBlock(BrotliOnePassArena* s,
    const uint8_t* data, size_t len, const uint8_t* depths) {
  uint32_t* BROTLI_RESTRICT const histo = s->histogram;
  static const size_t kSampleRate = 43;
  size_t i;
  memset(histo, 0, sizeof(s->histogram));
  for (i = 0; i < len; i += kSampleRate) {
    ++histo[data[i]];
  }
  {
    const size_t total = (len + kSampleRate - 1) / kSampleRate;
    double r = (FastLog2(total) + 0.5) * (double)total + 200;
    for (i = 0; i < 256; ++i) {
      r -= (double)histo[i] * (depths[i] + FastLog2(histo[i]));
    }
    return TO_BROTLI_BOOL(r >= 0.0);
  }
}

/* Acceptable loss for uncompressible speedup is 2% */
#define MIN_RATIO 980

static BROTLI_INLINE BROTLI_BOOL ShouldUseUncompressedMode(
    const uint8_t* metablock_start, const uint8_t* next_emit,
    const size_t insertlen, const size_t literal_ratio) {
  const size_t compressed = (size_t)(next_emit - metablock_start);
  if (compressed * 50 > insertlen) {
    return BROTLI_FALSE;
  } else {
    return TO_BROTLI_BOOL(literal_ratio > MIN_RATIO);
  }
}

static void EmitUncompressedMetaBlock(const uint8_t* begin, const uint8_t* end,
                                      const size_t storage_ix_start,
                                      size_t* storage_ix, uint8_t* storage) {
  const size_t len = (size_t)(end - begin);
  RewindBitPosition(storage_ix_start, storage_ix, storage);
  BrotliStoreMetaBlockHeader(len, 1, storage_ix, storage);
  *storage_ix = (*storage_ix + 7u) & ~7u;
  memcpy(&storage[*storage_ix >> 3], begin, len);
  *storage_ix += len << 3;
  storage[*storage_ix >> 3] = 0;
}

static uint32_t kCmdHistoSeed[128] = {
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 0, 0, 0, 0,
};

static BROTLI_INLINE void BrotliCompressFragmentFastImpl(
    BrotliOnePassArena* s, const uint8_t* input, size_t input_size,
    BROTLI_BOOL is_last, int* table, size_t table_bits,
    size_t* storage_ix, uint8_t* storage) {
  uint8_t* BROTLI_RESTRICT const cmd_depth = s->cmd_depth;
  uint16_t* BROTLI_RESTRICT const cmd_bits = s->cmd_bits;
  uint32_t* BROTLI_RESTRICT const cmd_histo = s->cmd_histo;
  uint8_t* BROTLI_RESTRICT const lit_depth = s->lit_depth;
  uint16_t* BROTLI_RESTRICT const lit_bits = s->lit_bits;
  const uint8_t* ip_end;

  /* "next_emit" is a pointer to the first byte that is not covered by a
     previous copy. Bytes between "next_emit" and the start of the next copy or
     the end of the input will be emitted as literal bytes. */
  const uint8_t* next_emit = input;
  /* Save the start of the first block for position and distance computations.
  */
  const uint8_t* base_ip = input;

  static const size_t kFirstBlockSize = 3 << 15;
  static const size_t kMergeBlockSize = 1 << 16;

  const size_t kInputMarginBytes = BROTLI_WINDOW_GAP;
  const size_t kMinMatchLen = 5;

  const uint8_t* metablock_start = input;
  size_t block_size = BROTLI_MIN(size_t, input_size, kFirstBlockSize);
  size_t total_block_size = block_size;
  /* Save the bit position of the MLEN field of the meta-block header, so that
     we can update it later if we decide to extend this meta-block. */
  size_t mlen_storage_ix = *storage_ix + 3;

  size_t literal_ratio;

  const uint8_t* ip;
  int last_distance;

  const size_t shift = 64u - table_bits;

  BrotliStoreMetaBlockHeader(block_size, 0, storage_ix, storage);
  /* No block splits, no contexts. */
  BrotliWriteBits(13, 0, storage_ix, storage);

  literal_ratio = BuildAndStoreLiteralPrefixCode(
      s, input, block_size, s->lit_depth, s->lit_bits, storage_ix, storage);

  {
    /* Store the pre-compressed command and distance prefix codes. */
    size_t i;
    for (i = 0; i + 7 < s->cmd_code_numbits; i += 8) {
      BrotliWriteBits(8, s->cmd_code[i >> 3], storage_ix, storage);
    }
  }
  BrotliWriteBits(s->cmd_code_numbits & 7,
                  s->cmd_code[s->cmd_code_numbits >> 3], storage_ix, storage);

 emit_commands:
  /* Initialize the command and distance histograms. We will gather
     statistics of command and distance codes during the processing
     of this block and use it to update the command and distance
     prefix codes for the next block. */
  memcpy(s->cmd_histo, kCmdHistoSeed, sizeof(kCmdHistoSeed));

  /* "ip" is the input pointer. */
  ip = input;
  last_distance = -1;
  ip_end = input + block_size;

  if (BROTLI_PREDICT_TRUE(block_size >= kInputMarginBytes)) {
    /* For the last block, we need to keep a 16 bytes margin so that we can be
       sure that all distances are at most window size - 16.
       For all other blocks, we only need to keep a margin of 5 bytes so that
       we don't go over the block size with a copy. */
    const size_t len_limit = BROTLI_MIN(size_t, block_size - kMinMatchLen,
                                        input_size - kInputMarginBytes);
    const uint8_t* ip_limit = input + len_limit;

    uint32_t next_hash;
    for (next_hash = Hash(++ip, shift); ; ) {
      /* Step 1: Scan forward in the input looking for a 5-byte-long match.
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
         last match; dividing it by 32 (i.e. right-shifting by five) gives the
         number of bytes to move ahead for each iteration. */
      uint32_t skip = 32;

      const uint8_t* next_ip = ip;
      const uint8_t* candidate;
      BROTLI_DCHECK(next_emit < ip);
trawl:
      do {
        uint32_t hash = next_hash;
        uint32_t bytes_between_hash_lookups = skip++ >> 5;
        BROTLI_DCHECK(hash == Hash(next_ip, shift));
        ip = next_ip;
        next_ip = ip + bytes_between_hash_lookups;
        if (BROTLI_PREDICT_FALSE(next_ip > ip_limit)) {
          goto emit_remainder;
        }
        next_hash = Hash(next_ip, shift);
        candidate = ip - last_distance;
        if (IsMatch(ip, candidate)) {
          if (BROTLI_PREDICT_TRUE(candidate < ip)) {
            table[hash] = (int)(ip - base_ip);
            break;
          }
        }
        candidate = base_ip + table[hash];
        BROTLI_DCHECK(candidate >= base_ip);
        BROTLI_DCHECK(candidate < ip);

        table[hash] = (int)(ip - base_ip);
      } while (BROTLI_PREDICT_TRUE(!IsMatch(ip, candidate)));

      /* Check copy distance. If candidate is not feasible, continue search.
         Checking is done outside of hot loop to reduce overhead. */
      if (ip - candidate > MAX_DISTANCE) goto trawl;

      /* Step 2: Emit the found match together with the literal bytes from
         "next_emit" to the bit stream, and then see if we can find a next match
         immediately afterwards. Repeat until we find no match for the input
         without emitting some literal bytes. */

      {
        /* We have a 5-byte match at ip, and we need to emit bytes in
           [next_emit, ip). */
        const uint8_t* base = ip;
        size_t matched = 5 + FindMatchLengthWithLimit(
            candidate + 5, ip + 5, (size_t)(ip_end - ip) - 5);
        int distance = (int)(base - candidate);  /* > 0 */
        size_t insert = (size_t)(base - next_emit);
        ip += matched;
        BROTLI_LOG(("[CompressFragment] pos = %d insert = %lu copy = %d\n",
                    (int)(next_emit - base_ip), (unsigned long)insert, 2));
        BROTLI_DCHECK(0 == memcmp(base, candidate, matched));
        if (BROTLI_PREDICT_TRUE(insert < 6210)) {
          EmitInsertLen(insert, cmd_depth, cmd_bits, cmd_histo,
                        storage_ix, storage);
        } else if (ShouldUseUncompressedMode(metablock_start, next_emit, insert,
                                             literal_ratio)) {
          EmitUncompressedMetaBlock(metablock_start, base, mlen_storage_ix - 3,
                                    storage_ix, storage);
          input_size -= (size_t)(base - input);
          input = base;
          next_emit = input;
          goto next_block;
        } else {
          EmitLongInsertLen(insert, cmd_depth, cmd_bits, cmd_histo,
                            storage_ix, storage);
        }
        EmitLiterals(next_emit, insert, lit_depth, lit_bits,
                     storage_ix, storage);
        if (distance == last_distance) {
          BrotliWriteBits(cmd_depth[64], cmd_bits[64], storage_ix, storage);
          ++cmd_histo[64];
        } else {
          EmitDistance((size_t)distance, cmd_depth, cmd_bits,
                       cmd_histo, storage_ix, storage);
          last_distance = distance;
        }
        EmitCopyLenLastDistance(matched, cmd_depth, cmd_bits, cmd_histo,
                                storage_ix, storage);
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
        /* We could immediately start working at ip now, but to improve
           compression we first update "table" with the hashes of some positions
           within the last copy. */
        {
          uint64_t input_bytes = BROTLI_UNALIGNED_LOAD64LE(ip - 3);
          uint32_t prev_hash = HashBytesAtOffset(input_bytes, 0, shift);
          uint32_t cur_hash = HashBytesAtOffset(input_bytes, 3, shift);
          table[prev_hash] = (int)(ip - base_ip - 3);
          prev_hash = HashBytesAtOffset(input_bytes, 1, shift);
          table[prev_hash] = (int)(ip - base_ip - 2);
          prev_hash = HashBytesAtOffset(input_bytes, 2, shift);
          table[prev_hash] = (int)(ip - base_ip - 1);

          candidate = base_ip + table[cur_hash];
          table[cur_hash] = (int)(ip - base_ip);
        }
      }

      while (IsMatch(ip, candidate)) {
        /* We have a 5-byte match at ip, and no need to emit any literal bytes
           prior to ip. */
        const uint8_t* base = ip;
        size_t matched = 5 + FindMatchLengthWithLimit(
            candidate + 5, ip + 5, (size_t)(ip_end - ip) - 5);
        if (ip - candidate > MAX_DISTANCE) break;
        ip += matched;
        last_distance = (int)(base - candidate);  /* > 0 */
        BROTLI_DCHECK(0 == memcmp(base, candidate, matched));
        EmitCopyLen(matched, cmd_depth, cmd_bits, cmd_histo,
                    storage_ix, storage);
        EmitDistance((size_t)last_distance, cmd_depth, cmd_bits,
                     cmd_histo, storage_ix, storage);
        BROTLI_LOG(("[CompressFragment] pos = %d insert = %d copy = %d\n"
                    "[CompressFragment] pos = %d distance = %d\n",
                    (int)(base - base_ip), 0, (int)matched,
                    (int)(base - base_ip), (int)last_distance));

        next_emit = ip;
        if (BROTLI_PREDICT_FALSE(ip >= ip_limit)) {
          goto emit_remainder;
        }
        /* We could immediately start working at ip now, but to improve
           compression we first update "table" with the hashes of some positions
           within the last copy. */
        {
          uint64_t input_bytes = BROTLI_UNALIGNED_LOAD64LE(ip - 3);
          uint32_t prev_hash = HashBytesAtOffset(input_bytes, 0, shift);
          uint32_t cur_hash = HashBytesAtOffset(input_bytes, 3, shift);
          table[prev_hash] = (int)(ip - base_ip - 3);
          prev_hash = HashBytesAtOffset(input_bytes, 1, shift);
          table[prev_hash] = (int)(ip - base_ip - 2);
          prev_hash = HashBytesAtOffset(input_bytes, 2, shift);
          table[prev_hash] = (int)(ip - base_ip - 1);

          candidate = base_ip + table[cur_hash];
          table[cur_hash] = (int)(ip - base_ip);
        }
      }

      next_hash = Hash(++ip, shift);
    }
  }

 emit_remainder:
  BROTLI_DCHECK(next_emit <= ip_end);
  input += block_size;
  input_size -= block_size;
  block_size = BROTLI_MIN(size_t, input_size, kMergeBlockSize);

  /* Decide if we want to continue this meta-block instead of emitting the
     last insert-only command. */
  if (input_size > 0 &&
      total_block_size + block_size <= (1 << 20) &&
      ShouldMergeBlock(s, input, block_size, lit_depth)) {
    BROTLI_DCHECK(total_block_size > (1 << 16));
    /* Update the size of the current meta-block and continue emitting commands.
       We can do this because the current size and the new size both have 5
       nibbles. */
    total_block_size += block_size;
    UpdateBits(20, (uint32_t)(total_block_size - 1), mlen_storage_ix, storage);
    goto emit_commands;
  }

  /* Emit the remaining bytes as literals. */
  if (next_emit < ip_end) {
    const size_t insert = (size_t)(ip_end - next_emit);
    BROTLI_LOG(("[CompressFragment] pos = %d insert = %lu copy = %d\n",
                (int)(next_emit - base_ip), (unsigned long)insert, 2));
    if (BROTLI_PREDICT_TRUE(insert < 6210)) {
      EmitInsertLen(insert, cmd_depth, cmd_bits, cmd_histo,
                    storage_ix, storage);
      EmitLiterals(next_emit, insert, lit_depth, lit_bits, storage_ix, storage);
    } else if (ShouldUseUncompressedMode(metablock_start, next_emit, insert,
                                         literal_ratio)) {
      EmitUncompressedMetaBlock(metablock_start, ip_end, mlen_storage_ix - 3,
                                storage_ix, storage);
    } else {
      EmitLongInsertLen(insert, cmd_depth, cmd_bits, cmd_histo,
                        storage_ix, storage);
      EmitLiterals(next_emit, insert, lit_depth, lit_bits,
                   storage_ix, storage);
    }
  }
  next_emit = ip_end;

next_block:
  /* If we have more data, write a new meta-block header and prefix codes and
     then continue emitting commands. */
  if (input_size > 0) {
    metablock_start = input;
    block_size = BROTLI_MIN(size_t, input_size, kFirstBlockSize);
    total_block_size = block_size;
    /* Save the bit position of the MLEN field of the meta-block header, so that
       we can update it later if we decide to extend this meta-block. */
    mlen_storage_ix = *storage_ix + 3;
    BrotliStoreMetaBlockHeader(block_size, 0, storage_ix, storage);
    /* No block splits, no contexts. */
    BrotliWriteBits(13, 0, storage_ix, storage);
    literal_ratio = BuildAndStoreLiteralPrefixCode(
        s, input, block_size, lit_depth, lit_bits, storage_ix, storage);
    BuildAndStoreCommandPrefixCode(s, storage_ix, storage);
    goto emit_commands;
  }

  if (!is_last) {
    /* If this is not the last block, update the command and distance prefix
       codes for the next block and store the compressed forms. */
    s->cmd_code[0] = 0;
    s->cmd_code_numbits = 0;
    BuildAndStoreCommandPrefixCode(s, &s->cmd_code_numbits, s->cmd_code);
  }
}

#define FOR_TABLE_BITS_(X) X(9) X(11) X(13) X(15)

#define BAKE_METHOD_PARAM_(B) \
static BROTLI_NOINLINE void BrotliCompressFragmentFastImpl ## B(             \
    BrotliOnePassArena* s, const uint8_t* input, size_t input_size,          \
    BROTLI_BOOL is_last, int* table, size_t* storage_ix, uint8_t* storage) { \
  BrotliCompressFragmentFastImpl(s, input, input_size, is_last, table, B,    \
      storage_ix, storage);                                                  \
}
FOR_TABLE_BITS_(BAKE_METHOD_PARAM_)
#undef BAKE_METHOD_PARAM_

void BrotliCompressFragmentFast(
    BrotliOnePassArena* s, const uint8_t* input, size_t input_size,
    BROTLI_BOOL is_last, int* table, size_t table_size,
    size_t* storage_ix, uint8_t* storage) {
  const size_t initial_storage_ix = *storage_ix;
  const size_t table_bits = Log2FloorNonZero(table_size);

  if (input_size == 0) {
    BROTLI_DCHECK(is_last);
    BrotliWriteBits(1, 1, storage_ix, storage);  /* islast */
    BrotliWriteBits(1, 1, storage_ix, storage);  /* isempty */
    *storage_ix = (*storage_ix + 7u) & ~7u;
    return;
  }

  switch (table_bits) {
#define CASE_(B)                                                     \
    case B:                                                          \
      BrotliCompressFragmentFastImpl ## B(                           \
          s, input, input_size, is_last, table, storage_ix, storage);\
      break;
    FOR_TABLE_BITS_(CASE_)
#undef CASE_
    default: BROTLI_DCHECK(0); break;
  }

  /* If output is larger than single uncompressed block, rewrite it. */
  if (*storage_ix - initial_storage_ix > 31 + (input_size << 3)) {
    EmitUncompressedMetaBlock(input, input + input_size, initial_storage_ix,
                              storage_ix, storage);
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
