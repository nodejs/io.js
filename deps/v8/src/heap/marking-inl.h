// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_INL_H_
#define V8_HEAP_MARKING_INL_H_

#include "src/base/build_config.h"
#include "src/base/macros.h"
#include "src/heap/heap-inl.h"
#include "src/heap/marking.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/spaces.h"
#include "src/objects/instance-type-inl.h"

namespace v8::internal {

template <>
inline void MarkingBitmap::SetBitsInCell<AccessMode::NON_ATOMIC>(
    uint32_t cell_index, MarkBit::CellType mask) {
  cells()[cell_index] |= mask;
}

template <>
inline void MarkingBitmap::SetBitsInCell<AccessMode::ATOMIC>(
    uint32_t cell_index, MarkBit::CellType mask) {
  base::AsAtomicWord::SetBits(cells() + cell_index, mask, mask);
}

template <>
inline void MarkingBitmap::ClearBitsInCell<AccessMode::NON_ATOMIC>(
    uint32_t cell_index, MarkBit::CellType mask) {
  cells()[cell_index] &= ~mask;
}

template <>
inline void MarkingBitmap::ClearBitsInCell<AccessMode::ATOMIC>(
    uint32_t cell_index, MarkBit::CellType mask) {
  base::AsAtomicWord::SetBits(cells() + cell_index,
                              static_cast<MarkBit::CellType>(0u), mask);
}

template <>
inline void MarkingBitmap::ClearCellRangeRelaxed<AccessMode::ATOMIC>(
    uint32_t start_cell_index, uint32_t end_cell_index) {
  base::AtomicWord* cell_base = reinterpret_cast<base::AtomicWord*>(cells());
  for (uint32_t i = start_cell_index; i < end_cell_index; i++) {
    base::Relaxed_Store(cell_base + i, 0);
  }
}

template <>
inline void MarkingBitmap::ClearCellRangeRelaxed<AccessMode::NON_ATOMIC>(
    uint32_t start_cell_index, uint32_t end_cell_index) {
  for (uint32_t i = start_cell_index; i < end_cell_index; i++) {
    cells()[i] = 0;
  }
}

template <>
inline void MarkingBitmap::SetCellRangeRelaxed<AccessMode::ATOMIC>(
    uint32_t start_cell_index, uint32_t end_cell_index) {
  base::AtomicWord* cell_base = reinterpret_cast<base::AtomicWord*>(cells());
  for (uint32_t i = start_cell_index; i < end_cell_index; i++) {
    base::Relaxed_Store(cell_base + i,
                        std::numeric_limits<MarkBit::CellType>::max());
  }
}

template <>
inline void MarkingBitmap::SetCellRangeRelaxed<AccessMode::NON_ATOMIC>(
    uint32_t start_cell_index, uint32_t end_cell_index) {
  for (uint32_t i = start_cell_index; i < end_cell_index; i++) {
    cells()[i] = std::numeric_limits<MarkBit::CellType>::max();
  }
}

template <AccessMode mode>
void MarkingBitmap::Clear() {
  ClearCellRangeRelaxed<mode>(0, kCellsCount);
  if constexpr (mode == AccessMode::ATOMIC) {
    // This fence prevents re-ordering of publishing stores with the mark-bit
    // setting stores.
    base::SeqCst_MemoryFence();
  }
}

template <AccessMode mode>
inline void MarkingBitmap::SetRange(MarkBitIndex start_index,
                                    MarkBitIndex end_index) {
  if (start_index >= end_index) return;
  end_index--;

  const CellIndex start_cell_index = IndexToCell(start_index);
  const MarkBit::CellType start_index_mask = IndexInCellMask(start_index);
  const CellIndex end_cell_index = IndexToCell(end_index);
  const MarkBit::CellType end_index_mask = IndexInCellMask(end_index);

  if (start_cell_index != end_cell_index) {
    // Firstly, fill all bits from the start address to the end of the first
    // cell with 1s.
    SetBitsInCell<mode>(start_cell_index, ~(start_index_mask - 1));
    // Then fill all in between cells with 1s.
    SetCellRangeRelaxed<mode>(start_cell_index + 1, end_cell_index);
    // Finally, fill all bits until the end address in the last cell with 1s.
    SetBitsInCell<mode>(end_cell_index, end_index_mask | (end_index_mask - 1));
  } else {
    SetBitsInCell<mode>(start_cell_index,
                        end_index_mask | (end_index_mask - start_index_mask));
  }
  if (mode == AccessMode::ATOMIC) {
    // This fence prevents re-ordering of publishing stores with the mark-bit
    // setting stores.
    base::SeqCst_MemoryFence();
  }
}

template <AccessMode mode>
inline void MarkingBitmap::ClearRange(MarkBitIndex start_index,
                                      MarkBitIndex end_index) {
  if (start_index >= end_index) return;
  end_index--;

  const CellIndex start_cell_index = IndexToCell(start_index);
  const MarkBit::CellType start_index_mask = IndexInCellMask(start_index);
  const CellIndex end_cell_index = IndexToCell(end_index);
  const MarkBit::CellType end_index_mask = IndexInCellMask(end_index);

  if (start_cell_index != end_cell_index) {
    // Firstly, fill all bits from the start address to the end of the first
    // cell with 0s.
    ClearBitsInCell<mode>(start_cell_index, ~(start_index_mask - 1));
    // Then fill all in between cells with 0s.
    ClearCellRangeRelaxed<mode>(start_cell_index + 1, end_cell_index);
    // Finally, set all bits until the end address in the last cell with 0s.
    ClearBitsInCell<mode>(end_cell_index,
                          end_index_mask | (end_index_mask - 1));
  } else {
    ClearBitsInCell<mode>(start_cell_index,
                          end_index_mask | (end_index_mask - start_index_mask));
  }
  if (mode == AccessMode::ATOMIC) {
    // This fence prevents re-ordering of publishing stores with the mark-bit
    // clearing stores.
    base::SeqCst_MemoryFence();
  }
}

// static
MarkingBitmap* MarkingBitmap::FromAddress(Address address) {
  Address metadata_address =
      MutablePageMetadata::FromAddress(address)->MetadataAddress();
  return Cast(metadata_address + MemoryChunkLayout::kMarkingBitmapOffset);
}

// static
MarkBit MarkingBitmap::MarkBitFromAddress(Address address) {
  const auto index = AddressToIndex(address);
  const auto mask = IndexInCellMask(index);
  MarkBit::CellType* cell = FromAddress(address)->cells() + IndexToCell(index);
  return MarkBit(cell, mask);
}

// static
constexpr MarkingBitmap::MarkBitIndex MarkingBitmap::AddressToIndex(
    Address address) {
  return MemoryChunk::AddressToOffset(address) >> kTaggedSizeLog2;
}

// static
constexpr MarkingBitmap::MarkBitIndex MarkingBitmap::LimitAddressToIndex(
    Address address) {
  if (MemoryChunk::IsAligned(address)) return kLength;
  return AddressToIndex(address);
}

// static
inline Address MarkingBitmap::FindPreviousValidObject(const PageMetadata* page,
                                                      Address maybe_inner_ptr) {
  DCHECK(page->Contains(maybe_inner_ptr));
  const auto* bitmap = page->marking_bitmap();
  const MarkBit::CellType* cells = bitmap->cells();

  // The first actual bit of the bitmap, corresponding to page->area_start(),
  // is at start_index which is somewhere in (not necessarily at the start of)
  // start_cell_index.
  const auto start_index = MarkingBitmap::AddressToIndex(page->area_start());
  const auto start_cell_index = MarkingBitmap::IndexToCell(start_index);
  // We assume that all markbits before start_index are clear:
  // SLOW_DCHECK(bitmap->AllBitsClearInRange(0, start_index));
  // This has already been checked for the entire bitmap before starting marking
  // by MarkCompactCollector::VerifyMarkbitsAreClean.

  const auto index = MarkingBitmap::AddressToIndex(maybe_inner_ptr);
  auto cell_index = MarkingBitmap::IndexToCell(index);
  const auto index_in_cell = MarkingBitmap::IndexInCell(index);
  DCHECK_GT(MarkingBitmap::kBitsPerCell, index_in_cell);
  auto cell = cells[cell_index];

  // Clear the bits corresponding to higher addresses in the cell.
  cell &= ((~static_cast<MarkBit::CellType>(0)) >>
           (MarkingBitmap::kBitsPerCell - index_in_cell - 1));

  // Traverse the bitmap backwards, until we find a markbit that is set and
  // whose previous markbit (if it exists) is unset.
  // First, iterate backwards to find a cell with any set markbit.
  while (cell == 0 && cell_index > start_cell_index) cell = cells[--cell_index];
  if (cell == 0) {
    DCHECK_EQ(start_cell_index, cell_index);
    // We have reached the start of the page.
    return page->area_start();
  }

  // We have found such a cell.
  const auto leading_zeros = base::bits::CountLeadingZeros(cell);
  const auto leftmost_ones =
      base::bits::CountLeadingZeros(~(cell << leading_zeros));
  const auto index_of_last_leftmost_one =
      MarkingBitmap::kBitsPerCell - leading_zeros - leftmost_ones;

  const MemoryChunk* chunk = page->Chunk();

  // If the leftmost sequence of set bits does not reach the start of the cell,
  // we found it.
  if (index_of_last_leftmost_one > 0) {
    return chunk->address() + MarkingBitmap::IndexToAddressOffset(
                                  cell_index * MarkingBitmap::kBitsPerCell +
                                  index_of_last_leftmost_one);
  }

  // The leftmost sequence of set bits reaches the start of the cell. We must
  // keep traversing backwards until we find the first unset markbit.
  if (cell_index == start_cell_index) {
    // We have reached the start of the page.
    return page->area_start();
  }

  // Iterate backwards to find a cell with any unset markbit.
  do {
    cell = cells[--cell_index];
  } while (~cell == 0 && cell_index > start_cell_index);
  if (~cell == 0) {
    DCHECK_EQ(start_cell_index, cell_index);
    // We have reached the start of the page.
    return page->area_start();
  }

  // We have found such a cell.
  const auto leading_ones = base::bits::CountLeadingZeros(~cell);
  const auto index_of_last_leading_one =
      MarkingBitmap::kBitsPerCell - leading_ones;
  DCHECK_LT(0, index_of_last_leading_one);
  return chunk->address() + MarkingBitmap::IndexToAddressOffset(
                                cell_index * MarkingBitmap::kBitsPerCell +
                                index_of_last_leading_one);
}

// static
MarkBit MarkBit::From(Address address) {
  return MarkingBitmap::MarkBitFromAddress(address);
}

// static
MarkBit MarkBit::From(Tagged<HeapObject> heap_object) {
  return MarkingBitmap::MarkBitFromAddress(heap_object.ptr());
}

LiveObjectRange::iterator::iterator() : cage_base_(kNullAddress) {}

LiveObjectRange::iterator::iterator(const PageMetadata* page)
    : page_(page),
      cells_(page->marking_bitmap()->cells()),
      cage_base_(page->heap()->isolate()),
      current_cell_index_(MarkingBitmap::IndexToCell(
          MarkingBitmap::AddressToIndex(page->area_start()))),
      current_cell_(cells_[current_cell_index_]) {
  AdvanceToNextValidObject();
}

LiveObjectRange::iterator& LiveObjectRange::iterator::operator++() {
  AdvanceToNextValidObject();
  return *this;
}

LiveObjectRange::iterator LiveObjectRange::iterator::operator++(int) {
  iterator retval = *this;
  ++(*this);
  return retval;
}

void LiveObjectRange::iterator::AdvanceToNextValidObject() {
  // If we found a regular object we are done. In case of free space, we
  // need to continue.
  //
  // Reading the instance type of the map is safe here even in the presence
  // of the mutator writing a new Map because Map objects are published with
  // release stores (or are otherwise read-only) and the map is retrieved  in
  // `AdvanceToNextMarkedObject()` using an acquire load.
  while (AdvanceToNextMarkedObject() &&
         InstanceTypeChecker::IsFreeSpaceOrFiller(current_map_)) {
  }
}

bool LiveObjectRange::iterator::AdvanceToNextMarkedObject() {
  // The following block moves the iterator to the next cell from the current
  // object. This means skipping all possibly set mark bits (in case of black
  // allocation).
  if (!current_object_.is_null()) {
    // Compute an end address that is inclusive. This allows clearing the cell
    // up and including the end address. This works for one word fillers as
    // well as other objects.
    Address next_object = current_object_.address() + current_size_;
    current_object_ = HeapObject();
    if (MemoryChunk::IsAligned(next_object)) {
      return false;
    }
    // Area end may not be exactly aligned to kAlignment. We don't need to bail
    // out for area_end() though as we are guaranteed to have a bit for the
    // whole page.
    DCHECK_LE(next_object, page_->area_end());
    // Move to the corresponding cell of the end index.
    const auto next_markbit_index = MarkingBitmap::AddressToIndex(next_object);
    DCHECK_GE(MarkingBitmap::IndexToCell(next_markbit_index),
              current_cell_index_);
    current_cell_index_ = MarkingBitmap::IndexToCell(next_markbit_index);
    DCHECK_LT(current_cell_index_, MarkingBitmap::kCellsCount);
    // Mask out lower addresses in the cell.
    const MarkBit::CellType mask =
        MarkingBitmap::IndexInCellMask(next_markbit_index);
    current_cell_ = cells_[current_cell_index_] & ~(mask - 1);
  }
  // The next block finds any marked object starting from the current cell.
  const MemoryChunk* chunk = page_->Chunk();
  while (true) {
    if (current_cell_) {
      const auto trailing_zeros = base::bits::CountTrailingZeros(current_cell_);
      Address current_cell_base =
          chunk->address() + MarkingBitmap::CellToBase(current_cell_index_);
      Address object_address = current_cell_base + trailing_zeros * kTaggedSize;
      // The object may be a filler which we want to skip.
      current_object_ = HeapObject::FromAddress(object_address);
      current_map_ = current_object_->map(cage_base_, kAcquireLoad);
      DCHECK(MapWord::IsMapOrForwarded(current_map_));
      current_size_ = ALIGN_TO_ALLOCATION_ALIGNMENT(
          current_object_->SizeFromMap(current_map_));
      CHECK(page_->ContainsLimit(object_address + current_size_));
      return true;
    }
    if (++current_cell_index_ >= MarkingBitmap::kCellsCount) break;
    current_cell_ = cells_[current_cell_index_];
  }
  return false;
}

LiveObjectRange::iterator LiveObjectRange::begin() { return iterator(page_); }

LiveObjectRange::iterator LiveObjectRange::end() { return iterator(); }

// static
std::optional<MarkingHelper::WorklistTarget> MarkingHelper::ShouldMarkObject(
    Heap* heap, Tagged<HeapObject> object) {
  const auto* chunk = MemoryChunk::FromHeapObject(object);
  const auto flags = chunk->GetFlags();
  if (flags & MemoryChunk::READ_ONLY_HEAP) {
    return {};
  }
  if (V8_LIKELY(!(flags & MemoryChunk::IN_WRITABLE_SHARED_SPACE))) {
    return {MarkingHelper::WorklistTarget::kRegular};
  }
  // Object in shared writable space. Only mark it if the Isolate is owning the
  // shared space.
  //
  // TODO(340989496): Speed up check here by keeping the flag on Heap.
  if (heap->isolate()->is_shared_space_isolate()) {
    return {MarkingHelper::WorklistTarget::kRegular};
  }
  return {};
}

// static
MarkingHelper::LivenessMode MarkingHelper::GetLivenessMode(
    Heap* heap, Tagged<HeapObject> object) {
  const auto* chunk = MemoryChunk::FromHeapObject(object);
  const auto flags = chunk->GetFlags();
  if (flags & MemoryChunk::READ_ONLY_HEAP) {
    return MarkingHelper::LivenessMode::kAlwaysLive;
  }
  if (V8_LIKELY(!(flags & MemoryChunk::IN_WRITABLE_SHARED_SPACE))) {
    return MarkingHelper::LivenessMode::kMarkbit;
  }
  // Object in shared writable space. Only mark it if the Isolate is owning the
  // shared space.
  //
  // TODO(340989496): Speed up check here by keeping the flag on Heap.
  if (heap->isolate()->is_shared_space_isolate()) {
    return MarkingHelper::LivenessMode::kMarkbit;
  }
  return MarkingHelper::LivenessMode::kAlwaysLive;
}

// static
template <typename MarkingState>
bool MarkingHelper::TryMarkAndPush(Heap* heap,
                                   MarkingWorklists::Local* marking_worklist,
                                   MarkingState* marking_state,
                                   WorklistTarget target_worklist,
                                   Tagged<HeapObject> object) {
  DCHECK(heap->Contains(object));
  if (marking_state->TryMark(object)) {
    if (V8_LIKELY(target_worklist == WorklistTarget::kRegular)) {
      marking_worklist->Push(object);
    }
    return true;
  }
  return false;
}

}  // namespace v8::internal

#endif  // V8_HEAP_MARKING_INL_H_
