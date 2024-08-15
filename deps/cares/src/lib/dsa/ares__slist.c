/* MIT License
 *
 * Copyright (c) 2023 Brad House
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ares_private.h"
#include "ares__slist.h"

/* SkipList implementation */

#define ARES__SLIST_START_LEVELS 4

struct ares__slist {
  ares_rand_state         *rand_state;
  unsigned char            rand_data[8];
  size_t                   rand_bits;

  ares__slist_node_t     **head;
  size_t                   levels;
  ares__slist_node_t      *tail;

  ares__slist_cmp_t        cmp;
  ares__slist_destructor_t destruct;
  size_t                   cnt;
};

struct ares__slist_node {
  void                *data;
  ares__slist_node_t **prev;
  ares__slist_node_t **next;
  size_t               levels;
  ares__slist_t       *parent;
};

ares__slist_t *ares__slist_create(ares_rand_state         *rand_state,
                                  ares__slist_cmp_t        cmp,
                                  ares__slist_destructor_t destruct)
{
  ares__slist_t *list;

  if (rand_state == NULL || cmp == NULL) {
    return NULL;
  }

  list = ares_malloc_zero(sizeof(*list));

  if (list == NULL) {
    return NULL;
  }

  list->rand_state = rand_state;
  list->cmp        = cmp;
  list->destruct   = destruct;

  list->levels = ARES__SLIST_START_LEVELS;
  list->head   = ares_malloc_zero(sizeof(*list->head) * list->levels);
  if (list->head == NULL) {
    ares_free(list);
    return NULL;
  }

  return list;
}

static ares_bool_t ares__slist_coin_flip(ares__slist_t *list)
{
  size_t total_bits = sizeof(list->rand_data) * 8;
  size_t bit;

  /* Refill random data used for coin flips.  We pull this in 8 byte chunks.
   * ares__rand_bytes() has some built-in caching of its own so we don't need
   * to be excessive in caching ourselves.  Prefer to require less memory per
   * skiplist */
  if (list->rand_bits == 0) {
    ares__rand_bytes(list->rand_state, list->rand_data,
                     sizeof(list->rand_data));
    list->rand_bits = total_bits;
  }

  bit = total_bits - list->rand_bits;
  list->rand_bits--;

  return (list->rand_data[bit / 8] & (1 << (bit % 8))) ? ARES_TRUE : ARES_FALSE;
}

void ares__slist_replace_destructor(ares__slist_t           *list,
                                    ares__slist_destructor_t destruct)
{
  if (list == NULL) {
    return;
  }

  list->destruct = destruct;
}

static size_t ares__slist_max_level(const ares__slist_t *list)
{
  size_t max_level = 0;

  if (list->cnt + 1 <= (1 << ARES__SLIST_START_LEVELS)) {
    max_level = ARES__SLIST_START_LEVELS;
  } else {
    max_level = ares__log2(ares__round_up_pow2(list->cnt + 1));
  }

  if (list->levels > max_level) {
    max_level = list->levels;
  }

  return max_level;
}

static size_t ares__slist_calc_level(ares__slist_t *list)
{
  size_t max_level = ares__slist_max_level(list);
  size_t level;

  for (level = 1; ares__slist_coin_flip(list) && level < max_level; level++)
    ;

  return level;
}

static void ares__slist_node_push(ares__slist_t *list, ares__slist_node_t *node)
{
  size_t              i;
  ares__slist_node_t *left = NULL;

  /* Scan from highest level in the slist, even if we're not using that number
   * of levels for this entry as this is what makes it O(log n) */
  for (i = list->levels; i-- > 0;) {
    /* set left if left is NULL and the current node value is greater than the
     * head at this level */
    if (left == NULL && list->head[i] != NULL &&
        list->cmp(node->data, list->head[i]->data) > 0) {
      left = list->head[i];
    }

    if (left != NULL) {
      /* scan forward to find our insertion point */
      while (left->next[i] != NULL &&
             list->cmp(node->data, left->next[i]->data) > 0) {
        left = left->next[i];
      }
    }

    /* search only as we didn't randomly select this number of levels */
    if (i >= node->levels) {
      continue;
    }

    if (left == NULL) {
      /* head insertion */
      node->next[i] = list->head[i];
      node->prev[i] = NULL;
      list->head[i] = node;
    } else {
      /* Chain */
      node->next[i] = left->next[i];
      node->prev[i] = left;
      left->next[i] = node;
    }

    if (node->next[i] != NULL) {
      /* chain prev */
      node->next[i]->prev[i] = node;
    } else {
      if (i == 0) {
        /* update tail */
        list->tail = node;
      }
    }
  }
}

