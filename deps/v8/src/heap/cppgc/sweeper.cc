// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/sweeper.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "include/cppgc/platform.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/heap/cppgc/free-list.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-config.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap-space.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/memory.h"
#include "src/heap/cppgc/object-poisoner.h"
#include "src/heap/cppgc/object-start-bitmap.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/raw-heap.h"
#include "src/heap/cppgc/stats-collector.h"
#include "src/heap/cppgc/task-handle.h"

namespace cppgc::internal {

namespace {

class DeadlineChecker final {
 public:
  explicit DeadlineChecker(v8::base::TimeTicks end) : end_(end) {}

  bool Check() {
    return (++count_ % kInterval == 0) && (end_ < v8::base::TimeTicks::Now());
  }

 private:
  static constexpr size_t kInterval = 4;

  const v8::base::TimeTicks end_;
  size_t count_ = 0;
};

enum class MutatorThreadSweepingMode {
  kOnlyFinalizers,
  kAll,
};

constexpr const char* ToString(MutatorThreadSweepingMode sweeping_mode) {
  switch (sweeping_mode) {
    case MutatorThreadSweepingMode::kAll:
      return "all";
    case MutatorThreadSweepingMode::kOnlyFinalizers:
      return "only-finalizers";
  }
}

enum class StickyBits : uint8_t {
  kDisabled,
  kEnabled,
};

class ObjectStartBitmapVerifier
    : private HeapVisitor<ObjectStartBitmapVerifier> {
  friend class HeapVisitor<ObjectStartBitmapVerifier>;

 public:
  void Verify(RawHeap& heap) {
#if DEBUG
    Traverse(heap);
#endif  // DEBUG
  }
  void Verify(NormalPage& page) {
#if DEBUG
    Traverse(page);
#endif  // DEBUG
  }

 private:
  bool VisitNormalPage(NormalPage& page) {
    // Remember bitmap and reset previous pointer.
    bitmap_ = &page.object_start_bitmap();
    prev_ = nullptr;
    return false;
  }

  bool VisitHeapObjectHeader(HeapObjectHeader& header) {
    if (header.IsLargeObject()) return true;

    auto* raw_header = reinterpret_cast<ConstAddress>(&header);
    CHECK(bitmap_->CheckBit<AccessMode::kAtomic>(raw_header));
    if (prev_) {
      // No other bits in the range [prev_, raw_header) should be set.
      CHECK_EQ(prev_, bitmap_->FindHeader<AccessMode::kAtomic>(raw_header - 1));
    }
    prev_ = &header;
    return true;
  }

  PlatformAwareObjectStartBitmap* bitmap_ = nullptr;
  HeapObjectHeader* prev_ = nullptr;
};

class FreeHandlerBase {
 public:
  virtual ~FreeHandlerBase() = default;
  virtual void FreeFreeList(
      std::vector<FreeList::Block>& unfinalized_free_list) = 0;
};

class DiscardingFreeHandler : public FreeHandlerBase {
 public:
  DiscardingFreeHandler(PageAllocator& page_allocator, FreeList& free_list,
                        BasePage& page)
      : page_allocator_(page_allocator), free_list_(free_list), page_(page) {}

  void Free(FreeList::Block block) {
    const auto unused_range = free_list_.AddReturningUnusedBounds(block);
    const uintptr_t aligned_begin_unused =
        RoundUp(reinterpret_cast<uintptr_t>(unused_range.first),
                page_allocator_.CommitPageSize());
    const uintptr_t aligned_end_unused =
        RoundDown(reinterpret_cast<uintptr_t>(unused_range.second),
                  page_allocator_.CommitPageSize());
    if (aligned_begin_unused < aligned_end_unused) {
      const size_t discarded_size = aligned_end_unused - aligned_begin_unused;
      page_allocator_.DiscardSystemPages(
          reinterpret_cast<void*>(aligned_begin_unused),
          aligned_end_unused - aligned_begin_unused);
      page_.IncrementDiscardedMemory(discarded_size);
      page_.space()
          .raw_heap()
          ->heap()
          ->stats_collector()
          ->IncrementDiscardedMemory(discarded_size);
    }
  }

  void FreeFreeList(std::vector<FreeList::Block>& unfinalized_free_list) final {
    for (auto entry : unfinalized_free_list) {
      Free(std::move(entry));
    }
  }

 private:
  PageAllocator& page_allocator_;
  FreeList& free_list_;
  BasePage& page_;
};

class RegularFreeHandler : public FreeHandlerBase {
 public:
  RegularFreeHandler(PageAllocator& page_allocator, FreeList& free_list,
                     BasePage& page)
      : free_list_(free_list) {}

  void Free(FreeList::Block block) { free_list_.Add(std::move(block)); }

  void FreeFreeList(std::vector<FreeList::Block>& unfinalized_free_list) final {
    for (auto entry : unfinalized_free_list) {
      Free(std::move(entry));
    }
  }

 private:
  FreeList& free_list_;
};

template <typename T>
class ThreadSafeStack {
 public:
  ThreadSafeStack() = default;

  void Push(T t) {
    v8::base::LockGuard<v8::base::Mutex> lock(&mutex_);
    vector_.push_back(std::move(t));
    is_empty_.store(false, std::memory_order_relaxed);
  }

  std::optional<T> Pop() {
    v8::base::LockGuard<v8::base::Mutex> lock(&mutex_);
    if (vector_.empty()) {
      is_empty_.store(true, std::memory_order_relaxed);
      return std::nullopt;
    }
    T top = std::move(vector_.back());
    vector_.pop_back();
    // std::move is redundant but is needed to avoid the bug in gcc-7.
    return std::move(top);
  }

  template <typename It>
  void Insert(It begin, It end) {
    v8::base::LockGuard<v8::base::Mutex> lock(&mutex_);
    vector_.insert(vector_.end(), begin, end);
    is_empty_.store(false, std::memory_order_relaxed);
  }

  bool IsEmpty() const { return is_empty_.load(std::memory_order_relaxed); }

 private:
  std::vector<T> vector_;
  mutable v8::base::Mutex mutex_;
  std::atomic<bool> is_empty_{true};
};

struct SweepingState {
  struct SweptPageState {
    BasePage* page = nullptr;
#if defined(CPPGC_CAGED_HEAP)
    // The list of unfinalized objects may be extremely big. To save on space,
    // if cage is enabled, the list of unfinalized objects is stored inlined in
    // HeapObjectHeader.
    HeapObjectHeader* unfinalized_objects_head = nullptr;
#else   // !defined(CPPGC_CAGED_HEAP)
    std::vector<HeapObjectHeader*> unfinalized_objects;
#endif  // !defined(CPPGC_CAGED_HEAP)
    FreeList cached_free_list;
    std::vector<FreeList::Block> unfinalized_free_list;
    bool is_empty = false;
    size_t largest_new_free_list_entry = 0;
  };

