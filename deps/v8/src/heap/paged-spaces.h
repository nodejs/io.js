// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PAGED_SPACES_H_
#define V8_HEAP_PAGED_SPACES_H_

#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "src/base/bounds.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/allocation-observer.h"
#include "src/heap/allocation-stats.h"
#include "src/heap/heap-verifier.h"
#include "src/heap/heap.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

class CompactionSpace;
class Heap;
class HeapObject;
class Isolate;
class ObjectVisitor;
class PagedSpaceBase;
class Sweeper;

class HeapObjectRange final {
 public:
  class iterator final {
   public:
    using value_type = Tagged<HeapObject>;
    using pointer = const value_type*;
    using reference = const value_type&;
    using iterator_category = std::forward_iterator_tag;

    inline iterator();
    explicit inline iterator(const PageMetadata* page);

    inline iterator& operator++();
    inline iterator operator++(int);

    bool operator==(iterator other) const {
      return cur_addr_ == other.cur_addr_;
    }
    bool operator!=(iterator other) const { return !(*this == other); }

    value_type operator*() { return HeapObject::FromAddress(cur_addr_); }

   private:
    inline void AdvanceToNextObject();

    PtrComprCageBase cage_base() const { return cage_base_; }

    PtrComprCageBase cage_base_;
    Address cur_addr_ = kNullAddress;  // Current iteration point.
    int cur_size_ = 0;
    Address cur_end_ = kNullAddress;  // End iteration point.
  };

  explicit HeapObjectRange(const PageMetadata* page) : page_(page) {}

  inline iterator begin();
  inline iterator end();

 private:
  const PageMetadata* const page_;
};

// Heap object iterator in paged spaces.
//
// A PagedSpaceObjectIterator iterates objects from the bottom of the given
// space to its top or from the bottom of the given page to its top.
//
// If objects are allocated in the page during iteration the iterator may
// or may not iterate over those objects.  The caller must create a new
// iterator in order to be sure to visit these new objects.
class V8_EXPORT_PRIVATE PagedSpaceObjectIterator : public ObjectIterator {
 public:
  // Creates a new object iterator in a given space.
  PagedSpaceObjectIterator(Heap* heap, const PagedSpaceBase* space);

  // Advance to the next object, skipping free spaces and other fillers and
  // skipping the special garbage section of which there is one per space.
  // Returns nullptr when the iteration has ended.
  inline Tagged<HeapObject> Next() override;

 private:
  // Slow path of next(), goes into the next page.  Returns false if the
  // iteration has ended.
  bool AdvanceToNextPage();

  HeapObjectRange::iterator cur_;
  HeapObjectRange::iterator end_;
  const PagedSpaceBase* const space_;
  ConstPageRange page_range_;
  ConstPageRange::iterator current_page_;
};

