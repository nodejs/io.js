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
#ifndef __ARES__SLIST_H
#define __ARES__SLIST_H


/*! \addtogroup ares__slist SkipList Data Structure
 *
 * This data structure is known as a Skip List, which in essence is a sorted
 * linked list with multiple levels of linkage to gain some algorithmic
 * advantages.  The usage symantecs are almost identical to what you'd expect
 * with a linked list.
 *
 * Average time complexity:
 *  - Insert: O(log n)
 *  - Search: O(log n)
 *  - Delete: O(1)   -- delete assumes you hold a node pointer
 *
 * It should be noted, however, that "effort" involved with an insert or
 * remove operation is higher than a normal linked list.  For very small
 * lists this may be less efficient, but for any list with a moderate number
 * of entries this will prove much more efficient.
 *
 * This data structure is often compared with a Binary Search Tree in
 * functionality and usage.
 *
 * @{
 */
struct ares__slist;

/*! SkipList Object, opaque */
typedef struct ares__slist ares__slist_t;

struct ares__slist_node;

/*! SkipList Node Object, opaque */
typedef struct ares__slist_node ares__slist_node_t;

/*! SkipList Node Value destructor callback
 *
 *  \param[in] data  User-defined data to destroy
 */
typedef void (*ares__slist_destructor_t)(void *data);

/*! SkipList comparison function
 *
 *  \param[in] data1 First user-defined data object
 *  \param[in] data2 Second user-defined data object
 *  \return < 0 if data1 < data1, > 0 if data1 > data2, 0 if data1 == data2
 */
typedef int (*ares__slist_cmp_t)(const void *data1, const void *data2);

/*! Create SkipList
 *
 *  \param[in] rand_state   Initialized ares random state.
 *  \param[in] cmp          SkipList comparison function
 *  \param[in] destruct     SkipList Node Value Destructor. Optional, use NULL.
 *  \return Initialized SkipList Object or NULL on misuse or ENOMEM
 */
ares__slist_t      *ares__slist_create(ares_rand_state         *rand_state,
                                       ares__slist_cmp_t        cmp,
                                       ares__slist_destructor_t destruct);

/*! Replace SkipList Node Value Destructor
 *
 *  \param[in] list      Initialized SkipList Object
 *  \param[in] destruct  Replacement destructor. May be NULL.
 */
void                ares__slist_replace_destructor(ares__slist_t           *list,
                                                   ares__slist_destructor_t destruct);

/*! Insert Value into SkipList
 *
 *  \param[in] list   Initialized SkipList Object
 *  \param[in] val    Node Value. Must not be NULL.  Function takes ownership
 *                    and will have destructor called.
 *  \return SkipList Node Object or NULL on misuse or ENOMEM
 */
ares__slist_node_t *ares__slist_insert(ares__slist_t *list, void *val);

/*! Fetch first node in SkipList
 *
 *  \param[in] list  Initialized SkipList Object
 *  \return SkipList Node Object or NULL if none
 */
ares__slist_node_t *ares__slist_node_first(ares__slist_t *list);

/*! Fetch last node in SkipList
 *
 *  \param[in] list  Initialized SkipList Object
 *  \return SkipList Node Object or NULL if none
 */
ares__slist_node_t *ares__slist_node_last(ares__slist_t *list);

/*! Fetch next node in SkipList
 *
 *  \param[in] node  SkipList Node Object
 *  \return SkipList Node Object or NULL if none
 */
ares__slist_node_t *ares__slist_node_next(ares__slist_node_t *node);

/*! Fetch previous node in SkipList
 *
 *  \param[in] node  SkipList Node Object
 *  \return SkipList Node Object or NULL if none
 */
ares__slist_node_t *ares__slist_node_prev(ares__slist_node_t *node);

/*! Fetch SkipList Node Object by Value
 *
 *  \param[in] list  Initialized SkipList Object
 *  \param[in] val   Object to use for comparison
 *  \return SkipList Node Object or NULL if not found
 */
ares__slist_node_t *ares__slist_node_find(ares__slist_t *list, const void *val);


/*! Fetch Node Value
 *
 *  \param[in] node  SkipList Node Object
 *  \return user defined node value
 */
void               *ares__slist_node_val(ares__slist_node_t *node);

/*! Fetch number of entries in SkipList Object
 *
 *  \param[in] list  Initialized SkipList Object
 *  \return number of entries
 */
size_t              ares__slist_len(const ares__slist_t *list);

/*! Fetch SkipList Object from SkipList Node
 *
 *  \param[in] node  SkipList Node Object
 *  \return SkipList Object
 */
ares__slist_t      *ares__slist_node_parent(ares__slist_node_t *node);

/*! Fetch first Node Value in SkipList
 *
 *  \param[in] list  Initialized SkipList Object
 *  \return user defined node value or NULL if none
 */
void               *ares__slist_first_val(ares__slist_t *list);

/*! Fetch last Node Value in SkipList
 *
 *  \param[in] list  Initialized SkipList Object
 *  \return user defined node value or NULL if none
 */
void               *ares__slist_last_val(ares__slist_t *list);

/*! Take back ownership of Node Value in SkipList, remove from SkipList.
 *
 *  \param[in] node  SkipList Node Object
 *  \return user defined node value
 */
void               *ares__slist_node_claim(ares__slist_node_t *node);

/*! The internals of the node have changed, thus its position in the sorted
 *  list is no longer valid.  This function will remove it and re-add it to
 *  the proper position without needing to perform any memory allocations
 *  and thus cannot fail.
 *
 *  \param[in] node  SkipList Node Object
 */
void                ares__slist_node_reinsert(ares__slist_node_t *node);

/*! Remove Node from SkipList, calling destructor for Node Value.
 *
 *  \param[in] node  SkipList Node Object
 */
void                ares__slist_node_destroy(ares__slist_node_t *node);

/*! Destroy SkipList Object.  If there are any nodes, they will be destroyed.
 *
 *  \param[in] list  Initialized SkipList Object
 */
void                ares__slist_destroy(ares__slist_t *list);

/*! @} */

#endif /* __ARES__SLIST_H */
