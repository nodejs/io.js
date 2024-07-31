// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/sweeper.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "include/cppgc/platform.h"
#include "src/base/optional.h"
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

using v8::base::Optional;

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

  Optional<T> Pop() {
    v8::base::LockGuard<v8::base::Mutex> lock(&mutex_);
    if (vector_.empty()) {
      is_empty_.store(true, std::memory_order_relaxed);
      return v8::base::nullopt;
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

struct SpaceState {
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

using SpaceStates = std::vector<SpaceState>;

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
  using ResultType = SpaceState::SweptPageState;

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

  if (start_of_gap != page->PayloadStart() &&
      start_of_gap != page->PayloadEnd()) {
    builder.AddFreeListEntry(
        start_of_gap, static_cast<size_t>(page->PayloadEnd() - start_of_gap));
    DCHECK(bitmap.CheckBit<AccessMode::kAtomic>(start_of_gap));
  }
  page->SetAllocatedBytesAtLastGC(live_bytes);

  const bool is_empty = (start_of_gap == page->PayloadStart());
  return builder.GetResult(is_empty);
}

// SweepFinalizer is responsible for heap/space/page finalization. Finalization
// is defined as a step following concurrent sweeping which:
// - calls finalizers;
// - returns (unmaps) empty pages;
// - merges freelists to the space's freelist.
class SweepFinalizer final {
  using FreeMemoryHandling = SweepingConfig::FreeMemoryHandling;

 public:
  enum class EmptyPageHandling {
    kDestroy,
    kReturn,
  };

  SweepFinalizer(cppgc::Platform* platform,
                 FreeMemoryHandling free_memory_handling,
                 EmptyPageHandling empty_page_handling_type)
      : platform_(platform),
        free_memory_handling_(free_memory_handling),
        empty_page_handling_(empty_page_handling_type) {}

  void FinalizeHeap(SpaceStates* space_states) {
    for (SpaceState& space_state : *space_states) {
      FinalizeSpace(&space_state);
    }
  }

  void FinalizeSpace(SpaceState* space_state) {
    while (auto page_state = space_state->swept_unfinalized_pages.Pop()) {
      FinalizePage(&*page_state);
    }
  }

  bool FinalizeSpaceWithDeadline(SpaceState* space_state,
                                 v8::base::TimeTicks deadline) {
    DCHECK(platform_);
    DeadlineChecker deadline_check(deadline);
    while (auto page_state = space_state->swept_unfinalized_pages.Pop()) {
      FinalizePage(&*page_state);

      if (deadline_check.Check()) return false;
    }

    return true;
  }

  void FinalizePage(SpaceState::SweptPageState* page_state) {
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
    const uint64_t cage_base = CagedHeapBase::GetBase();
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
      if (empty_page_handling_ == EmptyPageHandling::kDestroy ||
          page->is_large()) {
        BasePage::Destroy(page, free_memory_handling_);
        return;
      }

      // Otherwise, we currently sweep on allocation. Reinitialize the empty
      // page and return it right away.
      auto* normal_page = NormalPage::From(page);

      page_state->cached_free_list.Clear();
      page_state->cached_free_list.Add(
          {normal_page->PayloadStart(), normal_page->PayloadSize()});

      page_state->unfinalized_free_list.clear();
      page_state->largest_new_free_list_entry = normal_page->PayloadSize();
    }

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

  size_t largest_new_free_list_entry() const {
    return largest_new_free_list_entry_;
  }

 private:
  cppgc::Platform* platform_;
  size_t largest_new_free_list_entry_ = 0;
  const FreeMemoryHandling free_memory_handling_;
  const EmptyPageHandling empty_page_handling_;
};

class MutatorThreadSweeper final : private HeapVisitor<MutatorThreadSweeper> {
  friend class HeapVisitor<MutatorThreadSweeper>;

  using FreeMemoryHandling = SweepingConfig::FreeMemoryHandling;

 public:
  MutatorThreadSweeper(HeapBase* heap, SpaceStates* states,
                       cppgc::Platform* platform,
                       FreeMemoryHandling free_memory_handling)
      : states_(states),
        platform_(platform),
        free_memory_handling_(free_memory_handling),
        sticky_bits_(heap->generational_gc_supported()
                         ? StickyBits::kEnabled
                         : StickyBits::kDisabled) {}

  void Sweep() {
    for (SpaceState& state : *states_) {
      while (auto page = state.unswept_pages.Pop()) {
        SweepPage(**page);
      }
    }
  }

  void SweepPage(BasePage& page) { Traverse(page); }

  bool SweepWithDeadline(v8::base::TimeDelta max_duration,
                         MutatorThreadSweepingMode sweeping_mode) {
    DCHECK(platform_);
    for (SpaceState& state : *states_) {
      const auto deadline = v8::base::TimeTicks::Now() + max_duration;

      // First, prioritize finalization of pages that were swept concurrently.
      SweepFinalizer finalizer(platform_, free_memory_handling_,
                               SweepFinalizer::EmptyPageHandling::kDestroy);
      if (!finalizer.FinalizeSpaceWithDeadline(&state, deadline)) {
        return false;
      }

      if (sweeping_mode == MutatorThreadSweepingMode::kOnlyFinalizers)
        return false;

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

 private:
  bool SweepSpaceWithDeadline(SpaceState* state, v8::base::TimeTicks deadline) {
    DeadlineChecker deadline_check(deadline);
    while (auto page = state->unswept_pages.Pop()) {
      Traverse(**page);
      if (deadline_check.Check()) return false;
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
    if (result.is_empty) {
      NormalPage::Destroy(&page, free_memory_handling_);
    } else {
      // The page was eagerly finalized and all the freelist have been merged.
      // Verify that the bitmap is consistent with headers.
      ObjectStartBitmapVerifier().Verify(page);
      page.space().AddPage(&page);
      largest_new_free_list_entry_ = std::max(
          result.largest_new_free_list_entry, largest_new_free_list_entry_);
    }
    return true;
  }

  bool VisitLargePage(LargePage& page) {
    HeapObjectHeader* header = page.ObjectHeader();
    if (header->IsMarked()) {
      StickyUnmark(header, sticky_bits_);
      page.space().AddPage(&page);
    } else {
      header->Finalize();
      LargePage::Destroy(&page);
    }
    return true;
  }

  SpaceStates* states_;
  cppgc::Platform* platform_;
  size_t largest_new_free_list_entry_ = 0;
  const FreeMemoryHandling free_memory_handling_;
  const StickyBits sticky_bits_;
};

class ConcurrentSweepTask final : public cppgc::JobTask,
                                  private HeapVisitor<ConcurrentSweepTask> {
  friend class HeapVisitor<ConcurrentSweepTask>;

  using FreeMemoryHandling = SweepingConfig::FreeMemoryHandling;

 public:
  ConcurrentSweepTask(HeapBase& heap, SpaceStates* states, Platform* platform,
                      FreeMemoryHandling free_memory_handling)
      : heap_(heap),
        states_(states),
        platform_(platform),
        free_memory_handling_(free_memory_handling),
        sticky_bits_(heap.generational_gc_supported() ? StickyBits::kEnabled
                                                      : StickyBits::kDisabled) {
  }

  void Run(cppgc::JobDelegate* delegate) final {
    StatsCollector::EnabledConcurrentScope stats_scope(
        heap_.stats_collector(), StatsCollector::kConcurrentSweep);

    for (SpaceState& state : *states_) {
      while (auto page = state.unswept_pages.Pop()) {
        Traverse(**page);
        if (delegate->ShouldYield()) return;
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
    SpaceState::SweptPageState sweep_result =
        (free_memory_handling_ == FreeMemoryHandling::kDiscardWherePossible)
            ? SweepNormalPage<
                  DeferredFinalizationBuilder<DiscardingFreeHandler>>(
                  &page, *platform_->GetPageAllocator(), sticky_bits_)
            : SweepNormalPage<DeferredFinalizationBuilder<RegularFreeHandler>>(
                  &page, *platform_->GetPageAllocator(), sticky_bits_);
    const size_t space_index = page.space().index();
    DCHECK_GT(states_->size(), space_index);
    SpaceState& space_state = (*states_)[space_index];
    space_state.swept_unfinalized_pages.Push(std::move(sweep_result));
    return true;
  }

  bool VisitLargePage(LargePage& page) {
    HeapObjectHeader* header = page.ObjectHeader();
    if (header->IsMarked()) {
      StickyUnmark(header, sticky_bits_);
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
    DCHECK_GT(states_->size(), space_index);
    SpaceState& state = (*states_)[space_index];
    // Avoid directly destroying large pages here as counter updates and
    // backend access in BasePage::Destroy() are not concurrency safe.
    state.swept_unfinalized_pages.Push(
        {&page, std::move(unfinalized_objects), {}, {}, true});
    return true;
  }

  HeapBase& heap_;
  SpaceStates* states_;
  Platform* platform_;
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
  PrepareForSweepVisitor(SpaceStates* states,
                         CompactableSpaceHandling compactable_space_handling)
      : states_(states),
        compactable_space_handling_(compactable_space_handling) {
    DCHECK_NOT_NULL(states);
  }

  void Run(RawHeap& raw_heap) {
    DCHECK(states_->empty());
    *states_ = SpaceStates(raw_heap.size());
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
    (*states_)[space.index()].unswept_pages.Insert(space_pages.begin(),
                                                   space_pages.end());
  }

  SpaceStates* states_;
  CompactableSpaceHandling compactable_space_handling_;
};

}  // namespace

class Sweeper::SweeperImpl final {
  using FreeMemoryHandling = SweepingConfig::FreeMemoryHandling;

 public:
  SweeperImpl(RawHeap& heap, StatsCollector* stats_collector)
      : heap_(heap), stats_collector_(stats_collector) {}

  ~SweeperImpl() { CancelSweepers(); }

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

    PrepareForSweepVisitor(&space_states_, config.compactable_space_handling)
        .Run(heap_);

    if (config.sweeping_type >= SweepingConfig::SweepingType::kIncremental) {
      ScheduleIncrementalSweeping();
    }
    if (config.sweeping_type >=
        SweepingConfig::SweepingType::kIncrementalAndConcurrent) {
      ScheduleConcurrentSweeping();
    }
  }

  bool SweepForAllocationIfRunning(NormalPageSpace* space, size_t size,
                                   v8::base::TimeDelta max_duration) {
    if (!is_in_progress_) return false;

    // Bail out for recursive sweeping calls. This can happen when finalizers
    // allocate new memory.
    if (is_sweeping_on_mutator_thread_) return false;

    SpaceState& space_state = space_states_[space->index()];

    // Bail out if there's no pages to be processed for the space at this
    // moment.
    if (space_state.swept_unfinalized_pages.IsEmpty() &&
        space_state.unswept_pages.IsEmpty()) {
      return false;
    }

    StatsCollector::EnabledScope stats_scope(stats_collector_,
                                             StatsCollector::kIncrementalSweep);
    StatsCollector::EnabledScope inner_scope(
        stats_collector_, StatsCollector::kSweepOnAllocation);
    MutatorThreadSweepingScope sweeping_in_progress(*this);
    DeadlineChecker deadline_check(v8::base::TimeTicks::Now() + max_duration);
    {
      // First, process unfinalized pages as finalizing a page is faster than
      // sweeping.
      SweepFinalizer finalizer(platform_, config_.free_memory_handling,
                               SweepFinalizer::EmptyPageHandling::kReturn);
      while (auto page = space_state.swept_unfinalized_pages.Pop()) {
        finalizer.FinalizePage(&*page);
        if (size <= finalizer.largest_new_free_list_entry()) {
          return true;
        }
        if (deadline_check.Check()) {
          return false;
        }
      }
    }
    {
      // Then, if no matching slot is found in the unfinalized pages, search the
      // unswept page. This also helps out the concurrent sweeper.
      MutatorThreadSweeper sweeper(heap_.heap(), &space_states_, platform_,
                                   config_.free_memory_handling);
      while (auto page = space_state.unswept_pages.Pop()) {
        sweeper.SweepPage(**page);
        if (size <= sweeper.largest_new_free_list_entry()) {
          return true;
        }
        if (deadline_check.Check()) {
          return false;
        }
      }
    }

    return false;
  }

  bool FinishIfRunning() {
    if (!is_in_progress_) return false;

    // Bail out for recursive sweeping calls. This can happen when finalizers
    // allocate new memory.
    if (is_sweeping_on_mutator_thread_) return false;

    {
      v8::base::Optional<StatsCollector::EnabledScope> stats_scope;
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
           (concurrent_sweeper_handle_->IsValid() &&
            !concurrent_sweeper_handle_->IsActive());
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
    DCHECK(std::all_of(
        space_states_.begin(), space_states_.end(),
        [](const SpaceState& state) { return state.unswept_pages.IsEmpty(); }));
    if (std::any_of(space_states_.begin(), space_states_.end(),
                    [](const SpaceState& state) {
                      return !state.swept_unfinalized_pages.IsEmpty();
                    })) {
      return;
    }
    // All pages have also been finalized. Finalizing pages likely occured on
    // allocation, in which sweeping is not finalized even thopugh all work is
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

    // First, call finalizers on the mutator thread.
    SweepFinalizer finalizer(platform_, config_.free_memory_handling,
                             SweepFinalizer::EmptyPageHandling::kDestroy);
    finalizer.FinalizeHeap(&space_states_);

    // Then, help out the concurrent thread.
    MutatorThreadSweeper sweeper(heap_.heap(), &space_states_, platform_,
                                 config_.free_memory_handling);
    sweeper.Sweep();

    FinalizeSweep();
  }

  void FinalizeSweep() {
    // Synchronize with the concurrent sweeper and call remaining finalizers.
    SynchronizeAndFinalizeConcurrentSweeping();

    // Clear space taken up by sweeper metadata.
    space_states_.clear();

    platform_ = nullptr;
    is_in_progress_ = false;
    notify_done_pending_ = true;
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

    bool sweep_complete;
    {
      StatsCollector::EnabledScope stats_scope(
          stats_collector_, StatsCollector::kIncrementalSweep);

      MutatorThreadSweeper sweeper(heap_.heap(), &space_states_, platform_,
                                   config_.free_memory_handling);
      {
        StatsCollector::EnabledScope inner_stats_scope(
            stats_collector_, internal_scope_id, "max_duration_ms",
            max_duration.InMillisecondsF(), "sweeping_mode",
            ToString(sweeping_mode));
        sweep_complete = sweeper.SweepWithDeadline(max_duration, sweeping_mode);
      }
      if (sweep_complete) {
        FinalizeSweep();
      }
    }
    if (sweep_complete) NotifyDone();
    return sweep_complete;
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

  class IncrementalSweepTask final : public cppgc::Task {
   public:
    using Handle = SingleThreadedHandle;

    explicit IncrementalSweepTask(SweeperImpl& sweeper)
        : sweeper_(sweeper), handle_(Handle::NonEmptyTag{}) {}

    static Handle Post(SweeperImpl& sweeper, cppgc::TaskRunner* runner) {
      auto task = std::make_unique<IncrementalSweepTask>(sweeper);
      auto handle = task->GetHandle();
      runner->PostTask(std::move(task));
      return handle;
    }

   private:
    void Run() override {
      if (handle_.IsCanceled()) return;

      if (!sweeper_.PerformSweepOnMutatorThread(
              v8::base::TimeDelta::FromMilliseconds(5),
              StatsCollector::kSweepInTask,
              sweeper_.IsConcurrentSweepingDone()
                  ? MutatorThreadSweepingMode::kAll
                  : MutatorThreadSweepingMode::kOnlyFinalizers)) {
        sweeper_.ScheduleIncrementalSweeping();
      }
    }

    Handle GetHandle() const { return handle_; }

    SweeperImpl& sweeper_;
    // TODO(chromium:1056170): Change to CancelableTask.
    Handle handle_;
  };

  void ScheduleIncrementalSweeping() {
    DCHECK(platform_);
    DCHECK_GE(config_.sweeping_type,
              SweepingConfig::SweepingType::kIncremental);

    auto runner = platform_->GetForegroundTaskRunner();
    if (!runner) return;

    incremental_sweeper_handle_ =
        IncrementalSweepTask::Post(*this, runner.get());
  }

  void ScheduleConcurrentSweeping() {
    DCHECK(platform_);
    DCHECK_GE(config_.sweeping_type,
              SweepingConfig::SweepingType::kIncrementalAndConcurrent);

    concurrent_sweeper_handle_ =
        platform_->PostJob(cppgc::TaskPriority::kUserVisible,
                           std::make_unique<ConcurrentSweepTask>(
                               *heap_.heap(), &space_states_, platform_,
                               config_.free_memory_handling));
  }

  void CancelSweepers() {
    if (incremental_sweeper_handle_) incremental_sweeper_handle_.Cancel();
    if (concurrent_sweeper_handle_ && concurrent_sweeper_handle_->IsValid())
      concurrent_sweeper_handle_->Cancel();
  }

  void SynchronizeAndFinalizeConcurrentSweeping() {
    CancelSweepers();

    SweepFinalizer finalizer(platform_, config_.free_memory_handling,
                             SweepFinalizer::EmptyPageHandling::kDestroy);
    finalizer.FinalizeHeap(&space_states_);
  }

  RawHeap& heap_;
  StatsCollector* const stats_collector_;
  SpaceStates space_states_;
  cppgc::Platform* platform_;
  SweepingConfig config_;
  IncrementalSweepTask::Handle incremental_sweeper_handle_;
  std::unique_ptr<cppgc::JobHandle> concurrent_sweeper_handle_;
  std::vector<Sweeper::SweepingOnMutatorThreadObserver*>
      mutator_thread_sweeping_observers_;
  // Indicates whether the sweeping phase is in progress.
  bool is_in_progress_ = false;
  bool notify_done_pending_ = false;
  // Indicates whether whether the sweeper (or its finalization) is currently
  // running on the main thread.
  bool is_sweeping_on_mutator_thread_ = false;
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