  ThreadSafeStack<BasePage*> unswept_pages;
  ThreadSafeStack<SweptPageState> swept_unfinalized_pages;
};

using SpaceStates = std::vector<SweepingState>;

void StickyUnmark(HeapObjectHeader* header, StickyBits sticky_bits) {
#if defined(CPPGC_YOUNG_GENERATION)
  // Young generation in Oilpan uses sticky mark bits.
  if (sticky_bits == StickyBits::kDisabled)
    header->Unmark<AccessMode::kAtomic>();
#else   // !defined(CPPGC_YOUNG_GENERATION)
  header->Unmark<AccessMode::kAtomic>();
#endif  // !defined(CPPGC_YOUNG_GENERATION)
}

class InlinedFinalizationBuilderBase {
 public:
  struct ResultType {
    bool is_empty = false;
    size_t largest_new_free_list_entry = 0;
  };

 protected:
  ResultType result_;
};

// Builder that finalizes objects and adds freelist entries right away.
template <typename FreeHandler>
class InlinedFinalizationBuilder final : public InlinedFinalizationBuilderBase,
                                         public FreeHandler {
 public:
  InlinedFinalizationBuilder(BasePage& page, PageAllocator& page_allocator)
      : FreeHandler(page_allocator,
                    NormalPageSpace::From(page.space()).free_list(), page) {}

  void AddFinalizer(HeapObjectHeader* header, size_t size) {
    header->Finalize();
    SetMemoryInaccessible(header, size);
  }

  void AddFreeListEntry(Address start, size_t size) {
    FreeHandler::Free({start, size});
    result_.largest_new_free_list_entry =
        std::max(result_.largest_new_free_list_entry, size);
  }

  ResultType&& GetResult(bool is_empty) {
    result_.is_empty = is_empty;
    return std::move(result_);
  }
};

// Builder that produces results for deferred processing.
template <typename FreeHandler>
class DeferredFinalizationBuilder final : public FreeHandler {
 public:
  using ResultType = SweepingState::SweptPageState;

  DeferredFinalizationBuilder(BasePage& page, PageAllocator& page_allocator)
      : FreeHandler(page_allocator, result_.cached_free_list, page) {
    result_.page = &page;
  }

  void AddFinalizer(HeapObjectHeader* header, size_t size) {
    if (header->IsFinalizable()) {
#if defined(CPPGC_CAGED_HEAP)
      if (!current_unfinalized_) {
        DCHECK_NULL(result_.unfinalized_objects_head);
        current_unfinalized_ = header;
        result_.unfinalized_objects_head = header;
      } else {
        current_unfinalized_->SetNextUnfinalized(header);
        current_unfinalized_ = header;
      }
#else   // !defined(CPPGC_CAGED_HEAP)
      result_.unfinalized_objects.push_back({header});
#endif  // !defined(CPPGC_CAGED_HEAP)
      found_finalizer_ = true;
    } else {
      SetMemoryInaccessible(header, size);
    }
  }

  void AddFreeListEntry(Address start, size_t size) {
    if (found_finalizer_) {
      result_.unfinalized_free_list.push_back({start, size});
    } else {
      FreeHandler::Free({start, size});
    }
    result_.largest_new_free_list_entry =
        std::max(result_.largest_new_free_list_entry, size);
    found_finalizer_ = false;
  }

  ResultType&& GetResult(bool is_empty) {
    result_.is_empty = is_empty;
    return std::move(result_);
  }

 private:
  ResultType result_;
  HeapObjectHeader* current_unfinalized_ = nullptr;
  bool found_finalizer_ = false;
};

template <typename FinalizationBuilder>
typename FinalizationBuilder::ResultType SweepNormalPage(
    NormalPage* page, PageAllocator& page_allocator, StickyBits sticky_bits) {
  constexpr auto kAtomicAccess = AccessMode::kAtomic;
  FinalizationBuilder builder(*page, page_allocator);

  PlatformAwareObjectStartBitmap& bitmap = page->object_start_bitmap();

  size_t live_bytes = 0;

  Address start_of_gap = page->PayloadStart();

  const auto clear_bit_if_coalesced_entry = [&bitmap,
                                             &start_of_gap](Address address) {
    if (address != start_of_gap) {
      // Clear only if not the first freed entry.
      bitmap.ClearBit<AccessMode::kAtomic>(address);
    } else {
      // Otherwise check that the bit is set.
      DCHECK(bitmap.CheckBit<AccessMode::kAtomic>(address));
    }
  };

  for (Address begin = page->PayloadStart(), end = page->PayloadEnd();
       begin != end;) {
    DCHECK(bitmap.CheckBit<AccessMode::kAtomic>(begin));
    HeapObjectHeader* header = reinterpret_cast<HeapObjectHeader*>(begin);
    const size_t size = header->AllocatedSize();
    // Check if this is a free list entry.
    if (header->IsFree<kAtomicAccess>()) {
      SetMemoryInaccessible(header, std::min(kFreeListEntrySize, size));
      // This prevents memory from being discarded in configurations where
      // `CheckMemoryIsInaccessibleIsNoop()` is false.
      CheckMemoryIsInaccessible(header, size);
      clear_bit_if_coalesced_entry(begin);
      begin += size;
      continue;
    }
    // Check if object is not marked (not reachable).
    if (!header->IsMarked<kAtomicAccess>()) {
      builder.AddFinalizer(header, size);
      clear_bit_if_coalesced_entry(begin);
      begin += size;
      continue;
    }
    // The object is alive.
    const Address header_address = reinterpret_cast<Address>(header);
    if (start_of_gap != header_address) {
      const size_t new_free_list_entry_size =
          static_cast<size_t>(header_address - start_of_gap);
      builder.AddFreeListEntry(start_of_gap, new_free_list_entry_size);
      DCHECK(bitmap.CheckBit<AccessMode::kAtomic>(start_of_gap));
    }
    StickyUnmark(header, sticky_bits);
    begin += size;
    start_of_gap = begin;
    live_bytes += size;
  }

  const bool is_empty = live_bytes == 0;
  CHECK_EQ(is_empty, page->marked_bytes() == 0);
  CHECK_IMPLIES(is_empty, start_of_gap == page->PayloadStart());

  // Empty pages are not added to the free list directly here. The free list is
  // either added later on or the page is destroyed.
  if (!is_empty && start_of_gap != page->PayloadEnd()) {
    builder.AddFreeListEntry(
        start_of_gap, static_cast<size_t>(page->PayloadEnd() - start_of_gap));
    DCHECK(bitmap.CheckBit<AccessMode::kAtomic>(start_of_gap));
  }
  page->SetAllocatedBytesAtLastGC(live_bytes);
  page->ResetMarkedBytes(sticky_bits == StickyBits::kDisabled ? 0 : live_bytes);
  return builder.GetResult(is_empty);
}

constexpr BaseSpace* kSweepWithoutSpaceAssignment = nullptr;

enum class EmptyPageHandling {
  kDestroy,
  kReturn,
};

// SweepFinalizer is responsible for heap/space/page finalization. Finalization
// is defined as a step following concurrent sweeping which:
// - calls finalizers;
// - returns (unmaps) empty pages;
// - merges freelists to the space's freelist.
class SweepFinalizer final {
  using FreeMemoryHandling = SweepingConfig::FreeMemoryHandling;

 public:
  SweepFinalizer(cppgc::Platform* platform, BaseSpace* space,
                 size_t* unused_destroyed_pages,
                 FreeMemoryHandling free_memory_handling,
                 EmptyPageHandling empty_page_handling_type)
      : platform_(platform),
        space_(space),
        unused_destroyed_pages_(unused_destroyed_pages),
        free_memory_handling_(free_memory_handling),
        empty_page_handling_(empty_page_handling_type) {}

