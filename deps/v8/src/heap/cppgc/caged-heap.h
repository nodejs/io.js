// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_CAGED_HEAP_H_
#define V8_HEAP_CPPGC_CAGED_HEAP_H_

#include <limits>
#include <memory>

#include "include/cppgc/internal/caged-heap.h"
#include "include/cppgc/platform.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/lazy-instance.h"
#include "src/base/platform/mutex.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/virtual-memory.h"

namespace cppgc {
namespace internal {

namespace testing {
class TestWithHeap;
}

class V8_EXPORT_PRIVATE CagedHeap final {
 public:
  using AllocatorType = v8::base::BoundedPageAllocator;

  template <typename RetType = uintptr_t>
  static RetType OffsetFromAddress(const void* address) {
    static_assert(std::numeric_limits<RetType>::max() >=
                      (api_constants::kCagedHeapMaxReservationSize - 1),
                  "The return type should be large enough");
    return reinterpret_cast<uintptr_t>(address) &
           (api_constants::kCagedHeapReservationAlignment - 1);
  }

  static uintptr_t BaseFromAddress(const void* address) {
    return reinterpret_cast<uintptr_t>(address) &
           ~(api_constants::kCagedHeapReservationAlignment - 1);
  }

  static void InitializeIfNeeded(PageAllocator& platform_allocator,
                                 size_t desired_heap_size);

  static void CommitAgeTable(PageAllocator& platform_allocator);

  static CagedHeap& Instance();

  CagedHeap(const CagedHeap&) = delete;
  CagedHeap& operator=(const CagedHeap&) = delete;

  AllocatorType& page_allocator() { return *page_bounded_allocator_; }
  const AllocatorType& page_allocator() const {
    return *page_bounded_allocator_;
  }

  bool IsOnHeap(const void* address) const {
    DCHECK_EQ(reserved_area_.address(),
              reinterpret_cast<void*>(CagedHeapBase::GetBase()));
    return reinterpret_cast<void*>(BaseFromAddress(address)) ==
           reserved_area_.address();
  }

  void* base() const { return reserved_area_.address(); }

 private:
  friend class v8::base::LeakyObject<CagedHeap>;
  friend class testing::TestWithHeap;

  explicit CagedHeap(PageAllocator& platform_allocator,
                     size_t desired_heap_size);

  static CagedHeap* instance_;

  const VirtualMemory reserved_area_;
  // BoundedPageAllocator is thread-safe, no need to use external
  // synchronization.
  std::unique_ptr<AllocatorType> page_bounded_allocator_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_CAGED_HEAP_H_
