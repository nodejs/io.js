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
#ifndef __ARES__ARRAY_H
#define __ARES__ARRAY_H

#include "ares.h"

/*! \addtogroup ares_array Array Data Structure
 *
 * This is an array with helpers.  It is meant to have as little overhead
 * as possible over direct array management by applications but to provide
 * safety and some optimization features.  It can also return the array in
 * native form once all manipulation has been performed.
 *
 * @{
 */

struct ares_array;

/*! Opaque data structure for array */
typedef struct ares_array ares_array_t;

/*! Callback to free user-defined member data
 *
 *  \param[in] data  pointer to member of array to be destroyed. The pointer
 *                   itself must not be destroyed, just the data it contains.
 */
typedef void (*ares_array_destructor_t)(void *data);

/*! Callback to compare two array elements used for sorting
 *
 *  \param[in] data1 array member 1
 *  \param[in] data2 array member 2
 *  \return < 0 if data1 < data2, > 0 if data1 > data2, 0 if data1 == data2
 */
typedef int (*ares_array_cmp_t)(const void *data1, const void *data2);

/*! Create an array object
 *
 *  NOTE: members of the array are typically going to be an going to be a
 *        struct with compiler/ABI specific padding to ensure proper alignment.
 *        Care needs to be taken if using primitive types, especially floating
 *        point numbers which size may not indicate the required alignment.
 *        For example, a double may be 80 bits (10 bytes), but required
 *        alignment of 16 bytes.  In such a case, a member_size of 16 would be
 *        required to be used.
 *
 *  \param[in] destruct     Optional. Destructor to call on a removed member
 *  \param[in] member_size  Size of array member, usually determined using
 *                          sizeof() for the member such as a struct.
 *
 *  \return array object or NULL on out of memory
 */
CARES_EXTERN ares_array_t *ares_array_create(size_t member_size,
                                             ares_array_destructor_t destruct);


/*! Request the array be at least the requested size.  Useful if the desired
 *  array size is known prior to populating the array to prevent reallocations.
 *
 *  \param[in] arr  Initialized array object.
 *  \param[in] size Minimum number of members
 *  \return ARES_SUCCESS on success, ARES_EFORMERR on misuse,
 *    ARES_ENOMEM on out of memory */
CARES_EXTERN ares_status_t ares_array_set_size(ares_array_t *arr, size_t size);

/*! Sort the array using the given comparison function.  This is not
 *  persistent, any future elements inserted will not maintain this sort.
 *
 *  \param[in]  arr      Initialized array object.
 *  \param[in]  cb       Sort callback
 *  \return ARES_SUCCESS on success
 */
CARES_EXTERN ares_status_t ares_array_sort(ares_array_t    *arr,
                                           ares_array_cmp_t cmp);

/*! Destroy an array object.  If a destructor is set, will be called on each
 *  member of the array.
 *
 *  \param[in] arr     Initialized array object.
 */
CARES_EXTERN void          ares_array_destroy(ares_array_t *arr);

/*! Retrieve the array in the native format.  This will also destroy the
 *  container.  It is the responsibility of the caller to free the returned
 *  pointer and also any data within each array element.
 *
 *  \param[in] arr  Initialized array object
 *  \param[out] num_members the number of members in the returned array
 *  \return pointer to native array on success, NULL on failure.
 */
CARES_EXTERN void  *ares_array_finish(ares_array_t *arr, size_t *num_members);

/*! Retrieve the number of members in the array
 *
 *  \param[in] arr     Initialized array object.
 *  \return numbrer of members
 */
CARES_EXTERN size_t ares_array_len(const ares_array_t *arr);

/*! Insert a new array member at the given index
 *
 *  \param[out] elem_ptr Optional. Pointer to the returned array element.
 *  \param[in]  arr      Initialized array object.
 *  \param[in]  idx      Index in array to place new element, will shift any
 *                       elements down that exist after this point.
 *  \return ARES_SUCCESS on success, ARES_EFORMERR on bad index,
 *          ARES_ENOMEM on out of memory.
 */
CARES_EXTERN ares_status_t ares_array_insert_at(void        **elem_ptr,
                                                ares_array_t *arr, size_t idx);

/*! Insert a new array member at the end of the array
 *
 *  \param[out] elem_ptr Optional. Pointer to the returned array element.
 *  \param[in]  arr      Initialized array object.
 *  \return ARES_SUCCESS on success, ARES_ENOMEM on out of memory.
 */
CARES_EXTERN ares_status_t ares_array_insert_last(void        **elem_ptr,
                                                  ares_array_t *arr);

/*! Insert a new array member at the beginning of the array
 *
 *  \param[out] elem_ptr Optional. Pointer to the returned array element.
 *  \param[in]  arr      Initialized array object.
 *  \return ARES_SUCCESS on success, ARES_ENOMEM on out of memory.
 */
CARES_EXTERN ares_status_t ares_array_insert_first(void        **elem_ptr,
                                                   ares_array_t *arr);


