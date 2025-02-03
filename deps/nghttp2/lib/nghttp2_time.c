/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2023 nghttp2 contributors
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
#include "nghttp2_time.h"

#ifdef HAVE_WINDOWS_H
#  include <windows.h>
#endif /* HAVE_WINDOWS_H */

#include <time.h>

#if !defined(HAVE_GETTICKCOUNT64) || defined(__CYGWIN__)
static uint64_t time_now_sec(void) {
  time_t t = time(NULL);

  if (t == -1) {
    return 0;
  }

  return (uint64_t)t;
}
#endif /* !HAVE_GETTICKCOUNT64 || __CYGWIN__ */

#if defined(HAVE_GETTICKCOUNT64) && !defined(__CYGWIN__)
uint64_t nghttp2_time_now_sec(void) { return GetTickCount64() / 1000; }
#elif defined(HAVE_CLOCK_GETTIME) && defined(HAVE_DECL_CLOCK_MONOTONIC) &&     \
  HAVE_DECL_CLOCK_MONOTONIC
uint64_t nghttp2_time_now_sec(void) {
  struct timespec tp;
  int rv = clock_gettime(CLOCK_MONOTONIC, &tp);

  if (rv == -1) {
    return time_now_sec();
  }

  return (uint64_t)tp.tv_sec;
}
#else  /* (!HAVE_CLOCK_GETTIME || !HAVE_DECL_CLOCK_MONOTONIC) &&               \
          (!HAVE_GETTICKCOUNT64 || __CYGWIN__)) */
uint64_t nghttp2_time_now_sec(void) { return time_now_sec(); }
#endif /* (!HAVE_CLOCK_GETTIME || !HAVE_DECL_CLOCK_MONOTONIC) &&               \
         (!HAVE_GETTICKCOUNT64 || __CYGWIN__)) */
