/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2018 ngtcp2 contributors
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
#ifndef NGHTTP3_KSL_H
#define NGHTTP3_KSL_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <stdlib.h>

#include <nghttp3/nghttp3.h>

#include "nghttp3_objalloc.h"

#define NGHTTP3_KSL_DEGR 16
/* NGHTTP3_KSL_MAX_NBLK is the maximum number of nodes which a single
   block can contain. */
#define NGHTTP3_KSL_MAX_NBLK (2 * NGHTTP3_KSL_DEGR - 1)
/* NGHTTP3_KSL_MIN_NBLK is the minimum number of nodes which a single
   block other than root must contain. */
#define NGHTTP3_KSL_MIN_NBLK (NGHTTP3_KSL_DEGR - 1)

/*
 * nghttp3_ksl_key represents key in nghttp3_ksl.
 */
typedef void nghttp3_ksl_key;

typedef struct nghttp3_ksl_node nghttp3_ksl_node;

typedef struct nghttp3_ksl_blk nghttp3_ksl_blk;

/*
 * nghttp3_ksl_node is a node which contains either nghttp3_ksl_blk or
 * opaque data.  If a node is an internal node, it contains
 * nghttp3_ksl_blk.  Otherwise, it has data.  The key is stored at the
 * location starting at key.
 */
struct nghttp3_ksl_node {
  union {
    nghttp3_ksl_blk *blk;
    void *data;
  };
  union {
    uint64_t align;
    /* key is a buffer to include key associated to this node.
       Because the length of key is unknown until nghttp3_ksl_init is
       called, the actual buffer will be allocated after this
       field. */
    uint8_t key[1];
  };
};

/*
 * nghttp3_ksl_blk contains nghttp3_ksl_node objects.
 */
struct nghttp3_ksl_blk {
  union {
    struct {
      /* next points to the next block if leaf field is nonzero. */
      nghttp3_ksl_blk *next;
      /* prev points to the previous block if leaf field is
         nonzero. */
      nghttp3_ksl_blk *prev;
      /* n is the number of nodes this object contains in nodes. */
      uint32_t n;
      /* leaf is nonzero if this block contains leaf nodes. */
      uint32_t leaf;
      union {
        uint64_t align;
        /* nodes is a buffer to contain NGHTTP3_KSL_MAX_NBLK
           nghttp3_ksl_node objects.  Because nghttp3_ksl_node object
           is allocated along with the additional variable length key
           storage, the size of buffer is unknown until
           nghttp3_ksl_init is called. */
        uint8_t nodes[1];
      };
    };

    nghttp3_opl_entry oplent;
  };
};

nghttp3_objalloc_decl(ksl_blk, nghttp3_ksl_blk, oplent)

/*
 * nghttp3_ksl_compar is a function type which returns nonzero if key
 * |lhs| should be placed before |rhs|.  It returns 0 otherwise.
 */
typedef int (*nghttp3_ksl_compar)(const nghttp3_ksl_key *lhs,
                                  const nghttp3_ksl_key *rhs);

typedef struct nghttp3_ksl nghttp3_ksl;

/*
 * nghttp3_ksl_search is a function to search for the first element in
 * |blk|->nodes which is not ordered before |key|.  It returns the
 * index of such element.  It returns |blk|->n if there is no such
 * element.
 */
typedef size_t (*nghttp3_ksl_search)(const nghttp3_ksl *ksl,
                                     nghttp3_ksl_blk *blk,
                                     const nghttp3_ksl_key *key);

/*
 * nghttp3_ksl_search_def is a macro to implement nghttp3_ksl_search
 * with COMPAR which is supposed to be nghttp3_ksl_compar.
 */
#define nghttp3_ksl_search_def(NAME, COMPAR)                                   \
  static size_t ksl_##NAME##_search(const nghttp3_ksl *ksl,                    \
                                    nghttp3_ksl_blk *blk,                      \
                                    const nghttp3_ksl_key *key) {              \
    size_t i;                                                                  \
    nghttp3_ksl_node *node;                                                    \
                                                                               \
    for (i = 0, node = (nghttp3_ksl_node *)(void *)blk->nodes;                 \
         i < blk->n && COMPAR((nghttp3_ksl_key *)node->key, key); ++i,         \
        node = (nghttp3_ksl_node *)(void *)((uint8_t *)node + ksl->nodelen))   \
      ;                                                                        \
                                                                               \
    return i;                                                                  \
  }

