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
#ifndef __ARES__LLIST_H
#define __ARES__LLIST_H

/*! \addtogroup ares__llist LinkedList Data Structure
 *
 * This is a doubly-linked list data structure.
 *
 * Average time complexity:
 *  - Insert: O(1)   -- head or tail
 *  - Search: O(n)
 *  - Delete: O(1)   -- delete assumes you hold a node pointer
 *
 * @{
 */

struct ares__llist;

/*! Opaque data structure for linked list */
typedef struct ares__llist ares__llist_t;

struct ares__llist_node;

/*! Opaque data structure for a node in a linked list */
typedef struct ares__llist_node ares__llist_node_t;

/*! Callback to free user-defined node data
 *
 *  \param[in] data  user supplied data
 */
typedef void (*ares__llist_destructor_t)(void *data);

/*! Create a linked list object
 *
 *  \param[in] destruct  Optional. Destructor to call on all removed nodes
 *  \return linked list object or NULL on out of memory
 */
ares__llist_t      *ares__llist_create(ares__llist_destructor_t destruct);

/*! Replace destructor for linked list nodes.  Typically this is used
 *  when wanting to disable the destructor by using NULL.
 *
 *  \param[in] list      Initialized linked list object
 *  \param[in] destruct  replacement destructor, NULL is allowed
 */
void                ares__llist_replace_destructor(ares__llist_t           *list,
                                                   ares__llist_destructor_t destruct);

/*! Insert value as the first node in the linked list
 *
 *  \param[in] list   Initialized linked list object
 *  \param[in] val    user-supplied value.
 *  \return node object referencing place in list, or null if out of memory or
 *   misuse
 */
ares__llist_node_t *ares__llist_insert_first(ares__llist_t *list, void *val);

/*! Insert value as the last node in the linked list
 *
 *  \param[in] list   Initialized linked list object
 *  \param[in] val    user-supplied value.
 *  \return node object referencing place in list, or null if out of memory or
 *   misuse
 */
ares__llist_node_t *ares__llist_insert_last(ares__llist_t *list, void *val);

/*! Insert value before specified node in the linked list
 *
 *  \param[in] node  node referenced to insert before
 *  \param[in] val   user-supplied value.
 *  \return node object referencing place in list, or null if out of memory or
 *   misuse
 */
ares__llist_node_t *ares__llist_insert_before(ares__llist_node_t *node,
                                              void               *val);

/*! Insert value after specified node in the linked list
 *
 *  \param[in] node  node referenced to insert after
 *  \param[in] val   user-supplied value.
 *  \return node object referencing place in list, or null if out of memory or
 *   misuse
 */
ares__llist_node_t *ares__llist_insert_after(ares__llist_node_t *node,
                                             void               *val);

/*! Obtain first node in list
 *
 *  \param[in] list  Initialized list object
 *  \return first node in list or NULL if none
 */
ares__llist_node_t *ares__llist_node_first(ares__llist_t *list);

/*! Obtain last node in list
 *
 *  \param[in] list  Initialized list object
 *  \return last node in list or NULL if none
 */
ares__llist_node_t *ares__llist_node_last(ares__llist_t *list);

/*! Obtain a node based on its index.  This is an O(n) operation.
 *
 *  \param[in] list Initialized list object
 *  \param[in] idx  Index of node to retrieve
 *  \return node at index or NULL if invalid index
 */
ares__llist_node_t *ares__llist_node_idx(ares__llist_t *list, size_t idx);

/*! Obtain next node in respect to specified node
 *
 *  \param[in] node  Node referenced
 *  \return node or NULL if none
 */
ares__llist_node_t *ares__llist_node_next(ares__llist_node_t *node);

/*! Obtain previous node in respect to specified node
 *
 *  \param[in] node  Node referenced
 *  \return node or NULL if none
 */
ares__llist_node_t *ares__llist_node_prev(ares__llist_node_t *node);


/*! Obtain value from node
 *
 *  \param[in] node  Node referenced
 *  \return user provided value from node
 */
void               *ares__llist_node_val(ares__llist_node_t *node);

/*! Obtain the number of entries in the list
 *
 *  \param[in] list  Initialized list object
 *  \return count
 */
size_t              ares__llist_len(const ares__llist_t *list);

/*! Clear all entries in the list, but don't destroy the list object.
 *
 *  \param[in] list  Initialized list object
 */
void                ares__llist_clear(ares__llist_t *list);

/*! Obtain list object from referenced node
 *
 *  \param[in] node  Node referenced
 *  \return list object node belongs to
 */
ares__llist_t      *ares__llist_node_parent(ares__llist_node_t *node);

/*! Obtain the first user-supplied value in the list
 *
 *  \param[in] list Initialized list object
 *  \return first user supplied value or NULL if none
 */
void               *ares__llist_first_val(ares__llist_t *list);

/*! Obtain the last user-supplied value in the list
 *
 *  \param[in] list Initialized list object
 *  \return last user supplied value or NULL if none
 */
void               *ares__llist_last_val(ares__llist_t *list);

/*! Take ownership of user-supplied value in list without calling destructor.
 *  Will unchain entry from list.
 *
 *  \param[in] node Node referenced
 *  \return user supplied value
 */
void               *ares__llist_node_claim(ares__llist_node_t *node);

/*! Replace user-supplied value for node
 *
 *  \param[in] node Node referenced
 *  \param[in] val  new user-supplied value
 */
void ares__llist_node_replace(ares__llist_node_t *node, void *val);

/*! Destroy the node, removing it from the list and calling destructor.
 *
 *  \param[in] node  Node referenced
 */
void ares__llist_node_destroy(ares__llist_node_t *node);

/*! Destroy the list object and all nodes in the list.
 *
 *  \param[in] list Initialized list object
 */
void ares__llist_destroy(ares__llist_t *list);

/*! Detach node from the current list and re-attach it to the new list as the
 *  last entry.
 *
 * \param[in] node       node to move
 * \param[in] new_parent new list
 */
void ares__llist_node_move_parent_last(ares__llist_node_t *node,
                                       ares__llist_t      *new_parent);

/*! Detach node from the current list and re-attach it to the new list as the
 *  first entry.
 *
 * \param[in] node       node to move
 * \param[in] new_parent new list
 */
void ares__llist_node_move_parent_first(ares__llist_node_t *node,
                                        ares__llist_t      *new_parent);
/*! @} */

#endif /* __ARES__LLIST_H */