ares__slist_node_t *ares__slist_insert(ares__slist_t *list, void *val)
{
  ares__slist_node_t *node = NULL;

  if (list == NULL || val == NULL) {
    return NULL;
  }

  node = ares_malloc_zero(sizeof(*node));

  if (node == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  node->data   = val;
  node->parent = list;

  /* Randomly determine the number of levels we want to use */
  node->levels = ares__slist_calc_level(list);

  /* Allocate array of next and prev nodes for linking each level */
  node->next = ares_malloc_zero(sizeof(*node->next) * node->levels);
  if (node->next == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  node->prev = ares_malloc_zero(sizeof(*node->prev) * node->levels);
  if (node->prev == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* If the number of levels is greater than we currently support in the slist,
   * increase the count */
  if (list->levels < node->levels) {
    void *ptr =
      ares_realloc_zero(list->head, sizeof(*list->head) * list->levels,
                        sizeof(*list->head) * node->levels);
    if (ptr == NULL) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    list->head   = ptr;
    list->levels = node->levels;
  }

  ares__slist_node_push(list, node);

  list->cnt++;

  return node;

/* LCOV_EXCL_START: OutOfMemory */
fail:
  if (node) {
    ares_free(node->prev);
    ares_free(node->next);
    ares_free(node);
  }
  return NULL;
  /* LCOV_EXCL_STOP */
}

static void ares__slist_node_pop(ares__slist_node_t *node)
{
  ares__slist_t *list = node->parent;
  size_t         i;

  /* relink each node at each level */
  for (i = node->levels; i-- > 0;) {
    if (node->next[i] == NULL) {
      if (i == 0) {
        list->tail = node->prev[0];
      }
    } else {
      node->next[i]->prev[i] = node->prev[i];
    }

    if (node->prev[i] == NULL) {
      list->head[i] = node->next[i];
    } else {
      node->prev[i]->next[i] = node->next[i];
    }
  }

  memset(node->next, 0, sizeof(*node->next) * node->levels);
  memset(node->prev, 0, sizeof(*node->prev) * node->levels);
}

void *ares__slist_node_claim(ares__slist_node_t *node)
{
  ares__slist_t *list;
  void          *val;

  if (node == NULL) {
    return NULL;
  }

  list = node->parent;
  val  = node->data;

  ares__slist_node_pop(node);

  ares_free(node->next);
  ares_free(node->prev);
  ares_free(node);

  list->cnt--;

  return val;
}

void ares__slist_node_reinsert(ares__slist_node_t *node)
{
  ares__slist_t *list;

  if (node == NULL) {
    return;
  }

  list = node->parent;

  ares__slist_node_pop(node);
  ares__slist_node_push(list, node);
}

ares__slist_node_t *ares__slist_node_find(ares__slist_t *list, const void *val)
{
  size_t              i;
  ares__slist_node_t *node = NULL;
  int                 rv   = -1;

  if (list == NULL || val == NULL) {
    return NULL;
  }

  /* Scan nodes starting at the highest level. For each level scan forward
   * until the value is between the prior and next node, or if equal quit
   * as we found a match */
  for (i = list->levels; i-- > 0;) {
    if (node == NULL) {
      node = list->head[i];
    }

    if (node == NULL) {
      continue;
    }

    do {
      rv = list->cmp(val, node->data);

      if (rv < 0) {
        /* back off, our value is greater than current node reference */
        node = node->prev[i];
      } else if (rv > 0) {
        /* move forward and try again. if it goes past, it will loop again and
         * go to previous entry */
        node = node->next[i];
      }

      /* rv == 0 will terminate loop */

    } while (node != NULL && rv > 0);

    /* Found a match, no need to continue */
    if (rv == 0) {
      break;
    }
  }

  /* no match */
  if (rv != 0) {
    return NULL;
  }

  /* The list may have multiple entries that match.  They're guaranteed to be
   * in order, but we're not guaranteed to have selected the _first_ matching
   * node.  Lets scan backwards to find the first match */
  while (node->prev[0] != NULL && list->cmp(node->prev[0]->data, val) == 0) {
    node = node->prev[0];
  }

  return node;
}

ares__slist_node_t *ares__slist_node_first(ares__slist_t *list)
{
  if (list == NULL) {
    return NULL;
  }

  return list->head[0];
}

ares__slist_node_t *ares__slist_node_last(ares__slist_t *list)
{
  if (list == NULL) {
    return NULL;
  }
  return list->tail;
}

ares__slist_node_t *ares__slist_node_next(ares__slist_node_t *node)
{
  if (node == NULL) {
    return NULL;
  }
  return node->next[0];
}

ares__slist_node_t *ares__slist_node_prev(ares__slist_node_t *node)
{
  if (node == NULL) {
    return NULL;
  }
  return node->prev[0];
}

void *ares__slist_node_val(ares__slist_node_t *node)
{
  if (node == NULL) {
    return NULL;
  }

  return node->data;
}

size_t ares__slist_len(const ares__slist_t *list)
{
  if (list == NULL) {
    return 0;
  }
  return list->cnt;
}

ares__slist_t *ares__slist_node_parent(ares__slist_node_t *node)
{
  if (node == NULL) {
    return NULL;
  }
  return node->parent;
}

void *ares__slist_first_val(ares__slist_t *list)
{
  return ares__slist_node_val(ares__slist_node_first(list));
}

void *ares__slist_last_val(ares__slist_t *list)
{
  return ares__slist_node_val(ares__slist_node_last(list));
}

void ares__slist_node_destroy(ares__slist_node_t *node)
{
  ares__slist_destructor_t destruct;
  void                    *val;

  if (node == NULL) {
    return;
  }

  destruct = node->parent->destruct;
  val      = ares__slist_node_claim(node);

  if (val != NULL && destruct != NULL) {
    destruct(val);
  }
}

void ares__slist_destroy(ares__slist_t *list)
{
  ares__slist_node_t *node;

  if (list == NULL) {
    return;
  }

  while ((node = ares__slist_node_first(list)) != NULL) {
    ares__slist_node_destroy(node);
  }

  ares_free(list->head);
  ares_free(list);
}
