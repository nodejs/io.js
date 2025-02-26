// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_H_
#define V8_HEAP_MARKING_H_

#include <cstdint>

#include "src/base/atomic-utils.h"
#include "src/common/globals.h"
#include "src/heap/marking-worklist.h"
#include "src/objects/heap-object.h"

namespace v8::internal {

class PageMetadata;

class MarkBit final {
 public:
  using CellType = uintptr_t;
  static_assert(sizeof(CellType) == sizeof(base::AtomicWord));

  V8_ALLOW_UNUSED static inline MarkBit From(Address);
  V8_ALLOW_UNUSED static inline MarkBit From(Tagged<HeapObject>);

  // These methods are meant to be used from the debugger and therefore
  // intentionally not inlined such that they are always available.
  V8_ALLOW_UNUSED static MarkBit FromForTesting(Address);
  V8_ALLOW_UNUSED static MarkBit FromForTesting(Tagged<HeapObject>);

  // The function returns true if it succeeded to
  // transition the bit from 0 to 1.
  template <AccessMode mode = AccessMode::NON_ATOMIC>
  inline bool Set();

  template <AccessMode mode = AccessMode::NON_ATOMIC>
  inline bool Get() const;

  // The function returns true if it succeeded to
  // transition the bit from 1 to 0. Only works in non-atomic contexts.
  inline bool Clear();

#ifdef DEBUG
  bool operator==(const MarkBit& other) {
    return cell_ == other.cell_ && mask_ == other.mask_;
  }
#endif

  const CellType* CellAddress() const { return cell_; }
  CellType Mask() const { return mask_; }

 private:
  inline MarkBit(CellType* cell, CellType mask) : cell_(cell), mask_(mask) {}

  CellType* const cell_;
  const CellType mask_;

  friend class MarkingBitmap;
};

template <>
inline bool MarkBit::Set<AccessMode::NON_ATOMIC>() {
  CellType old_value = *cell_;
  if ((old_value & mask_) == mask_) return false;
  *cell_ = old_value | mask_;
  return true;
}

template <>
inline bool MarkBit::Set<AccessMode::ATOMIC>() {
  return base::AsAtomicWord::Relaxed_SetBits(cell_, mask_, mask_);
}

template <>
inline bool MarkBit::Get<AccessMode::NON_ATOMIC>() const {
  return (*cell_ & mask_) != 0;
}

template <>
inline bool MarkBit::Get<AccessMode::ATOMIC>() const {
  return (base::AsAtomicWord::Relaxed_Load(cell_) & mask_) != 0;
}

inline bool MarkBit::Clear() {
  CellType old_value = *cell_;
  *cell_ = old_value & ~mask_;
  return (old_value & mask_) == mask_;
}

// Bitmap is a sequence of cells each containing fixed number of bits.
class V8_EXPORT_PRIVATE MarkingBitmap final {
 public:
  using CellType = MarkBit::CellType;
  using CellIndex = uint32_t;
  using MarkBitIndex = uint32_t;

  static constexpr uint32_t kBitsPerCell = sizeof(CellType) * kBitsPerByte;
  static constexpr uint32_t kBitsPerCellLog2 =
      base::bits::CountTrailingZeros(kBitsPerCell);
  static constexpr uint32_t kBitIndexMask = kBitsPerCell - 1;
  static constexpr uint32_t kBytesPerCell = kBitsPerCell / kBitsPerByte;
  static constexpr uint32_t kBytesPerCellLog2 =
      kBitsPerCellLog2 - kBitsPerByteLog2;

  // The length is the number of bits in this bitmap.
  static constexpr size_t kLength = ((1 << kPageSizeBits) >> kTaggedSizeLog2);

  static constexpr size_t kCellsCount =
      (kLength + kBitsPerCell - 1) >> kBitsPerCellLog2;

  // The size of the bitmap in bytes is CellsCount() * kBytesPerCell.
  static constexpr size_t kSize = kCellsCount * kBytesPerCell;

  V8_INLINE static constexpr MarkBitIndex AddressToIndex(Address address);

  V8_INLINE static constexpr MarkBitIndex LimitAddressToIndex(Address address);

  V8_INLINE static constexpr CellIndex IndexToCell(MarkBitIndex index) {
    return index >> kBitsPerCellLog2;
  }

  V8_INLINE static constexpr Address IndexToAddressOffset(MarkBitIndex index) {
    return index << kTaggedSizeLog2;
  }

  V8_INLINE static constexpr Address CellToBase(CellIndex cell_index) {
    return IndexToAddressOffset(cell_index << kBitsPerCellLog2);
  }

  V8_INLINE static constexpr uint32_t IndexInCell(MarkBitIndex index) {
    return index & kBitIndexMask;
  }

  V8_INLINE static constexpr CellType IndexInCellMask(MarkBitIndex index) {
    return static_cast<CellType>(1u) << IndexInCell(index);
  }

  // Retrieves the cell containing the provided markbit index.
  V8_INLINE static constexpr uint32_t CellAlignIndex(uint32_t index) {
    return index & ~kBitIndexMask;
  }

  V8_INLINE static MarkingBitmap* Cast(Address addr) {
    return reinterpret_cast<MarkingBitmap*>(addr);
  }

  // Gets the MarkBit for an `address` which may be unaligned (include the tag
  // bit).
  V8_INLINE static MarkBit MarkBitFromAddress(Address address);
  V8_INLINE static MarkBit MarkBitFromAddress(MarkingBitmap* bitmap,
                                              Address address);

