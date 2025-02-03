// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/sweeper.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "src/base/atomic-utils.h"
#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/execution/vm-state-inl.h"
#include "src/flags/flags.h"
#include "src/heap/base/active-system-pages.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/free-list-inl.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-inl.h"
#include "src/heap/marking-state.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/new-spaces.h"
#include "src/heap/page-metadata-inl.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/pretenuring-handler-inl.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/remembered-set.h"
#include "src/heap/slot-set.h"
#include "src/heap/zapping.h"
#include "src/objects/hash-table.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/map.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

class Sweeper::ConcurrentMajorSweeper final {
 public:
  explicit ConcurrentMajorSweeper(Sweeper* sweeper)
      : sweeper_(sweeper), local_sweeper_(sweeper_) {}

  bool ConcurrentSweepSpace(AllocationSpace identity, JobDelegate* delegate) {
    DCHECK(IsValidSweepingSpace(identity));
    DCHECK_NE(NEW_SPACE, identity);
    while (!delegate->ShouldYield()) {
      PageMetadata* page = sweeper_->GetSweepingPageSafe(identity);
      if (page == nullptr) return true;
      local_sweeper_.ParallelSweepPage(page, identity,
                                       SweepingMode::kLazyOrConcurrent);
    }
    TRACE_GC_NOTE("Sweeper::ConcurrentMajorSweeper Preempted");
    return false;
  }

  // This method is expected by `SweepingState::FinishSweeping`.
  void Finalize() {}

 private:
  Sweeper* const sweeper_;
  LocalSweeper local_sweeper_;
};

static constexpr auto kNewSpace =
    v8_flags.sticky_mark_bits.value() ? OLD_SPACE : NEW_SPACE;

class Sweeper::ConcurrentMinorSweeper final {
 public:
  explicit ConcurrentMinorSweeper(Sweeper* sweeper)
      : sweeper_(sweeper), local_sweeper_(sweeper_) {}

  bool ConcurrentSweepSpace(JobDelegate* delegate) {
    DCHECK(IsValidSweepingSpace(kNewSpace));
    while (!delegate->ShouldYield()) {
      PageMetadata* page = sweeper_->GetSweepingPageSafe(kNewSpace);
      if (page == nullptr) return true;
      local_sweeper_.ParallelSweepPage(page, kNewSpace,
                                       SweepingMode::kLazyOrConcurrent);
    }
    TRACE_GC_NOTE("Sweeper::ConcurrentMinorSweeper Preempted");
    return false;
  }

  bool ConcurrentSweepPromotedPages(JobDelegate* delegate) {
    if (local_sweeper_.ParallelIteratePromotedPages(delegate)) return true;
    TRACE_GC_NOTE("Sweeper::ConcurrentMinorSweeper Preempted");
    return false;
  }

 private:
  Sweeper* const sweeper_;
  LocalSweeper local_sweeper_;
};

class Sweeper::MajorSweeperJob final : public JobTask {
 private:
  // Major sweeping jobs don't sweep new space.
  static constexpr int kNumberOfMajorSweepingSpaces =
      kNumberOfSweepingSpaces - 1;

 public:
  static constexpr int kMaxTasks = kNumberOfMajorSweepingSpaces;

  MajorSweeperJob(Isolate* isolate, Sweeper* sweeper)
      : sweeper_(sweeper),
        concurrent_sweepers(
            sweeper_->major_sweeping_state_.concurrent_sweepers()),
        tracer_(isolate->heap()->tracer()),
        trace_id_(sweeper_->major_sweeping_state_.background_trace_id()) {
    DCHECK_LE(concurrent_sweepers.size(), kMaxTasks);
  }

  ~MajorSweeperJob() override = default;

  MajorSweeperJob(const MajorSweeperJob&) = delete;
  MajorSweeperJob& operator=(const MajorSweeperJob&) = delete;

  void Run(JobDelegate* delegate) final {
    RunImpl(delegate, delegate->IsJoiningThread());
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    static constexpr int kPagePerTask = 2;
    return std::min<size_t>(
        concurrent_sweepers.size(),
        worker_count +
            (sweeper_->ConcurrentMajorSweepingPageCount() + kPagePerTask - 1) /
                kPagePerTask);
  }

 private:
  void RunImpl(JobDelegate* delegate, bool is_joining_thread) {
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(
        sweeper_->heap_->isolate());

    DCHECK(sweeper_->major_sweeping_in_progress());
    const int offset = delegate->GetTaskId();
    DCHECK_LT(offset, concurrent_sweepers.size());
    ConcurrentMajorSweeper& concurrent_sweeper = concurrent_sweepers[offset];
    TRACE_GC_EPOCH_WITH_FLOW(
        tracer_, sweeper_->GetTracingScope(OLD_SPACE, is_joining_thread),
        is_joining_thread ? ThreadKind::kMain : ThreadKind::kBackground,
        trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
    for (int i = 0; i < kNumberOfMajorSweepingSpaces; i++) {
      const AllocationSpace space_id = static_cast<AllocationSpace>(
          FIRST_SWEEPABLE_SPACE + 1 +
          ((offset + i) % kNumberOfMajorSweepingSpaces));
      DCHECK_LE(FIRST_SWEEPABLE_SPACE, space_id);
      DCHECK_LE(space_id, LAST_SWEEPABLE_SPACE);
      DCHECK_NE(NEW_SPACE, space_id);
      if (!concurrent_sweeper.ConcurrentSweepSpace(space_id, delegate)) return;
    }
  }

  Sweeper* const sweeper_;
  std::vector<ConcurrentMajorSweeper>& concurrent_sweepers;
  GCTracer* const tracer_;
  const uint64_t trace_id_;
};

class Sweeper::MinorSweeperJob final : public JobTask {
 public:
  static constexpr int kMaxTasks = 1;

  MinorSweeperJob(Isolate* isolate, Sweeper* sweeper)
      : sweeper_(sweeper),
        concurrent_sweepers(
            sweeper_->minor_sweeping_state_.concurrent_sweepers()),
        tracer_(isolate->heap()->tracer()),
        trace_id_(sweeper_->minor_sweeping_state_.background_trace_id()) {
    DCHECK_LE(concurrent_sweepers.size(), kMaxTasks);
  }

  ~MinorSweeperJob() override = default;

  MinorSweeperJob(const MinorSweeperJob&) = delete;
  MinorSweeperJob& operator=(const MinorSweeperJob&) = delete;

  void Run(JobDelegate* delegate) final {
    RunImpl(delegate, delegate->IsJoiningThread());
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    static constexpr int kPagePerTask = 2;
    return std::min<size_t>(
        concurrent_sweepers.size(),
        worker_count +
            (sweeper_->ConcurrentMinorSweepingPageCount() + kPagePerTask - 1) /
                kPagePerTask);
  }

