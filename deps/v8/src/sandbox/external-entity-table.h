// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_ENTITY_TABLE_H_
#define V8_SANDBOX_EXTERNAL_ENTITY_TABLE_H_

#include <set>

#include "include/v8-platform.h"
#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/common/code-memory-access.h"
#include "src/common/globals.h"
#include "src/common/segmented-table.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

class Isolate;

/**
 * A thread-safe table with a fixed maximum size for storing references to
 * objects located outside of the sandbox.
 *
 * An external entity table provides the basic mechanisms to ensure
 * safe access to objects located outside the sandbox, but referenced
 * from within it. When an external entity table is used, objects located
 * inside the sandbox reference outside objects through indices into the table.
 *
 * The ExternalEntityTable class should be seen an an incomplete class that
 * needs to be extended by a concrete implementation class, such as the
 * ExternalPointerTable class, as it is lacking some functionality. In
 * particular, while the ExternalEntityTable implements basic table memory
 * management as well as entry allocation routines, it does not implement any
 * logic for reclaiming entries such as garbage collection. This must be done
 * by the child classes.
 *
 * For the purpose of memory management, the table is partitioned into Segments
 * (for example 64kb memory chunks) that are grouped together in "Spaces". All
 * segments in a space share a freelist, and so entry allocation and garbage
 * collection happen on the level of spaces.
 */