typedef struct nghttp3_ksl_it nghttp3_ksl_it;

/*
 * nghttp3_ksl_it is a bidirectional iterator to iterate nodes.
 */
struct nghttp3_ksl_it {
  const nghttp3_ksl *ksl;
  nghttp3_ksl_blk *blk;
  size_t i;
};

/*
 * nghttp3_ksl is a deterministic paged skip list.
 */
struct nghttp3_ksl {
  nghttp3_objalloc blkalloc;
  /* head points to the root block. */
  nghttp3_ksl_blk *head;
  /* front points to the first leaf block. */
  nghttp3_ksl_blk *front;
  /* back points to the last leaf block. */
  nghttp3_ksl_blk *back;
  nghttp3_ksl_compar compar;
  nghttp3_ksl_search search;
  /* n is the number of elements stored. */
  size_t n;
  /* keylen is the size of key */
  size_t keylen;
  /* nodelen is the actual size of nghttp3_ksl_node including key
     storage. */
  size_t nodelen;
};

/*
 * nghttp3_ksl_init initializes |ksl|.  |compar| specifies compare
 * function.  |search| is a search function which must use |compar|.
 * |keylen| is the length of key and must be at least
 * sizeof(uint64_t).
 */
void nghttp3_ksl_init(nghttp3_ksl *ksl, nghttp3_ksl_compar compar,
                      nghttp3_ksl_search search, size_t keylen,
                      const nghttp3_mem *mem);

/*
 * nghttp3_ksl_free frees resources allocated for |ksl|.  If |ksl| is
 * NULL, this function does nothing.  It does not free the memory
 * region pointed by |ksl| itself.
 */
void nghttp3_ksl_free(nghttp3_ksl *ksl);

/*
 * nghttp3_ksl_insert inserts |key| with its associated |data|.  On
 * successful insertion, the iterator points to the inserted node is
 * stored in |*it| if |it| is not NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_NOMEM
 *     Out of memory.
 * NGHTTP3_ERR_INVALID_ARGUMENT
 *     |key| already exists.
 */
int nghttp3_ksl_insert(nghttp3_ksl *ksl, nghttp3_ksl_it *it,
                       const nghttp3_ksl_key *key, void *data);

/*
 * nghttp3_ksl_remove removes the |key| from |ksl|.
 *
 * This function assigns the iterator to |*it|, which points to the
 * node which is located at the right next of the removed node if |it|
 * is not NULL.  If |key| is not found, no deletion takes place and
 * the return value of nghttp3_ksl_end(ksl) is assigned to |*it| if
 * |it| is not NULL.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP3_ERR_INVALID_ARGUMENT
 *     |key| does not exist.
 */
int nghttp3_ksl_remove(nghttp3_ksl *ksl, nghttp3_ksl_it *it,
                       const nghttp3_ksl_key *key);

/*
 * nghttp3_ksl_remove_hint removes the |key| from |ksl|.  |hint| must
 * point to the same node denoted by |key|.  |hint| is used to remove
 * a node efficiently in some cases.  Other than that, it behaves
 * exactly like nghttp3_ksl_remove.  |it| and |hint| can point to the
 * same object.
 */
int nghttp3_ksl_remove_hint(nghttp3_ksl *ksl, nghttp3_ksl_it *it,
                            const nghttp3_ksl_it *hint,
                            const nghttp3_ksl_key *key);

/*
 * nghttp3_ksl_lower_bound returns the iterator which points to the
 * first node which has the key which is equal to |key| or the last
 * node which satisfies !compar(&node->key, key).  If there is no such
 * node, it returns the iterator which satisfies
 * nghttp3_ksl_it_end(it) != 0.
 */