  MarkingBitmap() = default;
  MarkingBitmap(const MarkingBitmap&) = delete;
  MarkingBitmap& operator=(const MarkingBitmap&) = delete;

  V8_INLINE CellType* cells() { return cells_; }
  V8_INLINE const CellType* cells() const { return cells_; }

  // Returns true if all bits in the range [start_index, end_index) are cleared.
  bool AllBitsClearInRange(MarkBitIndex start_index,
                           MarkBitIndex end_index) const;

  // Returns true if all bits in the range [start_index, end_index) are set.
  bool AllBitsSetInRange(MarkBitIndex start_index,
                         MarkBitIndex end_index) const;

  template <AccessMode mode>
  inline void Clear();

  // Sets all bits in the range [start_index, end_index). If the access is
  // atomic, the cells at the boundary of the range are updated with atomic
  // compare and swap operation. The inner cells are updated with relaxed write.
  template <AccessMode mode>
  inline void SetRange(MarkBitIndex start_index, MarkBitIndex end_index);

  // Clears all bits in the range [start_index, end_index). If the access is
  // atomic, the cells at the boundary of the range are updated with atomic
  // compare and swap operation. The inner cells are updated with relaxed write.
  template <AccessMode mode>
  inline void ClearRange(MarkBitIndex start_index, MarkBitIndex end_index);

  // Returns true if all bits are cleared.
  bool IsClean() const;

  // Not safe in a concurrent context.
  void Print() const;

  V8_INLINE MarkBit MarkBitFromIndexForTesting(uint32_t index) {
    const auto mask = IndexInCellMask(index);
    MarkBit::CellType* cell = cells() + IndexToCell(index);
    return MarkBit(cell, mask);
  }

  // This method provides a basis for inner-pointer resolution. It expects a
  // page and a maybe_inner_ptr that is contained in that page. It returns the
  // highest address in the page that is not larger than maybe_inner_ptr, has
  // its markbit set, and whose previous address (if it exists) does not have
  // its markbit set. If no such address exists, it returns the page area start.
  // If the page is iterable, the returned address is guaranteed to be the start
  // of a valid object in the page.
  static inline Address FindPreviousValidObject(const PageMetadata* page,
                                                Address maybe_inner_ptr);

 private:
  V8_INLINE static MarkingBitmap* FromAddress(Address address);

  // Sets bits in the given cell. The mask specifies bits to set: if a
  // bit is set in the mask then the corresponding bit is set in the cell.
  template <AccessMode mode>
  inline void SetBitsInCell(uint32_t cell_index, MarkBit::CellType mask);

  // Clears bits in the given cell. The mask specifies bits to clear: if a
  // bit is set in the mask then the corresponding bit is cleared in the cell.
  template <AccessMode mode>
  inline void ClearBitsInCell(uint32_t cell_index, MarkBit::CellType mask);

  // Set all bits in the cell range [start_cell_index, end_cell_index). If the
  // access is atomic then *still* use a relaxed memory ordering.
  template <AccessMode mode>
  void SetCellRangeRelaxed(uint32_t start_cell_index, uint32_t end_cell_index);

  template <AccessMode mode>
  // Clear all bits in the cell range [start_cell_index, end_cell_index). If the
  // access is atomic then *still* use a relaxed memory ordering.
  inline void ClearCellRangeRelaxed(uint32_t start_cell_index,
                                    uint32_t end_cell_index);

  CellType cells_[kCellsCount] = {0};
};

struct MarkingHelper final : public AllStatic {
  // TODO(340989496): Add on hold as target in ShouldMarkObject() and
  // TryMarkAndPush().
  enum class WorklistTarget : uint8_t {
    kRegular,
  };

  enum class LivenessMode : uint8_t {
    kMarkbit,
    kAlwaysLive,
  };

  // Returns whether an object should be marked and if so also returns the
  // worklist that must be used to do so.
  //
  //  Can be used with full GC and young GC using sticky markbits.
  static V8_INLINE std::optional<WorklistTarget> ShouldMarkObject(
      Heap* heap, Tagged<HeapObject> object);

  // Returns whether the markbit of an object should be considered or whether
  // the object is always considered as live.
  static V8_INLINE LivenessMode GetLivenessMode(Heap* heap,
                                                Tagged<HeapObject> object);

  // Returns true if the object is marked or resides on an always live page.
  template <typename MarkingStateT>
  static V8_INLINE bool IsMarkedOrAlwaysLive(Heap* heap,
                                             MarkingStateT* marking_state,
                                             Tagged<HeapObject> object);

  // Returns true if the object is unmarked and doesn't reside on an always live
  // page.
  template <typename MarkingStateT>
  static V8_INLINE bool IsUnmarkedAndNotAlwaysLive(Heap* heap,
                                                   MarkingStateT* marking_state,
                                                   Tagged<HeapObject> object);

  // Convenience helper around marking and pushing an object.
  //
  //  Can be used with full GC and young GC using sticky markbits.
  template <typename MarkingState>
  static V8_INLINE bool TryMarkAndPush(
      Heap* heap, MarkingWorklists::Local* marking_worklist,
      MarkingState* marking_state, WorklistTarget target_worklis,
      Tagged<HeapObject> object);
};

}  // namespace v8::internal

#endif  // V8_HEAP_MARKING_H_