class V8_EXPORT_PRIVATE PagedSpaceBase
    : NON_EXPORTED_BASE(public SpaceWithLinearArea) {
 public:
  using iterator = PageIterator;
  using const_iterator = ConstPageIterator;

  static const size_t kCompactionMemoryWanted = 500 * KB;

  PagedSpaceBase(Heap* heap, AllocationSpace id, Executability executable,
                 std::unique_ptr<FreeList> free_list,
                 CompactionSpaceKind compaction_space_kind);

  ~PagedSpaceBase() override { TearDown(); }

  // Checks whether an object/address is in this space.
  inline bool Contains(Address a) const;
  inline bool Contains(Tagged<Object> o) const;
  bool ContainsSlow(Address addr) const;

  // Does the space need executable memory?
  Executability executable() const { return executable_; }

  // Current capacity without growing (Size() + Available()).
  size_t Capacity() const { return accounting_stats_.Capacity(); }

  // Approximate amount of physical memory committed for this space.
  size_t CommittedPhysicalMemory() const override;

#if DEBUG
  void VerifyCommittedPhysicalMemory() const;
#endif  // DEBUG

  void IncrementCommittedPhysicalMemory(size_t increment_value);
  void DecrementCommittedPhysicalMemory(size_t decrement_value);

  // Sets the capacity, the available space and the wasted space to zero.
  // The stats are rebuilt during sweeping by adding each page to the
  // capacity and the size when it is encountered.  As free spaces are
  // discovered during the sweeping they are subtracted from the size and added
  // to the available and wasted totals. The free list is cleared as well.
  void ClearAllocatorState() {
    accounting_stats_.ClearSize();
    free_list_->Reset();
  }

  // Available bytes without growing.  These are the bytes on the free list.
  // The bytes in the linear allocation area are not included in this total
  // because updating the stats would slow down allocation.  New pages are
  // immediately added to the free list so they show up here.
  size_t Available() const override;

  // Allocated bytes in this space.  Garbage bytes that were not found due to
  // concurrent sweeping are counted as being allocated!  The bytes in the
  // current linear allocation area (between top and limit) are also counted
  // here.
  size_t Size() const override { return accounting_stats_.Size(); }

  // Wasted bytes in this space.  These are just the bytes that were thrown away
  // due to being too small to use for allocation.
  size_t Waste() const;

  // Allocate the requested number of bytes in the space from a background
  // thread.
  V8_WARN_UNUSED_RESULT std::optional<std::pair<Address, size_t>>
  RawAllocateBackground(LocalHeap* local_heap, size_t min_size_in_bytes,
                        size_t max_size_in_bytes, AllocationOrigin origin);

  // Free a block of memory. During sweeping, we don't update the accounting
  // stats and don't link the free list category.
  V8_INLINE size_t Free(Address start, size_t size_in_bytes);
  V8_INLINE size_t FreeDuringSweep(Address start, size_t size_in_bytes);

  void ResetFreeList();

  void DecreaseAllocatedBytes(size_t bytes, PageMetadata* page) {
    accounting_stats_.DecreaseAllocatedBytes(bytes, page);
  }
  void IncreaseAllocatedBytes(size_t bytes, PageMetadata* page) {
    accounting_stats_.IncreaseAllocatedBytes(bytes, page);
  }
  void DecreaseCapacity(size_t bytes) {
    accounting_stats_.DecreaseCapacity(bytes);
  }
  void IncreaseCapacity(size_t bytes) {
    accounting_stats_.IncreaseCapacity(bytes);
  }

  PageMetadata* InitializePage(MutablePageMetadata* chunk) override;

  virtual void ReleasePage(PageMetadata* page);

  // Adds the page to this space and returns the number of bytes added to the
  // free list of the space.
  virtual size_t AddPage(PageMetadata* page);
  virtual void RemovePage(PageMetadata* page);
  // Remove a page if it has at least |size_in_bytes| bytes available that can
  // be used for allocation.
  PageMetadata* RemovePageSafe(int size_in_bytes);

#ifdef VERIFY_HEAP
  // Verify integrity of this space.
  void Verify(Isolate* isolate,
              SpaceVerificationVisitor* visitor) const override;

  void VerifyLiveBytes() const;
#endif

#ifdef DEBUG
  void VerifyCountersAfterSweeping(Heap* heap) const;
  void VerifyCountersBeforeConcurrentSweeping() const;
  // Print meta info and objects in this space.
  void Print() override;

  // Report code object related statistics
  static void ReportCodeStatistics(Isolate* isolate);
  static void ResetCodeStatistics(Isolate* isolate);
#endif

  bool CanExpand(size_t size) const;

  // Returns the number of total pages in this space.
  int CountTotalPages() const;

  // Return size of allocatable area on a page in this space.
  inline int AreaSize() const { return static_cast<int>(area_size_); }

  bool is_compaction_space() const {
    return compaction_space_kind_ != CompactionSpaceKind::kNone;
  }

  CompactionSpaceKind compaction_space_kind() const {
    return compaction_space_kind_;
  }

  // Merges {other} into the current space. Note that this modifies {other},
  // e.g., removes its bump pointer area and resets statistics.
  void MergeCompactionSpace(CompactionSpace* other);

  // Refills the free list from the corresponding free list filled by the
  // sweeper.
  virtual void RefillFreeList();

  base::Mutex* mutex() { return &space_mutex_; }

  void UnlinkFreeListCategories(PageMetadata* page);
  size_t RelinkFreeListCategories(PageMetadata* page);

  PageMetadata* first_page() override {
    return reinterpret_cast<PageMetadata*>(memory_chunk_list_.front());
  }
  const PageMetadata* first_page() const override {
    return reinterpret_cast<const PageMetadata*>(memory_chunk_list_.front());
  }

  PageMetadata* last_page() override {
    return reinterpret_cast<PageMetadata*>(memory_chunk_list_.back());
  }
  const PageMetadata* last_page() const override {
    return reinterpret_cast<const PageMetadata*>(memory_chunk_list_.back());
  }

  iterator begin() { return iterator(first_page()); }
  iterator end() { return iterator(nullptr); }

  const_iterator begin() const { return const_iterator(first_page()); }
  const_iterator end() const { return const_iterator(nullptr); }

  // Shrink immortal immovable pages of the space to be exactly the size needed
  // using the high water mark.
  void ShrinkImmortalImmovablePages();

  size_t ShrinkPageToHighWaterMark(PageMetadata* page);

  std::unique_ptr<ObjectIterator> GetObjectIterator(Heap* heap) override;

  void AddRangeToActiveSystemPages(PageMetadata* page, Address start,
                                   Address end);
  void ReduceActiveSystemPages(PageMetadata* page,
                               ActiveSystemPages active_system_pages);

  // Expands the space by a single page and returns true on success.
  bool TryExpand(LocalHeap* local_heap, AllocationOrigin origin);

  void RefineAllocatedBytesAfterSweeping(PageMetadata* page);
  virtual void AdjustDifferenceInAllocatedBytes(size_t diff) {}

 protected:
  // PagedSpaces that should be included in snapshots have different, i.e.,
  // smaller, initial pages.
  virtual bool snapshotable() const { return true; }

  bool HasPages() const { return first_page() != nullptr; }

  // Cleans up the space, frees all pages in this space except those belonging
  // to the initial chunk, uncommits addresses in the initial chunk.
  void TearDown();

  // Spaces can use this method to get notified about pages added to it.
  virtual void NotifyNewPage(PageMetadata* page) {}

  size_t committed_physical_memory() const {
    return committed_physical_memory_.load(std::memory_order_relaxed);
  }

  void ReleasePageImpl(PageMetadata* page, MemoryAllocator::FreeMode free_mode);

  void AddPageImpl(PageMetadata* page);

  Executability executable_;

  CompactionSpaceKind compaction_space_kind_;

  size_t area_size_;

  // Accounting information for this space.
  AllocationStats accounting_stats_;

  // Mutex guarding any concurrent access to the space.
  mutable base::Mutex space_mutex_;

  std::atomic<size_t> committed_physical_memory_{0};

  // Used for tracking bytes allocated since last gc in new space.
  size_t size_at_last_gc_ = 0;

 private:
  template <bool during_sweep>
  V8_INLINE size_t FreeInternal(Address start, size_t size_in_bytes);

  class ConcurrentAllocationMutex {
   public:
    explicit ConcurrentAllocationMutex(const PagedSpaceBase* space) {
      if (space->SupportsConcurrentAllocation()) {
        guard_.emplace(&space->space_mutex_);
      }
    }

    std::optional<base::MutexGuard> guard_;
  };

  bool SupportsConcurrentAllocation() const {
    return !is_compaction_space() && (identity() != NEW_SPACE);
  }

  friend class IncrementalMarking;
  friend class MarkCompactCollector;
  friend class PagedSpaceAllocatorPolicy;

  // Used in cctest.
  friend class heap::HeapTester;
};

