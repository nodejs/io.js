// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_OBJECT_START_BITMAP_H_
#define V8_HEAP_CPPGC_OBJECT_START_BITMAP_H_

#include <limits.h>
#include <stdint.h>

#include <array>

#include "include/cppgc/internal/write-barrier.h"
#include "src/base/atomic-utils.h"
#include "src/base/bits.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"

namespace cppgc {
namespace internal {

// A bitmap for recording object starts. Objects have to be allocated at
// minimum granularity of kGranularity.
//
// Depends on internals such as:
// - kBlinkPageSize
// - kAllocationGranularity
//
// ObjectStartBitmap supports concurrent reads from multiple threads but
// only a single mutator thread can write to it. ObjectStartBitmap relies on
// being allocated inside the same normal page.
class V8_EXPORT_PRIVATE ObjectStartBitmap {
 public:
  // Granularity of addresses added to the bitmap.
  static constexpr size_t Granularity() { return kAllocationGranularity; }

  // Maximum number of entries in the bitmap.
  static constexpr size_t MaxEntries() {
    return kReservedForBitmap * kBitsPerCell;
  }

  inline ObjectStartBitmap();

  // Finds an object header based on a
  // address_maybe_pointing_to_the_middle_of_object. Will search for an object
  // start in decreasing address order.
  template <AccessMode = AccessMode::kNonAtomic>
  inline HeapObjectHeader* FindHeader(
      ConstAddress address_maybe_pointing_to_the_middle_of_object) const;

  template <AccessMode = AccessMode::kNonAtomic>
  inline void SetBit(ConstAddress);
  template <AccessMode = AccessMode::kNonAtomic>
  inline void ClearBit(ConstAddress);
  template <AccessMode = AccessMode::kNonAtomic>
  inline bool CheckBit(ConstAddress) const;

  // Iterates all object starts recorded in the bitmap.
  //
  // The callback is of type
  //   void(Address)
  // and is passed the object start address as parameter.
  template <typename Callback>
  inline void Iterate(Callback) const;

  // Clear the object start bitmap.
  inline void Clear();

  // Marks the bitmap as fully populated. Unpopulated bitmaps are in an
  // inconsistent state and must be populated before they can be used to find
  // object headers.
  inline void MarkAsFullyPopulated();

 private:
  template <AccessMode = AccessMode::kNonAtomic>
  inline void store(size_t cell_index, uint8_t value);
  template <AccessMode = AccessMode::kNonAtomic>
  inline uint8_t load(size_t cell_index) const;

  static constexpr size_t kBitsPerCell = sizeof(uint8_t) * CHAR_BIT;
  static constexpr size_t kCellMask = kBitsPerCell - 1;
  static constexpr size_t kBitmapSize =
      (kPageSize + ((kBitsPerCell * kAllocationGranularity) - 1)) /
      (kBitsPerCell * kAllocationGranularity);
  static constexpr size_t kReservedForBitmap =
      ((kBitmapSize + kAllocationMask) & ~kAllocationMask);

  inline void ObjectStartIndexAndBit(ConstAddress, size_t*, size_t*) const;