nghttp3_ksl_it nghttp3_ksl_lower_bound(const nghttp3_ksl *ksl,
                                       const nghttp3_ksl_key *key);

/*
 * nghttp3_ksl_lower_bound_search works like nghttp3_ksl_lower_bound,
 * but it takes custom function |search| to do lower bound search.
 */
nghttp3_ksl_it nghttp3_ksl_lower_bound_search(const nghttp3_ksl *ksl,
                                              const nghttp3_ksl_key *key,
                                              nghttp3_ksl_search search);

/*
 * nghttp3_ksl_update_key replaces the key of nodes which has
 * |old_key| with |new_key|.  |new_key| must be strictly greater than
 * the previous node and strictly smaller than the next node.
 */
void nghttp3_ksl_update_key(nghttp3_ksl *ksl, const nghttp3_ksl_key *old_key,
                            const nghttp3_ksl_key *new_key);

/*
 * nghttp3_ksl_begin returns the iterator which points to the first
 * node.  If there is no node in |ksl|, it returns the iterator which
 * satisfies both nghttp3_ksl_it_begin(it) != 0 and
 * nghttp3_ksl_it_end(it) != 0.
 */
nghttp3_ksl_it nghttp3_ksl_begin(const nghttp3_ksl *ksl);

/*
 * nghttp3_ksl_end returns the iterator which points to the node
 * following the last node.  The returned object satisfies
 * nghttp3_ksl_it_end().  If there is no node in |ksl|, it returns the
 * iterator which satisfies nghttp3_ksl_it_begin(it) != 0 and
 * nghttp3_ksl_it_end(it) != 0.
 */
nghttp3_ksl_it nghttp3_ksl_end(const nghttp3_ksl *ksl);

/*
 * nghttp3_ksl_len returns the number of elements stored in |ksl|.
 */
size_t nghttp3_ksl_len(const nghttp3_ksl *ksl);

/*
 * nghttp3_ksl_clear removes all elements stored in |ksl|.
 */
void nghttp3_ksl_clear(nghttp3_ksl *ksl);

/*
 * nghttp3_ksl_nth_node returns the |n|th node under |blk|.
 */
#define nghttp3_ksl_nth_node(KSL, BLK, N)                                      \
  ((nghttp3_ksl_node *)(void *)((BLK)->nodes + (KSL)->nodelen * (N)))

#ifndef WIN32
/*
 * nghttp3_ksl_print prints its internal state in stderr.  It assumes
 * that the key is of type int64_t.  This function should be used for
 * the debugging purpose only.
 */
void nghttp3_ksl_print(const nghttp3_ksl *ksl);
#endif /* !defined(WIN32) */

/*
 * nghttp3_ksl_it_init initializes |it|.
 */
void nghttp3_ksl_it_init(nghttp3_ksl_it *it, const nghttp3_ksl *ksl,
                         nghttp3_ksl_blk *blk, size_t i);

/*
 * nghttp3_ksl_it_get returns the data associated to the node which
 * |it| points to.  It is undefined to call this function when
 * nghttp3_ksl_it_end(it) returns nonzero.
 */
#define nghttp3_ksl_it_get(IT)                                                 \
  nghttp3_ksl_nth_node((IT)->ksl, (IT)->blk, (IT)->i)->data

/*
 * nghttp3_ksl_it_next advances the iterator by one.  It is undefined
 * if this function is called when nghttp3_ksl_it_end(it) returns
 * nonzero.
 */
#define nghttp3_ksl_it_next(IT)                                                \
  (++(IT)->i == (IT)->blk->n && (IT)->blk->next                                \
     ? ((IT)->blk = (IT)->blk->next, (IT)->i = 0)                              \
     : 0)

/*
 * nghttp3_ksl_it_prev moves backward the iterator by one.  It is
 * undefined if this function is called when nghttp3_ksl_it_begin(it)
 * returns nonzero.
 */
void nghttp3_ksl_it_prev(nghttp3_ksl_it *it);

/*
 * nghttp3_ksl_it_end returns nonzero if |it| points to the one beyond
 * the last node.
 */
#define nghttp3_ksl_it_end(IT)                                                 \
  ((IT)->blk->n == (IT)->i && (IT)->blk->next == NULL)

