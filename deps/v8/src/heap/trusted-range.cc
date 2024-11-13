// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/trusted-range.h"

#include "src/base/lazy-instance.h"
#include "src/base/once.h"
#include "src/heap/heap-inl.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX

bool TrustedRange::InitReservation(size_t requested) {
  DCHECK_LE(requested, kMaximalTrustedRangeSize);
  DCHECK_GE(requested, kMinimumTrustedRangeSize);

  auto page_allocator = GetPlatformPageAllocator();

  const size_t kPageSize = MutablePageMetadata::kPageSize;
  CHECK(IsAligned(kPageSize, page_allocator->AllocatePageSize()));

  // We want the trusted range to be allocated above 4GB, for a few reasons:
  //   1. Certain (sandbox) bugs allow access to (only) the first 4GB of the
  //      address space, so we don't want sensitive objects to live there.
  //   2. When pointers to trusted objects have the upper 32 bits cleared, they
  //      may look like compressed pointers to some code in V8. For example, the
  //      stack spill slot visiting logic (VisitSpillSlot in frames.cc)
  //      currently assumes that when the top 32-bits are zero, then it's
  //      dealing with a compressed pointer and will attempt to decompress them
  //      with the main cage base, which in this case would break.
  //
  // To achieve this, we simply require 4GB alignment of the allocation and
  // assume that we can never map the zeroth page.
  const size_t base_alignment = size_t{4} * GB;

  const Address requested_start_hint =
      RoundDown(reinterpret_cast<Address>(page_allocator->GetRandomMmapAddr()),
                base_alignment);

  VirtualMemoryCage::ReservationParams params;
  params.page_allocator = page_allocator;
  params.reservation_size = requested;
  params.page_size = kPageSize;
  params.base_alignment = base_alignment;
  params.requested_start_hint = requested_start_hint;
  params.permissions = PageAllocator::Permission::kNoAccess;
  params.page_initialization_mode =
      base::PageInitializationMode::kAllocatedPagesCanBeUninitialized;
  params.page_freeing_mode = base::PageFreeingMode::kMakeInaccessible;
  return VirtualMemoryCage::InitReservation(params);
}

namespace {

TrustedRange* process_wide_trusted_range_ = nullptr;

V8_DECLARE_ONCE(init_trusted_range_once);
void InitProcessWideTrustedRange(size_t requested_size) {
  TrustedRange* trusted_range = new TrustedRange();
  if (!trusted_range->InitReservation(requested_size)) {
    V8::FatalProcessOutOfMemory(
        nullptr, "Failed to reserve virtual memory for TrustedRange");
  }
  process_wide_trusted_range_ = trusted_range;

  TrustedSpaceCompressionScheme::InitBase(trusted_range->base());
}
}  // namespace

// static
TrustedRange* TrustedRange::EnsureProcessWideTrustedRange(
    size_t requested_size) {
  base::CallOnce(&init_trusted_range_once, InitProcessWideTrustedRange,
                 requested_size);
  return process_wide_trusted_range_;
}

// static
TrustedRange* TrustedRange::GetProcessWideTrustedRange() {
  return process_wide_trusted_range_;
}

#endif  // V8_ENABLE_SANDBOX

}  // namespace internal
}  // namespace v8