  // `fully_populated_` is used to denote that the bitmap is populated with all
  // currently allocated objects on the page and is in a consistent state. It is
  // used to guard against using the bitmap for finding headers during
  // concurrent sweeping.
  //
  // Although this flag can be used by both the main thread and concurrent
  // sweeping threads, it is not atomic. The flag should never be accessed by
  // multiple threads at the same time. If data races are observed on this flag,
  // it likely means that the bitmap is queried while concurrent sweeping is
  // active, which is not supported and should be avoided.
  bool fully_populated_ = false;
  // The bitmap contains a bit for every kGranularity aligned address on a
  // a NormalPage, i.e., for a page of size kBlinkPageSize.
  std::array<uint8_t, kReservedForBitmap> object_start_bit_map_;
};

ObjectStartBitmap::ObjectStartBitmap() {
  Clear();
  MarkAsFullyPopulated();
}

template <AccessMode mode>
HeapObjectHeader* ObjectStartBitmap::FindHeader(
    ConstAddress address_maybe_pointing_to_the_middle_of_object) const {
  DCHECK(fully_populated_);
  const size_t page_base = reinterpret_cast<uintptr_t>(
                               address_maybe_pointing_to_the_middle_of_object) &
                           kPageBaseMask;
  DCHECK_EQ(page_base, reinterpret_cast<uintptr_t>(this) & kPageBaseMask);
  size_t object_offset = reinterpret_cast<uintptr_t>(
                             address_maybe_pointing_to_the_middle_of_object) &
                         kPageOffsetMask;
  size_t object_start_number = object_offset / kAllocationGranularity;
  size_t cell_index = object_start_number / kBitsPerCell;
  DCHECK_GT(object_start_bit_map_.size(), cell_index);
  const size_t bit = object_start_number & kCellMask;
  uint8_t byte = load<mode>(cell_index) & ((1 << (bit + 1)) - 1);
  while (!byte && cell_index) {
    DCHECK_LT(0u, cell_index);
    byte = load<mode>(--cell_index);
  }
  const int leading_zeroes = v8::base::bits::CountLeadingZeros(byte);
  object_start_number =
      (cell_index * kBitsPerCell) + (kBitsPerCell - 1) - leading_zeroes;
  object_offset = object_start_number * kAllocationGranularity;
  return reinterpret_cast<HeapObjectHeader*>(page_base + object_offset);
}

template <AccessMode mode>
void ObjectStartBitmap::SetBit(ConstAddress header_address) {
  size_t cell_index, object_bit;
  ObjectStartIndexAndBit(header_address, &cell_index, &object_bit);
  // Only a single mutator thread can write to the bitmap, so no need for CAS.
  store<mode>(cell_index,
              static_cast<uint8_t>(load(cell_index) | (1 << object_bit)));
}

template <AccessMode mode>
void ObjectStartBitmap::ClearBit(ConstAddress header_address) {
  size_t cell_index, object_bit;
  ObjectStartIndexAndBit(header_address, &cell_index, &object_bit);
  store<mode>(cell_index,
              static_cast<uint8_t>(load(cell_index) & ~(1 << object_bit)));
}

template <AccessMode mode>
bool ObjectStartBitmap::CheckBit(ConstAddress header_address) const {
  size_t cell_index, object_bit;
  ObjectStartIndexAndBit(header_address, &cell_index, &object_bit);
  return load<mode>(cell_index) & (1 << object_bit);
}

template <AccessMode mode>
void ObjectStartBitmap::store(size_t cell_index, uint8_t value) {
  if (mode == AccessMode::kNonAtomic) {
    object_start_bit_map_[cell_index] = value;
    return;
  }
  v8::base::AsAtomicPtr(&object_start_bit_map_[cell_index])
      ->store(value, std::memory_order_release);
}

template <AccessMode mode>
uint8_t ObjectStartBitmap::load(size_t cell_index) const {
  if (mode == AccessMode::kNonAtomic) {
    return object_start_bit_map_[cell_index];
  }
  return v8::base::AsAtomicPtr(&object_start_bit_map_[cell_index])
      ->load(std::memory_order_acquire);
}

void ObjectStartBitmap::ObjectStartIndexAndBit(ConstAddress header_address,
                                               size_t* cell_index,
                                               size_t* bit) const {
  const size_t object_offset =
      reinterpret_cast<size_t>(header_address) & kPageOffsetMask;
  DCHECK(!(object_offset & kAllocationMask));
  const size_t object_start_number = object_offset / kAllocationGranularity;
  *cell_index = object_start_number / kBitsPerCell;
  DCHECK_GT(kBitmapSize, *cell_index);
  *bit = object_start_number & kCellMask;
}

template <typename Callback>
inline void ObjectStartBitmap::Iterate(Callback callback) const {
  const Address page_base = reinterpret_cast<Address>(
      reinterpret_cast<uintptr_t>(this) & kPageBaseMask);
  for (size_t cell_index = 0; cell_index < kReservedForBitmap; cell_index++) {
    if (!object_start_bit_map_[cell_index]) continue;

    uint8_t value = object_start_bit_map_[cell_index];
    while (value) {
      const int trailing_zeroes = v8::base::bits::CountTrailingZeros(value);
      const size_t object_start_number =
          (cell_index * kBitsPerCell) + trailing_zeroes;
      const Address object_address =
          page_base + (kAllocationGranularity * object_start_number);
      callback(object_address);
      // Clear current object bit in temporary value to advance iteration.
      value &= ~(1 << (object_start_number & kCellMask));
    }
  }
}

void ObjectStartBitmap::MarkAsFullyPopulated() {
  DCHECK(!fully_populated_);
  fully_populated_ = true;
}

void ObjectStartBitmap::Clear() {
  fully_populated_ = false;
  std::fill(object_start_bit_map_.begin(), object_start_bit_map_.end(), 0);
}

// A platform aware version of ObjectStartBitmap to provide platform specific
// optimizations (e.g. Use non-atomic stores on ARMv7 when not marking).
class V8_EXPORT_PRIVATE PlatformAwareObjectStartBitmap
    : public ObjectStartBitmap {
 public:
  template <AccessMode = AccessMode::kNonAtomic>
  inline void SetBit(ConstAddress);
  template <AccessMode = AccessMode::kNonAtomic>
  inline void ClearBit(ConstAddress);

 private:
  template <AccessMode>
  static bool ShouldForceNonAtomic();
};

// static
template <AccessMode mode>
bool PlatformAwareObjectStartBitmap::ShouldForceNonAtomic() {
#if defined(V8_HOST_ARCH_ARM)
  // Use non-atomic accesses on ARMv7 when marking is not active.
  if (mode == AccessMode::kAtomic) {
    if (V8_LIKELY(!WriteBarrier::IsEnabled())) return true;
  }
#endif  // defined(V8_HOST_ARCH_ARM)
  return false;
}

template <AccessMode mode>
void PlatformAwareObjectStartBitmap::SetBit(ConstAddress header_address) {
  if (ShouldForceNonAtomic<mode>()) {
    ObjectStartBitmap::SetBit<AccessMode::kNonAtomic>(header_address);
    return;
  }
  ObjectStartBitmap::SetBit<mode>(header_address);
}

template <AccessMode mode>
void PlatformAwareObjectStartBitmap::ClearBit(ConstAddress header_address) {
  if (ShouldForceNonAtomic<mode>()) {
    ObjectStartBitmap::ClearBit<AccessMode::kNonAtomic>(header_address);
    return;
  }
  ObjectStartBitmap::ClearBit<mode>(header_address);
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_OBJECT_START_BITMAP_H_
