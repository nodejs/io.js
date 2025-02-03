// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MUTABLE_PAGE_METADATA_H_
#define V8_HEAP_MUTABLE_PAGE_METADATA_H_

#include <atomic>

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/heap/base/active-system-pages.h"
#include "src/heap/list.h"
#include "src/heap/marking.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/slot-set.h"

namespace v8 {
namespace internal {

class FreeListCategory;
class Space;

// MutablePageMetadata represents a memory region owned by a specific space.
// It is divided into the header and the body. Chunk start is always
// 1MB aligned. Start of the body is aligned so it can accommodate
// any heap object.
class MutablePageMetadata : public MemoryChunkMetadata {
 public:
  // |kDone|: The page state when sweeping is complete or sweeping must not be
  //   performed on that page. Sweeper threads that are done with their work
  //   will set this value and not touch the page anymore.
  // |kPendingSweeping|: This page is ready for parallel sweeping.
  // |kPendingIteration|: This page is ready for parallel promoted page
  // iteration. |kInProgress|: This page is currently swept by a sweeper thread.
  enum class ConcurrentSweepingState : intptr_t {
    kDone,
    kPendingSweeping,
    kPendingIteration,
    kInProgress,
  };

  static const size_t kHeaderSize = MemoryChunkLayout::kMemoryChunkHeaderSize;

  static const intptr_t kOldToNewSlotSetOffset =
      MemoryChunkLayout::kSlotSetOffset;

  // Page size in bytes.  This must be a multiple of the OS page size.
  static const int kPageSize = kRegularPageSize;

  MutablePageMetadata(Heap* heap, BaseSpace* space, size_t size,
                      Address area_start, Address area_end,
                      VirtualMemory reservation, PageSize page_size);

  MemoryChunk::MainThreadFlags InitialFlags(Executability executable) const;

  // Only works if the pointer is in the first kPageSize of the MemoryChunk.
  V8_INLINE static MutablePageMetadata* FromAddress(Address a);

  // Only works if the object is in the first kPageSize of the MemoryChunk.
  V8_INLINE static MutablePageMetadata* FromHeapObject(Tagged<HeapObject> o);

  static MutablePageMetadata* cast(MemoryChunkMetadata* metadata) {
    SLOW_DCHECK(!metadata || !metadata->Chunk()->InReadOnlySpace());
    return static_cast<MutablePageMetadata*>(metadata);
  }

  static const MutablePageMetadata* cast(const MemoryChunkMetadata* metadata) {
    SLOW_DCHECK(!metadata->Chunk()->InReadOnlySpace());
    return static_cast<const MutablePageMetadata*>(metadata);
  }

  size_t buckets() const { return SlotSet::BucketsForSize(size()); }

  V8_INLINE void SetOldGenerationPageFlags(MarkingMode marking_mode);
  void SetYoungGenerationPageFlags(MarkingMode marking_mode) {
    return Chunk()->SetYoungGenerationPageFlags(marking_mode);
  }

  static inline void MoveExternalBackingStoreBytes(
      ExternalBackingStoreType type, MutablePageMetadata* from,
      MutablePageMetadata* to, size_t amount);

  void DiscardUnusedMemory(Address addr, size_t size);

  base::Mutex* mutex() const { return mutex_; }
  base::SharedMutex* shared_mutex() const { return shared_mutex_; }

  void set_concurrent_sweeping_state(ConcurrentSweepingState state) {
    concurrent_sweeping_ = state;
  }

  ConcurrentSweepingState concurrent_sweeping_state() {
    return static_cast<ConcurrentSweepingState>(concurrent_sweeping_.load());
  }

  bool SweepingDone() const {
    return concurrent_sweeping_ == ConcurrentSweepingState::kDone;
  }

  template <RememberedSetType type, AccessMode access_mode = AccessMode::ATOMIC>
  SlotSet* slot_set() {
    if constexpr (access_mode == AccessMode::ATOMIC)
      return base::AsAtomicPointer::Acquire_Load(&slot_set_[type]);
    return slot_set_[type];
  }

  template <RememberedSetType type, AccessMode access_mode = AccessMode::ATOMIC>
  const SlotSet* slot_set() const {
    return const_cast<MutablePageMetadata*>(this)
        ->slot_set<type, access_mode>();
  }

  template <RememberedSetType type, AccessMode access_mode = AccessMode::ATOMIC>
  TypedSlotSet* typed_slot_set() {
    if constexpr (access_mode == AccessMode::ATOMIC)
      return base::AsAtomicPointer::Acquire_Load(&typed_slot_set_[type]);
    return typed_slot_set_[type];
  }