  // Finalizes all space states, irrespective of deadlines and sizes.
  void Finalize(SpaceStates& states) {
    for (SweepingState& state : states) {
      Finalize(state);
    }
  }

  void Finalize(SweepingState& state) {
    while (auto page_state = state.swept_unfinalized_pages.Pop()) {
      FinalizePage(&*page_state);
    }
  }

  // Finalizes a given SweepingState with a deadline and size. Only returns
  // true if a single memory block of at least `size` bytes was returned to the
  // free list and false otherwise.
  bool FinalizeWithDeadlineAndSize(SweepingState& state,
                                   v8::base::TimeTicks deadline, size_t size) {
    DeadlineChecker deadline_check(deadline);
    while (auto page_state = state.swept_unfinalized_pages.Pop()) {
      FinalizePage(&*page_state);
      if (size <= largest_new_free_list_entry()) {
        return true;
      }
      if (deadline_check.Check()) {
        break;
      }
    }
    return false;
  }

  // Finalizes a given SweepingState with a deadline. Returns false if the
  // deadline exceeded and true if all pages are finalized.
  bool FinalizeWithDeadline(SweepingState& state,
                            v8::base::TimeTicks deadline) {
    DCHECK(platform_);
    DeadlineChecker deadline_check(deadline);
    while (auto page_state = state.swept_unfinalized_pages.Pop()) {
      FinalizePage(&*page_state);

      if (deadline_check.Check()) {
        return false;
      }
    }

    return true;
  }

  size_t largest_new_free_list_entry() const {
    return largest_new_free_list_entry_;
  }

 private:
  void FinalizePage(SweepingState::SweptPageState* page_state) {
    DCHECK(page_state);
    DCHECK(page_state->page);
    BasePage* page = page_state->page;

    // Call finalizers.
    const auto finalize_header = [](HeapObjectHeader* header) {
      const size_t size = header->AllocatedSize();
      header->Finalize();
      SetMemoryInaccessible(header, size);
    };
#if defined(CPPGC_CAGED_HEAP)
#if defined(CPPGC_POINTER_COMPRESSION)
    const uint64_t cage_base = CageBaseGlobal::Get();
#else
    const uint64_t cage_base = CagedHeapBase::GetBase();
#endif
    HeapObjectHeader* next_unfinalized = nullptr;

    for (auto* unfinalized_header = page_state->unfinalized_objects_head;
         unfinalized_header; unfinalized_header = next_unfinalized) {
      next_unfinalized = unfinalized_header->GetNextUnfinalized(cage_base);
      finalize_header(unfinalized_header);
    }
#else   // !defined(CPPGC_CAGED_HEAP)
    for (HeapObjectHeader* unfinalized_header :
         page_state->unfinalized_objects) {
      finalize_header(unfinalized_header);
    }
#endif  // !defined(CPPGC_CAGED_HEAP)

    // Unmap page if empty.
    if (page_state->is_empty) {
      if (empty_page_handling_ == EmptyPageHandling::kDestroy) {
        if (!page->is_large()) {
          (*unused_destroyed_pages_)++;
        }
        BasePage::Destroy(page, free_memory_handling_);
        return;
      }

      // Otherwise, we currently sweep on allocation. Reinitialize the empty
      // page and return it right away.
      auto* normal_page = NormalPage::From(page);

      // If a space has been assigned to the finalizer, then repurpose empty
      // pages for that space. Otherwise just retain the current space for an
      // empty page.
      if (space_) {
        normal_page->ChangeOwner(*space_);
      }

      page_state->cached_free_list.Clear();
      page_state->cached_free_list.Add(
          {normal_page->PayloadStart(), normal_page->PayloadSize()});

      page_state->unfinalized_free_list.clear();
      page_state->largest_new_free_list_entry = normal_page->PayloadSize();
    }
    // We either swept a non-empty page for which the space should already match
    // or we swept an empty page for which the owner was changed.
    DCHECK_IMPLIES(space_, space_ == &page->space());
    DCHECK(!page->is_large());

    // Merge freelists without finalizers.
    FreeList& space_freelist = NormalPageSpace::From(page->space()).free_list();
    space_freelist.Append(std::move(page_state->cached_free_list));

    // Merge freelist with finalizers.
    if (!page_state->unfinalized_free_list.empty()) {
      std::unique_ptr<FreeHandlerBase> handler =
          (free_memory_handling_ == FreeMemoryHandling::kDiscardWherePossible)
              ? std::unique_ptr<FreeHandlerBase>(new DiscardingFreeHandler(
                    *platform_->GetPageAllocator(), space_freelist, *page))
              : std::unique_ptr<FreeHandlerBase>(new RegularFreeHandler(
                    *platform_->GetPageAllocator(), space_freelist, *page));
      handler->FreeFreeList(page_state->unfinalized_free_list);
    }

    largest_new_free_list_entry_ = std::max(
        page_state->largest_new_free_list_entry, largest_new_free_list_entry_);

    // After the page was fully finalized and freelists have been merged, verify
    // that the bitmap is consistent.
    ObjectStartBitmapVerifier().Verify(static_cast<NormalPage&>(*page));

    // Add the page to the space.
    page->space().AddPage(page);
  }

  cppgc::Platform* platform_;
  BaseSpace* space_;
  size_t* unused_destroyed_pages_;
  size_t largest_new_free_list_entry_ = 0;
  const FreeMemoryHandling free_memory_handling_;
  const EmptyPageHandling empty_page_handling_;
};

class MutatorThreadSweeper final : private HeapVisitor<MutatorThreadSweeper> {
  friend class HeapVisitor<MutatorThreadSweeper>;

  using FreeMemoryHandling = SweepingConfig::FreeMemoryHandling;

 public:
  MutatorThreadSweeper(HeapBase* heap, cppgc::Platform* platform,
                       BaseSpace* space, size_t* unused_destroyed_pages,
                       FreeMemoryHandling free_memory_handling,
                       EmptyPageHandling empty_page_handling)
      : platform_(platform),
        space_(space),
        unused_destroyed_pages_(unused_destroyed_pages),
        free_memory_handling_(free_memory_handling),
        empty_page_handling_(empty_page_handling),
        sticky_bits_(heap->generational_gc_supported()
                         ? StickyBits::kEnabled
                         : StickyBits::kDisabled) {}

  void Sweep(SpaceStates& states) {
    for (SweepingState& state : states) {
      Sweep(state);
    }
  }

  void Sweep(SweepingState& state) {
    while (auto page = state.unswept_pages.Pop()) {
      SweepPage(**page);
    }
  }

  void SweepPage(BasePage& page) { Traverse(page); }

  // Returns true if out of work. This implies that sweeping is done only if
  // `sweeping_mode` is kAll.
  bool FinalizeAndSweepWithDeadline(SweepingState& state,
                                    v8::base::TimeTicks deadline,
                                    MutatorThreadSweepingMode sweeping_mode) {
    DCHECK(platform_);
    // First, prioritize finalization of pages that were swept concurrently.
    SweepFinalizer finalizer(platform_, space_, unused_destroyed_pages_,
                             free_memory_handling_,
                             EmptyPageHandling::kDestroy);
    if (!finalizer.FinalizeWithDeadline(state, deadline)) {
      return false;
    }

    if (sweeping_mode != MutatorThreadSweepingMode::kOnlyFinalizers) {
      // Help out the concurrent sweeper.
      if (!SweepSpaceWithDeadline(&state, deadline)) {
        return false;
      }
    }
    return true;
  }