 private:
  void RunImpl(JobDelegate* delegate, bool is_joining_thread) {
    DCHECK(sweeper_->minor_sweeping_in_progress());
    const int offset = delegate->GetTaskId();
    DCHECK_LT(offset, concurrent_sweepers.size());
    ConcurrentMinorSweeper& concurrent_sweeper = concurrent_sweepers[offset];
    TRACE_GC_EPOCH_WITH_FLOW(
        tracer_, sweeper_->GetTracingScope(NEW_SPACE, is_joining_thread),
        is_joining_thread ? ThreadKind::kMain : ThreadKind::kBackground,
        trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(
        sweeper_->heap_->isolate());

    if (!concurrent_sweeper.ConcurrentSweepSpace(delegate)) return;
    concurrent_sweeper.ConcurrentSweepPromotedPages(delegate);
  }

  Sweeper* const sweeper_;
  std::vector<ConcurrentMinorSweeper>& concurrent_sweepers;
  GCTracer* const tracer_;
  const uint64_t trace_id_;
};

template <Sweeper::SweepingScope scope>
Sweeper::SweepingState<scope>::SweepingState(Sweeper* sweeper)
    : sweeper_(sweeper) {}

template <Sweeper::SweepingScope scope>
Sweeper::SweepingState<scope>::~SweepingState() {
  DCHECK(!in_progress_);
  DCHECK(concurrent_sweepers_.empty());
  DCHECK(!HasValidJob());
}

template <Sweeper::SweepingScope scope>
bool Sweeper::SweepingState<scope>::HasValidJob() const {
  return job_handle_ && job_handle_->IsValid();
}

template <Sweeper::SweepingScope scope>
bool Sweeper::SweepingState<scope>::HasActiveJob() const {
  return HasValidJob() && job_handle_->IsActive();
}

template <Sweeper::SweepingScope scope>
void Sweeper::SweepingState<scope>::StopConcurrentSweeping() {
  if (HasValidJob()) job_handle_->Cancel();
}

template <Sweeper::SweepingScope scope>
void Sweeper::SweepingState<scope>::InitializeSweeping() {
  DCHECK(!HasValidJob());
  DCHECK(!in_progress_);
  DCHECK(concurrent_sweepers_.empty());
  DCHECK_IMPLIES(scope == Sweeper::SweepingScope::kMinor, v8_flags.minor_ms);
  DCHECK_IMPLIES(scope == Sweeper::SweepingScope::kMinor,
                 !sweeper_->heap_->ShouldReduceMemory());
  should_reduce_memory_ = (scope != Sweeper::SweepingScope::kMinor) &&
                          sweeper_->heap_->ShouldReduceMemory();
  trace_id_ =
      (reinterpret_cast<uint64_t>(sweeper_) ^
       sweeper_->heap_->tracer()->CurrentEpoch(
           scope == SweepingScope::kMajor ? GCTracer::Scope::MC_SWEEP
                                          : GCTracer::Scope::MINOR_MS_SWEEP))
      << 1;
  background_trace_id_ = trace_id_ + 1;
}

template <Sweeper::SweepingScope scope>
void Sweeper::SweepingState<scope>::StartSweeping() {
  DCHECK(!HasValidJob());
  DCHECK(!in_progress_);
  DCHECK(concurrent_sweepers_.empty());
  DCHECK_NE(0, trace_id_);
  DCHECK_NE(0, background_trace_id_);
  in_progress_ = true;
}

template <Sweeper::SweepingScope scope>
void Sweeper::SweepingState<scope>::StartConcurrentSweeping() {
  DCHECK(!HasValidJob());
  DCHECK(in_progress_);
  if (v8_flags.concurrent_sweeping &&
      !sweeper_->heap_->delay_sweeper_tasks_for_testing_) {
    auto job =
        std::make_unique<SweeperJob>(sweeper_->heap_->isolate(), sweeper_);
    GCTracer::Scope::ScopeId scope_id =
        scope == SweepingScope::kMinor
            ? GCTracer::Scope::MINOR_MS_SWEEP_START_JOBS
            : GCTracer::Scope::MC_SWEEP_START_JOBS;
    TRACE_GC_WITH_FLOW(sweeper_->heap_->tracer(), scope_id,
                       background_trace_id(), TRACE_EVENT_FLAG_FLOW_OUT);
    DCHECK_IMPLIES(v8_flags.minor_ms, concurrent_sweepers_.empty());
    int max_concurrent_sweeper_count =
        std::min(SweeperJob::kMaxTasks,
                 V8::GetCurrentPlatform()->NumberOfWorkerThreads() + 1);
    if (concurrent_sweepers_.empty()) {
      for (int i = 0; i < max_concurrent_sweeper_count; ++i) {
        concurrent_sweepers_.emplace_back(sweeper_);
      }
    }
    DCHECK_EQ(max_concurrent_sweeper_count, concurrent_sweepers_.size());
    job_handle_ = V8::GetCurrentPlatform()->PostJob(TaskPriority::kUserVisible,
                                                    std::move(job));
  }
}

template <Sweeper::SweepingScope scope>
void Sweeper::SweepingState<scope>::JoinSweeping() {
  DCHECK(in_progress_);
  if (HasValidJob()) job_handle_->Join();
}

template <Sweeper::SweepingScope scope>
void Sweeper::SweepingState<scope>::FinishSweeping() {
  DCHECK(in_progress_);
  // Sweeping jobs were already joined.
  DCHECK(!HasValidJob());

  concurrent_sweepers_.clear();
  in_progress_ = false;
}

template <Sweeper::SweepingScope scope>
void Sweeper::SweepingState<scope>::Pause() {
  if (!job_handle_ || !job_handle_->IsValid()) return;

  DCHECK(v8_flags.concurrent_sweeping);
  job_handle_->Cancel();
  job_handle_.reset();
}

template <Sweeper::SweepingScope scope>
void Sweeper::SweepingState<scope>::Resume() {
  DCHECK(in_progress_);
  job_handle_ = V8::GetCurrentPlatform()->PostJob(
      TaskPriority::kUserVisible,
      std::make_unique<SweeperJob>(sweeper_->heap_->isolate(), sweeper_));
}

bool Sweeper::LocalSweeper::ParallelSweepSpace(AllocationSpace identity,
                                               SweepingMode sweeping_mode,
                                               uint32_t max_pages) {
  uint32_t pages_swept = 0;
  bool found_usable_pages = false;
  PageMetadata* page = nullptr;
  while ((page = sweeper_->GetSweepingPageSafe(identity)) != nullptr) {
    ParallelSweepPage(page, identity, sweeping_mode);
    if (!page->Chunk()->IsFlagSet(MemoryChunk::NEVER_ALLOCATE_ON_PAGE)) {
      found_usable_pages = true;
#if DEBUG
    } else {
      // All remaining pages are also marked with NEVER_ALLOCATE_ON_PAGE.
      base::MutexGuard guard(&sweeper_->mutex_);
      int space_index = GetSweepSpaceIndex(identity);
      Sweeper::SweepingList& sweeping_list =
          sweeper_->sweeping_list_[space_index];
      DCHECK(std::all_of(sweeping_list.begin(), sweeping_list.end(),
                         [](const PageMetadata* p) {
                           return p->Chunk()->IsFlagSet(
                               MemoryChunk::NEVER_ALLOCATE_ON_PAGE);
                         }));
#endif  // DEBUG
    }
    if (++pages_swept >= max_pages) break;
  }
  return found_usable_pages;
}

void Sweeper::LocalSweeper::ParallelSweepPage(PageMetadata* page,
                                              AllocationSpace identity,
                                              SweepingMode sweeping_mode) {
  DCHECK(IsValidSweepingSpace(identity));

  // The Scavenger may add already swept pages back.
  if (page->SweepingDone()) return;

  {
    base::MutexGuard guard(page->mutex());
    DCHECK(!page->SweepingDone());
    DCHECK_EQ(PageMetadata::ConcurrentSweepingState::kPendingSweeping,
              page->concurrent_sweeping_state());
    page->set_concurrent_sweeping_state(
        PageMetadata::ConcurrentSweepingState::kInProgress);
    const FreeSpaceTreatmentMode free_space_treatment_mode =
        heap::ShouldZapGarbage() ? FreeSpaceTreatmentMode::kZapFreeSpace
                                 : FreeSpaceTreatmentMode::kIgnoreFreeSpace;
    DCHECK_IMPLIES(identity == NEW_SPACE,
                   !sweeper_->minor_sweeping_state_.should_reduce_memory());
    sweeper_->RawSweep(
        page, free_space_treatment_mode, sweeping_mode,
        identity == NEW_SPACE
            ? false
            : sweeper_->major_sweeping_state_.should_reduce_memory());
    sweeper_->AddSweptPage(page, identity);
    DCHECK(page->SweepingDone());
  }
}

bool Sweeper::LocalSweeper::ContributeAndWaitForPromotedPagesIteration(
    JobDelegate* delegate) {
  return ContributeAndWaitForPromotedPagesIterationImpl(
      [delegate]() { return delegate->ShouldYield(); });
}

bool Sweeper::LocalSweeper::ContributeAndWaitForPromotedPagesIteration() {
  return ContributeAndWaitForPromotedPagesIterationImpl([]() { return false; });
}

bool Sweeper::LocalSweeper::ParallelIteratePromotedPages(
    JobDelegate* delegate) {
  return ParallelIteratePromotedPagesImpl(
      [delegate]() { return delegate->ShouldYield(); });
}

bool Sweeper::LocalSweeper::ParallelIteratePromotedPages() {
  return ParallelIteratePromotedPagesImpl([]() { return false; });
}

namespace {
class PromotedPageRecordMigratedSlotVisitor final
    : public NewSpaceVisitor<PromotedPageRecordMigratedSlotVisitor> {
 public:
  explicit PromotedPageRecordMigratedSlotVisitor(MutablePageMetadata* host_page)
      : NewSpaceVisitor<PromotedPageRecordMigratedSlotVisitor>(
            host_page->heap()->isolate()),
        host_chunk_(host_page->Chunk()),
        host_page_(host_page),
        ephemeron_remembered_set_(
            host_page->heap()->ephemeron_remembered_set()) {
    DCHECK(host_page->owner_identity() == OLD_SPACE ||
           host_page->owner_identity() == LO_SPACE);
  }

  void Process(Tagged<HeapObject> object) {
    Tagged<Map> map = object->map(cage_base());
    if (Map::ObjectFieldsFrom(map->visitor_id()) == ObjectFields::kDataOnly) {
      return;
    }
    Visit(map, object);
  }

  // TODO(v8:13883): MakeExternal() right now allows to externalize a string in
  // the young generation (for testing) and on a promoted page that is currently
  // being swept. If we solve the testing cases and prohobit MakeExternal() on
  // page owned by the sweeper, this visitor can be simplified as there's no
  // more unsafe shape changes that happen concurrently.
  V8_INLINE static constexpr bool EnableConcurrentVisitation() { return true; }

  V8_INLINE void VisitMapPointer(Tagged<HeapObject> host) final {
    VerifyHost(host);
    VisitObjectImpl(host, host->map(cage_base()), host->map_slot().address());
  }

  V8_INLINE void VisitPointer(Tagged<HeapObject> host, ObjectSlot p) final {
    VisitPointersImpl(host, p, p + 1);
  }
  V8_INLINE void VisitPointer(Tagged<HeapObject> host,
                              MaybeObjectSlot p) final {
    VisitPointersImpl(host, p, p + 1);
  }
  V8_INLINE void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                               ObjectSlot end) final {
    VisitPointersImpl(host, start, end);
  }
  V8_INLINE void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                               MaybeObjectSlot end) final {
    VisitPointersImpl(host, start, end);
  }

