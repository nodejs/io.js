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
#ifndef NGHTTP3_RCBUF_H
#define NGHTTP3_RCBUF_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

struct nghttp3_rcbuf {
  /* mem is the memory allocator that allocates memory for this
     object. */
  const nghttp3_mem *mem;
  /* The pointer to the underlying buffer */
  uint8_t *base;
  /* Size of buffer pointed by |base|. */
  size_t len;
  /* Reference count */
  int32_t ref;
};

/*
 * Allocates nghttp3_rcbuf object with |size| as initial buffer size.
 * When the function succeeds, the reference count becomes 1.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM:
 *     Out of memory.
 */
int nghttp3_rcbuf_new(nghttp3_rcbuf **rcbuf_ptr, size_t size,
                      const nghttp3_mem *mem);

/*
 * Like nghttp3_rcbuf_new(), but initializes the buffer with |src| of
 * length |srclen|.  This function allocates additional byte at the
 * end and puts '\0' into it, so that the resulting buffer could be
 * used as NULL-terminated string.  Still (*rcbuf_ptr)->len equals to
 * |srclen|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM:
 *     Out of memory.
 */
int nghttp3_rcbuf_new2(nghttp3_rcbuf **rcbuf_ptr, const uint8_t *src,
                       size_t srclen, const nghttp3_mem *mem);

/*
 * Frees |rcbuf| itself, regardless of its reference cout.
 */
void nghttp3_rcbuf_del(nghttp3_rcbuf *rcbuf);

#endif /* !defined(NGHTTP3_RCBUF_H) */