  size_t largest_new_free_list_entry() const {
    return largest_new_free_list_entry_;
  }

  bool SweepWithDeadlineAndSize(SweepingState& state,
                                v8::base::TimeTicks deadline, size_t size) {
    DeadlineChecker deadline_check(deadline);
    while (auto page = state.unswept_pages.Pop()) {
      SweepPage(**page);
      if (size <= largest_new_free_list_entry()) {
        return true;
      }
      if (deadline_check.Check()) {
        break;
      }
    }
    return false;
  }

 private:
  bool SweepSpaceWithDeadline(SweepingState* state,
                              v8::base::TimeTicks deadline) {
    DeadlineChecker deadline_check(deadline);
    while (auto page = state->unswept_pages.Pop()) {
      Traverse(**page);
      if (deadline_check.Check()) {
        return false;
      }
    }

    return true;
  }

  bool VisitNormalPage(NormalPage& page) {
    if (free_memory_handling_ == FreeMemoryHandling::kDiscardWherePossible) {
      page.ResetDiscardedMemory();
    }
    const auto result =
        (free_memory_handling_ == FreeMemoryHandling::kDiscardWherePossible)
            ? SweepNormalPage<
                  InlinedFinalizationBuilder<DiscardingFreeHandler>>(
                  &page, *platform_->GetPageAllocator(), sticky_bits_)
            : SweepNormalPage<InlinedFinalizationBuilder<RegularFreeHandler>>(
                  &page, *platform_->GetPageAllocator(), sticky_bits_);
    if (result.is_empty &&
        empty_page_handling_ == EmptyPageHandling::kDestroy) {
      NormalPage::Destroy(&page, free_memory_handling_);
      (*unused_destroyed_pages_)++;
    } else {
      if (space_) {
        DCHECK_IMPLIES(!result.is_empty, space_ == &page.space());
        page.ChangeOwner(*space_);
      }
      auto& target_space = NormalPageSpace::From(page.space());
      target_space.AddPage(&page);
      if (result.is_empty) {
        target_space.free_list().Add({page.PayloadStart(), page.PayloadSize()});
      }
      // The page was eagerly finalized and all the freelist have been merged.
      // Verify that the bitmap is consistent with headers.
      ObjectStartBitmapVerifier().Verify(page);
      largest_new_free_list_entry_ =
          std::max(result.is_empty ? page.PayloadSize()
                                   : result.largest_new_free_list_entry,
                   largest_new_free_list_entry_);
    }
    return true;
  }

  bool VisitLargePage(LargePage& page) {
    if (sticky_bits_ == StickyBits::kDisabled) {
      page.ResetMarkedBytes();
    }
    HeapObjectHeader* header = page.ObjectHeader();
    if (header->IsMarked()) {
      StickyUnmark(header, sticky_bits_);
      if (sticky_bits_ == StickyBits::kDisabled) {
        page.ResetMarkedBytes();
      }
      page.space().AddPage(&page);
    } else {
      header->Finalize();
      LargePage::Destroy(&page);
    }
    return true;
  }

  cppgc::Platform* platform_;
  size_t largest_new_free_list_entry_ = 0;
  BaseSpace* space_;
  size_t* unused_destroyed_pages_;
  const FreeMemoryHandling free_memory_handling_;
  const EmptyPageHandling empty_page_handling_;
  const StickyBits sticky_bits_;
};

class ConcurrentSweepTask final : public cppgc::JobTask,
                                  private HeapVisitor<ConcurrentSweepTask> {
  friend class HeapVisitor<ConcurrentSweepTask>;

  using FreeMemoryHandling = SweepingConfig::FreeMemoryHandling;

 public:
  ConcurrentSweepTask(HeapBase& heap, SpaceStates* space_states,
                      SweepingState* empty_pages, Platform* platform,
                      FreeMemoryHandling free_memory_handling)
      : heap_(heap),
        space_states_(space_states),
        empty_pages_(empty_pages),
        platform_(platform),
        free_memory_handling_(free_memory_handling),
        sticky_bits_(heap.generational_gc_supported() ? StickyBits::kEnabled
                                                      : StickyBits::kDisabled) {
  }

  void Run(cppgc::JobDelegate* delegate) final {
    StatsCollector::EnabledConcurrentScope stats_scope(
        heap_.stats_collector(), StatsCollector::kConcurrentSweep);

    while (auto page = empty_pages_->unswept_pages.Pop()) {
      Traverse(**page);
      if (delegate->ShouldYield()) {
        return;
      }
    }
    for (SweepingState& state : *space_states_) {
      while (auto page = state.unswept_pages.Pop()) {
        Traverse(**page);
        if (delegate->ShouldYield()) {
          return;
        }
      }
    }
    is_completed_.store(true, std::memory_order_relaxed);
  }

  size_t GetMaxConcurrency(size_t /* active_worker_count */) const final {
    return is_completed_.load(std::memory_order_relaxed) ? 0 : 1;
  }

 private:
  bool VisitNormalPage(NormalPage& page) {
    if (free_memory_handling_ == FreeMemoryHandling::kDiscardWherePossible) {
      page.ResetDiscardedMemory();
    }
    SweepingState::SweptPageState sweep_result =
        (free_memory_handling_ == FreeMemoryHandling::kDiscardWherePossible)
            ? SweepNormalPage<
                  DeferredFinalizationBuilder<DiscardingFreeHandler>>(
                  &page, *platform_->GetPageAllocator(), sticky_bits_)
            : SweepNormalPage<DeferredFinalizationBuilder<RegularFreeHandler>>(
                  &page, *platform_->GetPageAllocator(), sticky_bits_);
    const size_t space_index = page.space().index();
    DCHECK_GT(space_states_->size(), space_index);
    SweepingState& space_state = (*space_states_)[space_index];
    space_state.swept_unfinalized_pages.Push(std::move(sweep_result));
    return true;
  }

  bool VisitLargePage(LargePage& page) {
    if (sticky_bits_ == StickyBits::kDisabled) {
      page.ResetMarkedBytes();
    }
    HeapObjectHeader* header = page.ObjectHeader();
    if (header->IsMarked()) {
      StickyUnmark(header, sticky_bits_);
      if (sticky_bits_ == StickyBits::kDisabled) {
        page.ResetMarkedBytes();
      }
      page.space().AddPage(&page);
      return true;
    }
#if defined(CPPGC_CAGED_HEAP)
    HeapObjectHeader* const unfinalized_objects =
        header->IsFinalizable() ? page.ObjectHeader() : nullptr;
#else   // !defined(CPPGC_CAGED_HEAP)
    std::vector<HeapObjectHeader*> unfinalized_objects;
    if (header->IsFinalizable()) {
      unfinalized_objects.push_back(page.ObjectHeader());
    }
#endif  // !defined(CPPGC_CAGED_HEAP)
    const size_t space_index = page.space().index();
    DCHECK_GT(space_states_->size(), space_index);
    SweepingState& state = (*space_states_)[space_index];
    // Avoid directly destroying large pages here as counter updates and
    // backend access in BasePage::Destroy() are not concurrency safe.
    state.swept_unfinalized_pages.Push(
        {&page, std::move(unfinalized_objects), {}, {}, true});
    return true;
  }

  HeapBase& heap_;
  SpaceStates* const space_states_;
  SweepingState* empty_pages_;
  Platform* const platform_;
  std::atomic_bool is_completed_{false};
  const FreeMemoryHandling free_memory_handling_;
  const StickyBits sticky_bits_;
};

// This visitor:
// - clears free lists for all spaces;
// - moves all Heap pages to local Sweeper's state (SpaceStates).
// - ASAN: Poisons all unmarked object payloads.
class PrepareForSweepVisitor final
    : protected HeapVisitor<PrepareForSweepVisitor> {
  friend class HeapVisitor<PrepareForSweepVisitor>;
  using CompactableSpaceHandling = SweepingConfig::CompactableSpaceHandling;

 public:
  PrepareForSweepVisitor(SpaceStates* space_states, SweepingState* empty_pages,
                         CompactableSpaceHandling compactable_space_handling)
      : space_states_(space_states),
        empty_pages_(empty_pages),
        compactable_space_handling_(compactable_space_handling) {}

  void Run(RawHeap& raw_heap) {
    *space_states_ = SpaceStates(raw_heap.size());
    Traverse(raw_heap);
  }

 protected:
  bool VisitNormalPageSpace(NormalPageSpace& space) {
    if ((compactable_space_handling_ == CompactableSpaceHandling::kIgnore) &&
        space.is_compactable())
      return true;
    DCHECK(!space.linear_allocation_buffer().size());
    space.free_list().Clear();
#ifdef V8_USE_ADDRESS_SANITIZER
    UnmarkedObjectsPoisoner().Traverse(space);
#endif  // V8_USE_ADDRESS_SANITIZER
    ExtractPages(space);
    return true;
  }

  bool VisitLargePageSpace(LargePageSpace& space) {
#ifdef V8_USE_ADDRESS_SANITIZER
    UnmarkedObjectsPoisoner().Traverse(space);
#endif  // V8_USE_ADDRESS_SANITIZER
    ExtractPages(space);
    return true;
  }

 private:
  void ExtractPages(BaseSpace& space) {
    BaseSpace::Pages space_pages = space.RemoveAllPages();
    std::sort(space_pages.begin(), space_pages.end(),
              [](const BasePage* a, const BasePage* b) {
                return a->marked_bytes() < b->marked_bytes();
              });
    auto first_non_empty_page = std::find_if(
        space_pages.begin(), space_pages.end(),
        [](const BasePage* page) { return page->marked_bytes() != 0; });
    empty_pages_->unswept_pages.Insert(space_pages.begin(),
                                       first_non_empty_page);
    (*space_states_)[space.index()].unswept_pages.Insert(first_non_empty_page,
                                                         space_pages.end());
  }

  SpaceStates* const space_states_;
  SweepingState* const empty_pages_;
  CompactableSpaceHandling compactable_space_handling_;
};

}  // namespace

class Sweeper::SweeperImpl final {
  using FreeMemoryHandling = SweepingConfig::FreeMemoryHandling;

