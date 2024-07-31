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
#ifndef NGHTTP3_OBJALLOC_H
#define NGHTTP3_OBJALLOC_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp3/nghttp3.h>

#include "nghttp3_balloc.h"
#include "nghttp3_opl.h"
#include "nghttp3_macro.h"
#include "nghttp3_mem.h"

/*
 * nghttp3_objalloc combines nghttp3_balloc and nghttp3_opl, and
 * provides an object pool with the custom allocator to reduce the
 * allocation and deallocation overheads for small objects.
 */
typedef struct nghttp3_objalloc {
  nghttp3_balloc balloc;
  nghttp3_opl opl;
} nghttp3_objalloc;

/*
 * nghttp3_objalloc_init initializes |objalloc|.  |blklen| is directly
 * passed to nghttp3_balloc_init.
 */
void nghttp3_objalloc_init(nghttp3_objalloc *objalloc, size_t blklen,
                           const nghttp3_mem *mem);

/*
 * nghttp3_objalloc_free releases all allocated resources.
 */
void nghttp3_objalloc_free(nghttp3_objalloc *objalloc);

/*
 * nghttp3_objalloc_clear releases all allocated resources and
 * initializes its state.
 */
void nghttp3_objalloc_clear(nghttp3_objalloc *objalloc);

#ifndef NOMEMPOOL
#  define nghttp3_objalloc_decl(NAME, TYPE, OPLENTFIELD)                       \
    inline static void nghttp3_objalloc_##NAME##_init(                         \
        nghttp3_objalloc *objalloc, size_t nmemb, const nghttp3_mem *mem) {    \
      nghttp3_objalloc_init(                                                   \
          objalloc, ((sizeof(TYPE) + 0xfu) & ~(uintptr_t)0xfu) * nmemb, mem);  \
    }                                                                          \
                                                                               \
    TYPE *nghttp3_objalloc_##NAME##_get(nghttp3_objalloc *objalloc);           \
                                                                               \
    TYPE *nghttp3_objalloc_##NAME##_len_get(nghttp3_objalloc *objalloc,        \
                                            size_t len);                       \
                                                                               \
    inline static void nghttp3_objalloc_##NAME##_release(                      \
        nghttp3_objalloc *objalloc, TYPE *obj) {                               \
      nghttp3_opl_push(&objalloc->opl, &obj->OPLENTFIELD);                     \
    }

#  define nghttp3_objalloc_def(NAME, TYPE, OPLENTFIELD)                        \
    TYPE *nghttp3_objalloc_##NAME##_get(nghttp3_objalloc *objalloc) {          \
      nghttp3_opl_entry *oplent = nghttp3_opl_pop(&objalloc->opl);             \
      TYPE *obj;                                                               \
      int rv;                                                                  \
                                                                               \
      if (!oplent) {                                                           \
        rv = nghttp3_balloc_get(&objalloc->balloc, (void **)&obj,              \
                                sizeof(TYPE));                                 \
        if (rv != 0) {                                                         \
          return NULL;                                                         \
        }                                                                      \
                                                                               \
        return obj;                                                            \
      }                                                                        \
                                                                               \
      return nghttp3_struct_of(oplent, TYPE, OPLENTFIELD);                     \
    }                                                                          \
                                                                               \
    TYPE *nghttp3_objalloc_##NAME##_len_get(nghttp3_objalloc *objalloc,        \
                                            size_t len) {                      \
      nghttp3_opl_entry *oplent = nghttp3_opl_pop(&objalloc->opl);             \
      TYPE *obj;                                                               \
      int rv;                                                                  \
                                                                               \
      if (!oplent) {                                                           \
        rv = nghttp3_balloc_get(&objalloc->balloc, (void **)&obj, len);        \
        if (rv != 0) {                                                         \
          return NULL;                                                         \
        }                                                                      \
                                                                               \
        return obj;                                                            \
      }                                                                        \
                                                                               \
      return nghttp3_struct_of(oplent, TYPE, OPLENTFIELD);                     \
    }
#else /* NOMEMPOOL */
#  define nghttp3_objalloc_decl(NAME, TYPE, OPLENTFIELD)                       \
    inline static void nghttp3_objalloc_##NAME##_init(                         \
        nghttp3_objalloc *objalloc, size_t nmemb, const nghttp3_mem *mem) {    \
      nghttp3_objalloc_init(                                                   \
          objalloc, ((sizeof(TYPE) + 0xfu) & ~(uintptr_t)0xfu) * nmemb, mem);  \
    }                                                                          \
                                                                               \
    inline static TYPE *nghttp3_objalloc_##NAME##_get(                         \
        nghttp3_objalloc *objalloc) {                                          \
      return nghttp3_mem_malloc(objalloc->balloc.mem, sizeof(TYPE));           \
    }                                                                          \
                                                                               \
    inline static TYPE *nghttp3_objalloc_##NAME##_len_get(                     \
        nghttp3_objalloc *objalloc, size_t len) {                              \
      return nghttp3_mem_malloc(objalloc->balloc.mem, len);                    \
    }                                                                          \
                                                                               \
    inline static void nghttp3_objalloc_##NAME##_release(                      \
        nghttp3_objalloc *objalloc, TYPE *obj) {                               \
      nghttp3_mem_free(objalloc->balloc.mem, obj);                             \
    }

#  define nghttp3_objalloc_def(NAME, TYPE, OPLENTFIELD)
#endif /* NOMEMPOOL */

#endif /* NGHTTP3_OBJALLOC_H */
