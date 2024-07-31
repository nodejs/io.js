/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/**
 * @file
 * Common constants used in decoder and encoder API.
 */

#ifndef BROTLI_COMMON_CONSTANTS_H_
#define BROTLI_COMMON_CONSTANTS_H_

#include <brotli/port.h>
#include <brotli/types.h>

#include "platform.h"

/* Specification: 7.3. Encoding of the context map */
#define BROTLI_CONTEXT_MAP_MAX_RLE 16

/* Specification: 2. Compressed representation overview */
#define BROTLI_MAX_NUMBER_OF_BLOCK_TYPES 256

/* Specification: 3.3. Alphabet sizes: insert-and-copy length */
#define BROTLI_NUM_LITERAL_SYMBOLS 256
#define BROTLI_NUM_COMMAND_SYMBOLS 704
#define BROTLI_NUM_BLOCK_LEN_SYMBOLS 26
#define BROTLI_MAX_CONTEXT_MAP_SYMBOLS (BROTLI_MAX_NUMBER_OF_BLOCK_TYPES + \
                                        BROTLI_CONTEXT_MAP_MAX_RLE)
#define BROTLI_MAX_BLOCK_TYPE_SYMBOLS (BROTLI_MAX_NUMBER_OF_BLOCK_TYPES + 2)

/* Specification: 3.5. Complex prefix codes */
#define BROTLI_REPEAT_PREVIOUS_CODE_LENGTH 16
#define BROTLI_REPEAT_ZERO_CODE_LENGTH 17
#define BROTLI_CODE_LENGTH_CODES (BROTLI_REPEAT_ZERO_CODE_LENGTH + 1)
/* "code length of 8 is repeated" */
#define BROTLI_INITIAL_REPEATED_CODE_LENGTH 8

/* "Large Window Brotli" */

/**
 * The theoretical maximum number of distance bits specified for large window
 * brotli, for 64-bit encoders and decoders. Even when in practice 32-bit
 * encoders and decoders only support up to 30 max distance bits, the value is
 * set to 62 because it affects the large window brotli file format.
 * Specifically, it affects the encoding of simple huffman tree for distances,
 * see Specification RFC 7932 chapter 3.4.
 */
#define BROTLI_LARGE_MAX_DISTANCE_BITS 62U
#define BROTLI_LARGE_MIN_WBITS 10
/**
 * The maximum supported large brotli window bits by the encoder and decoder.
 * Large window brotli allows up to 62 bits, however the current encoder and
 * decoder, designed for 32-bit integers, only support up to 30 bits maximum.
 */
#define BROTLI_LARGE_MAX_WBITS 30

/* Specification: 4. Encoding of distances */
#define BROTLI_NUM_DISTANCE_SHORT_CODES 16
/**
 * Maximal number of "postfix" bits.
 *
 * Number of "postfix" bits is stored as 2 bits in meta-block header.
 */
#define BROTLI_MAX_NPOSTFIX 3
#define BROTLI_MAX_NDIRECT 120
#define BROTLI_MAX_DISTANCE_BITS 24U
#define BROTLI_DISTANCE_ALPHABET_SIZE(NPOSTFIX, NDIRECT, MAXNBITS) ( \
    BROTLI_NUM_DISTANCE_SHORT_CODES + (NDIRECT) +                    \
    ((MAXNBITS) << ((NPOSTFIX) + 1)))
/* BROTLI_NUM_DISTANCE_SYMBOLS == 1128 */
#define BROTLI_NUM_DISTANCE_SYMBOLS \
    BROTLI_DISTANCE_ALPHABET_SIZE(  \
        BROTLI_MAX_NDIRECT, BROTLI_MAX_NPOSTFIX, BROTLI_LARGE_MAX_DISTANCE_BITS)

/* ((1 << 26) - 4) is the maximal distance that can be expressed in RFC 7932
   brotli stream using NPOSTFIX = 0 and NDIRECT = 0. With other NPOSTFIX and
   NDIRECT values distances up to ((1 << 29) + 88) could be expressed. */
#define BROTLI_MAX_DISTANCE 0x3FFFFFC

/* ((1 << 31) - 4) is the safe distance limit. Using this number as a limit
   allows safe distance calculation without overflows, given the distance
   alphabet size is limited to corresponding size
   (see kLargeWindowDistanceCodeLimits). */
#define BROTLI_MAX_ALLOWED_DISTANCE 0x7FFFFFFC


/* Specification: 4. Encoding of Literal Insertion Lengths and Copy Lengths */
#define BROTLI_NUM_INS_COPY_CODES 24

/* 7.1. Context modes and context ID lookup for literals */
/* "context IDs for literals are in the range of 0..63" */
#define BROTLI_LITERAL_CONTEXT_BITS 6

/* 7.2. Context ID for distances */
#define BROTLI_DISTANCE_CONTEXT_BITS 2

