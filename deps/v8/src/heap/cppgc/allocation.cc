// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/allocation.h"

#include "include/cppgc/internal/api-constants.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/object-allocator.h"

#if defined(__clang__) && !defined(DEBUG) && V8_HAS_ATTRIBUTE_ALWAYS_INLINE
#define CPPGC_FORCE_ALWAYS_INLINE __attribute__((always_inline))
#else
#define CPPGC_FORCE_ALWAYS_INLINE
#endif

namespace cppgc {
namespace internal {

static_assert(api_constants::kLargeObjectSizeThreshold ==
              kLargeObjectSizeThreshold);

#if !(defined(V8_HOST_ARCH_32_BIT) && defined(V8_CC_GNU))
// GCC on x86 has alignof(std::max_align_t) == 16 (quad word) which is not
// satisfied by Oilpan.
static_assert(api_constants::kMaxSupportedAlignment >=
                  alignof(std::max_align_t),
              "Maximum support alignment must at least cover "
              "alignof(std::max_align_t).");
#endif  // !(defined(V8_HOST_ARCH_32_BIT) && defined(V8_CC_GNU))

// Using CPPGC_FORCE_ALWAYS_INLINE to guide LTO for inlining the allocation
// fast path.
// static
CPPGC_FORCE_ALWAYS_INLINE void* MakeGarbageCollectedTraitInternal::Allocate(
    cppgc::AllocationHandle& handle, size_t size, GCInfoIndex index) {
  return static_cast<ObjectAllocator&>(handle).AllocateObject(size, index);
}

// Using CPPGC_FORCE_ALWAYS_INLINE to guide LTO for inlining the allocation
// fast path.
// static
CPPGC_FORCE_ALWAYS_INLINE void* MakeGarbageCollectedTraitInternal::Allocate(
    cppgc::AllocationHandle& handle, size_t size, AlignVal alignment,
    GCInfoIndex index) {
  return static_cast<ObjectAllocator&>(handle).AllocateObject(size, alignment,
                                                              index);
}

// Using CPPGC_FORCE_ALWAYS_INLINE to guide LTO for inlining the allocation
// fast path.
// static
CPPGC_FORCE_ALWAYS_INLINE void* MakeGarbageCollectedTraitInternal::Allocate(
    cppgc::AllocationHandle& handle, size_t size, GCInfoIndex index,
    CustomSpaceIndex space_index) {
  return static_cast<ObjectAllocator&>(handle).AllocateObject(size, index,
                                                              space_index);
}

// Using CPPGC_FORCE_ALWAYS_INLINE to guide LTO for inlining the allocation
// fast path.
// static
CPPGC_FORCE_ALWAYS_INLINE void* MakeGarbageCollectedTraitInternal::Allocate(
    cppgc::AllocationHandle& handle, size_t size, AlignVal alignment,
    GCInfoIndex index, CustomSpaceIndex space_index) {
  return static_cast<ObjectAllocator&>(handle).AllocateObject(
      size, alignment, index, space_index);
}

}  // namespace internal
}  // namespace cppgc
