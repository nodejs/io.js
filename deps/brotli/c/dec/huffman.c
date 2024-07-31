/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Utilities for building Huffman decoding tables. */

#include "huffman.h"

#include <string.h>  /* memcpy, memset */

#include <brotli/types.h>

#include "../common/constants.h"
#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define BROTLI_REVERSE_BITS_MAX 8

#if defined(BROTLI_RBIT)
#define BROTLI_REVERSE_BITS_BASE \
  ((sizeof(brotli_reg_t) << 3) - BROTLI_REVERSE_BITS_MAX)
#else
#define BROTLI_REVERSE_BITS_BASE 0
static uint8_t kReverseBits[1 << BROTLI_REVERSE_BITS_MAX] = {
  0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0,
  0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
  0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
  0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
  0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4,
  0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
  0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC,
  0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
  0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
  0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
  0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA,
  0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
  0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6,
  0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
  0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
  0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
  0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1,
  0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
  0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9,
  0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
  0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
  0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
  0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED,
  0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
  0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
  0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
  0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
  0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
  0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7,
  0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
  0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF,
  0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};
#endif  /* BROTLI_RBIT */

#define BROTLI_REVERSE_BITS_LOWEST \
  ((brotli_reg_t)1 << (BROTLI_REVERSE_BITS_MAX - 1 + BROTLI_REVERSE_BITS_BASE))

/* Returns reverse(num >> BROTLI_REVERSE_BITS_BASE, BROTLI_REVERSE_BITS_MAX),
   where reverse(value, len) is the bit-wise reversal of the len least
   significant bits of value. */
static BROTLI_INLINE brotli_reg_t BrotliReverseBits(brotli_reg_t num) {
#if defined(BROTLI_RBIT)
  return BROTLI_RBIT(num);
#else
  return kReverseBits[num];
#endif
}

/* Stores code in table[0], table[step], table[2*step], ..., table[end] */
/* Assumes that end is an integer multiple of step */
static BROTLI_INLINE void ReplicateValue(HuffmanCode* table,
                                         int step, int end,
                                         HuffmanCode code) {
  do {
    end -= step;
    table[end] = code;
  } while (end > 0);
}

/* Returns the table width of the next 2nd level table. |count| is the histogram
   of bit lengths for the remaining symbols, |len| is the code length of the
   next processed symbol. */
static BROTLI_INLINE int NextTableBitSize(const uint16_t* const count,
                                          int len, int root_bits) {
  int left = 1 << (len - root_bits);
  while (len < BROTLI_HUFFMAN_MAX_CODE_LENGTH) {
    left -= count[len];
    if (left <= 0) break;
    ++len;
    left <<= 1;
  }
  return len - root_bits;
}