  template <RememberedSetType type, AccessMode access_mode = AccessMode::ATOMIC>
  const TypedSlotSet* typed_slot_set() const {
    return const_cast<MutablePageMetadata*>(this)
        ->typed_slot_set<type, access_mode>();
  }

  template <RememberedSetType type>
  bool ContainsSlots() const {
    return slot_set<type>() != nullptr || typed_slot_set<type>() != nullptr;
  }
  bool ContainsAnySlots() const;

  V8_EXPORT_PRIVATE SlotSet* AllocateSlotSet(RememberedSetType type);
  // Not safe to be called concurrently.
  void ReleaseSlotSet(RememberedSetType type);
  TypedSlotSet* AllocateTypedSlotSet(RememberedSetType type);
  // Not safe to be called concurrently.
  void ReleaseTypedSlotSet(RememberedSetType type);

  template <RememberedSetType type>
  SlotSet* ExtractSlotSet() {
    SlotSet* slot_set = slot_set_[type];
    // Conditionally reset to nullptr (instead of e.g. using std::exchange) to
    // avoid data races when transitioning from nullptr to nullptr.
    if (slot_set) {
      slot_set_[type] = nullptr;
    }
    return slot_set;
  }

  template <RememberedSetType type>
  TypedSlotSet* ExtractTypedSlotSet() {
    TypedSlotSet* typed_slot_set = typed_slot_set_[type];
    // Conditionally reset to nullptr (instead of e.g. using std::exchange) to
    // avoid data races when transitioning from nullptr to nullptr.
    if (typed_slot_set) {
      typed_slot_set_[type] = nullptr;
    }
    return typed_slot_set;
  }

  int ComputeFreeListsLength();

  // Approximate amount of physical memory committed for this chunk.
  V8_EXPORT_PRIVATE size_t CommittedPhysicalMemory() const;

  class ProgressBar& ProgressBar() { return progress_bar_; }
  const class ProgressBar& ProgressBar() const { return progress_bar_; }

  inline void IncrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                 size_t amount);

  inline void DecrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                 size_t amount);

  size_t ExternalBackingStoreBytes(ExternalBackingStoreType type) const {
    return external_backing_store_bytes_[static_cast<int>(type)];
  }

  Space* owner() const {
    return reinterpret_cast<Space*>(MemoryChunkMetadata::owner());
  }

  // Gets the chunk's allocation space, potentially dealing with a null owner_
  // (like read-only chunks have).
  inline AllocationSpace owner_identity() const;

  static PageAllocator::Permission GetCodeModificationPermission() {
    return v8_flags.jitless ? PageAllocator::kReadWrite
                            : PageAllocator::kReadWriteExecute;
  }

  heap::ListNode<MutablePageMetadata>& list_node() { return list_node_; }
  const heap::ListNode<MutablePageMetadata>& list_node() const {
    return list_node_;
  }

  PossiblyEmptyBuckets* possibly_empty_buckets() {
    return &possibly_empty_buckets_;
  }

  // Release memory allocated by the chunk, except that which is needed by
  // read-only space chunks.
  void ReleaseAllocatedMemoryNeededForWritableChunk();

  void IncreaseAllocatedLabSize(size_t bytes) { allocated_lab_size_ += bytes; }
  void DecreaseAllocatedLabSize(size_t bytes) {
    DCHECK_GE(allocated_lab_size_, bytes);
    allocated_lab_size_ -= bytes;
  }
  size_t AllocatedLabSize() const { return allocated_lab_size_; }

  void IncrementAgeInNewSpace() { age_in_new_space_++; }
  void ResetAgeInNewSpace() { age_in_new_space_ = 0; }
  size_t AgeInNewSpace() const { return age_in_new_space_; }

  void ResetAllocationStatistics() {
    MemoryChunkMetadata::ResetAllocationStatistics();
    allocated_lab_size_ = 0;
  }

  void ResetAllocationStatisticsForPromotedPage() {
    DCHECK_NE(0, live_bytes());
    allocated_bytes_ = live_bytes();
    wasted_memory_ = area_size() - allocated_bytes_;
    allocated_lab_size_ = 0;
  }

  MarkingBitmap* marking_bitmap() {
    DCHECK(!Chunk()->InReadOnlySpace());
    return &marking_bitmap_;
  }

  const MarkingBitmap* marking_bitmap() const {
    DCHECK(!Chunk()->InReadOnlySpace());
    return &marking_bitmap_;
  }

  size_t live_bytes() const {
    return live_byte_count_.load(std::memory_order_relaxed);
  }