template <typename Entry, size_t size>
class V8_EXPORT_PRIVATE ExternalEntityTable
    : public SegmentedTable<Entry, size> {
 protected:
  using Base = SegmentedTable<Entry, size>;
  using FreelistHead = Base::FreelistHead;
  using Segment = Base::Segment;
  using WriteIterator = Base::WriteIterator;
  static constexpr size_t kSegmentSize = Base::kSegmentSize;
  static constexpr size_t kEntriesPerSegment = Base::kEntriesPerSegment;
  static constexpr size_t kEntrySize = Base::kEntrySize;

  // A collection of segments in an external entity table.
  //
  // For the purpose of memory management, a table is partitioned into segments
  // of a fixed size (e.g. 64kb). A Space is a collection of segments that all
  // share the same freelist. As such, entry allocation and freeing (e.g.
  // through garbage collection) all happen on the level of spaces.
  //
  // Spaces allow implementing features such as:
  // * Young generation GC support (a separate space is used for all entries
  //   belonging to the young generation)
  // * Having double-width entries in a table (a dedicated space is used that
  //   contains only double-width entries)
  // * Sharing one table between multiple isolates that perform GC independently
  //   (each Isolate owns one space)
  struct Space {
   public:
    Space() = default;
    Space(const Space&) = delete;
    Space& operator=(const Space&) = delete;
    ~Space();

    // Determines the number of entries currently on the freelist.
    // As entries can be allocated from other threads, the freelist size may
    // have changed by the time this method returns. As such, the returned
    // value should only be treated as an approximation.
    uint32_t freelist_length() const;

    // Returns the current number of segments currently associated with this
    // space.
    // The caller must lock the mutex.
    uint32_t num_segments();

    // Returns whether this space is currently empty.
    // The caller must lock the mutex.
    bool is_empty() { return num_segments() == 0; }

    // Returns the current capacity of this space.
    // The capacity of a space is the total number of entries it can contain.
    // The caller must lock the mutex.
    uint32_t capacity() { return num_segments() * kEntriesPerSegment; }

    // Returns true if this space contains the entry with the given index.
    bool Contains(uint32_t index);

    // Whether this space is attached to a table's internal read-only segment.
    bool is_internal_read_only_space() const {
      return is_internal_read_only_space_;
    }

#ifdef DEBUG
    // Check whether this space belongs to the given external entity table.
    bool BelongsTo(const void* table) const { return owning_table_ == table; }
#endif  // DEBUG

   protected:
    friend class ExternalEntityTable<Entry, size>;

#ifdef DEBUG
    // In debug builds we keep track of which table a space belongs to to be
    // able to insert additional DCHECKs that verify that spaces are always used
    // with the correct table.
    std::atomic<void*> owning_table_ = nullptr;
#endif

    // The freelist used by this space.
    // This contains both the index of the first entry in the freelist and the
    // total length of the freelist as both values need to be updated together
    // in a single atomic operation to stay consistent in the case of concurrent
    // entry allocations.
    std::atomic<FreelistHead> freelist_head_ = FreelistHead();

    // The collection of segments belonging to this space.
    std::set<Segment> segments_;

    // Whether this is the internal RO space, which has special semantics:
    // - read-only page permissions after initialization,
    // - the space is not swept since slots are live by definition,
    // - contains exactly one segment, located at offset 0, and
    // - the segment's lifecycle is managed by `owning_table_`.
    bool is_internal_read_only_space_ = false;

    // Mutex guarding access to the segments_ set.
    base::Mutex mutex_;
  };

  // A Space that supports black allocations.
  struct SpaceWithBlackAllocationSupport : public Space {
    bool allocate_black() { return allocate_black_; }
    void set_allocate_black(bool allocate_black) {
      allocate_black_ = allocate_black;
    }

   private:
    bool allocate_black_ = false;
  };

  ExternalEntityTable() = default;
  ExternalEntityTable(const ExternalEntityTable&) = delete;
  ExternalEntityTable& operator=(const ExternalEntityTable&) = delete;

  // Allocates a new entry in the given space and return its index.
  //
  // If there are no free entries, then this will extend the space by
  // allocating a new segment.
  // This method is atomic and can be called from background threads.
  uint32_t AllocateEntry(Space* space);

  // Attempts to allocate an entry in the given space below the specified index.
  //
  // If there are no free entries at a lower index, this method will fail and
  // return zero. This method will therefore never allocate a new segment.
  // This method is atomic and can be called from background threads.
  uint32_t AllocateEntryBelow(Space* space, uint32_t threshold_index);

  // Try to allocate the first entry of the freelist.
  //
  // This method is mostly a wrapper around an atomic compare-and-swap which
  // replaces the current freelist head with the next entry in the freelist,
  // thereby allocating the entry at the start of the freelist.
  bool TryAllocateEntryFromFreelist(Space* space, FreelistHead freelist);

  // Allocate a new segment and add it to the given space.
  //
  // This should only be called when the freelist of the space is currently
  // empty. It will then refill the freelist with all entries in the newly
  // allocated segment.
  FreelistHead Extend(Space* space);

  // Sweeps the given space.
  //
  // This will free all unmarked entries to the freelist and unmark all live
  // entries. The table is swept top-to-bottom so that the freelist ends up
  // sorted. During sweeping, new entries must not be allocated.
  //
  // This is a generic implementation of table sweeping and requires that the
  // Entry type implements the following additional methods:
  // - bool IsMarked()
  // - void Unmark()
  //
  // Returns the number of live entries after sweeping.
  uint32_t GenericSweep(Space* space);

  // Iterate over all entries in the given space.
  //
  // The callback function will be invoked for every entry and be passed the
  // index of that entry as argument.
  template <typename Callback>
  void IterateEntriesIn(Space* space, Callback callback);

  // Marker value for the freelist_head_ member to indicate that entry
  // allocation is currently forbidden, for example because the table is being
  // swept as part of a mark+sweep garbage collection. This value should never
  // occur as freelist_head_ value during normal operations and should be easy
  // to recognize.
  static constexpr FreelistHead kEntryAllocationIsForbiddenMarker =
      FreelistHead(-1, -1);

 public:
  // Initializes the table by reserving the backing memory, allocating an
  // initial segment, and populating the freelist.
  void Initialize();

  // Deallocates all memory associated with this table.
  void TearDown();

  // Initializes the given space for use with this table.
  void InitializeSpace(Space* space);

  // Deallocates all segments owned by the given space.
  void TearDownSpace(Space* space);

  // Attaches/detaches the given space to the internal read-only segment. Note
  // the lifetime of the underlying segment itself is managed by the table.
  void AttachSpaceToReadOnlySegment(Space* space);
  void DetachSpaceFromReadOnlySegment(Space* space);

  // Use this scope to temporarily unseal the read-only segment (i.e. change
  // permissions to RW).
  class UnsealReadOnlySegmentScope final {
   public:
    explicit UnsealReadOnlySegmentScope(ExternalEntityTable<Entry, size>* table)
        : table_(table) {
      table_->UnsealReadOnlySegment();
    }

    ~UnsealReadOnlySegmentScope() { table_->SealReadOnlySegment(); }

   private:
    ExternalEntityTable<Entry, size>* const table_;
  };

 private:
  // Required for Isolate::CheckIsolateLayout().
  friend class Isolate;

  static constexpr uint32_t kInternalReadOnlySegmentOffset = 0;
  static constexpr uint32_t kInternalNullEntryIndex = 0;

  // Helpers to toggle the first segment's permissions between kRead (sealed)
  // and kReadWrite (unsealed).
  void UnsealReadOnlySegment();
  void SealReadOnlySegment();

  // Extends the given space with the given segment.
  void Extend(Space* space, Segment segment, FreelistHead freelist);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_ENTITY_TABLE_H_