void BrotliBuildCodeLengthsHuffmanTable(HuffmanCode* table,
                                        const uint8_t* const code_lengths,
                                        uint16_t* count) {
  HuffmanCode code;       /* current table entry */
  int symbol;             /* symbol index in original or sorted table */
  brotli_reg_t key;       /* prefix code */
  brotli_reg_t key_step;  /* prefix code addend */
  int step;               /* step size to replicate values in current table */
  int table_size;         /* size of current table */
  int sorted[BROTLI_CODE_LENGTH_CODES];  /* symbols sorted by code length */
  /* offsets in sorted table for each length */
  int offset[BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH + 1];
  int bits;
  int bits_count;
  BROTLI_DCHECK(BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH <=
                BROTLI_REVERSE_BITS_MAX);
  BROTLI_DCHECK(BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH == 5);

  /* Generate offsets into sorted symbol table by code length. */
  symbol = -1;
  bits = 1;
  /* BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH == 5 */
  BROTLI_REPEAT_5({
    symbol += count[bits];
    offset[bits] = symbol;
    bits++;
  });
  /* Symbols with code length 0 are placed after all other symbols. */
  offset[0] = BROTLI_CODE_LENGTH_CODES - 1;

  /* Sort symbols by length, by symbol order within each length. */
  symbol = BROTLI_CODE_LENGTH_CODES;
  do {
    BROTLI_REPEAT_6({
      symbol--;
      sorted[offset[code_lengths[symbol]]--] = symbol;
    });
  } while (symbol != 0);

  table_size = 1 << BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH;

  /* Special case: all symbols but one have 0 code length. */
  if (offset[0] == 0) {
    code = ConstructHuffmanCode(0, (uint16_t)sorted[0]);
    for (key = 0; key < (brotli_reg_t)table_size; ++key) {
      table[key] = code;
    }
    return;
  }

  /* Fill in table. */
  key = 0;
  key_step = BROTLI_REVERSE_BITS_LOWEST;
  symbol = 0;
  bits = 1;
  step = 2;
  do {
    for (bits_count = count[bits]; bits_count != 0; --bits_count) {
      code = ConstructHuffmanCode((uint8_t)bits, (uint16_t)sorted[symbol++]);
      ReplicateValue(&table[BrotliReverseBits(key)], step, table_size, code);
      key += key_step;
    }
    step <<= 1;
    key_step >>= 1;
  } while (++bits <= BROTLI_HUFFMAN_MAX_CODE_LENGTH_CODE_LENGTH);
}

uint32_t BrotliBuildHuffmanTable(HuffmanCode* root_table,
                                 int root_bits,
                                 const uint16_t* const symbol_lists,
                                 uint16_t* count) {
  HuffmanCode code;       /* current table entry */
  HuffmanCode* table;     /* next available space in table */
  int len;                /* current code length */
  int symbol;             /* symbol index in original or sorted table */
  brotli_reg_t key;       /* prefix code */
  brotli_reg_t key_step;  /* prefix code addend */
  brotli_reg_t sub_key;   /* 2nd level table prefix code */
  brotli_reg_t sub_key_step;  /* 2nd level table prefix code addend */
  int step;               /* step size to replicate values in current table */
  int table_bits;         /* key length of current table */
  int table_size;         /* size of current table */
  int total_size;         /* sum of root table size and 2nd level table sizes */
  int max_length = -1;
  int bits;
  int bits_count;

  BROTLI_DCHECK(root_bits <= BROTLI_REVERSE_BITS_MAX);
  BROTLI_DCHECK(BROTLI_HUFFMAN_MAX_CODE_LENGTH - root_bits <=
                BROTLI_REVERSE_BITS_MAX);

  while (symbol_lists[max_length] == 0xFFFF) max_length--;
  max_length += BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1;

  table = root_table;
  table_bits = root_bits;
  table_size = 1 << table_bits;
  total_size = table_size;

  /* Fill in the root table. Reduce the table size to if possible,
     and create the repetitions by memcpy. */
  if (table_bits > max_length) {
    table_bits = max_length;
    table_size = 1 << table_bits;
  }
  key = 0;
  key_step = BROTLI_REVERSE_BITS_LOWEST;
  bits = 1;
  step = 2;
  do {
    symbol = bits - (BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1);
    for (bits_count = count[bits]; bits_count != 0; --bits_count) {
      symbol = symbol_lists[symbol];
      code = ConstructHuffmanCode((uint8_t)bits, (uint16_t)symbol);
      ReplicateValue(&table[BrotliReverseBits(key)], step, table_size, code);
      key += key_step;
    }
    step <<= 1;
    key_step >>= 1;
  } while (++bits <= table_bits);

  /* If root_bits != table_bits then replicate to fill the remaining slots. */
  while (total_size != table_size) {
    memcpy(&table[table_size], &table[0],
           (size_t)table_size * sizeof(table[0]));
    table_size <<= 1;
  }

  /* Fill in 2nd level tables and add pointers to root table. */
  key_step = BROTLI_REVERSE_BITS_LOWEST >> (root_bits - 1);
  sub_key = (BROTLI_REVERSE_BITS_LOWEST << 1);
  sub_key_step = BROTLI_REVERSE_BITS_LOWEST;
  for (len = root_bits + 1, step = 2; len <= max_length; ++len) {
    symbol = len - (BROTLI_HUFFMAN_MAX_CODE_LENGTH + 1);
    for (; count[len] != 0; --count[len]) {
      if (sub_key == (BROTLI_REVERSE_BITS_LOWEST << 1U)) {
        table += table_size;
        table_bits = NextTableBitSize(count, len, root_bits);
        table_size = 1 << table_bits;
        total_size += table_size;
        sub_key = BrotliReverseBits(key);
        key += key_step;
        root_table[sub_key] = ConstructHuffmanCode(
            (uint8_t)(table_bits + root_bits),
            (uint16_t)(((size_t)(table - root_table)) - sub_key));
        sub_key = 0;
      }
      symbol = symbol_lists[symbol];
      code = ConstructHuffmanCode((uint8_t)(len - root_bits), (uint16_t)symbol);
      ReplicateValue(
          &table[BrotliReverseBits(sub_key)], step, table_size, code);
      sub_key += sub_key_step;
    }
    step <<= 1;
    sub_key_step >>= 1;
  }
  return (uint32_t)total_size;
}

