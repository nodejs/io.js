/* MIT License
 *
 * Copyright (c) 2023 Brad House
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ares_private.h"

/* Uses public domain code snippets from
 * http://graphics.stanford.edu/~seander/bithacks.html */

static unsigned int ares__round_up_pow2_u32(unsigned int n)
{
  /* NOTE: if already a power of 2, will return itself, not the next */
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n++;
  return n;
}

static ares_int64_t ares__round_up_pow2_u64(ares_int64_t n)
{
  /* NOTE: if already a power of 2, will return itself, not the next */
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  n++;
  return n;
}

ares_bool_t ares__is_64bit(void)
{
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4127)
#endif

  return (sizeof(size_t) == 4) ? ARES_FALSE : ARES_TRUE;

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
}

size_t ares__round_up_pow2(size_t n)
{
  if (ares__is_64bit()) {
    return (size_t)ares__round_up_pow2_u64((ares_int64_t)n);
  }

  return (size_t)ares__round_up_pow2_u32((unsigned int)n);
}

size_t ares__log2(size_t n)
{
  static const unsigned char tab32[32] = { 0,  1,  28, 2,  29, 14, 24, 3,
                                           30, 22, 20, 15, 25, 17, 4,  8,
                                           31, 27, 13, 23, 21, 19, 16, 7,
                                           26, 12, 18, 6,  11, 5,  10, 9 };
  static const unsigned char tab64[64] = {
    63, 0,  58, 1,  59, 47, 53, 2,  60, 39, 48, 27, 54, 33, 42, 3,
    61, 51, 37, 40, 49, 18, 28, 20, 55, 30, 34, 11, 43, 14, 22, 4,
    62, 57, 46, 52, 38, 26, 32, 41, 50, 36, 17, 19, 29, 10, 13, 21,
    56, 45, 25, 31, 35, 16, 9,  12, 44, 24, 15, 8,  23, 7,  6,  5
  };

  if (!ares__is_64bit()) {
    return tab32[(n * 0x077CB531) >> 27];
  }

  return tab64[(n * 0x07EDD5E59A4E28C2) >> 58];
}

/* x^y */
size_t ares__pow(size_t x, size_t y)
{
  size_t res = 1;

  while (y > 0) {
    /* If y is odd, multiply x with result */
    if (y & 1) {
      res = res * x;
    }

    /* y must be even now */
    y = y >> 1; /* y /= 2; */
    x = x * x;  /* x^2 */
  }

  return res;
}

size_t ares__count_digits(size_t n)
{
  size_t digits;

  for (digits = 0; n > 0; digits++) {
    n /= 10;
  }
  if (digits == 0) {
    digits = 1;
  }

  return digits;
}

size_t ares__count_hexdigits(size_t n)
{
  size_t digits;

  for (digits = 0; n > 0; digits++) {
    n /= 16;
  }
  if (digits == 0) {
    digits = 1;
  }

  return digits;
}

unsigned char ares__count_bits_u8(unsigned char x)
{
  /* Implementation obtained from:
   * http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetTable */
#define B2(n) n, n + 1, n + 1, n + 2
#define B4(n) B2(n), B2(n + 1), B2(n + 1), B2(n + 2)
#define B6(n) B4(n), B4(n + 1), B4(n + 1), B4(n + 2)
  static const unsigned char lookup[256] = { B6(0), B6(1), B6(1), B6(2) };
  return lookup[x];
}