class V8_EXPORT_PRIVATE PagedSpace : public PagedSpaceBase {
 public:
  PagedSpace(Heap* heap, AllocationSpace id, Executability executable,
             std::unique_ptr<FreeList> free_list,
             CompactionSpaceKind compaction_space_kind)
      : PagedSpaceBase(heap, id, executable, std::move(free_list),
                       compaction_space_kind) {}

  AllocatorPolicy* CreateAllocatorPolicy(MainAllocator* allocator) final;
};

// -----------------------------------------------------------------------------
// Compaction space that is used temporarily during compaction.

class V8_EXPORT_PRIVATE CompactionSpace final : public PagedSpace {
 public:
  CompactionSpace(Heap* heap, AllocationSpace id, Executability executable,
                  CompactionSpaceKind compaction_space_kind)
      : PagedSpace(heap, id, executable, FreeList::CreateFreeList(),
                   compaction_space_kind) {
    DCHECK(is_compaction_space());
  }

  const std::vector<PageMetadata*>& GetNewPages() { return new_pages_; }

  void RefillFreeList() final;

 protected:
  void NotifyNewPage(PageMetadata* page) final;

  // The space is temporary and not included in any snapshots.
  bool snapshotable() const final { return false; }
  // Pages that were allocated in this local space and need to be merged
  // to the main space.
  std::vector<PageMetadata*> new_pages_;
};