uint32_t BrotliBuildSimpleHuffmanTable(HuffmanCode* table,
                                       int root_bits,
                                       uint16_t* val,
                                       uint32_t num_symbols) {
  uint32_t table_size = 1;
  const uint32_t goal_size = 1U << root_bits;
  switch (num_symbols) {
    case 0:
      table[0] = ConstructHuffmanCode(0, val[0]);
      break;
    case 1:
      if (val[1] > val[0]) {
        table[0] = ConstructHuffmanCode(1, val[0]);
        table[1] = ConstructHuffmanCode(1, val[1]);
      } else {
        table[0] = ConstructHuffmanCode(1, val[1]);
        table[1] = ConstructHuffmanCode(1, val[0]);
      }
      table_size = 2;
      break;
    case 2:
      table[0] = ConstructHuffmanCode(1, val[0]);
      table[2] = ConstructHuffmanCode(1, val[0]);
      if (val[2] > val[1]) {
        table[1] = ConstructHuffmanCode(2, val[1]);
        table[3] = ConstructHuffmanCode(2, val[2]);
      } else {
        table[1] = ConstructHuffmanCode(2, val[2]);
        table[3] = ConstructHuffmanCode(2, val[1]);
      }
      table_size = 4;
      break;
    case 3: {
      int i, k;
      for (i = 0; i < 3; ++i) {
        for (k = i + 1; k < 4; ++k) {
          if (val[k] < val[i]) {
            uint16_t t = val[k];
            val[k] = val[i];
            val[i] = t;
          }
        }
      }
      table[0] = ConstructHuffmanCode(2, val[0]);
      table[2] = ConstructHuffmanCode(2, val[1]);
      table[1] = ConstructHuffmanCode(2, val[2]);
      table[3] = ConstructHuffmanCode(2, val[3]);
      table_size = 4;
      break;
    }
    case 4: {
      if (val[3] < val[2]) {
        uint16_t t = val[3];
        val[3] = val[2];
        val[2] = t;
      }
      table[0] = ConstructHuffmanCode(1, val[0]);
      table[1] = ConstructHuffmanCode(2, val[1]);
      table[2] = ConstructHuffmanCode(1, val[0]);
      table[3] = ConstructHuffmanCode(3, val[2]);
      table[4] = ConstructHuffmanCode(1, val[0]);
      table[5] = ConstructHuffmanCode(2, val[1]);
      table[6] = ConstructHuffmanCode(1, val[0]);
      table[7] = ConstructHuffmanCode(3, val[3]);
      table_size = 8;
      break;
    }
  }
  while (table_size != goal_size) {
    memcpy(&table[table_size], &table[0],
           (size_t)table_size * sizeof(table[0]));
    table_size <<= 1;
  }
  return goal_size;
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
