// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/caged-heap.h"

#include <map>

#include "src/heap/cppgc/platform.h"
#include "v8config.h"  // NOLINT(build/include_directory)

#if !defined(CPPGC_CAGED_HEAP)
#error "Must be compiled with caged heap enabled"
#endif

#include "include/cppgc/internal/api-constants.h"
#include "include/cppgc/internal/caged-heap-local-data.h"
#include "include/cppgc/member.h"
#include "include/cppgc/platform.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/lazy-instance.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/caged-heap.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/member-storage.h"

namespace cppgc {
namespace internal {

uintptr_t CagedHeapBase::g_heap_base_ = 0u;
size_t CagedHeapBase::g_age_table_size_ = 0u;

CagedHeap* CagedHeap::instance_ = nullptr;

namespace {

VirtualMemory ReserveCagedHeap(PageAllocator& platform_allocator) {
  DCHECK_EQ(0u, api_constants::kCagedHeapMaxReservationSize %
                    platform_allocator.AllocatePageSize());

  static constexpr size_t kAllocationTries = 4;
  for (size_t i = 0; i < kAllocationTries; ++i) {
#if defined(CPPGC_POINTER_COMPRESSION)
    // We want compressed pointers to have the most significant bit set to 1.
    // That way, on decompression the bit will be sign-extended. This saves us a
    // branch and 'or' operation during compression.
    //
    // We achieve this by over-reserving the cage and selecting a sub-region
    // from the upper half of the reservation that has the bit pattern we need.
    // Examples:
    // - For a 4GB cage with 1 bit of pointer compression shift, reserve 8GB and
    // use the upper 4GB.
    // - For an 8GB cage with 3 bits of pointer compression shift, reserve 32GB
    // and use the first 8GB of the upper 16 GB.
    //
    // TODO(chromium:1325007): Provide API in PageAllocator to left trim
    // allocations and return unused portions of the reservation back to the OS.
    static constexpr size_t kTryReserveSize =
        2 * api_constants::kCagedHeapMaxReservationSize;
    static constexpr size_t kTryReserveAlignment =
        2 * api_constants::kCagedHeapReservationAlignment;
#else   // !defined(CPPGC_POINTER_COMPRESSION)
    static constexpr size_t kTryReserveSize =
        api_constants::kCagedHeapMaxReservationSize;
    static constexpr size_t kTryReserveAlignment =
        api_constants::kCagedHeapReservationAlignment;
#endif  // !defined(CPPGC_POINTER_COMPRESSION)
    void* hint = reinterpret_cast<void*>(RoundDown(
        reinterpret_cast<uintptr_t>(platform_allocator.GetRandomMmapAddr()),
        kTryReserveAlignment));

    VirtualMemory memory(&platform_allocator, kTryReserveSize,
                         kTryReserveAlignment, hint);
    if (memory.IsReserved()) return memory;
  }

  GetGlobalOOMHandler()("Oilpan: CagedHeap reservation.");
}

}  // namespace

// static
void CagedHeap::InitializeIfNeeded(PageAllocator& platform_allocator,
                                   size_t desired_heap_size) {
  static v8::base::LeakyObject<CagedHeap> caged_heap(platform_allocator,
                                                     desired_heap_size);
}

// static
CagedHeap& CagedHeap::Instance() {
  DCHECK_NOT_NULL(instance_);
  return *instance_;
}

CagedHeap::CagedHeap(PageAllocator& platform_allocator,
                     size_t desired_heap_size)
    : reserved_area_(ReserveCagedHeap(platform_allocator)) {
  using CagedAddress = CagedHeap::AllocatorType::Address;

#if defined(CPPGC_POINTER_COMPRESSION)
  // Pick a base offset according to pointer compression shift. See comment in
  // ReserveCagedHeap().
  static constexpr size_t kBaseOffset =
      api_constants::kCagedHeapMaxReservationSize;
#else   // !defined(CPPGC_POINTER_COMPRESSION)
  static constexpr size_t kBaseOffset = 0;
#endif  //! defined(CPPGC_POINTER_COMPRESSION)

  void* const cage_start =
      static_cast<uint8_t*>(reserved_area_.address()) + kBaseOffset;

  CagedHeapBase::g_heap_base_ = reinterpret_cast<uintptr_t>(cage_start);

#if defined(CPPGC_POINTER_COMPRESSION)
  // With pointer compression only single heap per thread is allowed.
  CHECK(!CageBaseGlobal::IsSet());
  CageBaseGlobalUpdater::UpdateCageBase(CagedHeapBase::g_heap_base_);
#endif  // defined(CPPGC_POINTER_COMPRESSION)

  const size_t total_heap_size = std::clamp<size_t>(
      v8::base::bits::RoundUpToPowerOfTwo64(desired_heap_size),
      api_constants::kCagedHeapDefaultReservationSize,
      api_constants::kCagedHeapMaxReservationSize);

  const size_t local_data_size =
      CagedHeapLocalData::CalculateLocalDataSizeForHeapSize(total_heap_size);
  const CagedAddress caged_heap_start = RoundUp(
      reinterpret_cast<CagedAddress>(cage_start) + local_data_size, kPageSize);
  const size_t local_data_size_with_padding =
      caged_heap_start - reinterpret_cast<CagedAddress>(cage_start);

  page_bounded_allocator_ = std::make_unique<v8::base::BoundedPageAllocator>(
      &platform_allocator, caged_heap_start,
      total_heap_size - local_data_size_with_padding, kPageSize,
      v8::base::PageInitializationMode::kAllocatedPagesMustBeZeroInitialized,
      v8::base::PageFreeingMode::kMakeInaccessible);

  instance_ = this;
  CagedHeapBase::g_age_table_size_ = AgeTable::CalculateAgeTableSizeForHeapSize(
      api_constants::kCagedHeapDefaultReservationSize);
}

void CagedHeap::CommitAgeTable(PageAllocator& platform_allocator) {
  if (!platform_allocator.SetPermissions(
          reinterpret_cast<void*>(CagedHeapBase::g_heap_base_),
          RoundUp(CagedHeapBase::g_age_table_size_,
                  platform_allocator.CommitPageSize()),
          PageAllocator::kReadWrite)) {
    GetGlobalOOMHandler()("Oilpan: CagedHeap commit CageHeapLocalData.");
  }
}

}  // namespace internal
}  // namespace cppgc