 public:
  SweeperImpl(RawHeap& heap, StatsCollector* stats_collector)
      : heap_(heap),
        page_pool_(heap_.heap()->page_backend()->page_pool()),
        stats_collector_(stats_collector) {}

  ~SweeperImpl() { CancelAllSweepers(); }

  void Start(SweepingConfig config, cppgc::Platform* platform) {
    StatsCollector::EnabledScope stats_scope(stats_collector_,
                                             StatsCollector::kAtomicSweep);
    is_in_progress_ = true;
    platform_ = platform;
    config_ = config;

    // Verify bitmap for all spaces regardless of |compactable_space_handling|.
    ObjectStartBitmapVerifier().Verify(heap_);

    // If inaccessible memory is touched to check whether it is set up
    // correctly it cannot be discarded.
    if (!CanDiscardMemory()) {
      config_.free_memory_handling = FreeMemoryHandling::kDoNotDiscard;
    }

    if (config_.free_memory_handling ==
        FreeMemoryHandling::kDiscardWherePossible) {
      // The discarded counter will be recomputed.
      heap_.heap()->stats_collector()->ResetDiscardedMemory();
    }
    PrepareForSweepVisitor(&space_states_, &empty_pages_,
                           config.compactable_space_handling)
        .Run(heap_);

    if (config.sweeping_type >= SweepingConfig::SweepingType::kIncremental) {
      foreground_task_runner_ = platform_->GetForegroundTaskRunner();
      // We start with a 0-delay sweeping task to get through a burst of work
      // in case this was started from synchronous execution.
      regular_task_is_delayed_idle_task_ =
          config_.sweeping_strategy ==
                  SweepingStrategy::kMinimizeMutatorInterference
              ? true
              : false;
      ScheduleIncrementalSweeping();
    }
    if (config.sweeping_type >=
        SweepingConfig::SweepingType::kIncrementalAndConcurrent) {
      ScheduleConcurrentSweeping();
    }
  }

  void SweepForTask(v8::base::TimeDelta max_duration) {
    // Before sweeping in a task, handle idle sweeping cases. These are no-ops
    // if idle sweeping is not running.
    if (config_.sweeping_strategy ==
        SweepingStrategy::kMinimizeMutatorInterference) {
      if (regular_task_is_delayed_idle_task_) {
        // Idle task asked for delayed schedule.
        regular_task_is_delayed_idle_task_ = false;
        ScheduleIdleIncrementalSweeping();
        ScheduleIncrementalSweeping(kDelayWhileIdleSweepingMakesProgress);
        return;
      }
      if (saved_idle_task_count_ != idle_task_count_) {
        // Idle task made progress. Reschedule with delay.
        ScheduleIncrementalSweeping(kDelayWhileIdleSweepingMakesProgress);
        return;
      }
    }

    // Idle sweeping is not running or not being invoked on time.
    switch (
        SweepInForegroundTaskImpl(max_duration, StatsCollector::kSweepInTask)) {
      case SweepResult::kFullyDone:
        return;
      case SweepResult::kInProgress:
        ScheduleIncrementalSweeping();
        return;
      case SweepResult::kMainThreadDoneConcurrentInProgress:
        // Throttle incremental sweeping while the concurrent Job is still
        // making progress.
        ScheduleIncrementalSweeping(kDelayWhileConcurrentSweepingMakesProgress);
        return;
    }
    UNREACHABLE();
  }

  void SweepForIdleTask(v8::base::TimeDelta max_duration) {
    idle_task_count_++;
    switch (SweepInForegroundTaskImpl(max_duration,
                                      StatsCollector::kSweepInIdleTask)) {
      case SweepResult::kFullyDone:
        return;
      case SweepResult::kInProgress:
        // More work to do. Continue idle sweeping.
        ScheduleIdleIncrementalSweeping();
        return;
      case SweepResult::kMainThreadDoneConcurrentInProgress:
        // We cannot schedule delayed idle tasks. Instead schedule a regular
        // delayed task that should reschedule idle work again. Use the idle
        // delay here to avoid switching to more eager task processing in case
        // we used idle tasks.
        regular_task_is_delayed_idle_task_ = true;
        ScheduleIncrementalSweeping(kDelayWhileIdleSweepingMakesProgress);
        return;
    }
    UNREACHABLE();
  }