/* 9.1. Format of the Stream Header */
/* Number of slack bytes for window size. Don't confuse
   with BROTLI_NUM_DISTANCE_SHORT_CODES. */
#define BROTLI_WINDOW_GAP 16
#define BROTLI_MAX_BACKWARD_LIMIT(W) (((size_t)1 << (W)) - BROTLI_WINDOW_GAP)

typedef struct BrotliDistanceCodeLimit {
  uint32_t max_alphabet_size;
  uint32_t max_distance;
} BrotliDistanceCodeLimit;

/* This function calculates maximal size of distance alphabet, such that the
   distances greater than the given values can not be represented.

   This limits are designed to support fast and safe 32-bit decoders.
   "32-bit" means that signed integer values up to ((1 << 31) - 1) could be
   safely expressed.

   Brotli distance alphabet symbols do not represent consecutive distance
   ranges. Each distance alphabet symbol (excluding direct distances and short
   codes), represent interleaved (for NPOSTFIX > 0) range of distances.
   A "group" of consecutive (1 << NPOSTFIX) symbols represent non-interleaved
   range. Two consecutive groups require the same amount of "extra bits".

   It is important that distance alphabet represents complete "groups".
   To avoid complex logic on encoder side about interleaved ranges
   it was decided to restrict both sides to complete distance code "groups".
 */
BROTLI_UNUSED_FUNCTION BrotliDistanceCodeLimit BrotliCalculateDistanceCodeLimit(
    uint32_t max_distance, uint32_t npostfix, uint32_t ndirect) {
  BrotliDistanceCodeLimit result;
  /* Marking this function as unused, because not all files
     including "constants.h" use it -> compiler warns about that. */
  BROTLI_UNUSED(&BrotliCalculateDistanceCodeLimit);
  if (max_distance <= ndirect) {
    /* This case never happens / exists only for the sake of completeness. */
    result.max_alphabet_size = max_distance + BROTLI_NUM_DISTANCE_SHORT_CODES;
    result.max_distance = max_distance;
    return result;
  } else {
    /* The first prohibited value. */
    uint32_t forbidden_distance = max_distance + 1;
    /* Subtract "directly" encoded region. */
    uint32_t offset = forbidden_distance - ndirect - 1;
    uint32_t ndistbits = 0;
    uint32_t tmp;
    uint32_t half;
    uint32_t group;
    /* Postfix for the last dcode in the group. */
    uint32_t postfix = (1u << npostfix) - 1;
    uint32_t extra;
    uint32_t start;
    /* Remove postfix and "head-start". */
    offset = (offset >> npostfix) + 4;
    /* Calculate the number of distance bits. */
    tmp = offset / 2;
    /* Poor-man's log2floor, to avoid extra dependencies. */
    while (tmp != 0) {ndistbits++; tmp = tmp >> 1;}
    /* One bit is covered with subrange addressing ("half"). */
    ndistbits--;
    /* Find subrange. */
    half = (offset >> ndistbits) & 1;
    /* Calculate the "group" part of dcode. */
    group = ((ndistbits - 1) << 1) | half;
    /* Calculated "group" covers the prohibited distance value. */
    if (group == 0) {
      /* This case is added for correctness; does not occur for limit > 128. */
      result.max_alphabet_size = ndirect + BROTLI_NUM_DISTANCE_SHORT_CODES;
      result.max_distance = ndirect;
      return result;
    }
    /* Decrement "group", so it is the last permitted "group". */
    group--;
    /* After group was decremented, ndistbits and half must be recalculated. */
    ndistbits = (group >> 1) + 1;
    /* The last available distance in the subrange has all extra bits set. */
    extra = (1u << ndistbits) - 1;
    /* Calculate region start. NB: ndistbits >= 1. */
    start = (1u << (ndistbits + 1)) - 4;
    /* Move to subregion. */
    start += (group & 1) << ndistbits;
    /* Calculate the alphabet size. */
    result.max_alphabet_size = ((group << npostfix) | postfix) + ndirect +
        BROTLI_NUM_DISTANCE_SHORT_CODES + 1;
    /* Calculate the maximal distance representable by alphabet. */
    result.max_distance = ((start + extra) << npostfix) + postfix + ndirect + 1;
    return result;
  }
}

/* Represents the range of values belonging to a prefix code:
   [offset, offset + 2^nbits) */
typedef struct {
  uint16_t offset;
  uint8_t nbits;
} BrotliPrefixCodeRange;

/* "Soft-private", it is exported, but not "advertised" as API. */
BROTLI_COMMON_API extern const BrotliPrefixCodeRange
    _kBrotliPrefixCodeRanges[BROTLI_NUM_BLOCK_LEN_SYMBOLS];

#endif  /* BROTLI_COMMON_CONSTANTS_H_ */