/*
 * nghttp3_ksl_it_begin returns nonzero if |it| points to the first
 * node.  |it| might satisfy both nghttp3_ksl_it_begin(it) != 0 and
 * nghttp3_ksl_it_end(it) != 0 if the skip list has no node.
 */
int nghttp3_ksl_it_begin(const nghttp3_ksl_it *it);

/*
 * nghttp3_ksl_key returns the key of the node which |it| points to.
 * It is undefined to call this function when nghttp3_ksl_it_end(it)
 * returns nonzero.
 */
#define nghttp3_ksl_it_key(IT)                                                 \
  ((nghttp3_ksl_key *)nghttp3_ksl_nth_node((IT)->ksl, (IT)->blk, (IT)->i)->key)

/*
 * nghttp3_ksl_range_compar is an implementation of
 * nghttp3_ksl_compar.  |lhs| and |rhs| must point to nghttp3_range
 * object, and the function returns nonzero if ((const nghttp3_range
 * *)lhs)->begin < ((const nghttp3_range *)rhs)->begin.
 */
int nghttp3_ksl_range_compar(const nghttp3_ksl_key *lhs,
                             const nghttp3_ksl_key *rhs);

/*
 * nghttp3_ksl_range_search is an implementation of nghttp3_ksl_search
 * that uses nghttp3_ksl_range_compar.
 */
size_t nghttp3_ksl_range_search(const nghttp3_ksl *ksl, nghttp3_ksl_blk *blk,
                                const nghttp3_ksl_key *key);

/*
 * nghttp3_ksl_range_exclusive_compar is an implementation of
 * nghttp3_ksl_compar.  |lhs| and |rhs| must point to nghttp3_range
 * object, and the function returns nonzero if ((const nghttp3_range
 * *)lhs)->begin < ((const nghttp3_range *)rhs)->begin, and the 2
 * ranges do not intersect.
 */
int nghttp3_ksl_range_exclusive_compar(const nghttp3_ksl_key *lhs,
                                       const nghttp3_ksl_key *rhs);

/*
 * nghttp3_ksl_range_exclusive_search is an implementation of
 * nghttp3_ksl_search that uses nghttp3_ksl_range_exclusive_compar.
 */
size_t nghttp3_ksl_range_exclusive_search(const nghttp3_ksl *ksl,
                                          nghttp3_ksl_blk *blk,
                                          const nghttp3_ksl_key *key);

/*
 * nghttp3_ksl_uint64_less is an implementation of nghttp3_ksl_compar.
 * |lhs| and |rhs| must point to uint64_t objects, and the function
 * returns nonzero if *(uint64_t *)|lhs| < *(uint64_t *)|rhs|.
 */
int nghttp3_ksl_uint64_less(const nghttp3_ksl_key *lhs,
                            const nghttp3_ksl_key *rhs);

/*
 * nghttp3_ksl_uint64_less_search is an implementation of
 * nghttp3_ksl_search that uses nghttp3_ksl_uint64_less.
 */
size_t nghttp3_ksl_uint64_less_search(const nghttp3_ksl *ksl,
                                      nghttp3_ksl_blk *blk,
                                      const nghttp3_ksl_key *key);

/*
 * nghttp3_ksl_int64_greater is an implementation of
 * nghttp3_ksl_compar.  |lhs| and |rhs| must point to int64_t objects,
 * and the function returns nonzero if *(int64_t *)|lhs| > *(int64_t
 * *)|rhs|.
 */
int nghttp3_ksl_int64_greater(const nghttp3_ksl_key *lhs,
                              const nghttp3_ksl_key *rhs);

/*
 * nghttp3_ksl_int64_greater_search is an implementation of
 * nghttp3_ksl_search that uses nghttp3_ksl_int64_greater.
 */
size_t nghttp3_ksl_int64_greater_search(const nghttp3_ksl *ksl,
                                        nghttp3_ksl_blk *blk,
                                        const nghttp3_ksl_key *key);

#endif /* !defined(NGHTTP3_KSL_H) */