  bool SweepForAllocationIfRunning(NormalPageSpace* space, size_t size,
                                   v8::base::TimeDelta max_duration) {
    if (!is_in_progress_) {
      return false;
    }

    // Bail out for recursive sweeping calls. This can happen when finalizers
    // allocate new memory.
    if (is_sweeping_on_mutator_thread_) {
      return false;
    }

    if (unused_destroyed_pages_ > 0 && page_pool_.pooled() > 0) {
      unused_destroyed_pages_--;
      // Destroyed pages during sweeping in tasks are generally sitting in the
      // page pool and can be reused without increasing memory footprint.
      return false;
    }

    SweepingState& space_state = space_states_[space->index()];

    // Bail out if there's no empty pages and no pages to be processed for the
    // specific space space at this moment.
    if (empty_pages_.swept_unfinalized_pages.IsEmpty() &&
        empty_pages_.unswept_pages.IsEmpty() &&
        space_state.swept_unfinalized_pages.IsEmpty() &&
        space_state.unswept_pages.IsEmpty()) {
      return false;
    }

    StatsCollector::EnabledScope stats_scope(stats_collector_,
                                             StatsCollector::kIncrementalSweep);
    StatsCollector::EnabledScope inner_scope(
        stats_collector_, StatsCollector::kSweepOnAllocation);
    MutatorThreadSweepingScope sweeping_in_progress(*this);

    const auto deadline = v8::base::TimeTicks::Now() + max_duration;

    SweepFinalizer finalizer(platform_, space, &unused_destroyed_pages_,
                             config_.free_memory_handling,
                             EmptyPageHandling::kReturn);
    // Check empty pages first. Try to just finalize a page without sweeping.
    // If there's a single page in there we will use it.
    if (finalizer.FinalizeWithDeadlineAndSize(empty_pages_, deadline, size)) {
      return true;
    }
    MutatorThreadSweeper sweeper(
        heap_.heap(), platform_, space, &unused_destroyed_pages_,
        config_.free_memory_handling, EmptyPageHandling::kReturn);
    // Sweeping an empty page in case there's nothing with finalizers. If
    // there's a single page in there we will use it.
    if (sweeper.SweepWithDeadlineAndSize(empty_pages_, deadline, size)) {
      return true;
    }
    // Process unfinalized non-empty pages as finalizing a page is generally
    // faster than sweeping.
    if (finalizer.FinalizeWithDeadlineAndSize(space_state, deadline, size)) {
      return true;
    }
    // Then, if no matching slot is found in the unfinalized pages, search the
    // unswept page. This also helps out the concurrent sweeper.
    if (sweeper.SweepWithDeadlineAndSize(space_state, deadline, size)) {
      return true;
    }
    return false;
  }

  bool FinishIfRunning() {
    if (!is_in_progress_) return false;

    // Bail out for recursive sweeping calls. This can happen when finalizers
    // allocate new memory.
    if (is_sweeping_on_mutator_thread_) return false;

    {
      std::optional<StatsCollector::EnabledScope> stats_scope;
      if (config_.sweeping_type != SweepingConfig::SweepingType::kAtomic) {
        stats_scope.emplace(stats_collector_,
                            StatsCollector::kIncrementalSweep);
      }
      StatsCollector::EnabledScope inner_scope(stats_collector_,
                                               StatsCollector::kSweepFinalize);
      if (concurrent_sweeper_handle_ && concurrent_sweeper_handle_->IsValid() &&
          concurrent_sweeper_handle_->UpdatePriorityEnabled()) {
        concurrent_sweeper_handle_->UpdatePriority(
            cppgc::TaskPriority::kUserBlocking);
      }
      Finish();
    }
    NotifyDone();
    return true;
  }

  bool IsConcurrentSweepingDone() const {
    return !concurrent_sweeper_handle_ ||
           !concurrent_sweeper_handle_->IsValid() ||
           !concurrent_sweeper_handle_->IsActive();
  }

  void FinishIfOutOfWork() {
    if (!is_in_progress_) return;
    if (is_sweeping_on_mutator_thread_) return;
    if (!concurrent_sweeper_handle_ || !concurrent_sweeper_handle_->IsValid() ||
        concurrent_sweeper_handle_->IsActive()) {
      return;
    }
    // At this point we know that the concurrent sweeping task has run
    // out-of-work: all pages are swept. The main thread still needs to finalize
    // swept pages.
    DCHECK(std::all_of(space_states_.begin(), space_states_.end(),
                       [](const SweepingState& state) {
                         return state.unswept_pages.IsEmpty();
                       }));
    DCHECK(empty_pages_.unswept_pages.IsEmpty());
    if (std::any_of(space_states_.begin(), space_states_.end(),
                    [](const SweepingState& state) {
                      return !state.swept_unfinalized_pages.IsEmpty();
                    })) {
      return;
    }
    if (!empty_pages_.swept_unfinalized_pages.IsEmpty()) {
      return;
    }
    // All pages have also been finalized. Finalizing pages likely occured on
    // allocation, in which sweeping is not finalized even though all work is
    // done.
    {
      StatsCollector::EnabledScope stats_scope(
          stats_collector_, StatsCollector::kSweepFinishIfOutOfWork);
      FinalizeSweep();
    }
    NotifyDone();
  }

  void Finish() {
    DCHECK(is_in_progress_);

    MutatorThreadSweepingScope sweeping_in_progress(*this);

    // First, call finalizers on the mutator thread. This is just an
    // optimization as we need to call finalizers after sweeping as well. It
    // allows to spend the time in the concurrent sweeper for actual sweeping.
    SweepFinalizer finalizer(
        platform_, kSweepWithoutSpaceAssignment, &unused_destroyed_pages_,
        config_.free_memory_handling, EmptyPageHandling::kDestroy);
    finalizer.Finalize(space_states_);
    finalizer.Finalize(empty_pages_);

    // Then, help out the concurrent thread.
    MutatorThreadSweeper sweeper(
        heap_.heap(), platform_, kSweepWithoutSpaceAssignment,
        &unused_destroyed_pages_, config_.free_memory_handling,
        EmptyPageHandling::kDestroy);
    sweeper.Sweep(space_states_);
    sweeper.Sweep(empty_pages_);

    // There's nothing left to sweep here for the main thread. The concurrent
    // sweeper may still sweep pages and create pages to be finalized after
    // joining the the job.
    FinalizeSweep();
  }

  void FinalizeSweep() {
    // Synchronize with the concurrent sweeper and call remaining finalizers.
    SynchronizeAndFinalizeConcurrentAndIncrementalSweeping();

    // Clear space taken up by sweeper metadata.
    space_states_.clear();

    platform_ = nullptr;
    foreground_task_runner_ = nullptr;
    is_in_progress_ = false;
    notify_done_pending_ = true;
    regular_task_is_delayed_idle_task_ = false;
    unused_destroyed_pages_ = 0;
  }

  void NotifyDone() {
    DCHECK(!is_in_progress_);
    DCHECK(notify_done_pending_);
    notify_done_pending_ = false;
    stats_collector_->NotifySweepingCompleted(config_.sweeping_type);
    if (config_.free_memory_handling ==
        FreeMemoryHandling::kDiscardWherePossible)
      heap_.heap()->page_backend()->DiscardPooledPages();
  }