// A collection of |CompactionSpace|s used by a single compaction task.
class CompactionSpaceCollection : public Malloced {
 public:
  explicit CompactionSpaceCollection(Heap* heap,
                                     CompactionSpaceKind compaction_space_kind);

  CompactionSpace* Get(AllocationSpace space) {
    switch (space) {
      case OLD_SPACE:
        return &old_space_;
      case CODE_SPACE:
        return &code_space_;
      case SHARED_SPACE:
        DCHECK(shared_space_);
        return &*shared_space_;
      case TRUSTED_SPACE:
        return &trusted_space_;
      default:
        UNREACHABLE();
    }
    UNREACHABLE();
  }

 private:
  CompactionSpace old_space_;
  CompactionSpace code_space_;
  std::optional<CompactionSpace> shared_space_;
  CompactionSpace trusted_space_;
};

// -----------------------------------------------------------------------------
// Old generation regular object space.

class V8_EXPORT_PRIVATE OldSpace : public PagedSpace {
 public:
  // Creates an old space object. The constructor does not allocate pages
  // from OS.
  explicit OldSpace(Heap* heap)
      : PagedSpace(heap, OLD_SPACE, NOT_EXECUTABLE, FreeList::CreateFreeList(),
                   CompactionSpaceKind::kNone) {}

  void AddPromotedPage(PageMetadata* page);

  void ReleasePage(PageMetadata* page) override;

  size_t ExternalBackingStoreBytes(ExternalBackingStoreType type) const final {
    if (type == ExternalBackingStoreType::kArrayBuffer)
      return heap()->OldArrayBufferBytes();
    return external_backing_store_bytes_[static_cast<int>(type)];
  }
};

// -----------------------------------------------------------------------------
// StickySpace is a paged space that contain mixed young and old objects. Note
// that its identity type is OLD_SPACE.

class V8_EXPORT_PRIVATE StickySpace final : public OldSpace {
 public:
  using OldSpace::OldSpace;

  static StickySpace* From(OldSpace* space) {
    DCHECK(v8_flags.sticky_mark_bits);
    return static_cast<StickySpace*>(space);
  }

  size_t young_objects_size() const {
    DCHECK_GE(Size(), allocated_old_size_);
    return Size() - allocated_old_size_;
  }

  size_t old_objects_size() const {
    DCHECK_GE(Size(), allocated_old_size_);
    return allocated_old_size_;
  }

  void set_old_objects_size(size_t allocated_old_size) {
    allocated_old_size_ = allocated_old_size;
  }

  void NotifyBlackAreaCreated(size_t size) override {
    DCHECK_LE(size, Capacity());
    allocated_old_size_ += size;
  }

  void NotifyBlackAreaDestroyed(size_t size) override {
    DCHECK_LE(size, Capacity());
    allocated_old_size_ -= size;
  }

 private:
  void AdjustDifferenceInAllocatedBytes(size_t) override;

