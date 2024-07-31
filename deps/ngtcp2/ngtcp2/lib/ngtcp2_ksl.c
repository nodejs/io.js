/*
 * ngtcp2
 *
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
#include "ngtcp2_ksl.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "ngtcp2_macro.h"
#include "ngtcp2_mem.h"
#include "ngtcp2_range.h"

static ngtcp2_ksl_blk null_blk = {{{NULL, NULL, 0, 0, {0}}}};

ngtcp2_objalloc_def(ksl_blk, ngtcp2_ksl_blk, oplent);

static size_t ksl_nodelen(size_t keylen) {
  return (sizeof(ngtcp2_ksl_node) + keylen - sizeof(uint64_t) + 0xfu) &
         ~(uintptr_t)0xfu;
}

static size_t ksl_blklen(size_t nodelen) {
  return sizeof(ngtcp2_ksl_blk) + nodelen * NGTCP2_KSL_MAX_NBLK -
         sizeof(uint64_t);
}

/*
 * ksl_node_set_key sets |key| to |node|.
 */
static void ksl_node_set_key(ngtcp2_ksl *ksl, ngtcp2_ksl_node *node,
                             const void *key) {
  memcpy(node->key, key, ksl->keylen);
}

void ngtcp2_ksl_init(ngtcp2_ksl *ksl, ngtcp2_ksl_compar compar, size_t keylen,
                     const ngtcp2_mem *mem) {
  size_t nodelen = ksl_nodelen(keylen);

  ngtcp2_objalloc_init(&ksl->blkalloc,
                       ((ksl_blklen(nodelen) + 0xfu) & ~(uintptr_t)0xfu) * 8,
                       mem);

  ksl->head = NULL;
  ksl->front = ksl->back = NULL;
  ksl->compar = compar;
  ksl->keylen = keylen;
  ksl->nodelen = nodelen;
  ksl->n = 0;
}

static ngtcp2_ksl_blk *ksl_blk_objalloc_new(ngtcp2_ksl *ksl) {
  return ngtcp2_objalloc_ksl_blk_len_get(&ksl->blkalloc,
                                         ksl_blklen(ksl->nodelen));
}

static void ksl_blk_objalloc_del(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk) {
  ngtcp2_objalloc_ksl_blk_release(&ksl->blkalloc, blk);
}

static int ksl_head_init(ngtcp2_ksl *ksl) {
  ngtcp2_ksl_blk *head = ksl_blk_objalloc_new(ksl);
  if (!head) {
    return NGTCP2_ERR_NOMEM;
  }

  head->next = head->prev = NULL;
  head->n = 0;
  head->leaf = 1;

  ksl->head = head;
  ksl->front = ksl->back = head;

  return 0;
}

#ifdef NOMEMPOOL
/*
 * ksl_free_blk frees |blk| recursively.
 */
static void ksl_free_blk(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk) {
  size_t i;

  if (!blk->leaf) {
    for (i = 0; i < blk->n; ++i) {
      ksl_free_blk(ksl, ngtcp2_ksl_nth_node(ksl, blk, i)->blk);
    }
  }

  ksl_blk_objalloc_del(ksl, blk);
}
#endif /* NOMEMPOOL */

void ngtcp2_ksl_free(ngtcp2_ksl *ksl) {
  if (!ksl || !ksl->head) {
    return;
  }

#ifdef NOMEMPOOL
  ksl_free_blk(ksl, ksl->head);
#endif /* NOMEMPOOL */

  ngtcp2_objalloc_free(&ksl->blkalloc);
}

/*
 * ksl_split_blk splits |blk| into 2 ngtcp2_ksl_blk objects.  The new
 * ngtcp2_ksl_blk is always the "right" block.
 *
 * It returns the pointer to the ngtcp2_ksl_blk created which is the
 * located at the right of |blk|, or NULL which indicates out of
 * memory error.
 */
