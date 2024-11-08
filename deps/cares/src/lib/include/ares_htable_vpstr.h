/* MIT License
 *
 * Copyright (c) 2024 Brad House
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
#ifndef __ARES__HTABLE_VPSTR_H
#define __ARES__HTABLE_VPSTR_H

/*! \addtogroup ares_htable_vpstr HashTable with void pointer Key and string
 *  value
 *
 * This data structure wraps the base ares_htable data structure in order to
 * split the key and value data types as void pointer and string, respectively.
 *
 * Average time complexity:
 *  - Insert: O(1)
 *  - Search: O(1)
 *  - Delete: O(1)
 *
 * @{
 */

struct ares_htable_vpstr;

/*! Opaque data type for void pointer key, string value hash table
 * implementation */
typedef struct ares_htable_vpstr ares_htable_vpstr_t;

/*! Destroy hashtable
 *
 *  \param[in] htable  Initialized hashtable
 */
CARES_EXTERN void ares_htable_vpstr_destroy(ares_htable_vpstr_t *htable);

/*! Create void pointer key, string value hash table
 *
 */
CARES_EXTERN ares_htable_vpstr_t *ares_htable_vpstr_create(void);

/*! Insert key/value into hash table
 *
 *  \param[in] htable Initialized hash table
 *  \param[in] key    key to associate with value
 *  \param[in] val    value to store (duplicates).
 *  \return ARES_TRUE on success, ARES_FALSE on failure or out of memory
 */
CARES_EXTERN ares_bool_t ares_htable_vpstr_insert(ares_htable_vpstr_t *htable,
                                                  void *key, const char *val);

/*! Retrieve value from hashtable based on key
 *
 *  \param[in]  htable  Initialized hash table
 *  \param[in]  key     key to use to search
 *  \param[out] val     Optional.  Pointer to store value.
 *  \return ARES_TRUE on success, ARES_FALSE on failure
 */
CARES_EXTERN ares_bool_t ares_htable_vpstr_get(
  const ares_htable_vpstr_t *htable, const void *key, const char **val);

/*! Retrieve value from hashtable directly as return value.  Caveat to this
 *  function over ares_htable_vpstr_get() is that if a NULL value is stored
 *  you cannot determine if the key is not found or the value is NULL.
 *
 *  \param[in] htable  Initialized hash table
 *  \param[in] key     key to use to search
 *  \return value associated with key in hashtable or NULL
 */
CARES_EXTERN const char *
  ares_htable_vpstr_get_direct(const ares_htable_vpstr_t *htable,
                               const void                *key);

/*! Remove a value from the hashtable by key
 *
 *  \param[in] htable  Initialized hash table
 *  \param[in] key     key to use to search
 *  \return ARES_TRUE if found, ARES_FALSE if not
 */
CARES_EXTERN ares_bool_t ares_htable_vpstr_remove(ares_htable_vpstr_t *htable,
                                                  const void          *key);

/*! Retrieve the number of keys stored in the hash table
 *
 *  \param[in] htable  Initialized hash table
 *  \return count
 */
CARES_EXTERN size_t
  ares_htable_vpstr_num_keys(const ares_htable_vpstr_t *htable);

/*! @} */

#endif /* __ARES__HTABLE_VPSTR_H */
