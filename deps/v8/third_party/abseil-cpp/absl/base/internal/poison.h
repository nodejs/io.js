// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_BASE_INTERNAL_POISON_H_
#define ABSL_BASE_INTERNAL_POISON_H_

#include <cstdint>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

inline void* GetBadPointerInternal() {
  // A likely bad pointer. Pointers are required to have high bits that are all
  // zero or all one for certain 64-bit CPUs. This pointer value will hopefully
  // cause a crash on dereference and also be clearly recognizable as invalid.
  constexpr uint64_t kBadPtr = 0xBAD0BAD0BAD0BAD0;
  auto ret = reinterpret_cast<void*>(static_cast<uintptr_t>(kBadPtr));
#ifndef _MSC_VER  // MSVC doesn't support inline asm with `volatile`.
  // Try to prevent the compiler from optimizing out the undefined behavior.
  asm volatile("" : : "r"(ret) :);  // NOLINT
#endif
  return ret;
}

void* InitializePoisonedPointerInternal();

inline void* get_poisoned_pointer() {
#if defined(NDEBUG) && !defined(ABSL_HAVE_ADDRESS_SANITIZER) && \
    !defined(ABSL_HAVE_MEMORY_SANITIZER)
  // In optimized non-sanitized builds, avoid the function-local static because
  // of the codegen and runtime cost.
  return GetBadPointerInternal();
#else
  // Non-optimized builds may use more robust implementation. Note that we can't
  // use a static global because Chromium doesn't allow non-constinit globals.
  static void* ptr = InitializePoisonedPointerInternal();
  return ptr;
#endif
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_POISON_H_
