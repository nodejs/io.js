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
#include "ares_llist.h"

struct ares_llist {
  ares_llist_node_t      *head;
  ares_llist_node_t      *tail;
  ares_llist_destructor_t destruct;
  size_t                  cnt;
};

struct ares_llist_node {
  void              *data;
  ares_llist_node_t *prev;
  ares_llist_node_t *next;
  ares_llist_t      *parent;
};

ares_llist_t *ares_llist_create(ares_llist_destructor_t destruct)
{
  ares_llist_t *list = ares_malloc_zero(sizeof(*list));

  if (list == NULL) {
    return NULL;
  }

  list->destruct = destruct;

  return list;
}

void ares_llist_replace_destructor(ares_llist_t           *list,
                                   ares_llist_destructor_t destruct)
{
  if (list == NULL) {
    return;
  }

  list->destruct = destruct;
}

typedef enum {
  ARES__LLIST_INSERT_HEAD,
  ARES__LLIST_INSERT_TAIL,
  ARES__LLIST_INSERT_BEFORE
} ares_llist_insert_type_t;

static void ares_llist_attach_at(ares_llist_t            *list,
                                 ares_llist_insert_type_t type,
                                 ares_llist_node_t *at, ares_llist_node_t *node)
{
  if (list == NULL || node == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  node->parent = list;

  if (type == ARES__LLIST_INSERT_BEFORE && (at == list->head || at == NULL)) {
    type = ARES__LLIST_INSERT_HEAD;
  }

  switch (type) {
    case ARES__LLIST_INSERT_HEAD:
      node->next = list->head;
      node->prev = NULL;
      if (list->head) {
        list->head->prev = node;
      }
      list->head = node;
      break;
    case ARES__LLIST_INSERT_TAIL:
      node->next = NULL;
      node->prev = list->tail;
      if (list->tail) {
        list->tail->next = node;
      }
      list->tail = node;
      break;
    case ARES__LLIST_INSERT_BEFORE:
      node->next = at;
      node->prev = at->prev;
      at->prev   = node;
      break;
  }
  if (list->tail == NULL) {
    list->tail = node;
  }
  if (list->head == NULL) {
    list->head = node;
  }

  list->cnt++;
}

static ares_llist_node_t *ares_llist_insert_at(ares_llist_t            *list,
                                               ares_llist_insert_type_t type,
                                               ares_llist_node_t *at, void *val)
{
  ares_llist_node_t *node = NULL;

  if (list == NULL || val == NULL) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  node = ares_malloc_zero(sizeof(*node));

  if (node == NULL) {
    return NULL;
  }

  node->data = val;
  ares_llist_attach_at(list, type, at, node);

  return node;
}

ares_llist_node_t *ares_llist_insert_first(ares_llist_t *list, void *val)
{
  return ares_llist_insert_at(list, ARES__LLIST_INSERT_HEAD, NULL, val);
}

ares_llist_node_t *ares_llist_insert_last(ares_llist_t *list, void *val)
{
  return ares_llist_insert_at(list, ARES__LLIST_INSERT_TAIL, NULL, val);
}

ares_llist_node_t *ares_llist_insert_before(ares_llist_node_t *node, void *val)
{
  if (node == NULL) {
    return NULL;
  }

  return ares_llist_insert_at(node->parent, ARES__LLIST_INSERT_BEFORE, node,
                              val);
}

ares_llist_node_t *ares_llist_insert_after(ares_llist_node_t *node, void *val)
{
  if (node == NULL) {
    return NULL;
  }

  if (node->next == NULL) {
    return ares_llist_insert_last(node->parent, val);
  }

  return ares_llist_insert_at(node->parent, ARES__LLIST_INSERT_BEFORE,
                              node->next, val);
}

ares_llist_node_t *ares_llist_node_first(ares_llist_t *list)
{
  if (list == NULL) {
    return NULL;
  }
  return list->head;
}

ares_llist_node_t *ares_llist_node_idx(ares_llist_t *list, size_t idx)
{
  ares_llist_node_t *node;
  size_t             cnt;

  if (list == NULL) {
    return NULL;
  }
  if (idx >= list->cnt) {
    return NULL;
  }

  node = list->head;
  for (cnt = 0; node != NULL && cnt < idx; cnt++) {
    node = node->next;
  }

  return node;
}

ares_llist_node_t *ares_llist_node_last(ares_llist_t *list)
{
  if (list == NULL) {
    return NULL;
  }
  return list->tail;
}

ares_llist_node_t *ares_llist_node_next(ares_llist_node_t *node)
{
  if (node == NULL) {
    return NULL;
  }
  return node->next;
}

ares_llist_node_t *ares_llist_node_prev(ares_llist_node_t *node)
{
  if (node == NULL) {
    return NULL;
  }
  return node->prev;
}

void *ares_llist_node_val(ares_llist_node_t *node)
{
  if (node == NULL) {
    return NULL;
  }

  return node->data;
}

size_t ares_llist_len(const ares_llist_t *list)
{
  if (list == NULL) {
    return 0;
  }
  return list->cnt;
}

ares_llist_t *ares_llist_node_parent(ares_llist_node_t *node)
{
  if (node == NULL) {
    return NULL;
  }
  return node->parent;
}

void *ares_llist_first_val(ares_llist_t *list)
{
  return ares_llist_node_val(ares_llist_node_first(list));
}

void *ares_llist_last_val(ares_llist_t *list)
{
  return ares_llist_node_val(ares_llist_node_last(list));
}

static void ares_llist_node_detach(ares_llist_node_t *node)
{
  ares_llist_t *list;

  if (node == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  list = node->parent;

  if (node->prev) {
    node->prev->next = node->next;
  }

  if (node->next) {
    node->next->prev = node->prev;
  }

  if (node == list->head) {
    list->head = node->next;
  }

  if (node == list->tail) {
    list->tail = node->prev;
  }

  node->parent = NULL;
  list->cnt--;
}

void *ares_llist_node_claim(ares_llist_node_t *node)
{
  void *val;

  if (node == NULL) {
    return NULL;
  }

  val = node->data;
  ares_llist_node_detach(node);
  ares_free(node);

  return val;
}

void ares_llist_node_destroy(ares_llist_node_t *node)
{
  ares_llist_destructor_t destruct;
  void                   *val;

  if (node == NULL) {
    return;
  }

  destruct = node->parent->destruct;

  val = ares_llist_node_claim(node);
  if (val != NULL && destruct != NULL) {
    destruct(val);
  }
}

void ares_llist_node_replace(ares_llist_node_t *node, void *val)
{
  ares_llist_destructor_t destruct;

  if (node == NULL) {
    return;
  }

  destruct = node->parent->destruct;
  if (destruct != NULL) {
    destruct(node->data);
  }

  node->data = val;
}

void ares_llist_clear(ares_llist_t *list)
{
  ares_llist_node_t *node;

  if (list == NULL) {
    return;
  }

  while ((node = ares_llist_node_first(list)) != NULL) {
    ares_llist_node_destroy(node);
  }
}

void ares_llist_destroy(ares_llist_t *list)
{
  if (list == NULL) {
    return;
  }
  ares_llist_clear(list);
  ares_free(list);
}

void ares_llist_node_mvparent_last(ares_llist_node_t *node,
                                   ares_llist_t      *new_parent)
{
  if (node == NULL || new_parent == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ares_llist_node_detach(node);
  ares_llist_attach_at(new_parent, ARES__LLIST_INSERT_TAIL, NULL, node);
}

void ares_llist_node_mvparent_first(ares_llist_node_t *node,
                                    ares_llist_t      *new_parent)
{
  if (node == NULL || new_parent == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ares_llist_node_detach(node);
  ares_llist_attach_at(new_parent, ARES__LLIST_INSERT_HEAD, NULL, node);
}
