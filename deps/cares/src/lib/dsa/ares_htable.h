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
#ifndef __ARES__HTABLE_H
#define __ARES__HTABLE_H


/*! \addtogroup ares_htable Base HashTable Data Structure
 *
 * This is a basic hashtable data structure that is meant to be wrapped
 * by a higher level implementation.  This data structure is designed to
 * be callback-based in order to facilitate wrapping without needing to
 * worry about any underlying complexities of the hashtable implementation.
 *
 * This implementation supports automatic growing by powers of 2 when reaching
 * 75% capacity.  A rehash will be performed on the expanded bucket list.
 *
 * Average time complexity:
 *  - Insert: O(1)
 *  - Search: O(1)
 *  - Delete: O(1)
 *
 * @{
 */

struct ares_htable;

/*! Opaque data type for generic hash table implementation */
typedef struct ares_htable ares_htable_t;

/*! Callback for generating a hash of the key.
 *
 *  \param[in] key   pointer to key to be hashed
 *  \param[in] seed  randomly generated seed used by hash function.
 *                   value is specific to the hashtable instance
 *                   but otherwise will not change between calls.
 *  \return hash
 */
typedef unsigned int (*ares_htable_hashfunc_t)(const void  *key,
                                               unsigned int seed);

/*! Callback to free the bucket
 *
 *  \param[in] bucket  user provided bucket
 */
typedef void (*ares_htable_bucket_free_t)(void *bucket);

/*! Callback to extract the key from the user-provided bucket
 *
 *  \param[in] bucket  user provided bucket
 *  \return pointer to key held in bucket
 */
typedef const void *(*ares_htable_bucket_key_t)(const void *bucket);

/*! Callback to compare two keys for equality
 *
 *  \param[in] key1  first key
 *  \param[in] key2  second key
 *  \return ARES_TRUE if equal, ARES_FALSE if not
 */
typedef ares_bool_t (*ares_htable_key_eq_t)(const void *key1, const void *key2);


/*! Destroy the initialized hashtable
 *
 *  \param[in] htable initialized hashtable
 */
void           ares_htable_destroy(ares_htable_t *htable);

/*! Create a new hashtable
 *
 *  \param[in] hash_func   Required. Callback for Hash function.
 *  \param[in] bucket_key  Required. Callback to extract key from bucket.
 *  \param[in] bucket_free Required. Callback to free bucket.
 *  \param[in] key_eq      Required. Callback to check for key equality.
 *  \return initialized hashtable.  NULL if out of memory or misuse.
 */
ares_htable_t *ares_htable_create(ares_htable_hashfunc_t    hash_func,
                                  ares_htable_bucket_key_t  bucket_key,
                                  ares_htable_bucket_free_t bucket_free,
                                  ares_htable_key_eq_t      key_eq);

/*! Count of keys from initialized hashtable
 *
 *  \param[in] htable  Initialized hashtable.
 *  \return count of keys
 */
size_t         ares_htable_num_keys(const ares_htable_t *htable);

/*! Retrieve an array of buckets from the hashtable.  This is mainly used as
 *  a helper for retrieving an array of keys.
 *
 *  \param[in]  htable   Initialized hashtable
 *  \param[out] num      Count of returned buckets
 *  \return Array of pointers to the buckets.  These are internal pointers
 *          to data within the hashtable, so if the key is removed, there
 *          will be a dangling pointer.  It is expected wrappers will make
 *          such values safe by duplicating them.
 */
const void **ares_htable_all_buckets(const ares_htable_t *htable, size_t *num);

/*! Insert bucket into hashtable
 *
 *  \param[in] htable  Initialized hashtable.
 *  \param[in] bucket  User-provided bucket to insert. Takes "ownership". Not
 *                     allowed to be NULL.
 *  \return ARES_TRUE on success, ARES_FALSE if out of memory
 */
ares_bool_t  ares_htable_insert(ares_htable_t *htable, void *bucket);

/*! Retrieve bucket from hashtable based on key.
 *
 *  \param[in] htable  Initialized hashtable
 *  \param[in] key     Pointer to key to use for comparison.
 *  \return matching bucket, or NULL if not found.
 */
void        *ares_htable_get(const ares_htable_t *htable, const void *key);

/*! Remove bucket from hashtable by key
 *
 *  \param[in] htable  Initialized hashtable
 *  \param[in] key     Pointer to key to use for comparison
 *  \return ARES_TRUE if found, ARES_FALSE if not found
 */
ares_bool_t  ares_htable_remove(ares_htable_t *htable, const void *key);

/*! FNV1a hash algorithm.  Can be used as underlying primitive for building
 *  a wrapper hashtable.
 *
 *  \param[in] key      pointer to key
 *  \param[in] key_len  Length of key
 *  \param[in] seed     Seed for generating hash
 *  \return hash value
 */
unsigned int ares_htable_hash_FNV1a(const unsigned char *key, size_t key_len,
                                    unsigned int seed);

/*! FNV1a hash algorithm, but converts all characters to lowercase before
 *  hashing to make the hash case-insensitive. Can be used as underlying
 *  primitive for building a wrapper hashtable.  Used on string-based keys.
 *
 *  \param[in] key      pointer to key
 *  \param[in] key_len  Length of key
 *  \param[in] seed     Seed for generating hash
 *  \return hash value
 */
unsigned int ares_htable_hash_FNV1a_casecmp(const unsigned char *key,
                                            size_t key_len, unsigned int seed);

/*! @} */

#endif /* __ARES__HTABLE_H */
