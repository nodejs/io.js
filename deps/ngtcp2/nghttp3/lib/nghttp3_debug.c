/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2016 nghttp2 contributors
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
#include "nghttp3_debug.h"

#include <stdio.h>

#ifdef DEBUGBUILD

static void nghttp3_default_debug_vfprintf_callback(const char *fmt,
                                                    va_list args) {
  vfprintf(stderr, fmt, args);
}

static nghttp3_debug_vprintf_callback static_debug_vprintf_callback =
  nghttp3_default_debug_vfprintf_callback;

void nghttp3_debug_vprintf(const char *format, ...) {
  if (static_debug_vprintf_callback) {
    va_list args;
    va_start(args, format);
    static_debug_vprintf_callback(format, args);
    va_end(args);
  }
}

void nghttp3_set_debug_vprintf_callback(
  nghttp3_debug_vprintf_callback debug_vprintf_callback) {
  static_debug_vprintf_callback = debug_vprintf_callback;
}

#else /* !defined(DEBUGBUILD) */

void nghttp3_set_debug_vprintf_callback(
  nghttp3_debug_vprintf_callback debug_vprintf_callback) {
  (void)debug_vprintf_callback;
}

#endif /* !defined(DEBUGBUILD) */