/*! Insert a new array member at the given index and copy the data pointed
 *  to by the data pointer into the array.  This will copy member_size bytes
 *  from the provided pointer, this may not be safe for some data types
 *  that may have a smaller size than the provided member_size which includes
 *  padding as discussed in ares_array_create().
 *
 *  \param[in]  arr      Initialized array object.
 *  \param[in]  idx      Index in array to place new element, will shift any
 *                       elements down that exist after this point.
 *  \param[in]  data_ptr Pointer to data to copy into array.
 *  \return ARES_SUCCESS on success, ARES_EFORMERR on bad index or null data
 * ptr, ARES_ENOMEM on out of memory.
 */
CARES_EXTERN ares_status_t ares_array_insertdata_at(ares_array_t *arr,
                                                    size_t        idx,
                                                    const void   *data_ptr);

/*! Insert a new array member at the end of the array and copy the data pointed
 *  to by the data pointer into the array.  This will copy member_size bytes
 *  from the provided pointer, this may not be safe for some data types
 *  that may have a smaller size than the provided member_size which includes
 *  padding as discussed in ares_array_create().
 *
 *  \param[in]  arr      Initialized array object.
 *  \param[in]  data_ptr Pointer to data to copy into array.
 *  \return ARES_SUCCESS on success, ARES_EFORMERR on bad index or null data
 * ptr, ARES_ENOMEM on out of memory.
 */
CARES_EXTERN ares_status_t ares_array_insertdata_last(ares_array_t *arr,
                                                      const void   *data_ptr);

/*! Insert a new array member at the beginning of the array and copy the data
 * pointed to by the data pointer into the array.  This will copy member_size
 * bytes from the provided pointer, this may not be safe for some data types
 *  that may have a smaller size than the provided member_size which includes
 *  padding as discussed in ares_array_create().
 *
 *  \param[in]  arr      Initialized array object.
 *  \param[in]  data_ptr Pointer to data to copy into array.
 *  \return ARES_SUCCESS on success, ARES_EFORMERR on bad index or null data
 * ptr, ARES_ENOMEM on out of memory.
 */
CARES_EXTERN ares_status_t ares_array_insertdata_first(ares_array_t *arr,
                                                       const void   *data_ptr);

/*! Fetch a pointer to the given element in the array
 *  \param[in]  array  Initialized array object
 *  \param[in]  idx    Index to fetch
 *  \return pointer on success, NULL on failure */
CARES_EXTERN void         *ares_array_at(ares_array_t *arr, size_t idx);

/*! Fetch a pointer to the first element in the array
 *  \param[in]  array  Initialized array object
 *  \return pointer on success, NULL on failure */
CARES_EXTERN void         *ares_array_first(ares_array_t *arr);

/*! Fetch a pointer to the last element in the array
 *  \param[in]  array  Initialized array object
 *  \return pointer on success, NULL on failure */
CARES_EXTERN void         *ares_array_last(ares_array_t *arr);

/*! Fetch a constant pointer to the given element in the array
 *  \param[in]  array  Initialized array object
 *  \param[in]  idx    Index to fetch
 *  \return pointer on success, NULL on failure */
CARES_EXTERN const void   *ares_array_at_const(const ares_array_t *arr,
                                               size_t              idx);

/*! Fetch a constant pointer to the first element in the array
 *  \param[in]  array  Initialized array object
 *  \return pointer on success, NULL on failure */
CARES_EXTERN const void   *ares_array_first_const(const ares_array_t *arr);

/*! Fetch a constant pointer to the last element in the array
 *  \param[in]  array  Initialized array object
 *  \return pointer on success, NULL on failure */
CARES_EXTERN const void   *ares_array_last_const(const ares_array_t *arr);

/*! Claim the data from the specified array index, copying it to the buffer
 *  provided by the caller.  The index specified in the array will then be
 *  removed (without calling any possible destructor)
 *
 *  \param[in,out] dest      Optional. Buffer to hold array member. Pass NULL
 *                           if not needed.  This could leak memory if array
 *                           member needs destructor if not provided.
 *  \param[in]     dest_size Size of buffer provided, used as a sanity check.
 *                           Must match member_size provided to
 *                           ares_array_create() if dest_size specified.
 *  \param[in]     arr       Initialized array object
 *  \param[in]     idx       Index to claim
 *  \return ARES_SUCCESS on success, ARES_EFORMERR on usage failure.
 */
CARES_EXTERN ares_status_t ares_array_claim_at(void *dest, size_t dest_size,
                                               ares_array_t *arr, size_t idx);

/*! Remove the member at the specified array index.  The destructor will be
 *  called.
 *
 *  \param[in] arr  Initialized array object
 *  \param[in] idx  Index to remove
 *  \return ARES_SUCCESS if removed, ARES_EFORMERR on invalid use
 */
CARES_EXTERN ares_status_t ares_array_remove_at(ares_array_t *arr, size_t idx);

/*! Remove the first member of the array.
 *
 *  \param[in] arr  Initialized array object
 *  \return ARES_SUCCESS if removed, ARES_EFORMERR on invalid use
 */
CARES_EXTERN ares_status_t ares_array_remove_first(ares_array_t *arr);

/*! Remove the last member of the array.
 *
 *  \param[in] arr  Initialized array object
 *  \return ARES_SUCCESS if removed, ARES_EFORMERR on invalid use
 */
CARES_EXTERN ares_status_t ares_array_remove_last(ares_array_t *arr);


/*! @} */

#endif /* __ARES__ARRAY_H */
