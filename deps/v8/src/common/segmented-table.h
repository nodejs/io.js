// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_SEGMENTED_TABLE_H_
#define V8_COMMON_SEGMENTED_TABLE_H_

#include "include/v8-internal.h"
#include "src/base/macros.h"
#include "src/common/code-memory-access.h"

namespace v8 {
namespace internal {

/**
 * A thread-safe table with a fixed maximum size split into segments.
 *
 * The table provides thread-safe methods to allocate and free of segments and
 * an inline freelist implementation. Allocation and Freeing of entries is
 * implemented in subclasses since it depends on if the table is manually
 * managed or GCed.
 *
 * For the purpose of memory management, the table is partitioned into Segments
 * (for example 64kb memory chunks) that are grouped together in "Spaces". All
 * segments in a space share a freelist, and so entry allocation and garbage
 * collection happen on the level of spaces.
 *
 * The Entry type defines how the freelist is represented. For that, it must
 * implement the following methods:
 * - void MakeFreelistEntry(uint32_t next_entry_index)
 * - uint32_t GetNextFreelistEntry()
 */
template <typename Entry, size_t size>
class V8_EXPORT_PRIVATE SegmentedTable {
 protected:
  static constexpr bool kIsWriteProtected = Entry::IsWriteProtected;
  static constexpr int kEntrySize = sizeof(Entry);

#ifdef V8_TARGET_ARCH_64_BIT
  // On 64 bit, we use a large address space reservation for the table memory.
  static constexpr bool kUseContiguousMemory = true;
  static constexpr size_t kReservationSize = size;
  static constexpr size_t kMaxCapacity = kReservationSize / kEntrySize;
#else
  // On 32 bit, segments are individually mapped.
  static constexpr bool kUseContiguousMemory = false;
#endif

  // For managing the table's backing memory, the table is partitioned into
  // segments of this size. Segments can then be allocated and freed using the
  // AllocateAndInitializeSegment() and FreeTableSegment() routines.
  static constexpr size_t kSegmentSize = 64 * KB;
  static constexpr size_t kEntriesPerSegment = kSegmentSize / kEntrySize;

  // Struct representing a segment of the table.
  struct Segment {
   public:
    // Initialize a segment given its number.
    explicit Segment(uint32_t number) : number_(number) {}

    // Returns the segment starting at the specified offset from the base of the
    // table.
    static Segment At(uint32_t offset);

    // Returns the segment containing the entry at the given index.
    static Segment Containing(uint32_t entry_index);

    // The segments of a table are numbered sequentially. This method returns
    // the number of this segment.
    uint32_t number() const { return number_; }

    // Returns the offset of this segment from the table base.
    uint32_t offset() const { return number_ * kSegmentSize; }

    // Returns the index of the first entry in this segment.
    uint32_t first_entry() const { return number_ * kEntriesPerSegment; }

    // Return the index of the last entry in this segment.
    uint32_t last_entry() const {
      return first_entry() + kEntriesPerSegment - 1;
    }

    // Segments are ordered by their id/offset.
    bool operator<(const Segment& other) const {
      return number_ < other.number_;
    }

   private:
    // A segment is identified by its number, which is its offset from the base
    // of the table divided by the segment size.
    const uint32_t number_;
  };

  // Struct representing the head of the freelist.
  //
  // A segmented table uses simple, singly-linked lists to manage free entries.
  // Each entry on the freelist contains the 32-bit index of the next entry. The
  // last entry points to zero.
  struct FreelistHead {
    constexpr FreelistHead() : next_(0), length_(0) {}
    constexpr FreelistHead(uint32_t next, uint32_t length)
        : next_(next), length_(length) {}

    // Returns the index of the next entry on the freelist.
    // If the freelist is empty, this returns zero.
    uint32_t next() const { return next_; }

    // Returns the total length of the freelist.
    uint32_t length() const { return length_; }

    bool is_empty() const { return length_ == 0; }

   private:
    uint32_t next_;
    uint32_t length_;
  };

  // We expect the FreelistHead struct to fit into a single atomic word.
  // Otherwise, access to it would be slow.
  static_assert(std::atomic<FreelistHead>::is_always_lock_free);

  SegmentedTable() = default;
  SegmentedTable(const SegmentedTable&) = delete;
  SegmentedTable& operator=(const SegmentedTable&) = delete;

  // This Iterator also acts as a scope object to temporarily lift any
  // write-protection (if kIsWriteProtected is true).
  class WriteIterator {
   public:
    explicit WriteIterator(Entry* base, uint32_t index);

    uint32_t index() const { return index_; }
    Entry* operator->() {
      DCHECK(!crossed_segment_);
      return &base_[index_];
    }
    Entry& operator*() {
      DCHECK(!crossed_segment_);
      return base_[index_];
    }
    WriteIterator& operator++() {
      index_++;
#ifdef DEBUG
      if (IsAligned(index_, kEntriesPerSegment)) {
        crossed_segment_ = true;
      }
#endif
      return *this;
    }
    WriteIterator& operator--() {
      DCHECK_GT(index_, 0);
#ifdef DEBUG
      if (IsAligned(index_, kEntriesPerSegment)) {
        crossed_segment_ = true;
      }
#endif
      index_--;
      return *this;
    }

   private:
    Entry* base_;
    uint32_t index_;
    std::conditional_t<kIsWriteProtected, CFIMetadataWriteScope,
                       NopRwxMemoryWriteScope>
        write_scope_;
#ifdef DEBUG
    bool crossed_segment_ = false;
#endif
  };

  // Access the entry at the specified index.
  Entry& at(uint32_t index);
  const Entry& at(uint32_t index) const;

  // Returns an iterator that can be used to perform multiple write operations
  // without switching the write-protections all the time (if kIsWriteProtected
  // is true).
  WriteIterator iter_at(uint32_t index);

  // Returns true if this table has been initialized.
  bool is_initialized() const;

  // Returns the base address of this table.
  Address base() const;

  // Allocate a new segment in this table.
  //
  // The segment is initialized with freelist entries.
  std::pair<Segment, FreelistHead> AllocateAndInitializeSegment();

  // Initialize a table segment with a freelist.
  //
  // Note that you don't need to call this function on segments allocated with
  // `AllocateAndInitializeSegment()` since those already get initialized.
  FreelistHead InitializeFreeList(Segment segment, uint32_t start_offset = 0);

  // Free the specified segment of this table.
  //
  // The memory of this segment will afterwards be inaccessible.
  void FreeTableSegment(Segment segment);

  // Initializes the table by reserving the backing memory, allocating an
  // initial segment, and populating the freelist.
  void Initialize();

  // Deallocates all memory associated with this table.
  void TearDown();

  // The pointer to the base of the virtual address space backing this table.
  // All entry accesses happen through this pointer.
  // It is equivalent to |vas_->base()| and is effectively const after
  // initialization since the backing memory is never reallocated.
  Entry* base_ = nullptr;

  // The virtual address space backing this table.
  // This is used to manage the underlying OS pages, in particular to allocate
  // and free the segments that make up the table.
  VirtualAddressSpace* vas_ = nullptr;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_SEGMENTED_TABLE_H_
