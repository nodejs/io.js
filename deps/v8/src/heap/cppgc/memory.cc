// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/memory.h"

#include <cstddef>

#include "src/heap/cppgc/globals.h"

namespace cppgc {
namespace internal {

void NoSanitizeMemset(void* address, char c, size_t bytes) {
  volatile uint8_t* const base = static_cast<uint8_t*>(address);
  for (size_t i = 0; i < bytes; ++i) {
    base[i] = c;
  }
}

#if defined(V8_USE_MEMORY_SANITIZER) || defined(V8_USE_ADDRESS_SANITIZER) || \
    DEBUG

void SetMemoryAccessible(void* address, size_t size) {
#if defined(V8_USE_MEMORY_SANITIZER)

  MSAN_MEMORY_IS_INITIALIZED(address, size);

#elif defined(V8_USE_ADDRESS_SANITIZER)

  ASAN_UNPOISON_MEMORY_REGION(address, size);

#else  // Debug builds.

  memset(address, 0, size);

#endif  // Debug builds.
}

void SetMemoryInaccessible(void* address, size_t size) {
#if defined(V8_USE_MEMORY_SANITIZER)

  memset(address, 0, size);
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(address, size);

#elif defined(V8_USE_ADDRESS_SANITIZER)

  NoSanitizeMemset(address, 0, size);
  ASAN_POISON_MEMORY_REGION(address, size);

#else

  ::cppgc::internal::ZapMemory(address, size);

#endif  // Debug builds.
}

void CheckMemoryIsInaccessible(const void* address, size_t size) {
#if defined(V8_USE_MEMORY_SANITIZER)

  static_assert(CheckMemoryIsInaccessibleIsNoop(),
                "CheckMemoryIsInaccessibleIsNoop() needs to reflect "
                "CheckMemoryIsInaccessible().");
  // Unable to check that memory is marked as uninitialized by MSAN.

#elif defined(V8_USE_ADDRESS_SANITIZER)

  static_assert(!CheckMemoryIsInaccessibleIsNoop(),
                "CheckMemoryIsInaccessibleIsNoop() needs to reflect "
                "CheckMemoryIsInaccessible().");
  // Only check if memory is poisoned on 64 bit, since there we make sure that
  // object sizes and alignments are multiple of shadow memory granularity.
#if defined(V8_HOST_ARCH_64_BIT)
  ASAN_CHECK_WHOLE_MEMORY_REGION_IS_POISONED(address, size);
#endif
  ASAN_UNPOISON_MEMORY_REGION(address, size);
  CheckMemoryIsZero(address, size);
  ASAN_POISON_MEMORY_REGION(address, size);

#else  // Debug builds.

  static_assert(!CheckMemoryIsInaccessibleIsNoop(),
                "CheckMemoryIsInaccessibleIsNoop() needs to reflect "
                "CheckMemoryIsInaccessible().");
  CheckMemoryIsZapped(address, size);

#endif  // Debug builds.
}

#endif

}  // namespace internal
}  // namespace cppgc