  void WaitForConcurrentSweepingForTesting() {
    if (concurrent_sweeper_handle_) concurrent_sweeper_handle_->Join();
  }

  bool IsSweepingOnMutatorThread() const {
    return is_sweeping_on_mutator_thread_;
  }

  bool IsSweepingInProgress() const { return is_in_progress_; }

  bool PerformSweepOnMutatorThread(v8::base::TimeDelta max_duration,
                                   StatsCollector::ScopeId internal_scope_id,
                                   MutatorThreadSweepingMode sweeping_mode) {
    if (!is_in_progress_) return true;

    MutatorThreadSweepingScope sweeping_in_progress(*this);

    {
      StatsCollector::EnabledScope stats_scope(
          stats_collector_, StatsCollector::kIncrementalSweep);

      MutatorThreadSweeper sweeper(
          heap_.heap(), platform_, kSweepWithoutSpaceAssignment,
          &unused_destroyed_pages_, config_.free_memory_handling,
          EmptyPageHandling::kDestroy);
      {
        StatsCollector::EnabledScope inner_stats_scope(
            stats_collector_, internal_scope_id, "max_duration_ms",
            max_duration.InMillisecondsF(), "sweeping_mode",
            ToString(sweeping_mode));
        const auto deadline = v8::base::TimeTicks::Now() + max_duration;
        if (!sweeper.FinalizeAndSweepWithDeadline(empty_pages_, deadline,
                                                  sweeping_mode)) {
          return false;
        }
        for (auto& state : space_states_) {
          if (!sweeper.FinalizeAndSweepWithDeadline(state, deadline,
                                                    sweeping_mode)) {
            return false;
          }
        }
        if (sweeping_mode != MutatorThreadSweepingMode::kAll) {
          return false;
        }
      }
      FinalizeSweep();
    }
    NotifyDone();
    return true;
  }

  void AddMutatorThreadSweepingObserver(
      Sweeper::SweepingOnMutatorThreadObserver* observer) {
    DCHECK_EQ(mutator_thread_sweeping_observers_.end(),
              std::find(mutator_thread_sweeping_observers_.begin(),
                        mutator_thread_sweeping_observers_.end(), observer));
    mutator_thread_sweeping_observers_.push_back(observer);
  }

  void RemoveMutatorThreadSweepingObserver(
      Sweeper::SweepingOnMutatorThreadObserver* observer) {
    const auto it =
        std::find(mutator_thread_sweeping_observers_.begin(),
                  mutator_thread_sweeping_observers_.end(), observer);
    DCHECK_NE(mutator_thread_sweeping_observers_.end(), it);
    mutator_thread_sweeping_observers_.erase(it);
  }

 private:
  class MutatorThreadSweepingScope final {
   public:
    explicit MutatorThreadSweepingScope(SweeperImpl& sweeper)
        : sweeper_(sweeper) {
      DCHECK(!sweeper_.is_sweeping_on_mutator_thread_);
      sweeper_.is_sweeping_on_mutator_thread_ = true;
      for (auto* observer : sweeper_.mutator_thread_sweeping_observers_) {
        observer->Start();
      }
    }
    ~MutatorThreadSweepingScope() {
      sweeper_.is_sweeping_on_mutator_thread_ = false;
      for (auto* observer : sweeper_.mutator_thread_sweeping_observers_) {
        observer->End();
      }
    }

    MutatorThreadSweepingScope(const MutatorThreadSweepingScope&) = delete;
    MutatorThreadSweepingScope& operator=(const MutatorThreadSweepingScope&) =
        delete;

   private:
    SweeperImpl& sweeper_;
  };

  class IncrementalSweepIdleTask final : public cppgc::IdleTask {
   public:
    using Handle = SingleThreadedHandle;

    static Handle Post(SweeperImpl& sweeper, cppgc::Platform* platform,
                       const std::shared_ptr<cppgc::TaskRunner>& runner) {
      std::unique_ptr<IncrementalSweepIdleTask> task(
          new IncrementalSweepIdleTask(platform, sweeper));
      auto handle = task->handle_;
      runner->PostIdleTask(std::move(task));
      return handle;
    }

    void Run(double deadline_in_seconds) override {
      if (handle_.IsCanceled()) {
        return;
      }
      const auto idle_time = v8::base::TimeDelta::FromSecondsD(
          (deadline_in_seconds - platform_->MonotonicallyIncreasingTime()));
      sweeper_.SweepForIdleTask(idle_time);
    }

   private:
    explicit IncrementalSweepIdleTask(cppgc::Platform* platform,
                                      SweeperImpl& sweeper)
        : platform_(platform),
          sweeper_(sweeper),
          handle_(Handle::NonEmptyTag{}) {}

    cppgc::Platform* platform_;
    SweeperImpl& sweeper_;
    // TODO(chromium:1056170): Change to CancelableTask.
    Handle handle_;
  };

  class IncrementalSweepTask final : public cppgc::Task {
   public:
    using Handle = SingleThreadedHandle;

    explicit IncrementalSweepTask(SweeperImpl& sweeper)
        : sweeper_(sweeper), handle_(Handle::NonEmptyTag{}) {}

    static Handle Post(SweeperImpl& sweeper,
                       const std::shared_ptr<cppgc::TaskRunner>& runner,
                       std::optional<v8::base::TimeDelta> delay) {
      auto task = std::make_unique<IncrementalSweepTask>(sweeper);
      auto handle = task->handle_;
      if (delay.has_value()) {
        runner->PostDelayedTask(std::move(task), delay->InSecondsF());
      } else {
        runner->PostTask(std::move(task));
      }
      return handle;
    }

    void Run() override {
      if (handle_.IsCanceled()) {
        return;
      }
      sweeper_.SweepForTask(v8::base::TimeDelta::FromMilliseconds(5));
    }

   private:
    SweeperImpl& sweeper_;
    // TODO(chromium:1056170): Change to CancelableTask.
    Handle handle_;
  };

  enum class SweepResult {
    // Sweeping is fully done.
    kFullyDone,
    // Sweeping is still in progress.
    kInProgress,
    // Sweeping on the main thread is done but concurrent sweepers are still
    // making progress. This may be temporary.
    kMainThreadDoneConcurrentInProgress,
  };

  static constexpr double kMaxHeapPercentageForNoSweeping = 50;

  static constexpr auto kDelayWhileIdleSweepingMakesProgress =
      v8::base::TimeDelta::FromMilliseconds(100);

  static constexpr auto kDelayWhileConcurrentSweepingMakesProgress =
      v8::base::TimeDelta::FromMilliseconds(5);