  V8_INLINE int VisitJSArrayBuffer(Tagged<Map> map,
                                   Tagged<JSArrayBuffer> object) {
    object->YoungMarkExtensionPromoted();
    return NewSpaceVisitor<
        PromotedPageRecordMigratedSlotVisitor>::VisitJSArrayBuffer(map, object);
  }

  V8_INLINE int VisitEphemeronHashTable(Tagged<Map> map,
                                        Tagged<EphemeronHashTable> table) {
    NewSpaceVisitor<PromotedPageRecordMigratedSlotVisitor>::
        VisitMapPointerIfNeeded<VisitorId::kVisitEphemeronHashTable>(table);
    EphemeronRememberedSet::IndicesSet indices;
    for (InternalIndex i : table->IterateEntries()) {
      ObjectSlot value_slot =
          table->RawFieldOfElementAt(EphemeronHashTable::EntryToValueIndex(i));
      VisitPointer(table, value_slot);
      ObjectSlot key_slot =
          table->RawFieldOfElementAt(EphemeronHashTable::EntryToIndex(i));
      Tagged<Object> key = key_slot.Acquire_Load();
      Tagged<HeapObject> key_object;
      if (!key.GetHeapObject(&key_object)) continue;
#ifdef THREAD_SANITIZER
      MemoryChunk::FromHeapObject(key_object)->SynchronizedLoad();
#endif  // THREAD_SANITIZER
      // With sticky mark-bits we don't need to update the remembered set for
      // just promoted objects, since everything is promoted.
      if (!v8_flags.sticky_mark_bits && Heap::InYoungGeneration(key_object)) {
        indices.insert(i.as_int());
      }
    }
    if (!indices.empty()) {
      ephemeron_remembered_set_->RecordEphemeronKeyWrites(table,
                                                          std::move(indices));
    }
    return EphemeronHashTable::BodyDescriptor::SizeOf(map, table);
  }

  // Entries that are skipped for recording.
  void VisitExternalReference(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) final {}
  void VisitInternalReference(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) final {}
  void VisitExternalPointer(Tagged<HeapObject> host,
                            ExternalPointerSlot slot) final {}

  // Maps can be shared, so we need to visit them to record old to shared slots.
  V8_INLINE static constexpr bool ShouldVisitMapPointer() { return true; }
  V8_INLINE static constexpr bool ShouldVisitReadOnlyMapPointer() {
    return false;
  }

 private:
  V8_INLINE void VerifyHost(Tagged<HeapObject> host) {
    DCHECK(!InWritableSharedSpace(host));
    DCHECK(!Heap::InYoungGeneration(host));
    DCHECK(!MutablePageMetadata::FromHeapObject(host)->SweepingDone());
    DCHECK_EQ(MutablePageMetadata::FromHeapObject(host), host_page_);
  }

  template <typename TObject>
  V8_INLINE void VisitObjectImpl(Tagged<HeapObject> host, TObject object,
                                 Address slot) {
    Tagged<HeapObject> value_heap_object;
    if (!object.GetHeapObject(&value_heap_object)) return;

    MemoryChunk* value_chunk = MemoryChunk::FromHeapObject(value_heap_object);
#ifdef THREAD_SANITIZER
    value_chunk->SynchronizedLoad();
#endif  // THREAD_SANITIZER
    // With sticky mark-bits we don't need to update the remembered set for
    // just promoted objects, since everything is promoted.
    if (!v8_flags.sticky_mark_bits && value_chunk->InYoungGeneration()) {
      RememberedSet<OLD_TO_NEW_BACKGROUND>::Insert<AccessMode::ATOMIC>(
          host_page_, host_chunk_->Offset(slot));
    } else if (value_chunk->InWritableSharedSpace()) {
      RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(
          host_page_, host_chunk_->Offset(slot));
    }
  }

  template <typename TSlot>
  V8_INLINE void VisitPointersImpl(Tagged<HeapObject> host, TSlot start,
                                   TSlot end) {
    VerifyHost(host);
    for (TSlot slot = start; slot < end; ++slot) {
      typename TSlot::TObject target =
          slot.Relaxed_Load(ObjectVisitorWithCageBases::cage_base());
      VisitObjectImpl(host, target, slot.address());
    }
  }

  MemoryChunk* const host_chunk_;
  MutablePageMetadata* const host_page_;
  EphemeronRememberedSet* ephemeron_remembered_set_;
};

// Atomically zap the specified area.
V8_INLINE void AtomicZapBlock(Address addr, size_t size_in_bytes) {
  static_assert(sizeof(Tagged_t) == kTaggedSize);
  static constexpr Tagged_t kZapTagged = static_cast<Tagged_t>(kZapValue);
  DCHECK(IsAligned(addr, kTaggedSize));
  DCHECK(IsAligned(size_in_bytes, kTaggedSize));
  const size_t size_in_tagged = size_in_bytes / kTaggedSize;
  Tagged_t* current_addr = reinterpret_cast<Tagged_t*>(addr);
  for (size_t i = 0; i < size_in_tagged; ++i) {
    base::AsAtomicPtr(current_addr++)
        ->store(kZapTagged, std::memory_order_relaxed);
  }
}

void ZapDeadObjectsInRange(Heap* heap, Address dead_start, Address dead_end) {
  if (dead_end != dead_start) {
    size_t free_size = static_cast<size_t>(dead_end - dead_start);
    AtomicZapBlock(dead_start, free_size);
    WritableFreeSpace free_space =
        WritableFreeSpace::ForNonExecutableMemory(dead_start, free_size);
    heap->CreateFillerObjectAtBackground(free_space);
  }
}

void ZapDeadObjectsOnPage(Heap* heap, PageMetadata* p) {
  Address dead_start = p->area_start();
  // Iterate over the page using the live objects.
  for (auto [object, size] : LiveObjectRange(p)) {
    Address dead_end = object.address();
    ZapDeadObjectsInRange(heap, dead_start, dead_end);
    dead_start = dead_end + size;
  }
  ZapDeadObjectsInRange(heap, dead_start, p->area_end());
}

}  // namespace

