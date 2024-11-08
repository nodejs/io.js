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
#include "ares_private.h"
#include "ares_array.h"

#define ARES__ARRAY_MIN 4

struct ares_array {
  ares_array_destructor_t destruct;
  void                   *arr;
  size_t                  member_size;
  size_t                  cnt;
  size_t                  offset;
  size_t                  alloc_cnt;
};

ares_array_t *ares_array_create(size_t                  member_size,
                                ares_array_destructor_t destruct)
{
  ares_array_t *arr;

  if (member_size == 0) {
    return NULL;
  }

  arr = ares_malloc_zero(sizeof(*arr));
  if (arr == NULL) {
    return NULL;
  }

  arr->member_size = member_size;
  arr->destruct    = destruct;
  return arr;
}

size_t ares_array_len(const ares_array_t *arr)
{
  if (arr == NULL) {
    return 0;
  }
  return arr->cnt;
}

void *ares_array_at(ares_array_t *arr, size_t idx)
{
  if (arr == NULL || idx >= arr->cnt) {
    return NULL;
  }
  return (unsigned char *)arr->arr + ((idx + arr->offset) * arr->member_size);
}

const void *ares_array_at_const(const ares_array_t *arr, size_t idx)
{
  if (arr == NULL || idx >= arr->cnt) {
    return NULL;
  }
  return (unsigned char *)arr->arr + ((idx + arr->offset) * arr->member_size);
}

ares_status_t ares_array_sort(ares_array_t *arr, ares_array_cmp_t cmp)
{
  if (arr == NULL || cmp == NULL) {
    return ARES_EFORMERR;
  }

  /* Nothing to sort */
  if (arr->cnt < 2) {
    return ARES_SUCCESS;
  }

  qsort((unsigned char *)arr->arr + (arr->offset * arr->member_size), arr->cnt,
        arr->member_size, cmp);
  return ARES_SUCCESS;
}

void ares_array_destroy(ares_array_t *arr)
{
  size_t i;

  if (arr == NULL) {
    return;
  }

  if (arr->destruct != NULL) {
    for (i = 0; i < arr->cnt; i++) {
      arr->destruct(ares_array_at(arr, i));
    }
  }

  ares_free(arr->arr);
  ares_free(arr);
}

/* NOTE: this function operates on actual indexes, NOT indexes using the
 *       arr->offset */
static ares_status_t ares_array_move(ares_array_t *arr, size_t dest_idx,
                                     size_t src_idx)
{
  void       *dest_ptr;
  const void *src_ptr;
  size_t      nmembers;

  if (arr == NULL || dest_idx >= arr->alloc_cnt || src_idx >= arr->alloc_cnt) {
    return ARES_EFORMERR;
  }

  /* Nothing to do */
  if (dest_idx == src_idx) {
    return ARES_SUCCESS;
  }

  dest_ptr = (unsigned char *)arr->arr + (dest_idx * arr->member_size);
  src_ptr  = (unsigned char *)arr->arr + (src_idx * arr->member_size);

  /* Check to make sure shifting to the right won't overflow our allocation
   * boundary */
  if (dest_idx > src_idx && arr->cnt + (dest_idx - src_idx) > arr->alloc_cnt) {
    return ARES_EFORMERR;
  }

  nmembers = arr->cnt - (src_idx - arr->offset);
  memmove(dest_ptr, src_ptr, nmembers * arr->member_size);

  return ARES_SUCCESS;
}

void *ares_array_finish(ares_array_t *arr, size_t *num_members)
{
  void *ptr;

  if (arr == NULL || num_members == NULL) {
    return NULL;
  }

  /* Make sure we move data to beginning of allocation */
  if (arr->offset != 0) {
    if (ares_array_move(arr, 0, arr->offset) != ARES_SUCCESS) {
      return NULL;
    }
    arr->offset = 0;
  }

  ptr          = arr->arr;
  *num_members = arr->cnt;
  ares_free(arr);
  return ptr;
}

ares_status_t ares_array_set_size(ares_array_t *arr, size_t size)
{
  void *temp;

  if (arr == NULL || size == 0 || size < arr->cnt) {
    return ARES_EFORMERR;
  }

  /* Always operate on powers of 2 */
  size = ares_round_up_pow2(size);

  if (size < ARES__ARRAY_MIN) {
    size = ARES__ARRAY_MIN;
  }

  /* If our allocation size is already large enough, skip */
  if (size <= arr->alloc_cnt) {
    return ARES_SUCCESS;
  }

  temp = ares_realloc_zero(arr->arr, arr->alloc_cnt * arr->member_size,
                           size * arr->member_size);
  if (temp == NULL) {
    return ARES_ENOMEM;
  }
  arr->alloc_cnt = size;
  arr->arr       = temp;
  return ARES_SUCCESS;
}