static ngtcp2_ksl_blk *ksl_split_blk(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk) {
  ngtcp2_ksl_blk *rblk;

  rblk = ksl_blk_objalloc_new(ksl);
  if (rblk == NULL) {
    return NULL;
  }

  rblk->next = blk->next;
  blk->next = rblk;
  if (rblk->next) {
    rblk->next->prev = rblk;
  } else if (ksl->back == blk) {
    ksl->back = rblk;
  }
  rblk->prev = blk;
  rblk->leaf = blk->leaf;

  rblk->n = blk->n / 2;

  memcpy(rblk->nodes, blk->nodes + ksl->nodelen * (blk->n - rblk->n),
         ksl->nodelen * rblk->n);

  blk->n -= rblk->n;

  assert(blk->n >= NGTCP2_KSL_MIN_NBLK);
  assert(rblk->n >= NGTCP2_KSL_MIN_NBLK);

  return rblk;
}

/*
 * ksl_split_node splits a node included in |blk| at the position |i|
 * into 2 adjacent nodes.  The new node is always inserted at the
 * position |i+1|.
 *
 * It returns 0 if it succeeds, or one of the following negative error
 * codes:
 *
 * NGTCP2_ERR_NOMEM
 *   Out of memory.
 */
static int ksl_split_node(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk, size_t i) {
  ngtcp2_ksl_node *node;
  ngtcp2_ksl_blk *lblk = ngtcp2_ksl_nth_node(ksl, blk, i)->blk, *rblk;

  rblk = ksl_split_blk(ksl, lblk);
  if (rblk == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  memmove(blk->nodes + (i + 2) * ksl->nodelen,
          blk->nodes + (i + 1) * ksl->nodelen,
          ksl->nodelen * (blk->n - (i + 1)));

  node = ngtcp2_ksl_nth_node(ksl, blk, i + 1);
  node->blk = rblk;
  ++blk->n;
  ksl_node_set_key(ksl, node, ngtcp2_ksl_nth_node(ksl, rblk, rblk->n - 1)->key);

  node = ngtcp2_ksl_nth_node(ksl, blk, i);
  ksl_node_set_key(ksl, node, ngtcp2_ksl_nth_node(ksl, lblk, lblk->n - 1)->key);

  return 0;
}

/*
 * ksl_split_head splits a head (root) block.  It increases the height
 * of skip list by 1.
 *
 * It returns 0 if it succeeds, or one of the following negative error
 * codes:
 *
 * NGTCP2_ERR_NOMEM
 *   Out of memory.
 */
static int ksl_split_head(ngtcp2_ksl *ksl) {
  ngtcp2_ksl_blk *rblk = NULL, *lblk, *nhead = NULL;
  ngtcp2_ksl_node *node;

  rblk = ksl_split_blk(ksl, ksl->head);
  if (rblk == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  lblk = ksl->head;

  nhead = ksl_blk_objalloc_new(ksl);
  if (nhead == NULL) {
    ksl_blk_objalloc_del(ksl, rblk);
    return NGTCP2_ERR_NOMEM;
  }
  nhead->next = nhead->prev = NULL;
  nhead->n = 2;
  nhead->leaf = 0;

  node = ngtcp2_ksl_nth_node(ksl, nhead, 0);
  ksl_node_set_key(ksl, node, ngtcp2_ksl_nth_node(ksl, lblk, lblk->n - 1)->key);
  node->blk = lblk;

  node = ngtcp2_ksl_nth_node(ksl, nhead, 1);
  ksl_node_set_key(ksl, node, ngtcp2_ksl_nth_node(ksl, rblk, rblk->n - 1)->key);
  node->blk = rblk;

  ksl->head = nhead;

  return 0;
}

/*
 * insert_node inserts a node whose key is |key| with the associated
 * |data| at the index of |i|.  This function assumes that the number
 * of nodes contained by |blk| is strictly less than
 * NGTCP2_KSL_MAX_NBLK.
 */
static void ksl_insert_node(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk, size_t i,
                            const ngtcp2_ksl_key *key, void *data) {
  ngtcp2_ksl_node *node;

  assert(blk->n < NGTCP2_KSL_MAX_NBLK);

  memmove(blk->nodes + (i + 1) * ksl->nodelen, blk->nodes + i * ksl->nodelen,
          ksl->nodelen * (blk->n - i));

  node = ngtcp2_ksl_nth_node(ksl, blk, i);
  ksl_node_set_key(ksl, node, key);
  node->data = data;

  ++blk->n;
}

static size_t ksl_bsearch(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk,
                          const ngtcp2_ksl_key *key, ngtcp2_ksl_compar compar) {
  size_t i;
  ngtcp2_ksl_node *node;

  for (i = 0, node = (ngtcp2_ksl_node *)(void *)blk->nodes;
       i < blk->n && compar((ngtcp2_ksl_key *)node->key, key);
       ++i, node = (ngtcp2_ksl_node *)(void *)((uint8_t *)node + ksl->nodelen))
    ;

  return i;
}

int ngtcp2_ksl_insert(ngtcp2_ksl *ksl, ngtcp2_ksl_it *it,
                      const ngtcp2_ksl_key *key, void *data) {
  ngtcp2_ksl_blk *blk;
  ngtcp2_ksl_node *node;
  size_t i;
  int rv;

  if (!ksl->head) {
    rv = ksl_head_init(ksl);
    if (rv != 0) {
      return rv;
    }
  }

  blk = ksl->head;

  if (blk->n == NGTCP2_KSL_MAX_NBLK) {
    rv = ksl_split_head(ksl);
    if (rv != 0) {
      return rv;
    }
    blk = ksl->head;
  }

  for (;;) {
    i = ksl_bsearch(ksl, blk, key, ksl->compar);

    if (blk->leaf) {
      if (i < blk->n &&
          !ksl->compar(key, ngtcp2_ksl_nth_node(ksl, blk, i)->key)) {
        if (it) {
          *it = ngtcp2_ksl_end(ksl);
        }
        return NGTCP2_ERR_INVALID_ARGUMENT;
      }
      ksl_insert_node(ksl, blk, i, key, data);
      ++ksl->n;
      if (it) {
        ngtcp2_ksl_it_init(it, ksl, blk, i);
      }
      return 0;
    }

    if (i == blk->n) {
      /* This insertion extends the largest key in this subtree. */
      for (; !blk->leaf;) {
        node = ngtcp2_ksl_nth_node(ksl, blk, blk->n - 1);
        if (node->blk->n == NGTCP2_KSL_MAX_NBLK) {
          rv = ksl_split_node(ksl, blk, blk->n - 1);
          if (rv != 0) {
            return rv;
          }
          node = ngtcp2_ksl_nth_node(ksl, blk, blk->n - 1);
        }
        ksl_node_set_key(ksl, node, key);
        blk = node->blk;
      }
      ksl_insert_node(ksl, blk, blk->n, key, data);
      ++ksl->n;
      if (it) {
        ngtcp2_ksl_it_init(it, ksl, blk, blk->n - 1);
      }
      return 0;
    }

    node = ngtcp2_ksl_nth_node(ksl, blk, i);

    if (node->blk->n == NGTCP2_KSL_MAX_NBLK) {
      rv = ksl_split_node(ksl, blk, i);
      if (rv != 0) {
        return rv;
      }
      if (ksl->compar((ngtcp2_ksl_key *)node->key, key)) {
        node = ngtcp2_ksl_nth_node(ksl, blk, i + 1);
        if (ksl->compar((ngtcp2_ksl_key *)node->key, key)) {
          ksl_node_set_key(ksl, node, key);
        }
      }
    }

    blk = node->blk;
  }
}

/*
 * ksl_remove_node removes the node included in |blk| at the index of
 * |i|.
 */
static void ksl_remove_node(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk, size_t i) {
  memmove(blk->nodes + i * ksl->nodelen, blk->nodes + (i + 1) * ksl->nodelen,
          ksl->nodelen * (blk->n - (i + 1)));

  --blk->n;
}

/*
 * ksl_merge_node merges 2 nodes which are the nodes at the index of
 * |i| and |i + 1|.
 *
 * If |blk| is the direct descendant of head (root) block and the head
 * block contains just 2 nodes, the merged block becomes head block,
 * which decreases the height of |ksl| by 1.
 *
 * This function returns the pointer to the merged block.
 */
static ngtcp2_ksl_blk *ksl_merge_node(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk,
                                      size_t i) {
  ngtcp2_ksl_blk *lblk, *rblk;

  assert(i + 1 < blk->n);

  lblk = ngtcp2_ksl_nth_node(ksl, blk, i)->blk;
  rblk = ngtcp2_ksl_nth_node(ksl, blk, i + 1)->blk;

  assert(lblk->n + rblk->n < NGTCP2_KSL_MAX_NBLK);

  memcpy(lblk->nodes + ksl->nodelen * lblk->n, rblk->nodes,
         ksl->nodelen * rblk->n);

  lblk->n += rblk->n;
  lblk->next = rblk->next;
  if (lblk->next) {
    lblk->next->prev = lblk;
  } else if (ksl->back == rblk) {
    ksl->back = lblk;
  }

  ksl_blk_objalloc_del(ksl, rblk);

  if (ksl->head == blk && blk->n == 2) {
    ksl_blk_objalloc_del(ksl, ksl->head);
    ksl->head = lblk;
  } else {
    ksl_remove_node(ksl, blk, i + 1);
    ksl_node_set_key(ksl, ngtcp2_ksl_nth_node(ksl, blk, i),
                     ngtcp2_ksl_nth_node(ksl, lblk, lblk->n - 1)->key);
  }

  return lblk;
}

/*
 * ksl_shift_left moves the first nodes in blk->nodes[i]->blk->nodes
 * to blk->nodes[i - 1]->blk->nodes in a manner that they have the
 * same amount of nodes as much as possible.
 */
static void ksl_shift_left(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk, size_t i) {
  ngtcp2_ksl_node *lnode, *rnode;
  size_t n;

  assert(i > 0);

  lnode = ngtcp2_ksl_nth_node(ksl, blk, i - 1);
  rnode = ngtcp2_ksl_nth_node(ksl, blk, i);

  assert(lnode->blk->n < NGTCP2_KSL_MAX_NBLK);
  assert(rnode->blk->n > NGTCP2_KSL_MIN_NBLK);

  n = (lnode->blk->n + rnode->blk->n + 1) / 2 - lnode->blk->n;

  assert(n > 0);
  assert(lnode->blk->n <= NGTCP2_KSL_MAX_NBLK - n);
  assert(rnode->blk->n >= NGTCP2_KSL_MIN_NBLK + n);

  memcpy(lnode->blk->nodes + ksl->nodelen * lnode->blk->n, rnode->blk->nodes,
         ksl->nodelen * n);

  lnode->blk->n += (uint32_t)n;
  rnode->blk->n -= (uint32_t)n;

  ksl_node_set_key(
      ksl, lnode, ngtcp2_ksl_nth_node(ksl, lnode->blk, lnode->blk->n - 1)->key);

  memmove(rnode->blk->nodes, rnode->blk->nodes + ksl->nodelen * n,
          ksl->nodelen * rnode->blk->n);
}

/*
 * ksl_shift_right moves the last nodes in blk->nodes[i]->blk->nodes
 * to blk->nodes[i + 1]->blk->nodes in a manner that they have the
 * same amount of nodes as much as possible..
 */
static void ksl_shift_right(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk, size_t i) {
  ngtcp2_ksl_node *lnode, *rnode;
  size_t n;

  assert(i < blk->n - 1);

  lnode = ngtcp2_ksl_nth_node(ksl, blk, i);
  rnode = ngtcp2_ksl_nth_node(ksl, blk, i + 1);

  assert(lnode->blk->n > NGTCP2_KSL_MIN_NBLK);
  assert(rnode->blk->n < NGTCP2_KSL_MAX_NBLK);

  n = (lnode->blk->n + rnode->blk->n + 1) / 2 - rnode->blk->n;

  assert(n > 0);
  assert(lnode->blk->n >= NGTCP2_KSL_MIN_NBLK + n);
  assert(rnode->blk->n <= NGTCP2_KSL_MAX_NBLK - n);

  memmove(rnode->blk->nodes + ksl->nodelen * n, rnode->blk->nodes,
          ksl->nodelen * rnode->blk->n);

  rnode->blk->n += (uint32_t)n;
  lnode->blk->n -= (uint32_t)n;

  memcpy(rnode->blk->nodes, lnode->blk->nodes + ksl->nodelen * lnode->blk->n,
         ksl->nodelen * n);

  ksl_node_set_key(
      ksl, lnode, ngtcp2_ksl_nth_node(ksl, lnode->blk, lnode->blk->n - 1)->key);
}

/*
 * key_equal returns nonzero if |lhs| and |rhs| are equal using the
 * function |compar|.
 */
static int key_equal(ngtcp2_ksl_compar compar, const ngtcp2_ksl_key *lhs,
                     const ngtcp2_ksl_key *rhs) {
  return !compar(lhs, rhs) && !compar(rhs, lhs);
}

int ngtcp2_ksl_remove_hint(ngtcp2_ksl *ksl, ngtcp2_ksl_it *it,
                           const ngtcp2_ksl_it *hint,
                           const ngtcp2_ksl_key *key) {
  ngtcp2_ksl_blk *blk = hint->blk;

  assert(ksl->head);

  if (blk->n <= NGTCP2_KSL_MIN_NBLK) {
    return ngtcp2_ksl_remove(ksl, it, key);
  }

  ksl_remove_node(ksl, blk, hint->i);

  --ksl->n;

  if (it) {
    if (hint->i == blk->n && blk->next) {
      ngtcp2_ksl_it_init(it, ksl, blk->next, 0);
    } else {
      ngtcp2_ksl_it_init(it, ksl, blk, hint->i);
    }
  }

  return 0;
}

int ngtcp2_ksl_remove(ngtcp2_ksl *ksl, ngtcp2_ksl_it *it,
                      const ngtcp2_ksl_key *key) {
  ngtcp2_ksl_blk *blk = ksl->head;
  ngtcp2_ksl_node *node;
  size_t i;

  if (!ksl->head) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  if (!blk->leaf && blk->n == 2 &&
      ngtcp2_ksl_nth_node(ksl, blk, 0)->blk->n == NGTCP2_KSL_MIN_NBLK &&
      ngtcp2_ksl_nth_node(ksl, blk, 1)->blk->n == NGTCP2_KSL_MIN_NBLK) {
    blk = ksl_merge_node(ksl, ksl->head, 0);
  }

  for (;;) {
    i = ksl_bsearch(ksl, blk, key, ksl->compar);

    if (i == blk->n) {
      if (it) {
        *it = ngtcp2_ksl_end(ksl);
      }
      return NGTCP2_ERR_INVALID_ARGUMENT;
    }

    if (blk->leaf) {
      if (ksl->compar(key, ngtcp2_ksl_nth_node(ksl, blk, i)->key)) {
        if (it) {
          *it = ngtcp2_ksl_end(ksl);
        }
        return NGTCP2_ERR_INVALID_ARGUMENT;
      }
      ksl_remove_node(ksl, blk, i);
      --ksl->n;
      if (it) {
        if (blk->n == i && blk->next) {
          ngtcp2_ksl_it_init(it, ksl, blk->next, 0);
        } else {
          ngtcp2_ksl_it_init(it, ksl, blk, i);
        }
      }
      return 0;
    }

    node = ngtcp2_ksl_nth_node(ksl, blk, i);

    if (node->blk->n > NGTCP2_KSL_MIN_NBLK) {
      blk = node->blk;
      continue;
    }

    assert(node->blk->n == NGTCP2_KSL_MIN_NBLK);

    if (i + 1 < blk->n &&
        ngtcp2_ksl_nth_node(ksl, blk, i + 1)->blk->n > NGTCP2_KSL_MIN_NBLK) {
      ksl_shift_left(ksl, blk, i + 1);
      blk = node->blk;
      continue;
    }

    if (i > 0 &&
        ngtcp2_ksl_nth_node(ksl, blk, i - 1)->blk->n > NGTCP2_KSL_MIN_NBLK) {
      ksl_shift_right(ksl, blk, i - 1);
      blk = node->blk;
      continue;
    }

    if (i + 1 < blk->n) {
      blk = ksl_merge_node(ksl, blk, i);
      continue;
    }

    assert(i > 0);

    blk = ksl_merge_node(ksl, blk, i - 1);
  }
}

ngtcp2_ksl_it ngtcp2_ksl_lower_bound(ngtcp2_ksl *ksl,
                                     const ngtcp2_ksl_key *key) {
  ngtcp2_ksl_blk *blk = ksl->head;
  ngtcp2_ksl_it it;
  size_t i;

  if (!blk) {
    ngtcp2_ksl_it_init(&it, ksl, &null_blk, 0);
    return it;
  }

  for (;;) {
    i = ksl_bsearch(ksl, blk, key, ksl->compar);

    if (blk->leaf) {
      if (i == blk->n && blk->next) {
        blk = blk->next;
        i = 0;
      }
      ngtcp2_ksl_it_init(&it, ksl, blk, i);
      return it;
    }

    if (i == blk->n) {
      /* This happens if descendant has smaller key.  Fast forward to
         find last node in this subtree. */
      for (; !blk->leaf; blk = ngtcp2_ksl_nth_node(ksl, blk, blk->n - 1)->blk)
        ;
      if (blk->next) {
        blk = blk->next;
        i = 0;
      } else {
        i = blk->n;
      }
      ngtcp2_ksl_it_init(&it, ksl, blk, i);
      return it;
    }
    blk = ngtcp2_ksl_nth_node(ksl, blk, i)->blk;
  }
}

ngtcp2_ksl_it ngtcp2_ksl_lower_bound_compar(ngtcp2_ksl *ksl,
                                            const ngtcp2_ksl_key *key,
                                            ngtcp2_ksl_compar compar) {
  ngtcp2_ksl_blk *blk = ksl->head;
  ngtcp2_ksl_it it;
  size_t i;

  if (!blk) {
    ngtcp2_ksl_it_init(&it, ksl, &null_blk, 0);
    return it;
  }

  for (;;) {
    i = ksl_bsearch(ksl, blk, key, compar);

    if (blk->leaf) {
      if (i == blk->n && blk->next) {
        blk = blk->next;
        i = 0;
      }
      ngtcp2_ksl_it_init(&it, ksl, blk, i);
      return it;
    }

    if (i == blk->n) {
      /* This happens if descendant has smaller key.  Fast forward to
         find last node in this subtree. */
      for (; !blk->leaf; blk = ngtcp2_ksl_nth_node(ksl, blk, blk->n - 1)->blk)
        ;
      if (blk->next) {
        blk = blk->next;
        i = 0;
      } else {
        i = blk->n;
      }
      ngtcp2_ksl_it_init(&it, ksl, blk, i);
      return it;
    }
    blk = ngtcp2_ksl_nth_node(ksl, blk, i)->blk;
  }
}

void ngtcp2_ksl_update_key(ngtcp2_ksl *ksl, const ngtcp2_ksl_key *old_key,
                           const ngtcp2_ksl_key *new_key) {
  ngtcp2_ksl_blk *blk = ksl->head;
  ngtcp2_ksl_node *node;
  size_t i;

  assert(ksl->head);

  for (;;) {
    i = ksl_bsearch(ksl, blk, old_key, ksl->compar);

    assert(i < blk->n);
    node = ngtcp2_ksl_nth_node(ksl, blk, i);

    if (blk->leaf) {
      assert(key_equal(ksl->compar, (ngtcp2_ksl_key *)node->key, old_key));
      ksl_node_set_key(ksl, node, new_key);
      return;
    }

    if (key_equal(ksl->compar, (ngtcp2_ksl_key *)node->key, old_key) ||
        ksl->compar((ngtcp2_ksl_key *)node->key, new_key)) {
      ksl_node_set_key(ksl, node, new_key);
    }

    blk = node->blk;
  }
}

size_t ngtcp2_ksl_len(ngtcp2_ksl *ksl) { return ksl->n; }

void ngtcp2_ksl_clear(ngtcp2_ksl *ksl) {
  if (!ksl->head) {
    return;
  }

#ifdef NOMEMPOOL
  ksl_free_blk(ksl, ksl->head);
#endif /* NOMEMPOOL */

  ksl->front = ksl->back = ksl->head = NULL;
  ksl->n = 0;

  ngtcp2_objalloc_clear(&ksl->blkalloc);
}

#ifndef WIN32
static void ksl_print(ngtcp2_ksl *ksl, ngtcp2_ksl_blk *blk, size_t level) {
  size_t i;
  ngtcp2_ksl_node *node;

  fprintf(stderr, "LV=%zu n=%u\n", level, blk->n);

  if (blk->leaf) {
    for (i = 0; i < blk->n; ++i) {
      node = ngtcp2_ksl_nth_node(ksl, blk, i);
      fprintf(stderr, " %" PRId64, *(int64_t *)(void *)node->key);
    }
    fprintf(stderr, "\n");
    return;
  }

  for (i = 0; i < blk->n; ++i) {
    ksl_print(ksl, ngtcp2_ksl_nth_node(ksl, blk, i)->blk, level + 1);
  }
}

void ngtcp2_ksl_print(ngtcp2_ksl *ksl) {
  if (!ksl->head) {
    return;
  }

  ksl_print(ksl, ksl->head, 0);
}
#endif /* !WIN32 */

ngtcp2_ksl_it ngtcp2_ksl_begin(const ngtcp2_ksl *ksl) {
  ngtcp2_ksl_it it;

  if (ksl->head) {
    ngtcp2_ksl_it_init(&it, ksl, ksl->front, 0);
  } else {
    ngtcp2_ksl_it_init(&it, ksl, &null_blk, 0);
  }

  return it;
}

ngtcp2_ksl_it ngtcp2_ksl_end(const ngtcp2_ksl *ksl) {
  ngtcp2_ksl_it it;

  if (ksl->head) {
    ngtcp2_ksl_it_init(&it, ksl, ksl->back, ksl->back->n);
  } else {
    ngtcp2_ksl_it_init(&it, ksl, &null_blk, 0);
  }

  return it;
}

void ngtcp2_ksl_it_init(ngtcp2_ksl_it *it, const ngtcp2_ksl *ksl,
                        ngtcp2_ksl_blk *blk, size_t i) {
  it->ksl = ksl;
  it->blk = blk;
  it->i = i;
}

void ngtcp2_ksl_it_prev(ngtcp2_ksl_it *it) {
  assert(!ngtcp2_ksl_it_begin(it));

  if (it->i == 0) {
    it->blk = it->blk->prev;
    it->i = it->blk->n - 1;
  } else {
    --it->i;
  }
}

int ngtcp2_ksl_it_begin(const ngtcp2_ksl_it *it) {
  return it->i == 0 && it->blk->prev == NULL;
}

int ngtcp2_ksl_range_compar(const ngtcp2_ksl_key *lhs,
                            const ngtcp2_ksl_key *rhs) {
  const ngtcp2_range *a = lhs, *b = rhs;
  return a->begin < b->begin;
}

int ngtcp2_ksl_range_exclusive_compar(const ngtcp2_ksl_key *lhs,
                                      const ngtcp2_ksl_key *rhs) {
  const ngtcp2_range *a = lhs, *b = rhs;
  return a->begin < b->begin &&
         !(ngtcp2_max(a->begin, b->begin) < ngtcp2_min(a->end, b->end));
}