  SweepResult SweepInForegroundTaskImpl(v8::base::TimeDelta max_duration,
                                        StatsCollector::ScopeId scope) {
    // First round of sweeping.
    bool concurrent_sweep_complete = IsConcurrentSweepingDone();
    const auto start = v8::base::TimeTicks::Now();
    bool main_thread_sweep_complete = PerformSweepOnMutatorThread(
        max_duration, scope,
        concurrent_sweep_complete ? MutatorThreadSweepingMode::kAll
                                  : MutatorThreadSweepingMode::kOnlyFinalizers);
    if (main_thread_sweep_complete && !concurrent_sweep_complete &&
        IsConcurrentSweepingDone()) {
      // Concurrent sweeping finished while processing the first round. Use the
      // left over time for a second round to avoid scheduling another task.
      max_duration -= (v8::base::TimeTicks::Now() - start);
      if (max_duration > v8::base::TimeDelta::FromMilliseconds(0)) {
        concurrent_sweep_complete = true;
        main_thread_sweep_complete = PerformSweepOnMutatorThread(
            max_duration, scope, MutatorThreadSweepingMode::kAll);
      }
    }
    if (main_thread_sweep_complete) {
      if (!concurrent_sweep_complete) {
        return SweepResult::kMainThreadDoneConcurrentInProgress;
      } else {
        CHECK(!is_in_progress_);
        return SweepResult::kFullyDone;
      }
    }
    return SweepResult::kInProgress;
  }

  void ScheduleIncrementalSweeping(
      std::optional<v8::base::TimeDelta> delay = {}) {
    DCHECK(platform_);
    DCHECK_GE(config_.sweeping_type,
              SweepingConfig::SweepingType::kIncremental);

    if (!foreground_task_runner_) {
      return;
    }

    saved_idle_task_count_ = idle_task_count_;
    incremental_sweeper_handle_.CancelIfNonEmpty();
    incremental_sweeper_handle_ =
        IncrementalSweepTask::Post(*this, foreground_task_runner_, delay);
  }

  void ScheduleIdleIncrementalSweeping() {
    DCHECK(platform_);
    DCHECK_GE(config_.sweeping_type,
              SweepingConfig::SweepingType::kIncremental);
    DCHECK_NE(config_.sweeping_strategy, SweepingStrategy::kMinimizeMemory);

    if (!foreground_task_runner_) {
      return;
    }

    if (foreground_task_runner_->IdleTasksEnabled()) {
      incremental_sweeper_idle_handle_.CancelIfNonEmpty();
      incremental_sweeper_idle_handle_ = IncrementalSweepIdleTask::Post(
          *this, platform_, foreground_task_runner_);
    }
  }

  void ScheduleConcurrentSweeping() {
    DCHECK(platform_);
    DCHECK_GE(config_.sweeping_type,
              SweepingConfig::SweepingType::kIncrementalAndConcurrent);

    concurrent_sweeper_handle_ =
        platform_->PostJob(cppgc::TaskPriority::kUserVisible,
                           std::make_unique<ConcurrentSweepTask>(
                               *heap_.heap(), &space_states_, &empty_pages_,
                               platform_, config_.free_memory_handling));
  }

  void CancelAllSweepers() {
    if (incremental_sweeper_handle_) {
      incremental_sweeper_handle_.Cancel();
    }
    if (incremental_sweeper_idle_handle_) {
      incremental_sweeper_idle_handle_.Cancel();
    }
    if (concurrent_sweeper_handle_ && concurrent_sweeper_handle_->IsValid()) {
      concurrent_sweeper_handle_->Cancel();
    }
  }

  void SynchronizeAndFinalizeConcurrentAndIncrementalSweeping() {
    // The precondition for this call is that actual sweeping is done. So all
    // that's left is potentially invoking finalizers.

    CancelAllSweepers();

    DCHECK(std::all_of(space_states_.begin(), space_states_.end(),
                       [](const SweepingState& state) {
                         return state.unswept_pages.IsEmpty();
                       }));
    DCHECK(empty_pages_.unswept_pages.IsEmpty());

    SweepFinalizer finalizer(
        platform_, kSweepWithoutSpaceAssignment, &unused_destroyed_pages_,
        config_.free_memory_handling, EmptyPageHandling::kDestroy);
    finalizer.Finalize(space_states_);
    finalizer.Finalize(empty_pages_);
  }

  RawHeap& heap_;
  NormalPageMemoryPool& page_pool_;
  StatsCollector* const stats_collector_;
  SpaceStates space_states_;
  // States for empty pages. These pages do have a space as owner which is
  // updated as soon as the page is reused for a specific space.
  SweepingState empty_pages_;
  // Number of pages that have been destroyed and have not been reused by the
  // allocator yet. We assume that returning early on
  // SweepForAllocationIfRunning() causes such pages to be picked up.
  size_t unused_destroyed_pages_ = 0;
  cppgc::Platform* platform_;
  std::shared_ptr<cppgc::TaskRunner> foreground_task_runner_;
  SweepingConfig config_;
  IncrementalSweepTask::Handle incremental_sweeper_handle_;
  IncrementalSweepIdleTask::Handle incremental_sweeper_idle_handle_;
  std::unique_ptr<cppgc::JobHandle> concurrent_sweeper_handle_;
  std::vector<Sweeper::SweepingOnMutatorThreadObserver*>
      mutator_thread_sweeping_observers_;
  // Counter for idle task executions. Incremented each time an idle task is
  // invoked.
  size_t idle_task_count_ = 0;
  // The idle task count when scheduling an incremental task. Is used to signal
  // idle task progress.
  size_t saved_idle_task_count_ = 0;
  // Indicates whether the sweeping phase is in progress.
  bool is_in_progress_ = false;
  bool notify_done_pending_ = false;
  // Indicates whether whether the sweeper (or its finalization) is currently
  // running on the main thread.
  bool is_sweeping_on_mutator_thread_ = false;
  // Indicates whether the currently scheduled regular task is actually a
  // delayed idle task. This is necessary as the platform does not support
  // delayed idle tasks.
  bool regular_task_is_delayed_idle_task_ = false;
};

Sweeper::Sweeper(HeapBase& heap)
    : heap_(heap),
      impl_(std::make_unique<SweeperImpl>(heap.raw_heap(),
                                          heap.stats_collector())) {}

Sweeper::~Sweeper() = default;

void Sweeper::Start(SweepingConfig config) {
  impl_->Start(config, heap_.platform());
}

bool Sweeper::FinishIfRunning() { return impl_->FinishIfRunning(); }

void Sweeper::FinishIfOutOfWork() { impl_->FinishIfOutOfWork(); }

void Sweeper::WaitForConcurrentSweepingForTesting() {
  impl_->WaitForConcurrentSweepingForTesting();
}

bool Sweeper::SweepForAllocationIfRunning(NormalPageSpace* space, size_t size,
                                          v8::base::TimeDelta max_duration) {
  return impl_->SweepForAllocationIfRunning(space, size, max_duration);
}

bool Sweeper::IsSweepingOnMutatorThread() const {
  return impl_->IsSweepingOnMutatorThread();
}

bool Sweeper::IsSweepingInProgress() const {
  return impl_->IsSweepingInProgress();
}

bool Sweeper::PerformSweepOnMutatorThread(v8::base::TimeDelta max_duration,
                                          StatsCollector::ScopeId scope_id) {
  return impl_->PerformSweepOnMutatorThread(max_duration, scope_id,
                                            MutatorThreadSweepingMode::kAll);
}

Sweeper::SweepingOnMutatorThreadObserver::SweepingOnMutatorThreadObserver(
    Sweeper& sweeper)
    : sweeper_(sweeper) {
  sweeper_.impl_->AddMutatorThreadSweepingObserver(this);
}

Sweeper::SweepingOnMutatorThreadObserver::~SweepingOnMutatorThreadObserver() {
  sweeper_.impl_->RemoveMutatorThreadSweepingObserver(this);
}

}  // namespace cppgc::internal
