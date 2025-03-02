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

namespace v8::internal {

template <>
inline void MarkingBitmap::SetBitsInCell<AccessMode::NON_ATOMIC>(
    uint32_t cell_index, MarkBit::CellType mask) {
  cells()[cell_index] |= mask;
}

template <>
inline void MarkingBitmap::SetBitsInCell<AccessMode::ATOMIC>(
    uint32_t cell_index, MarkBit::CellType mask) {
  base::AsAtomicWord::Relaxed_SetBits(cells() + cell_index, mask, mask);
}

template <>
inline void MarkingBitmap::ClearBitsInCell<AccessMode::NON_ATOMIC>(
    uint32_t cell_index, MarkBit::CellType mask) {
  cells()[cell_index] &= ~mask;
}

template <>
inline void MarkingBitmap::ClearBitsInCell<AccessMode::ATOMIC>(
    uint32_t cell_index, MarkBit::CellType mask) {
  base::AsAtomicWord::Relaxed_SetBits(cells() + cell_index,
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
  return Cast(metadata_address + MutablePageMetadata::MarkingBitmapOffset());
}

// static
MarkBit MarkingBitmap::MarkBitFromAddress(Address address) {
  return MarkBitFromAddress(FromAddress(address), address);
}

// static
MarkBit MarkingBitmap::MarkBitFromAddress(MarkingBitmap* bitmap,
                                          Address address) {
  DCHECK_EQ(bitmap, FromAddress(address));
  const auto index = AddressToIndex(address);
  const auto mask = IndexInCellMask(index);
  MarkBit::CellType* cell = bitmap->cells() + IndexToCell(index);
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

// static
std::optional<MarkingHelper::WorklistTarget> MarkingHelper::ShouldMarkObject(
    Heap* heap, Tagged<HeapObject> object) {
  const auto* chunk = MemoryChunk::FromHeapObject(object);
  const auto flags = chunk->GetFlags();
  if (flags & MemoryChunk::READ_ONLY_HEAP) {
    return {};
  }
  if (v8_flags.black_allocated_pages &&
      V8_UNLIKELY(flags & MemoryChunk::BLACK_ALLOCATED)) {
    DCHECK(!(flags & MemoryChunk::kIsInYoungGenerationMask));
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
  if (v8_flags.black_allocated_pages &&
      (flags & MemoryChunk::BLACK_ALLOCATED)) {
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
template <typename MarkingStateT>
bool MarkingHelper::IsMarkedOrAlwaysLive(Heap* heap,
                                         MarkingStateT* marking_state,
                                         Tagged<HeapObject> object) {
  return (MarkingHelper::GetLivenessMode(heap, object) ==
          MarkingHelper::LivenessMode::kAlwaysLive) ||
         marking_state->IsMarked(object);
}

// static
template <typename MarkingStateT>
bool MarkingHelper::IsUnmarkedAndNotAlwaysLive(Heap* heap,
                                               MarkingStateT* marking_state,
                                               Tagged<HeapObject> object) {
  return (MarkingHelper::GetLivenessMode(heap, object) !=
          MarkingHelper::LivenessMode::kAlwaysLive) &&
         marking_state->IsUnmarked(object);
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
