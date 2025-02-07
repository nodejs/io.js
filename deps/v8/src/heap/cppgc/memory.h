// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MEMORY_H_
#define V8_HEAP_CPPGC_MEMORY_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "src/base/macros.h"
#include "src/base/sanitizer/asan.h"
#include "src/base/sanitizer/msan.h"
#include "src/heap/cppgc/globals.h"

namespace cppgc {
namespace internal {

V8_NOINLINE DISABLE_ASAN void NoSanitizeMemset(void* address, char c,
                                               size_t bytes);

static constexpr uint8_t kZappedValue = 0xdc;

V8_INLINE void ZapMemory(void* address, size_t size) {
  // The lowest bit of the zapped value should be 0 so that zapped object are
  // never viewed as fully constructed objects.
  memset(address, kZappedValue, size);
}

V8_INLINE void CheckMemoryIsZapped(const void* address, size_t size) {
  for (size_t i = 0; i < size; i++) {
    CHECK_EQ(kZappedValue, reinterpret_cast<ConstAddress>(address)[i]);
  }
}

V8_INLINE void CheckMemoryIsZero(const void* address, size_t size) {
  for (size_t i = 0; i < size; i++) {
    CHECK_EQ(0, reinterpret_cast<ConstAddress>(address)[i]);
  }
}

// Together `SetMemoryAccessible()` and `SetMemoryInaccessible()` form the
// memory access model for allocation and free.

#if defined(V8_USE_MEMORY_SANITIZER) || defined(V8_USE_ADDRESS_SANITIZER) || \
    DEBUG

void SetMemoryAccessible(void* address, size_t size);
void SetMemoryInaccessible(void* address, size_t size);
void CheckMemoryIsInaccessible(const void* address, size_t size);

constexpr bool CheckMemoryIsInaccessibleIsNoop() {
#if defined(V8_USE_MEMORY_SANITIZER)

  return true;

#elif defined(V8_USE_ADDRESS_SANITIZER)

  return false;

#else  // Debug builds.

  return false;

#endif  // Debug builds.
}

#else

// Nothing to be done for release builds.
V8_INLINE void SetMemoryAccessible(void* address, size_t size) {}
V8_INLINE void CheckMemoryIsInaccessible(const void* address, size_t size) {}
constexpr bool CheckMemoryIsInaccessibleIsNoop() { return true; }

V8_INLINE void SetMemoryInaccessible(void* address, size_t size) {
  memset(address, 0, size);
}

#endif

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MEMORY_H_
