// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ALLOCATION_STATS_H_
#define V8_HEAP_ALLOCATION_STATS_H_

#include <atomic>
#include <unordered_map>

#include "src/base/functional.h"
#include "src/base/macros.h"
#include "src/heap/memory-chunk-metadata.h"

namespace v8 {
namespace internal {

// An abstraction of the accounting statistics of a page-structured space.
//
// The stats are only set by functions that ensure they stay balanced. These
// functions increase or decrease one of the non-capacity stats in conjunction
// with capacity, or else they always balance increases and decreases to the
// non-capacity stats.
class AllocationStats {
 public:
  AllocationStats() { Clear(); }

  AllocationStats& operator=(const AllocationStats& stats) V8_NOEXCEPT {
    capacity_ = stats.capacity_.load();
    max_capacity_ = stats.max_capacity_;
    size_.store(stats.size_);
#ifdef DEBUG
    allocated_on_page_ = stats.allocated_on_page_;
#endif
    return *this;
  }

  // Zero out all the allocation statistics (i.e., no capacity).
  void Clear() {
    capacity_ = 0;
    max_capacity_ = 0;
    ClearSize();
  }

  void ClearSize() {
    size_ = 0;
#ifdef DEBUG
    allocated_on_page_.clear();
#endif
  }

  // Accessors for the allocation statistics.
  size_t Capacity() const { return capacity_; }
  size_t MaxCapacity() const { return max_capacity_; }
  size_t Size() const { return size_; }
#ifdef DEBUG
  size_t AllocatedOnPage(const MemoryChunkMetadata* page) const {
    return allocated_on_page_.at(page);
  }
#endif

  void IncreaseAllocatedBytes(size_t bytes, const MemoryChunkMetadata* page) {
    DCHECK_IMPLIES(V8_COMPRESS_POINTERS_8GB_BOOL,
                   IsAligned(bytes, kObjectAlignment8GbHeap));
#ifdef DEBUG
    size_t size = size_;
    DCHECK_GE(size + bytes, size);
#endif
    size_.fetch_add(bytes);
#ifdef DEBUG
    allocated_on_page_[page] += bytes;
#endif
  }

  void DecreaseAllocatedBytes(size_t bytes, const MemoryChunkMetadata* page) {
    DCHECK_GE(size_, bytes);
    size_.fetch_sub(bytes);
#ifdef DEBUG
    DCHECK_GE(allocated_on_page_[page], bytes);
    allocated_on_page_[page] -= bytes;
#endif
  }

  void DecreaseCapacity(size_t bytes) {
    DCHECK_GE(capacity_, bytes);
    DCHECK_GE(capacity_ - bytes, size_);
    capacity_ -= bytes;
  }

  void IncreaseCapacity(size_t bytes) {
    DCHECK_GE(capacity_ + bytes, capacity_);
    capacity_ += bytes;
    if (capacity_ > max_capacity_) {
      max_capacity_ = capacity_;
    }
  }

 private:
  // |capacity_|: The number of object-area bytes (i.e., not including page
  // bookkeeping structures) currently in the space.
  // During evacuation capacity of the main spaces is accessed from multiple
  // threads to check the old generation hard limit.
  std::atomic<size_t> capacity_;

  // |max_capacity_|: The maximum capacity ever observed.
  size_t max_capacity_;

  // |size_|: The number of allocated bytes.
  std::atomic<size_t> size_;

#ifdef DEBUG
  std::unordered_map<const MemoryChunkMetadata*, size_t,
                     base::hash<const MemoryChunkMetadata*>>
      allocated_on_page_;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATION_STATS_H_
