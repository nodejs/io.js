// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_ALLOCATOR_H_
#define V8_ZONE_ZONE_ALLOCATOR_H_

#include <limits>

#include "src/zone/zone.h"

namespace v8 {
namespace internal {

template <typename T>
class ZoneAllocator {
 public:
  using value_type = T;

#ifdef V8_OS_WIN
  // The exported class ParallelMove derives from ZoneVector, which derives
  // from std::vector.  On Windows, the semantics of dllexport mean that
  // a class's superclasses that are not explicitly exported themselves get
  // implicitly exported together with the subclass, and exporting a class
  // exports all its functions -- including the std::vector() constructors
  // that don't take an explicit allocator argument, which in turn reference
  // the vector allocator's default constructor. So this constructor needs
  // to exist for linking purposes, even if it's never called.
  // Other fixes would be to disallow subclasses of ZoneVector (etc) to be
  // exported, or using composition instead of inheritance for either
  // ZoneVector and friends or for ParallelMove.
  ZoneAllocator() : ZoneAllocator(nullptr) { UNREACHABLE(); }
#endif
  explicit ZoneAllocator(Zone* zone) : zone_(zone) {
    // If we are going to allocate compressed pointers in the zone it must
    // support compression.
    DCHECK_IMPLIES(is_compressed_pointer<T>::value,
                   zone_->supports_compression());
  }
  template <typename U>
  ZoneAllocator(const ZoneAllocator<U>& other) V8_NOEXCEPT
      : ZoneAllocator<T>(other.zone()) {
    // If we are going to allocate compressed pointers in the zone it must
    // support compression.
    DCHECK_IMPLIES(is_compressed_pointer<T>::value,
                   zone_->supports_compression());
  }

  T* allocate(size_t length) { return zone_->AllocateArray<T>(length); }
  void deallocate(T* p, size_t length) { zone_->DeleteArray<T>(p, length); }

  bool operator==(ZoneAllocator const& other) const {
    return zone_ == other.zone_;
  }
  bool operator!=(ZoneAllocator const& other) const {
    return zone_ != other.zone_;
  }

  Zone* zone() const { return zone_; }

 private:
  Zone* zone_;
};

// A recycling zone allocator maintains a free list of deallocated chunks
// to reuse on subsequent allocations. The free list management is purposely
// very simple and works best for data-structures which regularly allocate and
// free blocks of similar sized memory (such as std::deque).
template <typename T>
class RecyclingZoneAllocator : public ZoneAllocator<T> {
 public:
  explicit RecyclingZoneAllocator(Zone* zone)
      : ZoneAllocator<T>(zone), free_list_(nullptr) {}
  template <typename U>
  RecyclingZoneAllocator(const RecyclingZoneAllocator<U>& other) V8_NOEXCEPT
      : ZoneAllocator<T>(other),
        free_list_(nullptr) {}

  T* allocate(size_t n) {
    // Only check top block in free list, since this will be equal to or larger
    // than the other blocks in the free list.
    if (free_list_ && free_list_->size >= n) {
      T* return_val = reinterpret_cast<T*>(free_list_);
      free_list_ = free_list_->next;
      return return_val;
    }
    return ZoneAllocator<T>::allocate(n);
  }

  void deallocate(T* p, size_t n) {
    if ((sizeof(T) * n < sizeof(FreeBlock))) return;

    // Only add block to free_list if it is equal or larger than previous block
    // so that allocation stays O(1) only having to look at the top block.
    if (!free_list_ || free_list_->size <= n) {
      // Store the free-list within the block being deallocated.
      DCHECK((sizeof(T) * n >= sizeof(FreeBlock)));
      FreeBlock* new_free_block = reinterpret_cast<FreeBlock*>(p);

      new_free_block->size = n;
      new_free_block->next = free_list_;
      free_list_ = new_free_block;
    }
  }

 private:
  struct FreeBlock {
    FreeBlock* next;
    size_t size;
  };

  FreeBlock* free_list_;
};

using ZoneBoolAllocator = ZoneAllocator<bool>;
using ZoneIntAllocator = ZoneAllocator<int>;

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_ZONE_ALLOCATOR_H_