  // TODO(333906585): Consider tracking the young bytes instead.
  size_t allocated_old_size_ = 0;
};

// -----------------------------------------------------------------------------
// Old generation code object space.

class CodeSpace final : public PagedSpace {
 public:
  // Creates a code space object. The constructor does not allocate pages from
  // OS.
  explicit CodeSpace(Heap* heap)
      : PagedSpace(heap, CODE_SPACE, EXECUTABLE, FreeList::CreateFreeList(),
                   CompactionSpaceKind::kNone) {}
};

// -----------------------------------------------------------------------------
// Shared space regular object space.

class SharedSpace final : public PagedSpace {
 public:
  // Creates a shared space object. The constructor does not allocate pages from
  // OS.
  explicit SharedSpace(Heap* heap)
      : PagedSpace(heap, SHARED_SPACE, NOT_EXECUTABLE,
                   FreeList::CreateFreeList(), CompactionSpaceKind::kNone) {}

  void ReleasePage(PageMetadata* page) override;

  size_t ExternalBackingStoreBytes(ExternalBackingStoreType type) const final {
    if (type == ExternalBackingStoreType::kArrayBuffer) return 0;
    DCHECK_EQ(type, ExternalBackingStoreType::kExternalString);
    return external_backing_store_bytes_[static_cast<int>(type)];
  }
};

// -----------------------------------------------------------------------------
// Trusted space.
// Essentially another old space that, when the sandbox is enabled, will be
// located outside of the sandbox. As such an attacker cannot corrupt objects
// located in this space and therefore these objects can be considered trusted.

class TrustedSpace final : public PagedSpace {
 public:
  // Creates a trusted space object. The constructor does not allocate pages
  // from OS.
  explicit TrustedSpace(Heap* heap)
      : PagedSpace(heap, TRUSTED_SPACE, NOT_EXECUTABLE,
                   FreeList::CreateFreeList(), CompactionSpaceKind::kNone) {}

  size_t ExternalBackingStoreBytes(ExternalBackingStoreType type) const final {
    if (type == ExternalBackingStoreType::kArrayBuffer) return 0;
    DCHECK_EQ(type, ExternalBackingStoreType::kExternalString);
    return external_backing_store_bytes_[static_cast<int>(type)];
  }
};

class SharedTrustedSpace final : public PagedSpace {
 public:
  // Creates a trusted space object. The constructor does not allocate pages
  // from OS.
  explicit SharedTrustedSpace(Heap* heap)
      : PagedSpace(heap, SHARED_TRUSTED_SPACE, NOT_EXECUTABLE,
                   FreeList::CreateFreeList(), CompactionSpaceKind::kNone) {}

  size_t ExternalBackingStoreBytes(ExternalBackingStoreType type) const final {
    if (type == ExternalBackingStoreType::kArrayBuffer) return 0;
    DCHECK_EQ(type, ExternalBackingStoreType::kExternalString);
    return external_backing_store_bytes_[static_cast<int>(type)];
  }
};

// Iterates over the chunks (pages and large object pages) that can contain
// pointers to new space or to evacuation candidates.
class OldGenerationMemoryChunkIterator {
 public:
  inline explicit OldGenerationMemoryChunkIterator(Heap* heap);

  // Return nullptr when the iterator is done.
  inline MutablePageMetadata* next();

  // Applies `callback` to all `MutablePageMetadata` returned by the iterator.
  template <typename Callback>
  static void ForAll(Heap* heap, Callback callback) {
    OldGenerationMemoryChunkIterator it(heap);
    while (MutablePageMetadata* chunk = it.next()) {
      callback(chunk);
    }
  }

 private:
  enum State {
    kOldSpace,
    kCodeSpace,
    kLargeObjectSpace,
    kCodeLargeObjectSpace,
    kTrustedSpace,
    kTrustedLargeObjectSpace,
    kFinished
  };
  Heap* const heap_;
  State state_;
  // The current type of {iterator_} depends on {state_}.
  std::variant<PageIterator, LargePageIterator> iterator_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PAGED_SPACES_H_
