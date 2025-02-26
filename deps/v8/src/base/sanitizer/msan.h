// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// MemorySanitizer support.

#ifndef V8_BASE_SANITIZER_MSAN_H_
#define V8_BASE_SANITIZER_MSAN_H_

#include "src/base/macros.h"
#include "src/base/memory.h"

#ifdef V8_USE_MEMORY_SANITIZER

#include <sanitizer/msan_interface.h>

// Marks a memory range as uninitialized, as if it was allocated here.
#define MSAN_ALLOCATED_UNINITIALIZED_MEMORY(start, size) \
  __msan_allocated_memory(reinterpret_cast<const void*>(start), (size))

// Marks a memory range as initialized.
#define MSAN_MEMORY_IS_INITIALIZED(start, size) \
  __msan_unpoison(reinterpret_cast<const void*>(start), (size))

#else  // !V8_USE_MEMORY_SANITIZER

#define MSAN_ALLOCATED_UNINITIALIZED_MEMORY(start, size)              \
  static_assert((std::is_pointer_v<decltype(start)> ||                \
                 std::is_same_v<v8::base::Address, decltype(start)>), \
                "static type violation");                             \
  static_assert(std::is_convertible_v<decltype(size), size_t>,        \
                "static type violation");                             \
  USE(start, size)

#define MSAN_MEMORY_IS_INITIALIZED(start, size) \
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(start, size)

#endif  // V8_USE_MEMORY_SANITIZER

#endif  // V8_BASE_SANITIZER_MSAN_H_
