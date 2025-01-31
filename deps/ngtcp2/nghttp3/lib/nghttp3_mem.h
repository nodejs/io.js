/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2017 ngtcp2 contributors
 * Copyright (c) 2014 nghttp2 contributors
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
#ifndef NGHTTP3_MEM_H
#define NGHTTP3_MEM_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

/* Convenient wrapper functions to call allocator function in
   |mem|. */
#ifndef MEMDEBUG
void *nghttp3_mem_malloc(const nghttp3_mem *mem, size_t size);
void nghttp3_mem_free(const nghttp3_mem *mem, void *ptr);
void *nghttp3_mem_calloc(const nghttp3_mem *mem, size_t nmemb, size_t size);
void *nghttp3_mem_realloc(const nghttp3_mem *mem, void *ptr, size_t size);
#else /* defined(MEMDEBUG) */
void *nghttp3_mem_malloc_debug(const nghttp3_mem *mem, size_t size,
                               const char *func, const char *file, size_t line);

#  define nghttp3_mem_malloc(MEM, SIZE)                                        \
    nghttp3_mem_malloc_debug((MEM), (SIZE), __func__, __FILE__, __LINE__)

void nghttp3_mem_free_debug(const nghttp3_mem *mem, void *ptr, const char *func,
                            const char *file, size_t line);

#  define nghttp3_mem_free(MEM, PTR)                                           \
    nghttp3_mem_free_debug((MEM), (PTR), __func__, __FILE__, __LINE__)

void nghttp3_mem_free2_debug(nghttp3_free free_func, void *ptr, void *user_data,
                             const char *func, const char *file, size_t line);

#  define nghttp3_mem_free2(FREE_FUNC, PTR, USER_DATA)                         \
    nghttp3_mem_free2_debug((FREE_FUNC), (PTR), (USER_DATA), __func__,         \
                            __FILE__, __LINE__)

void *nghttp3_mem_calloc_debug(const nghttp3_mem *mem, size_t nmemb,
                               size_t size, const char *func, const char *file,
                               size_t line);

#  define nghttp3_mem_calloc(MEM, NMEMB, SIZE)                                 \
    nghttp3_mem_calloc_debug((MEM), (NMEMB), (SIZE), __func__, __FILE__,       \
                             __LINE__)

void *nghttp3_mem_realloc_debug(const nghttp3_mem *mem, void *ptr, size_t size,
                                const char *func, const char *file,
                                size_t line);

#  define nghttp3_mem_realloc(MEM, PTR, SIZE)                                  \
    nghttp3_mem_realloc_debug((MEM), (PTR), (SIZE), __func__, __FILE__,        \
                              __LINE__)
#endif /* defined(MEMDEBUG) */

#endif /* !defined(NGHTTP3_MEM_H) */