ares_status_t ares_array_insert_at(void **elem_ptr, ares_array_t *arr,
                                   size_t idx)
{
  void         *ptr;
  ares_status_t status;

  if (arr == NULL) {
    return ARES_EFORMERR;
  }

  /* Not >= since we are allowed to append to the end */
  if (idx > arr->cnt) {
    return ARES_EFORMERR;
  }

  /* Allocate more if needed */
  status = ares_array_set_size(arr, arr->cnt + 1);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Shift if we have memory but not enough room at the end */
  if (arr->cnt + 1 + arr->offset > arr->alloc_cnt) {
    status = ares_array_move(arr, 0, arr->offset);
    if (status != ARES_SUCCESS) {
      return status;
    }
    arr->offset = 0;
  }

  /* If we're inserting anywhere other than the end, we need to move some
   * elements out of the way */
  if (idx != arr->cnt) {
    status = ares_array_move(arr, idx + arr->offset + 1, idx + arr->offset);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  /* Ok, we're guaranteed to have a gap where we need it, lets zero it out,
   * and return it */
  ptr = (unsigned char *)arr->arr + ((idx + arr->offset) * arr->member_size);
  memset(ptr, 0, arr->member_size);
  arr->cnt++;

  if (elem_ptr) {
    *elem_ptr = ptr;
  }

  return ARES_SUCCESS;
}

ares_status_t ares_array_insert_last(void **elem_ptr, ares_array_t *arr)
{
  return ares_array_insert_at(elem_ptr, arr, ares_array_len(arr));
}

ares_status_t ares_array_insert_first(void **elem_ptr, ares_array_t *arr)
{
  return ares_array_insert_at(elem_ptr, arr, 0);
}

ares_status_t ares_array_insertdata_at(ares_array_t *arr, size_t idx,
                                       const void *data_ptr)
{
  ares_status_t status;
  void         *ptr = NULL;

  status = ares_array_insert_at(&ptr, arr, idx);
  if (status != ARES_SUCCESS) {
    return status;
  }
  memcpy(ptr, data_ptr, arr->member_size);
  return ARES_SUCCESS;
}

ares_status_t ares_array_insertdata_last(ares_array_t *arr,
                                         const void   *data_ptr)
{
  ares_status_t status;
  void         *ptr = NULL;

  status = ares_array_insert_last(&ptr, arr);
  if (status != ARES_SUCCESS) {
    return status;
  }
  memcpy(ptr, data_ptr, arr->member_size);
  return ARES_SUCCESS;
}

ares_status_t ares_array_insertdata_first(ares_array_t *arr,
                                          const void   *data_ptr)
{
  ares_status_t status;
  void         *ptr = NULL;

  status = ares_array_insert_last(&ptr, arr);
  if (status != ARES_SUCCESS) {
    return status;
  }
  memcpy(ptr, data_ptr, arr->member_size);
  return ARES_SUCCESS;
}

void *ares_array_first(ares_array_t *arr)
{
  return ares_array_at(arr, 0);
}

void *ares_array_last(ares_array_t *arr)
{
  size_t cnt = ares_array_len(arr);
  if (cnt == 0) {
    return NULL;
  }
  return ares_array_at(arr, cnt - 1);
}

const void *ares_array_first_const(const ares_array_t *arr)
{
  return ares_array_at_const(arr, 0);
}

const void *ares_array_last_const(const ares_array_t *arr)
{
  size_t cnt = ares_array_len(arr);
  if (cnt == 0) {
    return NULL;
  }
  return ares_array_at_const(arr, cnt - 1);
}

ares_status_t ares_array_claim_at(void *dest, size_t dest_size,
                                  ares_array_t *arr, size_t idx)
{
  ares_status_t status;

  if (arr == NULL || idx >= arr->cnt) {
    return ARES_EFORMERR;
  }

  if (dest != NULL && dest_size < arr->member_size) {
    return ARES_EFORMERR;
  }

  if (dest) {
    memcpy(dest, ares_array_at(arr, idx), arr->member_size);
  }

  if (idx == 0) {
    /* Optimization, if first element, just increment offset, makes removing a
     * lot from the start quick */
    arr->offset++;
  } else if (idx != arr->cnt - 1) {
    /* Must shift entire array if removing an element from the middle. Does
     * nothing if removing last element other than decrement count. */
    status = ares_array_move(arr, idx + arr->offset, idx + arr->offset + 1);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  arr->cnt--;
  return ARES_SUCCESS;
}

ares_status_t ares_array_remove_at(ares_array_t *arr, size_t idx)
{
  void *ptr = ares_array_at(arr, idx);
  if (arr == NULL || ptr == NULL) {
    return ARES_EFORMERR;
  }

  if (arr->destruct != NULL) {
    arr->destruct(ptr);
  }

  return ares_array_claim_at(NULL, 0, arr, idx);
}

ares_status_t ares_array_remove_first(ares_array_t *arr)
{
  return ares_array_remove_at(arr, 0);
}

ares_status_t ares_array_remove_last(ares_array_t *arr)
{
  size_t cnt = ares_array_len(arr);
  if (cnt == 0) {
    return ARES_EFORMERR;
  }
  return ares_array_remove_at(arr, cnt - 1);
}
