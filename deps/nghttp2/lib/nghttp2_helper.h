/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
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
#ifndef NGHTTP2_HELPER_H
#define NGHTTP2_HELPER_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <stddef.h>

#include <nghttp2/nghttp2.h>
#include "nghttp2_mem.h"

#define nghttp2_max_def(SUFFIX, T)                                             \
  static inline T nghttp2_max_##SUFFIX(T a, T b) { return a < b ? b : a; }

nghttp2_max_def(int8, int8_t);
nghttp2_max_def(int16, int16_t);
nghttp2_max_def(int32, int32_t);
nghttp2_max_def(int64, int64_t);
nghttp2_max_def(uint8, uint8_t);
nghttp2_max_def(uint16, uint16_t);
nghttp2_max_def(uint32, uint32_t);
nghttp2_max_def(uint64, uint64_t);
nghttp2_max_def(size, size_t);

#define nghttp2_min_def(SUFFIX, T)                                             \
  static inline T nghttp2_min_##SUFFIX(T a, T b) { return a < b ? a : b; }

nghttp2_min_def(int8, int8_t);
nghttp2_min_def(int16, int16_t);
nghttp2_min_def(int32, int32_t);
nghttp2_min_def(int64, int64_t);
nghttp2_min_def(uint8, uint8_t);
nghttp2_min_def(uint16, uint16_t);
nghttp2_min_def(uint32, uint32_t);
nghttp2_min_def(uint64, uint64_t);
nghttp2_min_def(size, size_t);

#define lstreq(A, B, N) ((sizeof((A)) - 1) == (N) && memcmp((A), (B), (N)) == 0)

#define nghttp2_struct_of(ptr, type, member)                                   \
  ((type *)(void *)((char *)(ptr)-offsetof(type, member)))

/*
 * Copies 2 byte unsigned integer |n| in host byte order to |buf| in
 * network byte order.
 */
void nghttp2_put_uint16be(uint8_t *buf, uint16_t n);

/*
 * Copies 4 byte unsigned integer |n| in host byte order to |buf| in
 * network byte order.
 */
void nghttp2_put_uint32be(uint8_t *buf, uint32_t n);

/*
 * Retrieves 2 byte unsigned integer stored in |data| in network byte
 * order and returns it in host byte order.
 */
uint16_t nghttp2_get_uint16(const uint8_t *data);

/*
 * Retrieves 4 byte unsigned integer stored in |data| in network byte
 * order and returns it in host byte order.
 */
uint32_t nghttp2_get_uint32(const uint8_t *data);

void nghttp2_downcase(uint8_t *s, size_t len);

/*
 * Adjusts |*local_window_size_ptr|, |*recv_window_size_ptr|,
 * |*recv_reduction_ptr| with |*delta_ptr| which is the
 * WINDOW_UPDATE's window_size_increment sent from local side. If
 * |delta| is strictly larger than |*recv_window_size_ptr|,
 * |*local_window_size_ptr| is increased by delta -
 * *recv_window_size_ptr. If |delta| is negative,
 * |*local_window_size_ptr| is decreased by delta.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_FLOW_CONTROL
 *     local_window_size overflow or gets negative.
 */
int nghttp2_adjust_local_window_size(int32_t *local_window_size_ptr,
                                     int32_t *recv_window_size_ptr,
                                     int32_t *recv_reduction_ptr,
                                     int32_t *delta_ptr);

/*
 * This function works like nghttp2_adjust_local_window_size().  The
 * difference is that this function assumes *delta_ptr >= 0, and
 * *recv_window_size_ptr is not decreased by *delta_ptr.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_FLOW_CONTROL
 *     local_window_size overflow or gets negative.
 */
int nghttp2_increase_local_window_size(int32_t *local_window_size_ptr,
                                       int32_t *recv_window_size_ptr,
                                       int32_t *recv_reduction_ptr,
                                       int32_t *delta_ptr);

/*
 * Returns non-zero if the function decided that WINDOW_UPDATE should
 * be sent.
 */
int nghttp2_should_send_window_update(int32_t local_window_size,
                                      int32_t recv_window_size);

/*
 * Copies the buffer |src| of length |len| to the destination pointed
 * by the |dest|, assuming that the |dest| is at lest |len| bytes long
 * . Returns dest + len.
 */
uint8_t *nghttp2_cpymem(uint8_t *dest, const void *src, size_t len);

#endif /* NGHTTP2_HELPER_H */
