/* Copyright 2015 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Algorithms for distributing the literals and commands of a metablock between
   block types and contexts. */

#include "memory.h"

#include <stdlib.h>  /* exit, free, malloc */
#include <string.h>  /* memcpy */

#include <brotli/types.h>

#include "../common/platform.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define MAX_NEW_ALLOCATED (BROTLI_ENCODER_MEMORY_MANAGER_SLOTS >> 2)
#define MAX_NEW_FREED (BROTLI_ENCODER_MEMORY_MANAGER_SLOTS >> 2)
#define MAX_PERM_ALLOCATED (BROTLI_ENCODER_MEMORY_MANAGER_SLOTS >> 1)

#define PERM_ALLOCATED_OFFSET 0
#define NEW_ALLOCATED_OFFSET MAX_PERM_ALLOCATED
#define NEW_FREED_OFFSET (MAX_PERM_ALLOCATED + MAX_NEW_ALLOCATED)

void BrotliInitMemoryManager(
    MemoryManager* m, brotli_alloc_func alloc_func, brotli_free_func free_func,
    void* opaque) {
  if (!alloc_func) {
    m->alloc_func = BrotliDefaultAllocFunc;
    m->free_func = BrotliDefaultFreeFunc;
    m->opaque = 0;
  } else {
    m->alloc_func = alloc_func;
    m->free_func = free_func;
    m->opaque = opaque;
  }
#if !defined(BROTLI_ENCODER_EXIT_ON_OOM)
  m->is_oom = BROTLI_FALSE;
  m->perm_allocated = 0;
  m->new_allocated = 0;
  m->new_freed = 0;
#endif  /* BROTLI_ENCODER_EXIT_ON_OOM */
}

#if defined(BROTLI_ENCODER_EXIT_ON_OOM)

void* BrotliAllocate(MemoryManager* m, size_t n) {
  void* result = m->alloc_func(m->opaque, n);
  if (!result) exit(EXIT_FAILURE);
  return result;
}

void BrotliFree(MemoryManager* m, void* p) {
  m->free_func(m->opaque, p);
}

void BrotliWipeOutMemoryManager(MemoryManager* m) {
  BROTLI_UNUSED(m);
}

#else  /* BROTLI_ENCODER_EXIT_ON_OOM */

static void SortPointers(void** items, const size_t n) {
  /* Shell sort. */
  /* TODO(eustas): fine-tune for "many slots" case */
  static const size_t gaps[] = {23, 10, 4, 1};
  int g = 0;
  for (; g < 4; ++g) {
    size_t gap = gaps[g];
    size_t i;
    for (i = gap; i < n; ++i) {
      size_t j = i;
      void* tmp = items[i];
      for (; j >= gap && tmp < items[j - gap]; j -= gap) {
        items[j] = items[j - gap];
      }
      items[j] = tmp;
    }
  }
}

static size_t Annihilate(void** a, size_t a_len, void** b, size_t b_len) {
  size_t a_read_index = 0;
  size_t b_read_index = 0;
  size_t a_write_index = 0;
  size_t b_write_index = 0;
  size_t annihilated = 0;
  while (a_read_index < a_len && b_read_index < b_len) {
    if (a[a_read_index] == b[b_read_index]) {
      a_read_index++;
      b_read_index++;
      annihilated++;
    } else if (a[a_read_index] < b[b_read_index]) {
      a[a_write_index++] = a[a_read_index++];
    } else {
      b[b_write_index++] = b[b_read_index++];
    }
  }
  while (a_read_index < a_len) a[a_write_index++] = a[a_read_index++];
  while (b_read_index < b_len) b[b_write_index++] = b[b_read_index++];
  return annihilated;
}

static void CollectGarbagePointers(MemoryManager* m) {
  size_t annihilated;
  SortPointers(m->pointers + NEW_ALLOCATED_OFFSET, m->new_allocated);
  SortPointers(m->pointers + NEW_FREED_OFFSET, m->new_freed);
  annihilated = Annihilate(
      m->pointers + NEW_ALLOCATED_OFFSET, m->new_allocated,
      m->pointers + NEW_FREED_OFFSET, m->new_freed);
  m->new_allocated -= annihilated;
  m->new_freed -= annihilated;

  if (m->new_freed != 0) {
    annihilated = Annihilate(
        m->pointers + PERM_ALLOCATED_OFFSET, m->perm_allocated,
        m->pointers + NEW_FREED_OFFSET, m->new_freed);
    m->perm_allocated -= annihilated;
    m->new_freed -= annihilated;
    BROTLI_DCHECK(m->new_freed == 0);
  }

  if (m->new_allocated != 0) {
    BROTLI_DCHECK(m->perm_allocated + m->new_allocated <= MAX_PERM_ALLOCATED);
    memcpy(m->pointers + PERM_ALLOCATED_OFFSET + m->perm_allocated,
           m->pointers + NEW_ALLOCATED_OFFSET,
           sizeof(void*) * m->new_allocated);
    m->perm_allocated += m->new_allocated;
    m->new_allocated = 0;
    SortPointers(m->pointers + PERM_ALLOCATED_OFFSET, m->perm_allocated);
  }
}

void* BrotliAllocate(MemoryManager* m, size_t n) {
  void* result = m->alloc_func(m->opaque, n);
  if (!result) {
    m->is_oom = BROTLI_TRUE;
    return NULL;
  }
  if (m->new_allocated == MAX_NEW_ALLOCATED) CollectGarbagePointers(m);
  m->pointers[NEW_ALLOCATED_OFFSET + (m->new_allocated++)] = result;
  return result;
}

void BrotliFree(MemoryManager* m, void* p) {
  if (!p) return;
  m->free_func(m->opaque, p);
  if (m->new_freed == MAX_NEW_FREED) CollectGarbagePointers(m);
  m->pointers[NEW_FREED_OFFSET + (m->new_freed++)] = p;
}

void BrotliWipeOutMemoryManager(MemoryManager* m) {
  size_t i;
  CollectGarbagePointers(m);
  /* Now all unfreed pointers are in perm-allocated list. */
  for (i = 0; i < m->perm_allocated; ++i) {
    m->free_func(m->opaque, m->pointers[PERM_ALLOCATED_OFFSET + i]);
  }
  m->perm_allocated = 0;
}

#endif  /* BROTLI_ENCODER_EXIT_ON_OOM */

void* BrotliBootstrapAlloc(size_t size,
    brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque) {
  if (!alloc_func && !free_func) {
    return malloc(size);
  } else if (alloc_func && free_func) {
    return alloc_func(opaque, size);
  }
  return NULL;
}

void BrotliBootstrapFree(void* address, MemoryManager* m) {
  if (!address) {
    /* Should not happen! */
    return;
  } else {
    /* Copy values, as those would be freed. */
    brotli_free_func free_func = m->free_func;
    void* opaque = m->opaque;
    free_func(opaque, address);
  }
}

#if defined(__cplusplus) || defined(c_plusplus)
}  /* extern "C" */
#endif
