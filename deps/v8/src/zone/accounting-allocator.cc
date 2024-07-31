// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zone/accounting-allocator.h"

#include <memory>

#include "src/base/bounded-page-allocator.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/utils/allocation.h"
#include "src/zone/zone-compression.h"
#include "src/zone/zone-segment.h"

namespace v8 {
namespace internal {

// These definitions are here in order to please the linker, which in debug mode
// sometimes requires static constants to be defined in .cc files.
const size_t ZoneCompression::kReservationSize;
const size_t ZoneCompression::kReservationAlignment;

namespace {

static constexpr size_t kZonePageSize = 256 * KB;

VirtualMemory ReserveAddressSpace(v8::PageAllocator* platform_allocator) {
  DCHECK(IsAligned(ZoneCompression::kReservationSize,
                   platform_allocator->AllocatePageSize()));

  void* hint = reinterpret_cast<void*>(RoundDown(
      reinterpret_cast<uintptr_t>(platform_allocator->GetRandomMmapAddr()),
      ZoneCompression::kReservationAlignment));

  VirtualMemory memory(platform_allocator, ZoneCompression::kReservationSize,
                       hint, ZoneCompression::kReservationAlignment);
  if (memory.IsReserved()) {
    CHECK(IsAligned(memory.address(), ZoneCompression::kReservationAlignment));
    return memory;
  }

  base::FatalOOM(base::OOMType::kProcess,
                 "Failed to reserve memory for compressed zones");
  UNREACHABLE();
}

std::unique_ptr<v8::base::BoundedPageAllocator> CreateBoundedAllocator(
    v8::PageAllocator* platform_allocator, Address reservation_start) {
  CHECK(reservation_start);
  CHECK(IsAligned(reservation_start, ZoneCompression::kReservationAlignment));

  auto allocator = std::make_unique<v8::base::BoundedPageAllocator>(
      platform_allocator, reservation_start, ZoneCompression::kReservationSize,
      kZonePageSize,
      base::PageInitializationMode::kAllocatedPagesCanBeUninitialized,
      base::PageFreeingMode::kMakeInaccessible);

  // Exclude first page from allocation to ensure that accesses through
  // decompressed null pointer will seg-fault.
  allocator->AllocatePagesAt(reservation_start, kZonePageSize,
                             v8::PageAllocator::kNoAccess);
  return allocator;
}

}  // namespace

AccountingAllocator::AccountingAllocator()
    : zone_backing_malloc_(
          V8::GetCurrentPlatform()->GetZoneBackingAllocator()->GetMallocFn()),
      zone_backing_free_(
          V8::GetCurrentPlatform()->GetZoneBackingAllocator()->GetFreeFn()) {
  if (COMPRESS_ZONES_BOOL) {
    v8::PageAllocator* platform_page_allocator = GetPlatformPageAllocator();
    VirtualMemory memory = ReserveAddressSpace(platform_page_allocator);
    reserved_area_ = std::make_unique<VirtualMemory>(std::move(memory));
    bounded_page_allocator_ = CreateBoundedAllocator(platform_page_allocator,
                                                     reserved_area_->address());
  }
}

AccountingAllocator::~AccountingAllocator() = default;

Segment* AccountingAllocator::AllocateSegment(size_t bytes,
                                              bool supports_compression) {
  void* memory;
  if (COMPRESS_ZONES_BOOL && supports_compression) {
    bytes = RoundUp(bytes, kZonePageSize);
    memory = AllocatePages(bounded_page_allocator_.get(), nullptr, bytes,
                           kZonePageSize, PageAllocator::kReadWrite);

  } else {
    auto result = AllocAtLeastWithRetry(bytes);
    memory = result.ptr;
    bytes = result.count;
  }
  if (memory == nullptr) return nullptr;

  size_t current =
      current_memory_usage_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
  size_t max = max_memory_usage_.load(std::memory_order_relaxed);
  while (current > max && !max_memory_usage_.compare_exchange_weak(
                              max, current, std::memory_order_relaxed)) {
    // {max} was updated by {compare_exchange_weak}; retry.
  }
  DCHECK_LE(sizeof(Segment), bytes);
  return new (memory) Segment(bytes);
}

void AccountingAllocator::ReturnSegment(Segment* segment,
                                        bool supports_compression) {
  segment->ZapContents();
  size_t segment_size = segment->total_size();
  current_memory_usage_.fetch_sub(segment_size, std::memory_order_relaxed);
  segment->ZapHeader();
  if (COMPRESS_ZONES_BOOL && supports_compression) {
    FreePages(bounded_page_allocator_.get(), segment, segment_size);
  } else {
    free(segment);
  }
}

}  // namespace internal
}  // namespace v8
