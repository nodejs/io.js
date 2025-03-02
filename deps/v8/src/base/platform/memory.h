// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_MEMORY_H_
#define V8_BASE_PLATFORM_MEMORY_H_

#include <cstddef>
#include <cstdlib>

#include "include/v8config.h"
#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/macros.h"

#if V8_OS_STARBOARD
#include "starboard/memory.h"
#endif  // V8_OS_STARBOARD

#if V8_OS_DARWIN
#include <malloc/malloc.h>
#elif V8_OS_OPENBSD
#include <sys/malloc.h>
#elif V8_OS_ZOS
#include <stdlib.h>
#else
#include <malloc.h>
#endif

#if (V8_OS_POSIX && !V8_OS_AIX && !V8_OS_SOLARIS && !V8_OS_ZOS && !V8_OS_OPENBSD) || V8_OS_WIN
#define V8_HAS_MALLOC_USABLE_SIZE 1
#endif

namespace v8::base {

inline void* Malloc(size_t size) {
#if V8_OS_STARBOARD
  return SbMemoryAllocate(size);
#elif V8_OS_AIX && _LINUX_SOURCE_COMPAT
  // Work around for GCC bug on AIX.
  // See: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=79839
  return __linux_malloc(size);
#else
  return malloc(size);
#endif
}

inline void* Realloc(void* memory, size_t size) {
  // The result of realloc with zero size is implementation dependent.
  // Disallow it.
  CHECK_NE(0, size);
#if V8_OS_STARBOARD
  return SbMemoryReallocate(memory, size);
#elif V8_OS_AIX && _LINUX_SOURCE_COMPAT
  // Work around for GCC bug on AIX, see Malloc().
  // See: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=79839
  return __linux_realloc(memory, size);
#else
  return realloc(memory, size);
#endif
}

inline void Free(void* memory) {
#if V8_OS_STARBOARD
  return SbMemoryDeallocate(memory);
#else   // !V8_OS_STARBOARD
  return free(memory);
#endif  // !V8_OS_STARBOARD
}

inline void* Calloc(size_t count, size_t size) {
#if V8_OS_STARBOARD
  return SbMemoryCalloc(count, size);
#elif V8_OS_AIX && _LINUX_SOURCE_COMPAT
  // Work around for GCC bug on AIX, see Malloc().
  return __linux_calloc(count, size);
#else
  return calloc(count, size);
#endif
}

// Aligned allocation. Memory must be freed with `AlignedFree()` as not all
// platforms support using general free for aligned allocations.
inline void* AlignedAlloc(size_t size, size_t alignment) {
  DCHECK_LE(alignof(void*), alignment);
  DCHECK(base::bits::IsPowerOfTwo(alignment));
#if V8_OS_WIN
  return _aligned_malloc(size, alignment);
#elif V8_LIBC_BIONIC
  // posix_memalign is not exposed in some Android versions, so we fall back to
  // memalign. See http://code.google.com/p/android/issues/detail?id=35391.
  return memalign(alignment, size);
#elif V8_OS_ZOS
  return __aligned_malloc(size, alignment);
#else   // POSIX
  void* ptr;
  if (posix_memalign(&ptr, alignment, size)) ptr = nullptr;
  return ptr;
#endif  // POSIX
}

inline void AlignedFree(void* ptr) {
#if V8_OS_WIN
  _aligned_free(ptr);
#elif V8_OS_ZOS
  __aligned_free(ptr);
#else
  // Using regular Free() is not correct in general. For most platforms,
  // including V8_LIBC_BIONIC, it is though.
  base::Free(ptr);
#endif
}

#if V8_HAS_MALLOC_USABLE_SIZE

// Note that the use of additional bytes that deviate from the original
// `Malloc()` request returned by `MallocUsableSize()` is not UBSan-safe. Use
// `AllocateAtLeast()` for a safe version.
inline size_t MallocUsableSize(void* ptr) {
#if V8_OS_WIN
  // |_msize| cannot handle a null pointer.
  if (!ptr) return 0;
  return _msize(ptr);
#elif V8_OS_DARWIN
  return malloc_size(ptr);
#else   // POSIX.
  return malloc_usable_size(ptr);
#endif  // POSIX.
}

#endif  // V8_HAS_MALLOC_USABLE_SIZE

// Mimics C++23 `allocation_result`.
template <class Pointer>
struct AllocationResult {
  Pointer ptr = nullptr;
  size_t count = 0;
};

// Allocates at least `n * sizeof(T)` uninitialized storage but may allocate
// more which is indicated by the return value. Mimics C++23
// `allocate_at_least()`.
template <typename T>
V8_NODISCARD AllocationResult<T*> AllocateAtLeast(size_t n) {
  const size_t min_wanted_size = n * sizeof(T);
  auto* memory = static_cast<T*>(Malloc(min_wanted_size));
#if !V8_HAS_MALLOC_USABLE_SIZE
  return {memory, min_wanted_size};
#else  // V8_HAS_MALLOC_USABLE_SIZE
  const size_t usable_size = MallocUsableSize(memory);
#if V8_USE_UNDEFINED_BEHAVIOR_SANITIZER
  if (memory == nullptr)
    return {nullptr, 0};
  // UBSan (specifically, -fsanitize=bounds) assumes that any access outside
  // of the requested size for malloc is UB and will trap in ud2 instructions.
  // This can be worked around by using `Realloc()` on the specific memory
  // region.
  if (usable_size != min_wanted_size) {
    memory = static_cast<T*>(Realloc(memory, usable_size));
  }
#endif  // V8_USE_UNDEFINED_BEHAVIOR_SANITIZER
  return {memory, usable_size};
#endif  // V8_HAS_MALLOC_USABLE_SIZE
}

}  // namespace v8::base

#undef V8_HAS_MALLOC_USABLE_SIZE

#endif  // V8_BASE_PLATFORM_MEMORY_H_
