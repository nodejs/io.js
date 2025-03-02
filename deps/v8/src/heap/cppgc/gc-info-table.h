// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_GC_INFO_TABLE_H_
#define V8_HEAP_CPPGC_GC_INFO_TABLE_H_

#include <stdint.h>

#include "include/cppgc/internal/gc-info.h"
#include "include/cppgc/platform.h"
#include "include/v8config.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/platform.h"

namespace cppgc {
namespace internal {

// GCInfo contains metadata for objects that are instantiated from classes that
// inherit from GarbageCollected.
struct GCInfo final {
  constexpr GCInfo(FinalizationCallback finalize, TraceCallback trace,
                   NameCallback name)
      : finalize(finalize), trace(trace), name(name) {}

  FinalizationCallback finalize;
  TraceCallback trace;
  NameCallback name;
  size_t padding = 0;
};

class V8_EXPORT GCInfoTable final {
 public:
  // At maximum |kMaxIndex - 1| indices are supported.
  //
  // We assume that 14 bits are enough to represent all possible types.
  //
  // For Blink during telemetry runs, we see about 1,000 different types;
  // looking at the output of the Oilpan GC clang plugin, there appear to be at
  // most about 6,000 types. Thus 14 bits should be more than twice as many bits
  // as we will ever need. Different contexts may require adjusting this limit.
  static constexpr GCInfoIndex kMaxIndex = 1 << 14;

  // Minimum index returned. Values smaller |kMinIndex| may be used as
  // sentinels.
  static constexpr GCInfoIndex kMinIndex = 1;

  // (Light) experimentation suggests that Blink doesn't need more than this
  // while handling content on popular web properties.
  static constexpr GCInfoIndex kInitialWantedLimit = 512;

  // Refer through GlobalGCInfoTable for retrieving the global table outside
  // of testing code.
  GCInfoTable(PageAllocator& page_allocator,
              FatalOutOfMemoryHandler& oom_handler);
  ~GCInfoTable();
  GCInfoTable(const GCInfoTable&) = delete;
  GCInfoTable& operator=(const GCInfoTable&) = delete;

  GCInfoIndex RegisterNewGCInfo(std::atomic<uint16_t>&, const GCInfo& info);

  const GCInfo& GCInfoFromIndex(GCInfoIndex index) const {
    DCHECK_GE(index, kMinIndex);
    DCHECK_LT(index, kMaxIndex);
    DCHECK(table_);
    return table_[index];
  }

  GCInfoIndex NumberOfGCInfos() const { return current_index_; }

  GCInfoIndex LimitForTesting() const { return limit_; }
  GCInfo& TableSlotForTesting(GCInfoIndex index) { return table_[index]; }

  PageAllocator& allocator() const { return page_allocator_; }

 private:
  void Resize();

  GCInfoIndex InitialTableLimit() const;
  size_t MaxTableSize() const;

  void CheckMemoryIsZeroed(uintptr_t* base, size_t len);

  PageAllocator& page_allocator_;
  FatalOutOfMemoryHandler& oom_handler_;
  // Holds the per-class GCInfo descriptors; each HeapObjectHeader keeps an
  // index into this table.
  GCInfo* table_;
  uint8_t* read_only_table_end_;
  // Current index used when requiring a new GCInfo object.
  GCInfoIndex current_index_ = kMinIndex;
  // The limit (exclusive) of the currently allocated table.
  GCInfoIndex limit_ = 0;

  v8::base::SpinningMutex table_mutex_;
};

class V8_EXPORT GlobalGCInfoTable final {
 public:
  GlobalGCInfoTable(const GlobalGCInfoTable&) = delete;
  GlobalGCInfoTable& operator=(const GlobalGCInfoTable&) = delete;

  // Sets up the table with the provided `page_allocator`. Will use an internal
  // allocator in case no PageAllocator is provided. May be called multiple
  // times with the same `page_allocator` argument.
  static void Initialize(PageAllocator& page_allocator);

  // Accessors for the singleton table.
  static GCInfoTable& GetMutable() { return *global_table_; }
  static const GCInfoTable& Get() { return *global_table_; }

  static const GCInfo& GCInfoFromIndex(GCInfoIndex index) {
    return Get().GCInfoFromIndex(index);
  }

 private:
  // Singleton for each process. Retrieved through Get().
  static GCInfoTable* global_table_;

  DISALLOW_NEW_AND_DELETE()
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_GC_INFO_TABLE_H_
