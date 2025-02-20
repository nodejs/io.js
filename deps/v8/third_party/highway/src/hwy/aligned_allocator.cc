// Copyright 2019 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hwy/aligned_allocator.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>  // malloc

#include <atomic>
#include <limits>

#include "hwy/base.h"

namespace hwy {
namespace {

#if HWY_ARCH_RISCV && defined(__riscv_v_intrinsic) && \
    __riscv_v_intrinsic >= 11000
// Not actually an upper bound on the size, but this value prevents crossing a
// 4K boundary (relevant on Andes).
constexpr size_t kAlignment = HWY_MAX(HWY_ALIGNMENT, 4096);
#else
constexpr size_t kAlignment = HWY_ALIGNMENT;
#endif

#if HWY_ARCH_X86
// On x86, aliasing can only occur at multiples of 2K. To reduce the chance of
// allocations being equal mod 2K, we round up to kAlias and add a cyclic
// offset which is a multiple of kAlignment. Rounding up to only 1K decreases
// the number of alias-free allocations, but also wastes less memory.
constexpr size_t kAlias = HWY_MAX(kAlignment, 1024);
#else
constexpr size_t kAlias = kAlignment;
#endif

#pragma pack(push, 1)
struct AllocationHeader {
  void* allocated;
  size_t payload_size;
};
#pragma pack(pop)

// Returns a 'random' (cyclical) offset for AllocateAlignedBytes.
size_t NextAlignedOffset() {
  static std::atomic<size_t> next{0};
  static_assert(kAlias % kAlignment == 0, "kAlias must be a multiple");
  constexpr size_t kGroups = kAlias / kAlignment;
  const size_t group = next.fetch_add(1, std::memory_order_relaxed) % kGroups;
  const size_t offset = kAlignment * group;
  HWY_DASSERT((offset % kAlignment == 0) && offset <= kAlias);
  return offset;
}

}  // namespace

HWY_DLLEXPORT void* AllocateAlignedBytes(const size_t payload_size,
                                         AllocPtr alloc_ptr, void* opaque_ptr) {
  HWY_ASSERT(payload_size != 0);  // likely a bug in caller
  if (payload_size >= std::numeric_limits<size_t>::max() / 2) {
    HWY_DASSERT(false && "payload_size too large");
    return nullptr;
  }

  size_t offset = NextAlignedOffset();

  // What: | misalign | unused | AllocationHeader |payload
  // Size: |<= kAlias | offset                    |payload_size
  //       ^allocated.^aligned.^header............^payload
  // The header must immediately precede payload, which must remain aligned.
  // To avoid wasting space, the header resides at the end of `unused`,
  // which therefore cannot be empty (offset == 0).
  if (offset == 0) {
    offset = RoundUpTo(sizeof(AllocationHeader), kAlignment);
  }

  const size_t allocated_size = kAlias + offset + payload_size;
  void* allocated;
  if (alloc_ptr == nullptr) {
    allocated = malloc(allocated_size);
  } else {
    allocated = (*alloc_ptr)(opaque_ptr, allocated_size);
  }
  if (allocated == nullptr) return nullptr;
  // Always round up even if already aligned - we already asked for kAlias
  // extra bytes and there's no way to give them back.
  uintptr_t aligned = reinterpret_cast<uintptr_t>(allocated) + kAlias;
  static_assert((kAlias & (kAlias - 1)) == 0, "kAlias must be a power of 2");
  static_assert(kAlias >= kAlignment, "Cannot align to more than kAlias");
  aligned &= ~(kAlias - 1);

  const uintptr_t payload = aligned + offset;  // still aligned
  HWY_DASSERT(payload % kAlignment == 0);

  // Stash `allocated` and payload_size inside header for FreeAlignedBytes().
  // The allocated_size can be reconstructed from the payload_size.
  AllocationHeader* header = reinterpret_cast<AllocationHeader*>(payload) - 1;
  HWY_DASSERT(reinterpret_cast<uintptr_t>(header) >= aligned);
  header->allocated = allocated;
  header->payload_size = payload_size;

  return HWY_ASSUME_ALIGNED(reinterpret_cast<void*>(payload), kAlignment);
}

HWY_DLLEXPORT void FreeAlignedBytes(const void* aligned_pointer,
                                    FreePtr free_ptr, void* opaque_ptr) {
  if (aligned_pointer == nullptr) return;

  const uintptr_t payload = reinterpret_cast<uintptr_t>(aligned_pointer);
  HWY_DASSERT(payload % kAlignment == 0);
  const AllocationHeader* header =
      reinterpret_cast<const AllocationHeader*>(payload) - 1;

  if (free_ptr == nullptr) {
    free(header->allocated);
  } else {
    (*free_ptr)(opaque_ptr, header->allocated);
  }
}

// static
HWY_DLLEXPORT void AlignedDeleter::DeleteAlignedArray(void* aligned_pointer,
                                                      FreePtr free_ptr,
                                                      void* opaque_ptr,
                                                      ArrayDeleter deleter) {
  if (aligned_pointer == nullptr) return;

  const uintptr_t payload = reinterpret_cast<uintptr_t>(aligned_pointer);
  HWY_DASSERT(payload % kAlignment == 0);
  const AllocationHeader* header =
      reinterpret_cast<const AllocationHeader*>(payload) - 1;

  if (deleter) {
    (*deleter)(aligned_pointer, header->payload_size);
  }

  if (free_ptr == nullptr) {
    free(header->allocated);
  } else {
    (*free_ptr)(opaque_ptr, header->allocated);
  }
}

}  // namespace hwy