void Sweeper::LocalSweeper::ParallelIteratePromotedPage(
    MutablePageMetadata* page) {
  DCHECK(v8_flags.minor_ms);
  DCHECK_NOT_NULL(page);
  {
    base::MutexGuard guard(page->mutex());
    DCHECK(!page->SweepingDone());
    DCHECK_EQ(PageMetadata::ConcurrentSweepingState::kPendingIteration,
              page->concurrent_sweeping_state());
    page->set_concurrent_sweeping_state(
        PageMetadata::ConcurrentSweepingState::kInProgress);
    PromotedPageRecordMigratedSlotVisitor record_visitor(page);
    const bool is_large_page = page->Chunk()->IsLargePage();
    if (is_large_page) {
      DCHECK_EQ(LO_SPACE, page->owner_identity());
      record_visitor.Process(LargePageMetadata::cast(page)->GetObject());
      page->ReleaseSlotSet(SURVIVOR_TO_EXTERNAL_POINTER);
    } else {
      DCHECK_EQ(OLD_SPACE, page->owner_identity());
      DCHECK(!page->Chunk()->IsEvacuationCandidate());
      for (auto [object, _] :
           LiveObjectRange(static_cast<PageMetadata*>(page))) {
        record_visitor.Process(object);
      }
    }
    if (heap::ShouldZapGarbage() && !is_large_page) {
      ZapDeadObjectsOnPage(sweeper_->heap_, static_cast<PageMetadata*>(page));
    }
    page->ClearLiveness();
    sweeper_->NotifyPromotedPageIterationFinished(page);
    DCHECK(page->SweepingDone());
  }
}

Sweeper::Sweeper(Heap* heap)
    : heap_(heap),
      marking_state_(heap_->non_atomic_marking_state()),
      main_thread_local_sweeper_(this) {}

Sweeper::~Sweeper() = default;

void Sweeper::TearDown() {
  minor_sweeping_state_.StopConcurrentSweeping();
  major_sweeping_state_.StopConcurrentSweeping();
}

void Sweeper::InitializeMajorSweeping() {
  major_sweeping_state_.InitializeSweeping();
}

void Sweeper::InitializeMinorSweeping() {
  minor_sweeping_state_.InitializeSweeping();
}

namespace {
V8_INLINE bool ComparePagesForSweepingOrder(const PageMetadata* a,
                                            const PageMetadata* b) {
  // Prioritize pages that can be allocated on.
  if (a->Chunk()->IsFlagSet(MemoryChunk::NEVER_ALLOCATE_ON_PAGE) !=
      b->Chunk()->IsFlagSet(MemoryChunk::NEVER_ALLOCATE_ON_PAGE))
    return a->Chunk()->IsFlagSet(MemoryChunk::NEVER_ALLOCATE_ON_PAGE);
  // We sort in descending order of live bytes, i.e., ascending order of
  // free bytes, because GetSweepingPageSafe returns pages in reverse order.
  return a->live_bytes() > b->live_bytes();
}
}  // namespace

void Sweeper::StartMajorSweeping() {
  DCHECK_EQ(GarbageCollector::MARK_COMPACTOR,
            heap_->tracer()->GetCurrentCollector());
  DCHECK(!minor_sweeping_in_progress());
  major_sweeping_state_.StartSweeping();
  ForAllSweepingSpaces([this](AllocationSpace space) {
    // Sorting is done in order to make compaction more efficient: by sweeping
    // pages with the most free bytes first, we make it more likely that when
    // evacuating a page, already swept pages will have enough free bytes to
    // hold the objects to move (and therefore, we won't need to wait for more
    // pages to be swept in order to move those objects).
    int space_index = GetSweepSpaceIndex(space);
    DCHECK_IMPLIES(space == NEW_SPACE, sweeping_list_[space_index].empty());
    std::sort(sweeping_list_[space_index].begin(),
              sweeping_list_[space_index].end(), ComparePagesForSweepingOrder);
  });
}

void Sweeper::StartMinorSweeping() {
  DCHECK_EQ(GarbageCollector::MINOR_MARK_SWEEPER,
            heap_->tracer()->GetCurrentCollector());
  minor_sweeping_state_.StartSweeping();
  int new_space_index = GetSweepSpaceIndex(kNewSpace);
  std::sort(sweeping_list_[new_space_index].begin(),
            sweeping_list_[new_space_index].end(),
            ComparePagesForSweepingOrder);
}

namespace {
bool ShouldUpdateRememberedSets(Heap* heap) {
  DCHECK_EQ(0, heap->new_lo_space()->Size());
  if (v8_flags.sticky_mark_bits) {
    // TODO(333906585): Update OLD_TO_SHARED remembered set for promoted
    // objects.
    return false;
  }
  if (heap->new_space()->Size() > 0) {
    // Keep track of OLD_TO_NEW slots
    return true;
  }
  // TODO(v8:12612): OLD_TO_SHARED is not really needed on the main isolate and
  // this condition should only apply to client isolates.
  if (heap->isolate()->has_shared_space()) {
    // Keep track of OLD_TO_SHARED slots
    return true;
  }
  return false;
}
}  // namespace

void Sweeper::StartMajorSweeperTasks() {
  DCHECK_IMPLIES(v8_flags.minor_ms, GarbageCollector::MARK_COMPACTOR ==
                                        heap_->tracer()->GetCurrentCollector());
  DCHECK(!minor_sweeping_in_progress());
  DCHECK(!promoted_page_iteration_in_progress_);
  DCHECK_EQ(0, promoted_pages_for_iteration_count_);
  major_sweeping_state_.StartConcurrentSweeping();
}

namespace {
void ClearPromotedPages(Heap* heap, std::vector<MutablePageMetadata*> pages) {
  DCHECK(v8_flags.minor_ms);
  for (auto* page : pages) {
    DCHECK(!page->SweepingDone());
    DCHECK_EQ(PageMetadata::ConcurrentSweepingState::kPendingIteration,
              page->concurrent_sweeping_state());
    if (heap::ShouldZapGarbage() && !page->Chunk()->IsLargePage()) {
      ZapDeadObjectsOnPage(heap, static_cast<PageMetadata*>(page));
    }
    page->ClearLiveness();
    page->set_concurrent_sweeping_state(
        PageMetadata::ConcurrentSweepingState::kDone);
  }
}
}  // namespace

void Sweeper::StartMinorSweeperTasks() {
  DCHECK(v8_flags.minor_ms);
  DCHECK_EQ(GarbageCollector::MINOR_MARK_SWEEPER,
            heap_->tracer()->GetCurrentCollector());
  DCHECK(!promoted_page_iteration_in_progress_);
  std::vector<MutablePageMetadata*> promoted_pages_for_clearing;
  if (promoted_pages_for_iteration_count_ > 0) {
    if (ShouldUpdateRememberedSets(heap_)) {
      promoted_page_iteration_in_progress_.store(true,
                                                 std::memory_order_release);
    } else {
      promoted_pages_for_clearing.swap(
          sweeping_list_for_promoted_page_iteration_);
      DCHECK(sweeping_list_for_promoted_page_iteration_.empty());
      promoted_pages_for_iteration_count_ = 0;
    }
  }
  minor_sweeping_state_.StartConcurrentSweeping();
  ClearPromotedPages(heap_, promoted_pages_for_clearing);
}

PageMetadata* Sweeper::GetSweptPageSafe(PagedSpaceBase* space) {
  base::MutexGuard guard(&mutex_);
  SweptList& list = swept_list_[GetSweepSpaceIndex(space->identity())];
  PageMetadata* page = nullptr;
  if (!list.empty()) {
    page = list.back();
    list.pop_back();
  }
  if (list.empty()) {
    has_swept_pages_[GetSweepSpaceIndex(space->identity())].store(
        false, std::memory_order_release);
  }
  return page;
}

