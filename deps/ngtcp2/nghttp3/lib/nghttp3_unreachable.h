/*
 * nghttp3
 *
 * Copyright (c) 2022 nghttp3 contributors
 * Copyright (c) 2022 ngtcp2 contributors
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
#ifndef NGHTTP3_UNREACHABLE_H
#define NGHTTP3_UNREACHABLE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

#ifdef __FILE_NAME__
#  define NGHTTP3_FILE_NAME __FILE_NAME__
#else /* !defined(__FILE_NAME__) */
#  define NGHTTP3_FILE_NAME "(file)"
#endif /* !defined(__FILE_NAME__) */

#define nghttp3_unreachable()                                                  \
  nghttp3_unreachable_fail(NGHTTP3_FILE_NAME, __LINE__, __func__)

#ifdef _MSC_VER
__declspec(noreturn)
#endif /* defined(_MSC_VER) */
    void nghttp3_unreachable_fail(const char *file, int line, const char *func)
#ifndef _MSC_VER
        __attribute__((noreturn))
#endif /* !defined(_MSC_VER) */
        ;

#endif /* !defined(NGHTTP3_UNREACHABLE_H) */