  void SetLiveBytes(size_t value) {
    DCHECK_IMPLIES(V8_COMPRESS_POINTERS_8GB_BOOL,
                   ::IsAligned(value, kObjectAlignment8GbHeap));
    live_byte_count_.store(value, std::memory_order_relaxed);
  }

  void IncrementLiveBytesAtomically(intptr_t diff) {
    DCHECK_IMPLIES(V8_COMPRESS_POINTERS_8GB_BOOL,
                   ::IsAligned(diff, kObjectAlignment8GbHeap));
    live_byte_count_.fetch_add(diff, std::memory_order_relaxed);
  }

  void ClearLiveness();

 protected:
  // Release all memory allocated by the chunk. Should be called when memory
  // chunk is about to be freed.
  void ReleaseAllAllocatedMemory();

#ifdef DEBUG
  static void ValidateOffsets(MutablePageMetadata* chunk);
#endif

  template <RememberedSetType type, AccessMode access_mode = AccessMode::ATOMIC>
  void set_slot_set(SlotSet* slot_set) {
    if (access_mode == AccessMode::ATOMIC) {
      base::AsAtomicPointer::Release_Store(&slot_set_[type], slot_set);
      return;
    }
    slot_set_[type] = slot_set;
  }

  template <RememberedSetType type, AccessMode access_mode = AccessMode::ATOMIC>
  void set_typed_slot_set(TypedSlotSet* typed_slot_set) {
    if (access_mode == AccessMode::ATOMIC) {
      base::AsAtomicPointer::Release_Store(&typed_slot_set_[type],
                                           typed_slot_set);
      return;
    }
    typed_slot_set_[type] = typed_slot_set;
  }

  // A single slot set for small pages (of size kPageSize) or an array of slot
  // set for large pages. In the latter case the number of entries in the array
  // is ceil(size() / kPageSize).
  SlotSet* slot_set_[NUMBER_OF_REMEMBERED_SET_TYPES] = {nullptr};
  // A single slot set for small pages (of size kPageSize) or an array of slot
  // set for large pages. In the latter case the number of entries in the array
  // is ceil(size() / kPageSize).
  TypedSlotSet* typed_slot_set_[NUMBER_OF_REMEMBERED_SET_TYPES] = {nullptr};

  // Used by the marker to keep track of the scanning progress in large objects
  // that have a progress bar and are scanned in increments.
  class ProgressBar progress_bar_;

  // Count of bytes marked black on page. With sticky mark-bits, the counter
  // represents the size of the old objects allocated on the page. This is
  // handy, since this counter is then used when starting sweeping to set the
  // approximate allocated size on the space (before it gets refined due to
  // right/left-trimming or slack tracking).
  std::atomic<intptr_t> live_byte_count_{0};

  base::Mutex* mutex_;
  base::SharedMutex* shared_mutex_;
  base::Mutex* page_protection_change_mutex_;

  std::atomic<ConcurrentSweepingState> concurrent_sweeping_{
      ConcurrentSweepingState::kDone};

  // Tracks off-heap memory used by this memory chunk.
  std::atomic<size_t> external_backing_store_bytes_[static_cast<int>(
      ExternalBackingStoreType::kNumValues)] = {0};

  heap::ListNode<MutablePageMetadata> list_node_;

  FreeListCategory** categories_ = nullptr;

  PossiblyEmptyBuckets possibly_empty_buckets_;

  ActiveSystemPages* active_system_pages_;

  // Counts overall allocated LAB size on the page since the last GC. Used
  // only for new space pages.
  size_t allocated_lab_size_ = 0;

  // Counts the number of young gen GCs that a page survived in new space. This
  // counter is reset to 0 whenever the page is empty.
  size_t age_in_new_space_ = 0;

  MarkingBitmap marking_bitmap_;

 private:
  friend class ConcurrentMarkingState;
  friend class MarkingState;
  friend class AtomicMarkingState;
  friend class NonAtomicMarkingState;
  friend class MemoryAllocator;
  friend class MemoryChunkValidator;
  friend class PagedSpace;
  template <RememberedSetType>
  friend class RememberedSet;
  friend class YoungGenerationMarkingState;
};

}  // namespace internal

namespace base {
// Define special hash function for chunk pointers, to be used with std data
// structures, e.g. std::unordered_set<MutablePageMetadata*,
// base::hash<MutablePageMetadata*>
template <>
struct hash<i::MutablePageMetadata*> : hash<i::MemoryChunkMetadata*> {};
template <>
struct hash<const i::MutablePageMetadata*>
    : hash<const i::MemoryChunkMetadata*> {};
}  // namespace base

}  // namespace v8

#endif  // V8_HEAP_MUTABLE_PAGE_METADATA_H_
