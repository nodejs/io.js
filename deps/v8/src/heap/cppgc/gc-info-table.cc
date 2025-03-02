// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/gc-info-table.h"

#include <algorithm>
#include <limits>
#include <memory>

#include "include/cppgc/internal/gc-info.h"
#include "include/cppgc/platform.h"
#include "src/base/bits.h"
#include "src/base/lazy-instance.h"
#include "src/base/page-allocator.h"
#include "src/heap/cppgc/platform.h"

namespace cppgc {
namespace internal {

namespace {

// GCInfoTable::table_, the table which holds GCInfos, is maintained as a
// contiguous array reserved upfront. Subparts of the array are (re-)committed
// as read/write or read-only in OS pages, whose size is a power of 2. To avoid
// having GCInfos that cross the boundaries between these subparts we force the
// size of GCInfo to be a power of 2 as well.
constexpr size_t kEntrySize = sizeof(GCInfo);
static_assert(v8::base::bits::IsPowerOfTwo(kEntrySize),
              "GCInfoTable entries size must be power of "
              "two");

}  // namespace

GCInfoTable* GlobalGCInfoTable::global_table_ = nullptr;

constexpr GCInfoIndex GCInfoTable::kMaxIndex;
constexpr GCInfoIndex GCInfoTable::kMinIndex;
constexpr GCInfoIndex GCInfoTable::kInitialWantedLimit;

// static
void GlobalGCInfoTable::Initialize(PageAllocator& page_allocator) {
  static v8::base::LeakyObject<GCInfoTable> table(page_allocator,
                                                  GetGlobalOOMHandler());
  if (!global_table_) {
    global_table_ = table.get();
  } else {
    CHECK_EQ(&page_allocator, &global_table_->allocator());
  }
}

GCInfoTable::GCInfoTable(PageAllocator& page_allocator,
                         FatalOutOfMemoryHandler& oom_handler)
    : page_allocator_(page_allocator),
      oom_handler_(oom_handler),
      table_(static_cast<decltype(table_)>(page_allocator_.AllocatePages(
          nullptr, MaxTableSize(), page_allocator_.AllocatePageSize(),
          PageAllocator::kNoAccess))),
      read_only_table_end_(reinterpret_cast<uint8_t*>(table_)) {
  if (!table_) {
    oom_handler_("Oilpan: GCInfoTable initial reservation.");
  }
  Resize();
}

GCInfoTable::~GCInfoTable() {
  page_allocator_.ReleasePages(const_cast<GCInfo*>(table_), MaxTableSize(), 0);
}

size_t GCInfoTable::MaxTableSize() const {
  return RoundUp(GCInfoTable::kMaxIndex * kEntrySize,
                 page_allocator_.AllocatePageSize());
}

GCInfoIndex GCInfoTable::InitialTableLimit() const {
  // Different OSes have different page sizes, so we have to choose the minimum
  // of memory wanted and OS page size.
  constexpr size_t memory_wanted = kInitialWantedLimit * kEntrySize;
  const size_t initial_limit =
      RoundUp(memory_wanted, page_allocator_.AllocatePageSize()) / kEntrySize;
  CHECK_GT(std::numeric_limits<GCInfoIndex>::max(), initial_limit);
  return static_cast<GCInfoIndex>(
      std::min(static_cast<size_t>(kMaxIndex), initial_limit));
}

void GCInfoTable::Resize() {
  const GCInfoIndex new_limit = (limit_) ? 2 * limit_ : InitialTableLimit();
  CHECK_GT(new_limit, limit_);
  const size_t old_committed_size = limit_ * kEntrySize;
  const size_t new_committed_size = new_limit * kEntrySize;
  CHECK(table_);
  CHECK_EQ(0u, new_committed_size % page_allocator_.AllocatePageSize());
  CHECK_GE(MaxTableSize(), new_committed_size);
  // Recommit new area as read/write.
  uint8_t* current_table_end =
      reinterpret_cast<uint8_t*>(table_) + old_committed_size;
  const size_t table_size_delta = new_committed_size - old_committed_size;
  if (!page_allocator_.SetPermissions(current_table_end, table_size_delta,
                                      PageAllocator::kReadWrite)) {
    oom_handler_("Oilpan: GCInfoTable resize.");
  }

  // Recommit old area as read-only.
  if (read_only_table_end_ != current_table_end) {
    DCHECK_GT(current_table_end, read_only_table_end_);
    const size_t read_only_delta = current_table_end - read_only_table_end_;
    CHECK(page_allocator_.SetPermissions(read_only_table_end_, read_only_delta,
                                         PageAllocator::kRead));
    read_only_table_end_ += read_only_delta;
  }

  // Check that newly-committed memory is zero-initialized.
  CheckMemoryIsZeroed(reinterpret_cast<uintptr_t*>(current_table_end),
                      table_size_delta / sizeof(uintptr_t));

  limit_ = new_limit;
}

void GCInfoTable::CheckMemoryIsZeroed(uintptr_t* base, size_t len) {
#if DEBUG
  for (size_t i = 0; i < len; ++i) {
    DCHECK(!base[i]);
  }
#endif  // DEBUG
}

GCInfoIndex GCInfoTable::RegisterNewGCInfo(
    std::atomic<GCInfoIndex>& registered_index, const GCInfo& info) {
  // Ensuring a new index involves current index adjustment as well as
  // potentially resizing the table. For simplicity we use a lock.
  v8::base::SpinningMutexGuard guard(&table_mutex_);

  // Check the registered index again after taking the lock as some other
  // thread may have registered the info at the same time.
  const GCInfoIndex index = registered_index.load(std::memory_order_relaxed);
  if (index) {
    return index;
  }

  if (current_index_ == limit_) {
    Resize();
  }

  const GCInfoIndex new_index = current_index_++;
  CHECK_LT(new_index, GCInfoTable::kMaxIndex);
  table_[new_index] = info;
  registered_index.store(new_index, std::memory_order_release);
  return new_index;
}

}  // namespace internal
}  // namespace cppgc