Sweeper::SweptList Sweeper::GetAllSweptPagesSafe(PagedSpaceBase* space) {
  base::MutexGuard guard(&mutex_);
  SweptList list;
  list.swap(swept_list_[GetSweepSpaceIndex(space->identity())]);
  has_swept_pages_[GetSweepSpaceIndex(space->identity())].store(
      false, std::memory_order_release);
  return list;
}

void Sweeper::FinishMajorJobs() {
  if (!major_sweeping_in_progress()) return;

  ForAllSweepingSpaces([this](AllocationSpace space) {
    if (space == NEW_SPACE) return;
    main_thread_local_sweeper_.ParallelSweepSpace(
        space, SweepingMode::kLazyOrConcurrent);
  });

  // Join all concurrent tasks.
  major_sweeping_state_.JoinSweeping();
  // All jobs are done but we still remain in sweeping state here.
  DCHECK(major_sweeping_in_progress());

  ForAllSweepingSpaces([this](AllocationSpace space) {
    if (space == NEW_SPACE) return;
    CHECK(sweeping_list_[GetSweepSpaceIndex(space)].empty());
    DCHECK(IsSweepingDoneForSpace(space));
  });
}

void Sweeper::EnsureMajorCompleted() {
  DCHECK(heap_->IsMainThread());

  // If sweeping is not completed or not running at all, we try to complete it
  // here.

  if (minor_sweeping_in_progress()) {
    TRACE_GC_EPOCH_WITH_FLOW(
        heap_->tracer(), GCTracer::Scope::MINOR_MS_COMPLETE_SWEEPING,
        ThreadKind::kMain,
        GetTraceIdForFlowEvent(GCTracer::Scope::MINOR_MS_COMPLETE_SWEEPING),
        TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
    // TODO(40096225): When finalizing sweeping for a starting a new major GC,
    // OLD_TO_NEW is no longer needed. If this is the main isolate, we could
    // cancel promoted page iteration instead of finishing it.
    EnsureMinorCompleted();
  }

  if (major_sweeping_in_progress()) {
    TRACE_GC_EPOCH_WITH_FLOW(
        heap_->tracer(), GCTracer::Scope::MC_COMPLETE_SWEEPING,
        ThreadKind::kMain,
        GetTraceIdForFlowEvent(GCTracer::Scope::MC_COMPLETE_SWEEPING),
        TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
    // Discard all pooled pages on memory-reducing GCs.
    if (major_sweeping_state_.should_reduce_memory()) {
      heap_->memory_allocator()->pool()->ReleasePooledChunks();
    }
    FinishMajorJobs();
    major_sweeping_state_.FinishSweeping();
    // Sweeping should not add pages to the pool.
    DCHECK_IMPLIES(
        major_sweeping_state_.should_reduce_memory(),
        heap_->memory_allocator()->pool()->NumberOfCommittedChunks() == 0);
  }
}

void Sweeper::FinishMinorJobs() {
  if (!minor_sweeping_in_progress()) return;

  main_thread_local_sweeper_.ParallelSweepSpace(
      kNewSpace, SweepingMode::kLazyOrConcurrent);
  // Array buffer sweeper may have grabbed a page for iteration to contribute.
  // Wait until it has finished iterating.
  main_thread_local_sweeper_.ContributeAndWaitForPromotedPagesIteration();

  // Join all concurrent tasks.
  minor_sweeping_state_.JoinSweeping();
  // All jobs are done but we still remain in sweeping state here.
  DCHECK(minor_sweeping_in_progress());

  CHECK(sweeping_list_[GetSweepSpaceIndex(kNewSpace)].empty());
  DCHECK(IsSweepingDoneForSpace(kNewSpace));

  DCHECK_EQ(promoted_pages_for_iteration_count_,
            iterated_promoted_pages_count_);
  CHECK(sweeping_list_for_promoted_page_iteration_.empty());
}

void Sweeper::EnsureMinorCompleted() {
  if (!minor_sweeping_in_progress()) return;

  DCHECK(!minor_sweeping_state_.should_reduce_memory());
  FinishMinorJobs();
  minor_sweeping_state_.FinishSweeping();

  promoted_pages_for_iteration_count_ = 0;
  iterated_promoted_pages_count_ = 0;
}

bool Sweeper::AreMinorSweeperTasksRunning() const {
  return minor_sweeping_state_.HasActiveJob();
}

bool Sweeper::AreMajorSweeperTasksRunning() const {
  return major_sweeping_state_.HasActiveJob();
}

bool Sweeper::UsingMajorSweeperTasks() const {
  return major_sweeping_state_.HasValidJob();
}

V8_INLINE size_t Sweeper::FreeAndProcessFreedMemory(
    Address free_start, Address free_end, PageMetadata* page, Space* space,
    FreeSpaceTreatmentMode free_space_treatment_mode,
    bool should_reduce_memory) {
  CHECK_GT(free_end, free_start);
  size_t freed_bytes = 0;
  size_t size = static_cast<size_t>(free_end - free_start);
  if (free_space_treatment_mode == FreeSpaceTreatmentMode::kZapFreeSpace) {
    CodePageMemoryModificationScopeForDebugging memory_modification_scope(page);
    AtomicZapBlock(free_start, size);
  }
  freed_bytes = reinterpret_cast<PagedSpaceBase*>(space)->FreeDuringSweep(
      free_start, size);
#if !V8_OS_WIN
  // Discarding memory on Windows does not decommit the memory and does not
  // contribute to reduce the memory footprint. On the other hand, these calls
  // become expensive the more memory is allocated in the system and can result
  // in hangs. Thus, it is better to not discard on Windows.
  if (should_reduce_memory) page->DiscardUnusedMemory(free_start, size);
#endif  // !V8_OS_WIN

  if (v8_flags.sticky_mark_bits) {
    // Clear the bitmap, since fillers or slack may still be marked from black
    // allocation.
    page->marking_bitmap()->ClearRange<AccessMode::NON_ATOMIC>(
        MarkingBitmap::AddressToIndex(free_start),
        MarkingBitmap::AddressToIndex(free_end));
  }

  return freed_bytes;
}

V8_INLINE void Sweeper::CleanupRememberedSetEntriesForFreedMemory(
    Address free_start, Address free_end, PageMetadata* page,
    bool record_free_ranges, TypedSlotSet::FreeRangesMap* free_ranges_map,
    SweepingMode sweeping_mode) {
  DCHECK_LE(free_start, free_end);
  if (sweeping_mode == SweepingMode::kEagerDuringGC) {
    // New space and in consequence the old-to-new remembered set is always
    // empty after a full GC, so we do not need to remove from it after the full
    // GC. However, we wouldn't even be allowed to do that, since the main
    // thread then owns the old-to-new remembered set. Removing from it from a
    // sweeper thread would race with the main thread.
    RememberedSet<OLD_TO_NEW>::RemoveRange(page, free_start, free_end,
                                           SlotSet::KEEP_EMPTY_BUCKETS);
    RememberedSet<OLD_TO_NEW_BACKGROUND>::RemoveRange(
        page, free_start, free_end, SlotSet::KEEP_EMPTY_BUCKETS);

    // While we only add old-to-old slots on live objects, we can still end up
    // with old-to-old slots in free memory with e.g. right-trimming of objects.
    RememberedSet<OLD_TO_OLD>::RemoveRange(page, free_start, free_end,
                                           SlotSet::KEEP_EMPTY_BUCKETS);
    RememberedSet<TRUSTED_TO_TRUSTED>::RemoveRange(page, free_start, free_end,
                                                   SlotSet::KEEP_EMPTY_BUCKETS);
  } else {
    DCHECK_NULL(page->slot_set<OLD_TO_OLD>());
    DCHECK_NULL(page->slot_set<TRUSTED_TO_TRUSTED>());
  }

  // Old-to-shared isn't reset after a full GC, so needs to be cleaned both
  // during and after a full GC.
  RememberedSet<OLD_TO_SHARED>::RemoveRange(page, free_start, free_end,
                                            SlotSet::KEEP_EMPTY_BUCKETS);
  RememberedSet<TRUSTED_TO_SHARED_TRUSTED>::RemoveRange(
      page, free_start, free_end, SlotSet::KEEP_EMPTY_BUCKETS);

  if (record_free_ranges) {
    MemoryChunk* chunk = page->Chunk();
    free_ranges_map->insert(std::pair<uint32_t, uint32_t>(
        static_cast<uint32_t>(chunk->Offset(free_start)),
        static_cast<uint32_t>(chunk->Offset(free_end))));
  }
}

void Sweeper::CleanupTypedSlotsInFreeMemory(
    PageMetadata* page, const TypedSlotSet::FreeRangesMap& free_ranges_map,
    SweepingMode sweeping_mode) {
  // No support for typed trusted-to-shared-trusted pointers.
  DCHECK_NULL(page->typed_slot_set<TRUSTED_TO_SHARED_TRUSTED>());

  if (sweeping_mode == SweepingMode::kEagerDuringGC) {
    page->ClearTypedSlotsInFreeMemory<OLD_TO_NEW>(free_ranges_map);

    // Typed old-to-old slot sets are only ever recorded in live code objects.
    // Also code objects are never right-trimmed, so there cannot be any slots
    // in a free range.
    page->AssertNoTypedSlotsInFreeMemory<OLD_TO_OLD>(free_ranges_map);
    page->ClearTypedSlotsInFreeMemory<OLD_TO_SHARED>(free_ranges_map);
    return;
  }

  DCHECK_EQ(sweeping_mode, SweepingMode::kLazyOrConcurrent);

  // After a full GC there are no old-to-new typed slots. The main thread
  // could create new slots but not in a free range.
  page->AssertNoTypedSlotsInFreeMemory<OLD_TO_NEW>(free_ranges_map);
  DCHECK_NULL(page->typed_slot_set<OLD_TO_OLD>());
  page->ClearTypedSlotsInFreeMemory<OLD_TO_SHARED>(free_ranges_map);
}

void Sweeper::ClearMarkBitsAndHandleLivenessStatistics(PageMetadata* page,
                                                       size_t live_bytes) {
  if (!v8_flags.sticky_mark_bits) {
    page->marking_bitmap()->Clear<AccessMode::NON_ATOMIC>();
  }
  // Keep the old live bytes counter of the page until RefillFreeList, where
  // the space size is refined.
  // The allocated_bytes() counter is precisely the total size of objects.
  DCHECK_EQ(live_bytes, page->allocated_bytes());
}

void Sweeper::RawSweep(PageMetadata* p,
                       FreeSpaceTreatmentMode free_space_treatment_mode,
                       SweepingMode sweeping_mode, bool should_reduce_memory) {
  Space* space = p->owner();
  DCHECK_NOT_NULL(space);
  DCHECK(space->identity() == OLD_SPACE || space->identity() == CODE_SPACE ||
         space->identity() == SHARED_SPACE ||
         space->identity() == TRUSTED_SPACE ||
         space->identity() == SHARED_TRUSTED_SPACE ||
         (space->identity() == NEW_SPACE && v8_flags.minor_ms));
  DCHECK(!p->Chunk()->IsEvacuationCandidate());
  DCHECK(!p->SweepingDone());
  DCHECK_IMPLIES(space->identity() == NEW_SPACE,
                 !heap_->incremental_marking()->IsMinorMarking());
  DCHECK_IMPLIES(space->identity() != NEW_SPACE,
                 !heap_->incremental_marking()->IsMajorMarking());

  // Phase 1: Prepare the page for sweeping.

  std::optional<ActiveSystemPages> active_system_pages_after_sweeping;
  if (should_reduce_memory) {
    // Only decrement counter when we discard unused system pages.
    active_system_pages_after_sweeping = ActiveSystemPages();
    active_system_pages_after_sweeping->Init(
        MemoryChunkLayout::kMemoryChunkHeaderSize,
        MemoryAllocator::GetCommitPageSizeBits(), PageMetadata::kPageSize);
  }

  // Phase 2: Free the non-live memory and clean-up the regular remembered set
  // entires.

  // Liveness and freeing statistics.
  size_t live_bytes = 0;

  // Promoted pages have no interesting remebered sets yet.
  bool record_free_ranges = (p->typed_slot_set<OLD_TO_NEW>() != nullptr ||
                             p->typed_slot_set<OLD_TO_OLD>() != nullptr ||
                             p->typed_slot_set<OLD_TO_SHARED>() != nullptr) ||
                            DEBUG_BOOL;

  // The free ranges map is used for filtering typed slots.
  TypedSlotSet::FreeRangesMap free_ranges_map;

  // Iterate over the page using the live objects and free the memory before
  // the given live object.
  Address free_start = p->area_start();
  for (auto [object, size] : LiveObjectRange(p)) {
    DCHECK(marking_state_->IsMarked(object));
    Address free_end = object.address();
    if (free_end != free_start) {
      FreeAndProcessFreedMemory(free_start, free_end, p, space,
                                free_space_treatment_mode,
                                should_reduce_memory);
      CleanupRememberedSetEntriesForFreedMemory(
          free_start, free_end, p, record_free_ranges, &free_ranges_map,
          sweeping_mode);
    }
    live_bytes += size;
    free_start = free_end + size;

    if (active_system_pages_after_sweeping) {
      MemoryChunk* chunk = p->Chunk();
      active_system_pages_after_sweeping->Add(
          chunk->Offset(free_end), chunk->Offset(free_start),
          MemoryAllocator::GetCommitPageSizeBits());
    }
  }

  // If there is free memory after the last live object also free that.
  Address free_end = p->area_end();
  if (free_end != free_start) {
    FreeAndProcessFreedMemory(free_start, free_end, p, space,
                              free_space_treatment_mode, should_reduce_memory);
    CleanupRememberedSetEntriesForFreedMemory(free_start, free_end, p,
                                              record_free_ranges,
                                              &free_ranges_map, sweeping_mode);
  }

  // Phase 3: Post process the page.
  p->ReleaseSlotSet(SURVIVOR_TO_EXTERNAL_POINTER);
  CleanupTypedSlotsInFreeMemory(p, free_ranges_map, sweeping_mode);
  ClearMarkBitsAndHandleLivenessStatistics(p, live_bytes);

  if (active_system_pages_after_sweeping) {
    // Decrement accounted memory for discarded memory.
    PagedSpaceBase* paged_space = static_cast<PagedSpaceBase*>(p->owner());
    paged_space->ReduceActiveSystemPages(p,
                                         *active_system_pages_after_sweeping);
  }
}

bool Sweeper::IsIteratingPromotedPages() const {
  return promoted_page_iteration_in_progress_.load(std::memory_order_acquire);
}

void Sweeper::ContributeAndWaitForPromotedPagesIteration() {
  main_thread_local_sweeper_.ContributeAndWaitForPromotedPagesIteration();
}

void Sweeper::NotifyPromotedPageIterationFinished(MutablePageMetadata* chunk) {
  if (++iterated_promoted_pages_count_ == promoted_pages_for_iteration_count_) {
    NotifyPromotedPagesIterationFinished();
  }
  chunk->set_concurrent_sweeping_state(
      PageMetadata::ConcurrentSweepingState::kDone);
  base::MutexGuard guard(&mutex_);
  cv_page_swept_.NotifyAll();
}

void Sweeper::NotifyPromotedPagesIterationFinished() {
  DCHECK_EQ(iterated_promoted_pages_count_,
            promoted_pages_for_iteration_count_);
  base::MutexGuard guard(&promoted_pages_iteration_notification_mutex_);
  promoted_page_iteration_in_progress_.store(false, std::memory_order_release);
  promoted_pages_iteration_notification_variable_.NotifyAll();
}

size_t Sweeper::ConcurrentMinorSweepingPageCount() {
  DCHECK(minor_sweeping_in_progress());
  base::MutexGuard guard(&mutex_);
  return sweeping_list_for_promoted_page_iteration_.size() +
         sweeping_list_[GetSweepSpaceIndex(NEW_SPACE)].size();
}

size_t Sweeper::ConcurrentMajorSweepingPageCount() {
  DCHECK(major_sweeping_in_progress());
  base::MutexGuard guard(&mutex_);
  size_t count = 0;
  for (int i = 0; i < kNumberOfSweepingSpaces; i++) {
    if (i == GetSweepSpaceIndex(NEW_SPACE)) continue;
    count += sweeping_list_[i].size();
  }
  return count;
}

bool Sweeper::ParallelSweepSpace(AllocationSpace identity,
                                 SweepingMode sweeping_mode,
                                 uint32_t max_pages) {
  DCHECK_IMPLIES(identity == NEW_SPACE, heap_->IsMainThread());
  return main_thread_local_sweeper_.ParallelSweepSpace(identity, sweeping_mode,
                                                       max_pages);
}

void Sweeper::EnsurePageIsSwept(PageMetadata* page) {
  DCHECK(heap_->IsMainThread());

  auto concurrent_sweeping_state = page->concurrent_sweeping_state();
  DCHECK_IMPLIES(!sweeping_in_progress(),
                 concurrent_sweeping_state ==
                     PageMetadata::ConcurrentSweepingState::kDone);
  if (concurrent_sweeping_state ==
      PageMetadata::ConcurrentSweepingState::kDone) {
    DCHECK(page->SweepingDone());
    return;
  }

  AllocationSpace space = page->owner_identity();
  DCHECK(IsValidSweepingSpace(space));

  auto scope_id = GetTracingScope(space, true);
  TRACE_GC_EPOCH_WITH_FLOW(
      heap_->tracer(), scope_id, ThreadKind::kMain,
      GetTraceIdForFlowEvent(scope_id),
      TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
  if ((concurrent_sweeping_state ==
       PageMetadata::ConcurrentSweepingState::kPendingSweeping) &&
      TryRemoveSweepingPageSafe(space, page)) {
    // Page was successfully removed and can now be swept.
    main_thread_local_sweeper_.ParallelSweepPage(
        page, space, SweepingMode::kLazyOrConcurrent);

  } else if ((concurrent_sweeping_state ==
              PageMetadata::ConcurrentSweepingState::kPendingIteration) &&
             TryRemovePromotedPageSafe(page)) {
    // Page was successfully removed and can now be iterated.
    main_thread_local_sweeper_.ParallelIteratePromotedPage(page);
  } else {
    // Some sweeper task already took ownership of that page, wait until
    // sweeping is finished.
    WaitForPageToBeSwept(page);
  }

  CHECK(page->SweepingDone());
}

void Sweeper::WaitForPageToBeSwept(PageMetadata* page) {
  DCHECK(heap_->IsMainThread());
  DCHECK(sweeping_in_progress());

  base::MutexGuard guard(&mutex_);
  while (!page->SweepingDone()) {
    cv_page_swept_.Wait(&mutex_);
  }
}

bool Sweeper::TryRemoveSweepingPageSafe(AllocationSpace space,
                                        PageMetadata* page) {
  base::MutexGuard guard(&mutex_);
  DCHECK(IsValidSweepingSpace(space));
  int space_index = GetSweepSpaceIndex(space);
  SweepingList& sweeping_list = sweeping_list_[space_index];
  SweepingList::iterator position =
      std::find(sweeping_list.begin(), sweeping_list.end(), page);
  if (position == sweeping_list.end()) return false;
  sweeping_list.erase(position);
  if (sweeping_list.empty()) {
    has_sweeping_work_[GetSweepSpaceIndex(space)].store(
        false, std::memory_order_release);
  }
  return true;
}

bool Sweeper::TryRemovePromotedPageSafe(MutablePageMetadata* chunk) {
  base::MutexGuard guard(&mutex_);
  auto position =
      std::find(sweeping_list_for_promoted_page_iteration_.begin(),
                sweeping_list_for_promoted_page_iteration_.end(), chunk);
  if (position == sweeping_list_for_promoted_page_iteration_.end())
    return false;
  sweeping_list_for_promoted_page_iteration_.erase(position);
  return true;
}

void Sweeper::AddPage(AllocationSpace space, PageMetadata* page) {
  DCHECK_NE(NEW_SPACE, space);
  AddPageImpl(space, page);
}

void Sweeper::AddNewSpacePage(PageMetadata* page) {
  DCHECK_EQ(NEW_SPACE, page->owner_identity());
  DCHECK_LE(page->AgeInNewSpace(), v8_flags.minor_ms_max_page_age);
  size_t live_bytes = page->live_bytes();
  heap_->IncrementNewSpaceSurvivingObjectSize(live_bytes);
  heap_->IncrementYoungSurvivorsCounter(live_bytes);
  AddPageImpl(NEW_SPACE, page);
  page->IncrementAgeInNewSpace();
}

void Sweeper::AddPageImpl(AllocationSpace space, PageMetadata* page) {
  DCHECK(heap_->IsMainThread());
  DCHECK(IsValidSweepingSpace(space));
  DCHECK_IMPLIES(v8_flags.concurrent_sweeping && (space != NEW_SPACE),
                 !major_sweeping_state_.HasValidJob());
  DCHECK_IMPLIES(v8_flags.concurrent_sweeping,
                 !minor_sweeping_state_.HasValidJob());
  PrepareToBeSweptPage(space, page);
  DCHECK_EQ(PageMetadata::ConcurrentSweepingState::kPendingSweeping,
            page->concurrent_sweeping_state());
  sweeping_list_[GetSweepSpaceIndex(space)].push_back(page);
  has_sweeping_work_[GetSweepSpaceIndex(space)].store(
      true, std::memory_order_release);
}

void Sweeper::AddPromotedPage(MutablePageMetadata* chunk) {
  DCHECK(heap_->IsMainThread());
  DCHECK(chunk->owner_identity() == OLD_SPACE ||
         chunk->owner_identity() == LO_SPACE);
  DCHECK_IMPLIES(v8_flags.concurrent_sweeping,
                 !minor_sweeping_state_.HasValidJob());
  size_t live_bytes = chunk->live_bytes();
  DCHECK_GE(chunk->area_size(), live_bytes);
  heap_->IncrementPromotedObjectsSize(live_bytes);
  heap_->IncrementYoungSurvivorsCounter(live_bytes);
  DCHECK_EQ(PageMetadata::ConcurrentSweepingState::kDone,
            chunk->concurrent_sweeping_state());
  if (!chunk->Chunk()->IsLargePage()) {
    PrepareToBeIteratedPromotedPage(static_cast<PageMetadata*>(chunk));
  } else {
    chunk->set_concurrent_sweeping_state(
        PageMetadata::ConcurrentSweepingState::kPendingIteration);
  }
  DCHECK_EQ(PageMetadata::ConcurrentSweepingState::kPendingIteration,
            chunk->concurrent_sweeping_state());
  // This method is called only from the main thread while sweeping tasks have
  // not yet started, thus a mutex is not needed.
  sweeping_list_for_promoted_page_iteration_.push_back(chunk);
  promoted_pages_for_iteration_count_++;
}

namespace {
void VerifyPreparedPage(PageMetadata* page) {
#ifdef DEBUG
  DCHECK_GE(page->area_size(), static_cast<size_t>(page->live_bytes()));
  DCHECK_EQ(PageMetadata::ConcurrentSweepingState::kDone,
            page->concurrent_sweeping_state());
  page->ForAllFreeListCategories([page](FreeListCategory* category) {
    DCHECK(!category->is_linked(page->owner()->free_list()));
  });
#endif  // DEBUG
}
}  // namespace

void Sweeper::PrepareToBeSweptPage(AllocationSpace space, PageMetadata* page) {
  VerifyPreparedPage(page);
  page->set_concurrent_sweeping_state(
      PageMetadata::ConcurrentSweepingState::kPendingSweeping);
  PagedSpaceBase* paged_space;
  if (space == NEW_SPACE) {
    DCHECK(v8_flags.minor_ms);
    paged_space = heap_->paged_new_space()->paged_space();
  } else {
    paged_space = heap_->paged_space(space);
  }
  paged_space->IncreaseAllocatedBytes(page->live_bytes(), page);

  // Set the allocated_bytes_ counter to area_size and clear the wasted_memory_
  // counter. The free operations during sweeping will decrease allocated_bytes_
  // to actual live bytes and keep track of wasted_memory_.
  page->ResetAllocationStatistics();
}

void Sweeper::PrepareToBeIteratedPromotedPage(PageMetadata* page) {
  DCHECK_EQ(OLD_SPACE, page->owner_identity());
  VerifyPreparedPage(page);
  page->set_concurrent_sweeping_state(
      PageMetadata::ConcurrentSweepingState::kPendingIteration);
  // Account the whole page as allocated since it won't be in the free list.
  // TODO(v8:12612): Consider accounting for wasted bytes when checking old gen
  // size against old gen allocation limit, and treat previously unallocated
  // memory as wasted rather than allocated.
  page->ResetAllocationStatisticsForPromotedPage();
  PagedSpace* space = static_cast<PagedSpace*>(page->owner());
  space->IncreaseAllocatedBytes(page->allocated_bytes(), page);
  space->free_list()->increase_wasted_bytes(page->wasted_memory());
}

PageMetadata* Sweeper::GetSweepingPageSafe(AllocationSpace space) {
  base::MutexGuard guard(&mutex_);
  DCHECK(IsValidSweepingSpace(space));
  int space_index = GetSweepSpaceIndex(space);
  PageMetadata* page = nullptr;
  SweepingList& sweeping_list = sweeping_list_[space_index];
  if (!sweeping_list.empty()) {
    page = sweeping_list.back();
    sweeping_list.pop_back();
  }
  if (sweeping_list.empty()) {
    has_sweeping_work_[GetSweepSpaceIndex(space)].store(
        false, std::memory_order_release);
  }
  return page;
}

MutablePageMetadata* Sweeper::GetPromotedPageSafe() {
  base::MutexGuard guard(&mutex_);
  MutablePageMetadata* chunk = nullptr;
  if (!sweeping_list_for_promoted_page_iteration_.empty()) {
    chunk = sweeping_list_for_promoted_page_iteration_.back();
    sweeping_list_for_promoted_page_iteration_.pop_back();
  }
  return chunk;
}

GCTracer::Scope::ScopeId Sweeper::GetTracingScope(AllocationSpace space,
                                                  bool is_joining_thread) {
  if (space == NEW_SPACE) {
    return is_joining_thread ? GCTracer::Scope::MINOR_MS_SWEEP
                             : GCTracer::Scope::MINOR_MS_BACKGROUND_SWEEPING;
  }
  return is_joining_thread ? GCTracer::Scope::MC_SWEEP
                           : GCTracer::Scope::MC_BACKGROUND_SWEEPING;
}

bool Sweeper::IsSweepingDoneForSpace(AllocationSpace space) const {
  return !has_sweeping_work_[GetSweepSpaceIndex(space)].load(
      std::memory_order_acquire);
}

void Sweeper::AddSweptPage(PageMetadata* page, AllocationSpace identity) {
  base::MutexGuard guard(&mutex_);
  page->set_concurrent_sweeping_state(
      PageMetadata::ConcurrentSweepingState::kDone);
  swept_list_[GetSweepSpaceIndex(identity)].push_back(page);
  has_swept_pages_[GetSweepSpaceIndex(identity)].store(
      true, std::memory_order_release);
  cv_page_swept_.NotifyAll();
}

bool Sweeper::ShouldRefillFreelistForSpace(AllocationSpace space) const {
  DCHECK_IMPLIES(space == NEW_SPACE, v8_flags.minor_ms);
  return has_swept_pages_[GetSweepSpaceIndex(space)].load(
      std::memory_order_acquire);
}

void Sweeper::SweepEmptyNewSpacePage(PageMetadata* page) {
  DCHECK(v8_flags.minor_ms);
  DCHECK_EQ(kNewSpace, page->owner_identity());
  DCHECK_EQ(0, page->live_bytes());
  DCHECK(page->marking_bitmap()->IsClean());
  DCHECK(heap_->IsMainThread());
  DCHECK(heap_->tracer()->IsInAtomicPause());
  DCHECK_EQ(PageMetadata::ConcurrentSweepingState::kDone,
            page->concurrent_sweeping_state());

  PagedSpaceBase* paged_space = nullptr;
  if (v8_flags.sticky_mark_bits) {
    paged_space = heap_->sticky_space();
  } else {
    paged_space = PagedNewSpace::From(heap_->new_space())->paged_space();
  }

  Address start = page->area_start();
  size_t size = page->area_size();

  if (heap::ShouldZapGarbage()) {
    static constexpr Tagged_t kZapTagged = static_cast<Tagged_t>(kZapValue);
    const size_t size_in_tagged = size / kTaggedSize;
    Tagged_t* current_addr = reinterpret_cast<Tagged_t*>(start);
    for (size_t i = 0; i < size_in_tagged; ++i) {
      base::AsAtomicPtr(current_addr++)
          ->store(kZapTagged, std::memory_order_relaxed);
    }
  }

  page->ResetAllocationStatistics();
  page->ResetAgeInNewSpace();
  page->ReleaseSlotSet(SURVIVOR_TO_EXTERNAL_POINTER);
  page->Chunk()->ClearFlagNonExecutable(MemoryChunk::NEVER_ALLOCATE_ON_PAGE);
  paged_space->FreeDuringSweep(start, size);
  paged_space->IncreaseAllocatedBytes(0, page);
  paged_space->RelinkFreeListCategories(page);

  if (heap_->ShouldReduceMemory()) {
    page->DiscardUnusedMemory(start, size);
    // Only decrement counter when we discard unused system pages.
    ActiveSystemPages active_system_pages_after_sweeping;
    active_system_pages_after_sweeping.Init(
        MemoryChunkLayout::kMemoryChunkHeaderSize,
        MemoryAllocator::GetCommitPageSizeBits(), PageMetadata::kPageSize);
    // Decrement accounted memory for discarded memory.
    paged_space->ReduceActiveSystemPages(page,
                                         active_system_pages_after_sweeping);
  }
}

Sweeper::PauseMajorSweepingScope::PauseMajorSweepingScope(Sweeper* sweeper)
    : sweeper_(sweeper),
      resume_on_exit_(sweeper->AreMajorSweeperTasksRunning()) {
  DCHECK(v8_flags.minor_ms);
  DCHECK_IMPLIES(resume_on_exit_, v8_flags.concurrent_sweeping);
  sweeper_->major_sweeping_state_.Pause();
}

Sweeper::PauseMajorSweepingScope::~PauseMajorSweepingScope() {
  if (resume_on_exit_) {
    sweeper_->major_sweeping_state_.Resume();
  }
}

uint64_t Sweeper::GetTraceIdForFlowEvent(
    GCTracer::Scope::ScopeId scope_id) const {
  return GCTracer::Scope::NeedsYoungEpoch(scope_id)
             ? minor_sweeping_state_.trace_id()
             : major_sweeping_state_.trace_id();
}

#if DEBUG
bool Sweeper::HasUnsweptPagesForMajorSweeping() const {
  DCHECK(heap_->IsMainThread());
  DCHECK(!AreMajorSweeperTasksRunning());
  bool has_unswept_pages = false;
  ForAllSweepingSpaces([this, &has_unswept_pages](AllocationSpace space) {
    DCHECK_EQ(IsSweepingDoneForSpace(space),
              sweeping_list_[GetSweepSpaceIndex(space)].empty());
    if (space == NEW_SPACE) return;
    if (!sweeping_list_[GetSweepSpaceIndex(space)].empty())
      has_unswept_pages = true;
  });
  return has_unswept_pages;
}
#endif  // DEBUG

}  // namespace internal
}  // namespace v8
