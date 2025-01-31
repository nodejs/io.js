/*
 * ngtcp2
 *
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
#ifndef NGTCP2_OBJALLOC_H
#define NGTCP2_OBJALLOC_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_balloc.h"
#include "ngtcp2_opl.h"
#include "ngtcp2_macro.h"
#include "ngtcp2_mem.h"

/*
 * ngtcp2_objalloc combines ngtcp2_balloc and ngtcp2_opl, and provides
 * an object pool with the custom allocator to reduce the allocation
 * and deallocation overheads for small objects.
 */
typedef struct ngtcp2_objalloc {
  ngtcp2_balloc balloc;
  ngtcp2_opl opl;
} ngtcp2_objalloc;

/*
 * ngtcp2_objalloc_init initializes |objalloc|.  |blklen| is directly
 * passed to ngtcp2_balloc_init.
 */
void ngtcp2_objalloc_init(ngtcp2_objalloc *objalloc, size_t blklen,
                          const ngtcp2_mem *mem);

/*
 * ngtcp2_objalloc_free releases all allocated resources.
 */
void ngtcp2_objalloc_free(ngtcp2_objalloc *objalloc);

/*
 * ngtcp2_objalloc_clear releases all allocated resources and
 * initializes its state.
 */
void ngtcp2_objalloc_clear(ngtcp2_objalloc *objalloc);

#ifndef NOMEMPOOL
#  define ngtcp2_objalloc_decl(NAME, TYPE, OPLENTFIELD)                        \
    inline static void ngtcp2_objalloc_##NAME##_init(                          \
      ngtcp2_objalloc *objalloc, size_t nmemb, const ngtcp2_mem *mem) {        \
      ngtcp2_objalloc_init(                                                    \
        objalloc, ((sizeof(TYPE) + 0xfu) & ~(uintptr_t)0xfu) * nmemb, mem);    \
    }                                                                          \
                                                                               \
    TYPE *ngtcp2_objalloc_##NAME##_get(ngtcp2_objalloc *objalloc);             \
                                                                               \
    TYPE *ngtcp2_objalloc_##NAME##_len_get(ngtcp2_objalloc *objalloc,          \
                                           size_t len);                        \
                                                                               \
    inline static void ngtcp2_objalloc_##NAME##_release(                       \
      ngtcp2_objalloc *objalloc, TYPE *obj) {                                  \
      ngtcp2_opl_push(&objalloc->opl, &obj->OPLENTFIELD);                      \
    }

#  define ngtcp2_objalloc_def(NAME, TYPE, OPLENTFIELD)                         \
    TYPE *ngtcp2_objalloc_##NAME##_get(ngtcp2_objalloc *objalloc) {            \
      ngtcp2_opl_entry *oplent = ngtcp2_opl_pop(&objalloc->opl);               \
      TYPE *obj;                                                               \
      int rv;                                                                  \
                                                                               \
      if (!oplent) {                                                           \
        rv =                                                                   \
          ngtcp2_balloc_get(&objalloc->balloc, (void **)&obj, sizeof(TYPE));   \
        if (rv != 0) {                                                         \
          return NULL;                                                         \
        }                                                                      \
                                                                               \
        return obj;                                                            \
      }                                                                        \
                                                                               \
      return ngtcp2_struct_of(oplent, TYPE, OPLENTFIELD);                      \
    }                                                                          \
                                                                               \
    TYPE *ngtcp2_objalloc_##NAME##_len_get(ngtcp2_objalloc *objalloc,          \
                                           size_t len) {                       \
      ngtcp2_opl_entry *oplent = ngtcp2_opl_pop(&objalloc->opl);               \
      TYPE *obj;                                                               \
      int rv;                                                                  \
                                                                               \
      if (!oplent) {                                                           \
        rv = ngtcp2_balloc_get(&objalloc->balloc, (void **)&obj, len);         \
        if (rv != 0) {                                                         \
          return NULL;                                                         \
        }                                                                      \
                                                                               \
        return obj;                                                            \
      }                                                                        \
                                                                               \
      return ngtcp2_struct_of(oplent, TYPE, OPLENTFIELD);                      \
    }
#else /* defined(NOMEMPOOL) */
#  define ngtcp2_objalloc_decl(NAME, TYPE, OPLENTFIELD)                        \
    inline static void ngtcp2_objalloc_##NAME##_init(                          \
      ngtcp2_objalloc *objalloc, size_t nmemb, const ngtcp2_mem *mem) {        \
      ngtcp2_objalloc_init(                                                    \
        objalloc, ((sizeof(TYPE) + 0xfu) & ~(uintptr_t)0xfu) * nmemb, mem);    \
    }                                                                          \
                                                                               \
    inline static TYPE *ngtcp2_objalloc_##NAME##_get(                          \
      ngtcp2_objalloc *objalloc) {                                             \
      return ngtcp2_mem_malloc(objalloc->balloc.mem, sizeof(TYPE));            \
    }                                                                          \
                                                                               \
    inline static TYPE *ngtcp2_objalloc_##NAME##_len_get(                      \
      ngtcp2_objalloc *objalloc, size_t len) {                                 \
      return ngtcp2_mem_malloc(objalloc->balloc.mem, len);                     \
    }                                                                          \
                                                                               \
    inline static void ngtcp2_objalloc_##NAME##_release(                       \
      ngtcp2_objalloc *objalloc, TYPE *obj) {                                  \
      ngtcp2_mem_free(objalloc->balloc.mem, obj);                              \
    }

#  define ngtcp2_objalloc_def(NAME, TYPE, OPLENTFIELD)
#endif /* defined(NOMEMPOOL) */

#endif /* !defined(NGTCP2_OBJALLOC_H) */
