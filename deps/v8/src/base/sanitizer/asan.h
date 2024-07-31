// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// AddressSanitizer support.

#ifndef V8_BASE_SANITIZER_ASAN_H_
#define V8_BASE_SANITIZER_ASAN_H_

#include <type_traits>

#include "src/base/macros.h"

#ifdef V8_USE_ADDRESS_SANITIZER

#include <sanitizer/asan_interface.h>

#if !defined(ASAN_POISON_MEMORY_REGION) || !defined(ASAN_UNPOISON_MEMORY_REGION)
#error \
    "ASAN_POISON_MEMORY_REGION and ASAN_UNPOISON_MEMORY_REGION must be defined"
#endif

#define DISABLE_ASAN __attribute__((no_sanitize_address))

// Check that all bytes in a memory region are poisoned. This is different from
// `__asan_region_is_poisoned()` which only requires a single byte in the region
// to be poisoned. Please note that the macro only works if both start and size
// are multiple of asan's shadow memory granularity.
#define ASAN_CHECK_WHOLE_MEMORY_REGION_IS_POISONED(start, size)               \
  do {                                                                        \
    for (size_t i = 0; i < size; i++) {                                       \
      CHECK(__asan_address_is_poisoned(reinterpret_cast<const char*>(start) + \
                                       i));                                   \
    }                                                                         \
  } while (0)

class AsanUnpoisonScope final {
 public:
  AsanUnpoisonScope(const void* addr, size_t size)
      : addr_(addr),
        size_(size),
        was_poisoned_(
            __asan_region_is_poisoned(const_cast<void*>(addr_), size_)) {
    if (was_poisoned_) {
      ASAN_UNPOISON_MEMORY_REGION(addr_, size_);
    }
  }
  ~AsanUnpoisonScope() {
    if (was_poisoned_) {
      ASAN_POISON_MEMORY_REGION(addr_, size_);
    }
  }

 private:
  const void* addr_;
  size_t size_;
  bool was_poisoned_;
};

#else  // !V8_USE_ADDRESS_SANITIZER

#define DISABLE_ASAN

#define ASAN_POISON_MEMORY_REGION(start, size)                      \
  static_assert(std::is_pointer<decltype(start)>::value,            \
                "static type violation");                           \
  static_assert(std::is_convertible<decltype(size), size_t>::value, \
                "static type violation");                           \
  USE(start, size)

#define ASAN_UNPOISON_MEMORY_REGION(start, size) \
  ASAN_POISON_MEMORY_REGION(start, size)

#define ASAN_CHECK_WHOLE_MEMORY_REGION_IS_POISONED(start, size) \
  ASAN_POISON_MEMORY_REGION(start, size)

class AsanUnpoisonScope final {
 public:
  AsanUnpoisonScope(const void*, size_t) {}
};

#endif  // !V8_USE_ADDRESS_SANITIZER

#endif  // V8_BASE_SANITIZER_ASAN_H_
