/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2017 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef NGHTTP3_MACRO_H
#define NGHTTP3_MACRO_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <stddef.h>

#include <nghttp3/nghttp3.h>

#define nghttp3_struct_of(ptr, type, member)                                   \
  ((type *)(void *)((char *)(ptr) - offsetof(type, member)))

#define nghttp3_arraylen(A) (sizeof(A) / sizeof(*(A)))

#define lstreq(A, B, N) ((sizeof((A)) - 1) == (N) && memcmp((A), (B), (N)) == 0)

/* NGHTTP3_MAX_VARINT` is the maximum value which can be encoded in
   variable-length integer encoding. */
#define NGHTTP3_MAX_VARINT ((1ULL << 62) - 1)

#define nghttp3_max_def(SUFFIX, T)                                             \
  static inline T nghttp3_max_##SUFFIX(T a, T b) { return a < b ? b : a; }

nghttp3_max_def(int8, int8_t);
nghttp3_max_def(int16, int16_t);
nghttp3_max_def(int32, int32_t);
nghttp3_max_def(int64, int64_t);
nghttp3_max_def(uint8, uint8_t);
nghttp3_max_def(uint16, uint16_t);
nghttp3_max_def(uint32, uint32_t);
nghttp3_max_def(uint64, uint64_t);
nghttp3_max_def(size, size_t);

#define nghttp3_min_def(SUFFIX, T)                                             \
  static inline T nghttp3_min_##SUFFIX(T a, T b) { return a < b ? a : b; }

nghttp3_min_def(int8, int8_t);
nghttp3_min_def(int16, int16_t);
nghttp3_min_def(int32, int32_t);
nghttp3_min_def(int64, int64_t);
nghttp3_min_def(uint8, uint8_t);
nghttp3_min_def(uint16, uint16_t);
nghttp3_min_def(uint32, uint32_t);
nghttp3_min_def(uint64, uint64_t);
nghttp3_min_def(size, size_t);

#endif /* !defined(NGHTTP3_MACRO_H) */
