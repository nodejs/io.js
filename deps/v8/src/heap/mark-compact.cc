// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/mark-compact.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/base/utils/random-number-generator.h"
#include "src/codegen/compilation-cache.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/execution.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/execution/isolate-utils.h"
#include "src/execution/vm-state-inl.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles.h"
#include "src/heap/array-buffer-sweeper.h"
#include "src/heap/base/basic-slot-set.h"
#include "src/heap/concurrent-marking.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/evacuation-allocator-inl.h"
#include "src/heap/evacuation-verifier-inl.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking-inl.h"
#include "src/heap/index-generator.h"
#include "src/heap/large-spaces.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/mark-sweep-utilities.h"
#include "src/heap/marking-barrier.h"
#include "src/heap/marking-inl.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/marking-visitor-inl.h"
#include "src/heap/marking.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/memory-measurement-inl.h"
#include "src/heap/memory-measurement.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/new-spaces.h"
#include "src/heap/object-stats.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/page-metadata-inl.h"
#include "src/heap/page-metadata.h"
#include "src/heap/parallel-work-item.h"
#include "src/heap/pretenuring-handler-inl.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/read-only-spaces.h"
#include "src/heap/remembered-set.h"
#include "src/heap/safepoint.h"
#include "src/heap/slot-set.h"
#include "src/heap/spaces-inl.h"
#include "src/heap/sweeper.h"
#include "src/heap/traced-handles-marking-visitor.h"
#include "src/heap/weak-object-worklists.h"
#include "src/heap/zapping.h"
#include "src/init/v8.h"
#include "src/logging/tracing-flags.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/foreign.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/maybe-object.h"
#include "src/objects/objects.h"
#include "src/objects/slots-inl.h"
#include "src/objects/smi.h"
#include "src/objects/string-forwarding-table-inl.h"
#include "src/objects/transitions-inl.h"
#include "src/objects/visitors.h"
#include "src/snapshot/shared-heap-serializer.h"
#include "src/tasks/cancelable-task.h"
#include "src/tracing/tracing-category-observer.h"
#include "src/utils/utils-inl.h"

namespace v8 {
namespace internal {

// =============================================================================
// Verifiers
// =============================================================================

#ifdef VERIFY_HEAP
namespace {

class FullMarkingVerifier : public MarkingVerifierBase {
 public:
  explicit FullMarkingVerifier(Heap* heap)
      : MarkingVerifierBase(heap),
        marking_state_(heap->non_atomic_marking_state()) {}

  void Run() override {
    VerifyRoots();
    VerifyMarking(heap_->new_space());
    VerifyMarking(heap_->new_lo_space());
    VerifyMarking(heap_->old_space());
    VerifyMarking(heap_->code_space());
    if (heap_->shared_space()) VerifyMarking(heap_->shared_space());
    VerifyMarking(heap_->lo_space());
    VerifyMarking(heap_->code_lo_space());
    if (heap_->shared_lo_space()) VerifyMarking(heap_->shared_lo_space());
    VerifyMarking(heap_->trusted_space());
    VerifyMarking(heap_->trusted_lo_space());
  }

 protected:
  const MarkingBitmap* bitmap(const MutablePageMetadata* chunk) override {
    return chunk->marking_bitmap();
  }

  bool IsMarked(Tagged<HeapObject> object) override {
    return marking_state_->IsMarked(object);
  }

  void VerifyMap(Tagged<Map> map) override { VerifyHeapObjectImpl(map); }

  void VerifyPointers(ObjectSlot start, ObjectSlot end) override {
    VerifyPointersImpl(start, end);
  }

  void VerifyPointers(MaybeObjectSlot start, MaybeObjectSlot end) override {
    VerifyPointersImpl(start, end);
  }

  void VerifyCodePointer(InstructionStreamSlot slot) override {
    Tagged<Object> maybe_code = slot.load(code_cage_base());
    Tagged<HeapObject> code;
    // The slot might contain smi during Code creation, so skip it.
    if (maybe_code.GetHeapObject(&code)) {
      VerifyHeapObjectImpl(code);
    }
  }

  void VerifyRootPointers(FullObjectSlot start, FullObjectSlot end) override {
    VerifyPointersImpl(start, end);
  }

  void VisitCodeTarget(Tagged<InstructionStream> host,
                       RelocInfo* rinfo) override {
    Tagged<InstructionStream> target =
        InstructionStream::FromTargetAddress(rinfo->target_address());
    VerifyHeapObjectImpl(target);
  }

  void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                            RelocInfo* rinfo) override {
    DCHECK(RelocInfo::IsEmbeddedObjectMode(rinfo->rmode()));
    Tagged<HeapObject> target_object = rinfo->target_object(cage_base());
    Tagged<Code> code = UncheckedCast<Code>(host->raw_code(kAcquireLoad));
    if (!code->IsWeakObject(target_object)) {
      VerifyHeapObjectImpl(target_object);
    }
  }

 private:
  V8_INLINE void VerifyHeapObjectImpl(Tagged<HeapObject> heap_object) {
    if (!ShouldVerifyObject(heap_object)) return;

    if (heap_->MustBeInSharedOldSpace(heap_object)) {
      CHECK(heap_->SharedHeapContains(heap_object));
    }

    CHECK(InReadOnlySpace(heap_object) ||
          marking_state_->IsMarked(heap_object));
  }

  V8_INLINE bool ShouldVerifyObject(Tagged<HeapObject> heap_object) {
    const bool in_shared_heap = InWritableSharedSpace(heap_object);
    return heap_->isolate()->is_shared_space_isolate() ? true : !in_shared_heap;
  }

  template <typename TSlot>
  V8_INLINE void VerifyPointersImpl(TSlot start, TSlot end) {
    PtrComprCageBase cage_base =
        GetPtrComprCageBaseFromOnHeapAddress(start.address());
    for (TSlot slot = start; slot < end; ++slot) {
      typename TSlot::TObject object = slot.load(cage_base);
#ifdef V8_ENABLE_DIRECT_HANDLE
      if (object.ptr() == kTaggedNullAddress) continue;
#endif
      Tagged<HeapObject> heap_object;
      if (object.GetHeapObjectIfStrong(&heap_object)) {
        VerifyHeapObjectImpl(heap_object);
      }
    }
  }

  NonAtomicMarkingState* const marking_state_;
};

}  // namespace
#endif  // VERIFY_HEAP

// ==================================================================
// MarkCompactCollector
// ==================================================================

namespace {

int NumberOfAvailableCores() {
  static int num_cores = V8::GetCurrentPlatform()->NumberOfWorkerThreads() + 1;
  // This number of cores should be greater than zero and never change.
  DCHECK_GE(num_cores, 1);
  DCHECK_EQ(num_cores, V8::GetCurrentPlatform()->NumberOfWorkerThreads() + 1);
  return num_cores;
}

int NumberOfParallelCompactionTasks(Heap* heap) {
  int tasks = v8_flags.parallel_compaction ? NumberOfAvailableCores() : 1;
  if (!heap->CanPromoteYoungAndExpandOldGeneration(
          static_cast<size_t>(tasks * PageMetadata::kPageSize))) {
    // Optimize for memory usage near the heap limit.
    tasks = 1;
  }
  return tasks;
}

}  // namespace

// This visitor is used for marking on the main thread. It is cheaper than
// the concurrent marking visitor because it does not snapshot JSObjects.
class MainMarkingVisitor final
    : public FullMarkingVisitorBase<MainMarkingVisitor> {
 public:
  MainMarkingVisitor(MarkingWorklists::Local* local_marking_worklists,
                     WeakObjects::Local* local_weak_objects, Heap* heap,
                     unsigned mark_compact_epoch,
                     base::EnumSet<CodeFlushMode> code_flush_mode,
                     bool should_keep_ages_unchanged,
                     uint16_t code_flushing_increase)
      : FullMarkingVisitorBase<MainMarkingVisitor>(
            local_marking_worklists, local_weak_objects, heap,
            mark_compact_epoch, code_flush_mode, should_keep_ages_unchanged,
            code_flushing_increase) {}

 private:
  // Functions required by MarkingVisitorBase.

  template <typename TSlot>
  void RecordSlot(Tagged<HeapObject> object, TSlot slot,
                  Tagged<HeapObject> target) {
    MarkCompactCollector::RecordSlot(object, slot, target);
  }

  void RecordRelocSlot(Tagged<InstructionStream> host, RelocInfo* rinfo,
                       Tagged<HeapObject> target) {
    MarkCompactCollector::RecordRelocSlot(host, rinfo, target);
  }

  friend class MarkingVisitorBase<MainMarkingVisitor>;
};

MarkCompactCollector::MarkCompactCollector(Heap* heap)
    : heap_(heap),
#ifdef DEBUG
      state_(IDLE),
#endif
      uses_shared_heap_(heap_->isolate()->has_shared_space()),
      is_shared_space_isolate_(heap_->isolate()->is_shared_space_isolate()),
      marking_state_(heap_->marking_state()),
      non_atomic_marking_state_(heap_->non_atomic_marking_state()),
      sweeper_(heap_->sweeper()) {
}

MarkCompactCollector::~MarkCompactCollector() = default;

void MarkCompactCollector::TearDown() {
  if (heap_->incremental_marking()->IsMajorMarking()) {
    local_marking_worklists_->Publish();
    heap_->main_thread_local_heap_->marking_barrier()->PublishIfNeeded();
    // Marking barriers of LocalHeaps will be published in their destructors.
    marking_worklists_.Clear();
    local_weak_objects()->Publish();
    weak_objects()->Clear();
  }
}

void MarkCompactCollector::AddEvacuationCandidate(PageMetadata* p) {
  DCHECK(!p->Chunk()->NeverEvacuate());

  if (v8_flags.trace_evacuation_candidates) {
    PrintIsolate(
        heap_->isolate(),
        "Evacuation candidate: Free bytes: %6zu. Free Lists length: %4d.\n",
        p->area_size() - p->allocated_bytes(), p->ComputeFreeListsLength());
  }

  p->MarkEvacuationCandidate();
  evacuation_candidates_.push_back(p);
}

static void TraceFragmentation(PagedSpace* space) {
  int number_of_pages = space->CountTotalPages();
  intptr_t reserved = (number_of_pages * space->AreaSize());
  intptr_t free = reserved - space->SizeOfObjects();
  PrintF("[%s]: %d pages, %d (%.1f%%) free\n", ToString(space->identity()),
         number_of_pages, static_cast<int>(free),
         static_cast<double>(free) * 100 / reserved);
}

bool MarkCompactCollector::StartCompaction(StartCompactionMode mode) {
  DCHECK(!compacting_);
  DCHECK(evacuation_candidates_.empty());

  // Bailouts for completely disabled compaction.
  if (!v8_flags.compact ||
      (mode == StartCompactionMode::kAtomic && heap_->IsGCWithStack() &&
       !v8_flags.compact_with_stack) ||
      (v8_flags.gc_experiment_less_compaction &&
       !heap_->ShouldReduceMemory())) {
    return false;
  }

  CollectEvacuationCandidates(heap_->old_space());

  if (heap_->shared_space()) {
    CollectEvacuationCandidates(heap_->shared_space());
  }

  CollectEvacuationCandidates(heap_->trusted_space());

  if (heap_->isolate()->AllowsCodeCompaction() &&
      (!heap_->IsGCWithStack() || v8_flags.compact_code_space_with_stack)) {
    CollectEvacuationCandidates(heap_->code_space());
  } else if (v8_flags.trace_fragmentation) {
    TraceFragmentation(heap_->code_space());
  }

  compacting_ = !evacuation_candidates_.empty();
  return compacting_;
}

void MarkCompactCollector::StartMarking() {
  // The state for background thread is saved here and maintained for the whole
  // GC cycle. Both CppHeap and regular V8 heap will refer to this flag.
  use_background_threads_in_cycle_ = heap_->ShouldUseBackgroundThreads();

  if (v8_flags.sticky_mark_bits) {
    heap()->Unmark();
  }

#ifdef V8_COMPRESS_POINTERS
  heap_->young_external_pointer_space()->StartCompactingIfNeeded();
  heap_->old_external_pointer_space()->StartCompactingIfNeeded();
  heap_->cpp_heap_pointer_space()->StartCompactingIfNeeded();
#endif  // V8_COMPRESS_POINTERS

  // CppHeap's marker must be initialized before the V8 marker to allow
  // exchanging of worklists.
  if (heap_->cpp_heap()) {
    TRACE_GC(heap()->tracer(), GCTracer::Scope::MC_MARK_EMBEDDER_PROLOGUE);
    CppHeap::From(heap_->cpp_heap())
        ->InitializeMarking(CppHeap::CollectionType::kMajor);
  }

  std::vector<Address> contexts =
      heap_->memory_measurement()->StartProcessing();
  if (v8_flags.stress_per_context_marking_worklist) {
    contexts.clear();
    HandleScope handle_scope(heap_->isolate());
    for (auto context : heap_->FindAllNativeContexts()) {
      contexts.push_back(context->ptr());
    }
  }
  heap_->tracer()->NotifyMarkingStart();
  code_flush_mode_ = Heap::GetCodeFlushMode(heap_->isolate());
  marking_worklists_.CreateContextWorklists(contexts);
  auto* cpp_heap = CppHeap::From(heap_->cpp_heap_);
  local_marking_worklists_ = std::make_unique<MarkingWorklists::Local>(
      &marking_worklists_,
      cpp_heap ? cpp_heap->CreateCppMarkingStateForMutatorThread()
               : MarkingWorklists::Local::kNoCppMarkingState);
  local_weak_objects_ = std::make_unique<WeakObjects::Local>(weak_objects());
  marking_visitor_ = std::make_unique<MainMarkingVisitor>(
      local_marking_worklists_.get(), local_weak_objects_.get(), heap_, epoch(),
      code_flush_mode(), heap_->ShouldCurrentGCKeepAgesUnchanged(),
      heap_->tracer()->CodeFlushingIncrease());
  // This method evicts SFIs with flushed bytecode from the cache before
  // iterating the compilation cache as part of the root set. SFIs that get
  // flushed in this GC cycle will get evicted out of the cache in the next GC
  // cycle. The SFI will remain in the cache until then and may remain in the
  // cache even longer in case the SFI is re-compiled.
  heap_->isolate()->compilation_cache()->MarkCompactPrologue();
  // Marking bits are cleared by the sweeper or unmarker (if sticky mark-bits
  // are enabled).
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    VerifyMarkbitsAreClean();
  }
#endif  // VERIFY_HEAP
}

void MarkCompactCollector::MaybeEnableBackgroundThreadsInCycle(
    CallOrigin origin) {
  if (v8_flags.concurrent_marking && !use_background_threads_in_cycle_) {
    // With --parallel_pause_for_gc_in_background we force background threads in
    // the atomic pause.
    const bool force_background_threads =
        v8_flags.parallel_pause_for_gc_in_background &&
        origin == CallOrigin::kAtomicGC;
    use_background_threads_in_cycle_ =
        force_background_threads || heap()->ShouldUseBackgroundThreads();

    if (use_background_threads_in_cycle_) {
      heap_->concurrent_marking()->RescheduleJobIfNeeded(
          GarbageCollector::MARK_COMPACTOR);

      if (auto* cpp_heap = CppHeap::From(heap_->cpp_heap_)) {
        cpp_heap->ReEnableConcurrentMarking();
      }
    }
  }
}

void MarkCompactCollector::CollectGarbage() {
  // Make sure that Prepare() has been called. The individual steps below will
  // update the state as they proceed.
  DCHECK(state_ == PREPARE_GC);

  MaybeEnableBackgroundThreadsInCycle(CallOrigin::kAtomicGC);

  MarkLiveObjects();
  // This will walk dead object graphs and so requires that all references are
  // still intact.
  RecordObjectStats();
  ClearNonLiveReferences();
  VerifyMarking();

  if (auto* cpp_heap = CppHeap::From(heap_->cpp_heap_)) {
    cpp_heap->FinishMarkingAndProcessWeakness();
  }

  heap_->memory_measurement()->FinishProcessing(native_context_stats_);

  Sweep();
  Evacuate();
  Finish();
}

#ifdef VERIFY_HEAP

void MarkCompactCollector::VerifyMarkbitsAreClean(PagedSpaceBase* space) {
  for (PageMetadata* p : *space) {
    CHECK(p->marking_bitmap()->IsClean());
    CHECK_EQ(0, p->live_bytes());
  }
}

void MarkCompactCollector::VerifyMarkbitsAreClean(NewSpace* space) {
  if (!space) return;
  if (v8_flags.minor_ms) {
    VerifyMarkbitsAreClean(PagedNewSpace::From(space)->paged_space());
    return;
  }
  for (PageMetadata* p : *space) {
    CHECK(p->marking_bitmap()->IsClean());
    CHECK_EQ(0, p->live_bytes());
  }
}

void MarkCompactCollector::VerifyMarkbitsAreClean(LargeObjectSpace* space) {
  if (!space) return;
  LargeObjectSpaceObjectIterator it(space);
  for (Tagged<HeapObject> obj = it.Next(); !obj.is_null(); obj = it.Next()) {
    CHECK(non_atomic_marking_state_->IsUnmarked(obj));
    CHECK_EQ(0, MutablePageMetadata::FromHeapObject(obj)->live_bytes());
  }
}

void MarkCompactCollector::VerifyMarkbitsAreClean() {
  VerifyMarkbitsAreClean(heap_->old_space());
  VerifyMarkbitsAreClean(heap_->code_space());
  VerifyMarkbitsAreClean(heap_->new_space());
  VerifyMarkbitsAreClean(heap_->lo_space());
  VerifyMarkbitsAreClean(heap_->code_lo_space());
  VerifyMarkbitsAreClean(heap_->new_lo_space());
  VerifyMarkbitsAreClean(heap_->trusted_space());
  VerifyMarkbitsAreClean(heap_->trusted_lo_space());
}

#endif  // VERIFY_HEAP

void MarkCompactCollector::ComputeEvacuationHeuristics(
    size_t area_size, int* target_fragmentation_percent,
    size_t* max_evacuated_bytes) {
  // For memory reducing and optimize for memory mode we directly define both
  // constants.
  const int kTargetFragmentationPercentForReduceMemory = 20;
  const size_t kMaxEvacuatedBytesForReduceMemory = 12 * MB;
  const int kTargetFragmentationPercentForOptimizeMemory = 20;
  const size_t kMaxEvacuatedBytesForOptimizeMemory = 6 * MB;

  // For regular mode (which is latency critical) we define less aggressive
  // defaults to start and switch to a trace-based (using compaction speed)
  // approach as soon as we have enough samples.
  const int kTargetFragmentationPercent = 70;
  const size_t kMaxEvacuatedBytes = 4 * MB;
  // Time to take for a single area (=payload of page). Used as soon as there
  // exist enough compaction speed samples.
  const float kTargetMsPerArea = .5;

  if (heap_->ShouldReduceMemory()) {
    *target_fragmentation_percent = kTargetFragmentationPercentForReduceMemory;
    *max_evacuated_bytes = kMaxEvacuatedBytesForReduceMemory;
  } else if (heap_->ShouldOptimizeForMemoryUsage()) {
    *target_fragmentation_percent =
        kTargetFragmentationPercentForOptimizeMemory;
    *max_evacuated_bytes = kMaxEvacuatedBytesForOptimizeMemory;
  } else {
    const double estimated_compaction_speed =
        heap_->tracer()->CompactionSpeedInBytesPerMillisecond();
    if (estimated_compaction_speed != 0) {
      // Estimate the target fragmentation based on traced compaction speed
      // and a goal for a single page.
      const double estimated_ms_per_area =
          1 + area_size / estimated_compaction_speed;
      *target_fragmentation_percent = static_cast<int>(
          100 - 100 * kTargetMsPerArea / estimated_ms_per_area);
      if (*target_fragmentation_percent <
          kTargetFragmentationPercentForReduceMemory) {
        *target_fragmentation_percent =
            kTargetFragmentationPercentForReduceMemory;
      }
    } else {
      *target_fragmentation_percent = kTargetFragmentationPercent;
    }
    *max_evacuated_bytes = kMaxEvacuatedBytes;
  }
}

void MarkCompactCollector::CollectEvacuationCandidates(PagedSpace* space) {
  DCHECK(space->identity() == OLD_SPACE || space->identity() == CODE_SPACE ||
         space->identity() == SHARED_SPACE ||
         space->identity() == TRUSTED_SPACE);

  int number_of_pages = space->CountTotalPages();
  size_t area_size = space->AreaSize();

  const bool in_standard_path =
      !(v8_flags.manual_evacuation_candidates_selection ||
        v8_flags.stress_compaction_random || v8_flags.stress_compaction ||
        v8_flags.compact_on_every_full_gc);
  // Those variables will only be initialized if |in_standard_path|, and are not
  // used otherwise.
  size_t max_evacuated_bytes;
  int target_fragmentation_percent;
  size_t free_bytes_threshold;
  if (in_standard_path) {
    // We use two conditions to decide whether a page qualifies as an evacuation
    // candidate, or not:
    // * Target fragmentation: How fragmented is a page, i.e., how is the ratio
    //   between live bytes and capacity of this page (= area).
    // * Evacuation quota: A global quota determining how much bytes should be
    //   compacted.
    ComputeEvacuationHeuristics(area_size, &target_fragmentation_percent,
                                &max_evacuated_bytes);
    free_bytes_threshold = target_fragmentation_percent * (area_size / 100);
  }

  // Pairs of (live_bytes_in_page, page).
  using LiveBytesPagePair = std::pair<size_t, PageMetadata*>;
  std::vector<LiveBytesPagePair> pages;
  pages.reserve(number_of_pages);

  DCHECK(!sweeper_->sweeping_in_progress());
  for (PageMetadata* p : *space) {
    MemoryChunk* chunk = p->Chunk();
    if (chunk->NeverEvacuate() || !chunk->CanAllocate()) continue;

    if (chunk->IsPinned()) {
      DCHECK(!chunk->IsFlagSet(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING));
      continue;
    }

    // Invariant: Evacuation candidates are just created when marking is
    // started. This means that sweeping has finished. Furthermore, at the end
    // of a GC all evacuation candidates are cleared and their slot buffers are
    // released.
    CHECK(!chunk->IsEvacuationCandidate());
    CHECK_NULL(p->slot_set<OLD_TO_OLD>());
    CHECK_NULL(p->typed_slot_set<OLD_TO_OLD>());
    CHECK(p->SweepingDone());
    DCHECK(p->area_size() == area_size);
    if (in_standard_path) {
      // Only the pages with at more than |free_bytes_threshold| free bytes are
      // considered for evacuation.
      if (area_size - p->allocated_bytes() >= free_bytes_threshold) {
        pages.push_back(std::make_pair(p->allocated_bytes(), p));
      }
    } else {
      pages.push_back(std::make_pair(p->allocated_bytes(), p));
    }
  }

  int candidate_count = 0;
  size_t total_live_bytes = 0;

  const bool reduce_memory = heap_->ShouldReduceMemory();
  if (v8_flags.manual_evacuation_candidates_selection) {
    for (size_t i = 0; i < pages.size(); i++) {
      PageMetadata* p = pages[i].second;
      MemoryChunk* chunk = p->Chunk();
      if (chunk->IsFlagSet(
              MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING)) {
        candidate_count++;
        total_live_bytes += pages[i].first;
        chunk->ClearFlagSlow(
            MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
        AddEvacuationCandidate(p);
      }
    }
  } else if (v8_flags.stress_compaction_random) {
    double fraction = heap_->isolate()->fuzzer_rng()->NextDouble();
    size_t pages_to_mark_count =
        static_cast<size_t>(fraction * (pages.size() + 1));
    for (uint64_t i : heap_->isolate()->fuzzer_rng()->NextSample(
             pages.size(), pages_to_mark_count)) {
      candidate_count++;
      total_live_bytes += pages[i].first;
      AddEvacuationCandidate(pages[i].second);
    }
  } else if (v8_flags.stress_compaction) {
    for (size_t i = 0; i < pages.size(); i++) {
      PageMetadata* p = pages[i].second;
      if (i % 2 == 0) {
        candidate_count++;
        total_live_bytes += pages[i].first;
        AddEvacuationCandidate(p);
      }
    }
  } else {
    // The following approach determines the pages that should be evacuated.
    //
    // Sort pages from the most free to the least free, then select
    // the first n pages for evacuation such that:
    // - the total size of evacuated objects does not exceed the specified
    // limit.
    // - fragmentation of (n+1)-th page does not exceed the specified limit.
    std::sort(pages.begin(), pages.end(),
              [](const LiveBytesPagePair& a, const LiveBytesPagePair& b) {
                return a.first < b.first;
              });
    for (size_t i = 0; i < pages.size(); i++) {
      size_t live_bytes = pages[i].first;
      DCHECK_GE(area_size, live_bytes);
      if (v8_flags.compact_on_every_full_gc ||
          ((total_live_bytes + live_bytes) <= max_evacuated_bytes)) {
        candidate_count++;
        total_live_bytes += live_bytes;
      }
      if (v8_flags.trace_fragmentation_verbose) {
        PrintIsolate(heap_->isolate(),
                     "compaction-selection-page: space=%s free_bytes_page=%zu "
                     "fragmentation_limit_kb=%zu "
                     "fragmentation_limit_percent=%d sum_compaction_kb=%zu "
                     "compaction_limit_kb=%zu\n",
                     ToString(space->identity()), (area_size - live_bytes) / KB,
                     free_bytes_threshold / KB, target_fragmentation_percent,
                     total_live_bytes / KB, max_evacuated_bytes / KB);
      }
    }
    // How many pages we will allocated for the evacuated objects
    // in the worst case: ceil(total_live_bytes / area_size)
    int estimated_new_pages =
        static_cast<int>((total_live_bytes + area_size - 1) / area_size);
    DCHECK_LE(estimated_new_pages, candidate_count);
    int estimated_released_pages = candidate_count - estimated_new_pages;
    // Avoid (compact -> expand) cycles.
    if ((estimated_released_pages == 0) && !v8_flags.compact_on_every_full_gc) {
      candidate_count = 0;
    }
    for (int i = 0; i < candidate_count; i++) {
      AddEvacuationCandidate(pages[i].second);
    }
  }

  if (v8_flags.trace_fragmentation) {
    PrintIsolate(heap_->isolate(),
                 "compaction-selection: space=%s reduce_memory=%d pages=%d "
                 "total_live_bytes=%zu\n",
                 ToString(space->identity()), reduce_memory, candidate_count,
                 total_live_bytes / KB);
  }
}

void MarkCompactCollector::Prepare() {
#ifdef DEBUG
  DCHECK(state_ == IDLE);
  state_ = PREPARE_GC;
#endif

  DCHECK(!sweeper_->sweeping_in_progress());

  DCHECK_IMPLIES(heap_->incremental_marking()->IsMarking(),
                 heap_->incremental_marking()->IsMajorMarking());
  if (!heap_->incremental_marking()->IsMarking()) {
    StartCompaction(StartCompactionMode::kAtomic);
    StartMarking();
    if (heap_->cpp_heap_) {
      TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_EMBEDDER_PROLOGUE);
      // StartTracing immediately starts marking which requires V8 worklists to
      // be set up.
      CppHeap::From(heap_->cpp_heap_)->StartMarking();
    }
  }
  if (auto* new_space = heap_->new_space()) {
    new_space->GarbageCollectionPrologue();
  }
  if (heap_->use_new_space()) {
    DCHECK_EQ(
        heap_->allocator()->new_space_allocator()->top(),
        heap_->allocator()->new_space_allocator()->original_top_acquire());
  }
}

void MarkCompactCollector::FinishConcurrentMarking() {
  // FinishConcurrentMarking is called for both, concurrent and parallel,
  // marking. It is safe to call this function when tasks are already finished.
  DCHECK_EQ(heap_->concurrent_marking()->garbage_collector(),
            GarbageCollector::MARK_COMPACTOR);
  if (v8_flags.parallel_marking || v8_flags.concurrent_marking) {
    heap_->concurrent_marking()->Join();
    heap_->concurrent_marking()->FlushMemoryChunkData();
    heap_->concurrent_marking()->FlushNativeContexts(&native_context_stats_);
  }
  if (auto* cpp_heap = CppHeap::From(heap_->cpp_heap_)) {
    cpp_heap->FinishConcurrentMarkingIfNeeded();
  }
}

void MarkCompactCollector::VerifyMarking() {
  CHECK(local_marking_worklists_->IsEmpty());
  DCHECK(heap_->incremental_marking()->IsStopped());
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_VERIFY);
    FullMarkingVerifier verifier(heap_);
    verifier.Run();
    heap_->old_space()->VerifyLiveBytes();
    heap_->code_space()->VerifyLiveBytes();
    if (heap_->shared_space()) heap_->shared_space()->VerifyLiveBytes();
    heap_->trusted_space()->VerifyLiveBytes();
    if (v8_flags.minor_ms && heap_->paged_new_space())
      heap_->paged_new_space()->paged_space()->VerifyLiveBytes();
  }
#endif  // VERIFY_HEAP
}

namespace {

void ShrinkPagesToObjectSizes(Heap* heap, OldLargeObjectSpace* space) {
  size_t surviving_object_size = 0;
  PtrComprCageBase cage_base(heap->isolate());
  for (auto it = space->begin(); it != space->end();) {
    LargePageMetadata* current = *(it++);
    Tagged<HeapObject> object = current->GetObject();
    const size_t object_size = static_cast<size_t>(object->Size(cage_base));
    space->ShrinkPageToObjectSize(current, object, object_size);
    surviving_object_size += object_size;
  }
  space->set_objects_size(surviving_object_size);
}

}  // namespace

void MarkCompactCollector::Finish() {
  {
    TRACE_GC_EPOCH_WITH_FLOW(
        heap_->tracer(), GCTracer::Scope::MC_SWEEP, ThreadKind::kMain,
        sweeper_->GetTraceIdForFlowEvent(GCTracer::Scope::MC_SWEEP),
        TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);

    // Delay releasing empty new space pages and dead new large object pages
    // until after pointer updating is done because dead old space objects may
    // have slots pointing to these pages and will need to be updated.
    DCHECK_IMPLIES(!v8_flags.minor_ms,
                   empty_new_space_pages_to_be_swept_.empty());
    if (!empty_new_space_pages_to_be_swept_.empty()) {
      GCTracer::Scope sweep_scope(
          heap_->tracer(), GCTracer::Scope::MC_SWEEP_NEW, ThreadKind::kMain);
      for (PageMetadata* p : empty_new_space_pages_to_be_swept_) {
        // Sweeping empty pages already relinks them to the freelist.
        sweeper_->SweepEmptyNewSpacePage(p);
      }
      empty_new_space_pages_to_be_swept_.clear();
    }

    if (heap_->new_lo_space()) {
      TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_SWEEP_NEW_LO);
      SweepLargeSpace(heap_->new_lo_space());
    }

#ifdef DEBUG
    heap_->VerifyCountersBeforeConcurrentSweeping(
        GarbageCollector::MARK_COMPACTOR);
#endif  // DEBUG
  }

  if (heap_->new_space()) {
    if (v8_flags.minor_ms) {
      switch (resize_new_space_) {
        case ResizeNewSpaceMode::kShrink:
          heap_->ReduceNewSpaceSize();
          break;
        case ResizeNewSpaceMode::kGrow:
          heap_->ExpandNewSpaceSize();
          break;
        case ResizeNewSpaceMode::kNone:
          break;
      }
      resize_new_space_ = ResizeNewSpaceMode::kNone;
    }
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_EVACUATE);
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_EVACUATE_REBALANCE);
    if (!heap_->new_space()->EnsureCurrentCapacity()) {
      heap_->FatalProcessOutOfMemory("NewSpace::EnsureCurrentCapacity");
    }
  }

  TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_FINISH);

  if (heap_->new_space()) {
    DCHECK(!heap_->allocator()->new_space_allocator()->IsLabValid());
    heap_->new_space()->GarbageCollectionEpilogue();
  }

  auto* isolate = heap_->isolate();
  isolate->global_handles()->ClearListOfYoungNodes();

  SweepArrayBufferExtensions();

  marking_visitor_.reset();
  local_marking_worklists_.reset();
  marking_worklists_.ReleaseContextWorklists();
  native_context_stats_.Clear();

  CHECK(weak_objects_.current_ephemerons.IsEmpty());
  CHECK(weak_objects_.discovered_ephemerons.IsEmpty());
  local_weak_objects_->next_ephemerons_local.Publish();
  local_weak_objects_.reset();
  weak_objects_.next_ephemerons.Clear();

  sweeper_->StartMajorSweeperTasks();

  // Release empty pages now, when the pointer-update phase is done.
  heap_->memory_allocator()->ReleaseQueuedPages();

  // Shrink pages if possible after processing and filtering slots.
  ShrinkPagesToObjectSizes(heap_, heap_->lo_space());

#ifdef DEBUG
  DCHECK(state_ == SWEEP_SPACES || state_ == RELOCATE_OBJECTS);
  state_ = IDLE;
#endif

  if (have_code_to_deoptimize_) {
    // Some code objects were marked for deoptimization during the GC.
    Deoptimizer::DeoptimizeMarkedCode(isolate);
    have_code_to_deoptimize_ = false;
  }
}

void MarkCompactCollector::SweepArrayBufferExtensions() {
  DCHECK_IMPLIES(heap_->new_space(), heap_->new_space()->Size() == 0);
  DCHECK_IMPLIES(heap_->new_lo_space(), heap_->new_lo_space()->Size() == 0);
  heap_->array_buffer_sweeper()->RequestSweep(
      ArrayBufferSweeper::SweepingType::kFull,
      ArrayBufferSweeper::TreatAllYoungAsPromoted::kYes);
}

// This visitor is used to visit the body of special objects held alive by
// other roots.
//
// It is currently used for
// - InstructionStream held alive by the top optimized frame. This code cannot
// be deoptimized and thus have to be kept alive in an isolate way, i.e., it
// should not keep alive other code objects reachable through the weak list but
// they should keep alive its embedded pointers (which would otherwise be
// dropped).
// - Prefix of the string table.
// - If V8_ENABLE_SANDBOX, client Isolates' waiter queue node
// ExternalPointer_t in shared Isolates.
class MarkCompactCollector::CustomRootBodyMarkingVisitor final
    : public ObjectVisitorWithCageBases {
 public:
  explicit CustomRootBodyMarkingVisitor(MarkCompactCollector* collector)
      : ObjectVisitorWithCageBases(collector->heap_->isolate()),
        collector_(collector) {}

  void VisitPointer(Tagged<HeapObject> host, ObjectSlot p) final {
    MarkObject(host, p.load(cage_base()));
  }

  void VisitMapPointer(Tagged<HeapObject> host) final {
    MarkObject(host, host->map(cage_base()));
  }

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) final {
    for (ObjectSlot p = start; p < end; ++p) {
      // The map slot should be handled in VisitMapPointer.
      DCHECK_NE(host->map_slot(), p);
      DCHECK(!HasWeakHeapObjectTag(p.load(cage_base())));
      MarkObject(host, p.load(cage_base()));
    }
  }

  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    MarkObject(host, slot.load(code_cage_base()));
  }

  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) final {
    // At the moment, custom roots cannot contain weak pointers.
    UNREACHABLE();
  }

  void VisitCodeTarget(Tagged<InstructionStream> host,
                       RelocInfo* rinfo) override {
    Tagged<InstructionStream> target =
        InstructionStream::FromTargetAddress(rinfo->target_address());
    MarkObject(host, target);
  }

  void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                            RelocInfo* rinfo) override {
    MarkObject(host, rinfo->target_object(cage_base()));
  }

 private:
  V8_INLINE void MarkObject(Tagged<HeapObject> host, Tagged<Object> object) {
    if (!IsHeapObject(object)) return;
    Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
    const auto target_worklist =
        MarkingHelper::ShouldMarkObject(collector_->heap(), heap_object);
    if (!target_worklist) {
      return;
    }
    collector_->MarkObject(host, heap_object, target_worklist.value());
  }

  MarkCompactCollector* const collector_;
};

class MarkCompactCollector::SharedHeapObjectVisitor final
    : public ObjectVisitorWithCageBases {
 public:
  explicit SharedHeapObjectVisitor(MarkCompactCollector* collector)
      : ObjectVisitorWithCageBases(collector->heap_->isolate()),
        collector_(collector) {}

  void VisitPointer(Tagged<HeapObject> host, ObjectSlot p) final {
    CheckForSharedObject(host, p, p.load(cage_base()));
  }

  void VisitPointer(Tagged<HeapObject> host, MaybeObjectSlot p) final {
    Tagged<MaybeObject> object = p.load(cage_base());
    Tagged<HeapObject> heap_object;
    if (object.GetHeapObject(&heap_object))
      CheckForSharedObject(host, ObjectSlot(p), heap_object);
  }

  void VisitMapPointer(Tagged<HeapObject> host) final {
    CheckForSharedObject(host, host->map_slot(), host->map(cage_base()));
  }

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) final {
    for (ObjectSlot p = start; p < end; ++p) {
      // The map slot should be handled in VisitMapPointer.
      DCHECK_NE(host->map_slot(), p);
      DCHECK(!HasWeakHeapObjectTag(p.load(cage_base())));
      CheckForSharedObject(host, p, p.load(cage_base()));
    }
  }

  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    UNREACHABLE();
  }

  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) final {
    for (MaybeObjectSlot p = start; p < end; ++p) {
      // The map slot should be handled in VisitMapPointer.
      DCHECK_NE(host->map_slot(), ObjectSlot(p));
      VisitPointer(host, p);
    }
  }

  void VisitCodeTarget(Tagged<InstructionStream> host,
                       RelocInfo* rinfo) override {
    UNREACHABLE();
  }

  void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                            RelocInfo* rinfo) override {
    UNREACHABLE();
  }

 private:
  V8_INLINE void CheckForSharedObject(Tagged<HeapObject> host, ObjectSlot slot,
                                      Tagged<Object> object) {
    DCHECK(!InAnySharedSpace(host));
    if (!IsHeapObject(object)) return;
    Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
    if (!InWritableSharedSpace(heap_object)) return;
    DCHECK(InWritableSharedSpace(heap_object));
    MemoryChunk* host_chunk = MemoryChunk::FromHeapObject(host);
    MutablePageMetadata* host_page_metadata =
        MutablePageMetadata::cast(host_chunk->Metadata());
    DCHECK(Heap::InYoungGeneration(host));
    // Temporarily record new-to-shared slots in the old-to-shared remembered
    // set so we don't need to iterate the page again later for updating the
    // references.
    RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::NON_ATOMIC>(
        host_page_metadata, host_chunk->Offset(slot.address()));
    collector_->MarkRootObject(Root::kClientHeap, heap_object,
                               MarkingHelper::WorklistTarget::kRegular);
  }

  MarkCompactCollector* const collector_;
};

class InternalizedStringTableCleaner final : public RootVisitor {
 public:
  explicit InternalizedStringTableCleaner(Heap* heap) : heap_(heap) {}

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    UNREACHABLE();
  }

  void VisitRootPointers(Root root, const char* description,
                         OffHeapObjectSlot start,
                         OffHeapObjectSlot end) override {
    DCHECK_EQ(root, Root::kStringTable);
    // Visit all HeapObject pointers in [start, end).
    auto* marking_state = heap_->marking_state();
    Isolate* const isolate = heap_->isolate();
    for (OffHeapObjectSlot p = start; p < end; ++p) {
      Tagged<Object> o = p.load(isolate);
      if (IsHeapObject(o)) {
        Tagged<HeapObject> heap_object = Cast<HeapObject>(o);
        DCHECK(!Heap::InYoungGeneration(heap_object));
        if (!InReadOnlySpace(heap_object) &&
            marking_state->IsUnmarked(heap_object)) {
          pointers_removed_++;
          p.store(StringTable::deleted_element());
        }
      }
    }
  }

  int PointersRemoved() const { return pointers_removed_; }

 private:
  Heap* heap_;
  int pointers_removed_ = 0;
};

#ifdef V8_ENABLE_SANDBOX
class MarkExternalPointerFromExternalStringTable : public RootVisitor {
 public:
  explicit MarkExternalPointerFromExternalStringTable(
      ExternalPointerTable* shared_table, ExternalPointerTable::Space* space)
      : visitor(shared_table, space) {}

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    // Visit all HeapObject pointers in [start, end).
    for (FullObjectSlot p = start; p < end; ++p) {
      Tagged<Object> o = *p;
      if (IsHeapObject(o)) {
        Tagged<HeapObject> heap_object = Cast<HeapObject>(o);
        if (IsExternalString(heap_object)) {
          Tagged<ExternalString> string = Cast<ExternalString>(heap_object);
          string->VisitExternalPointers(&visitor);
        } else {
          // The original external string may have been internalized.
          DCHECK(IsThinString(o));
        }
      }
    }
  }

 private:
  class MarkExternalPointerTableVisitor : public ObjectVisitor {
   public:
    explicit MarkExternalPointerTableVisitor(ExternalPointerTable* table,
                                             ExternalPointerTable::Space* space)
        : table_(table), space_(space) {}
    void VisitExternalPointer(Tagged<HeapObject> host,
                              ExternalPointerSlot slot) override {
      DCHECK_NE(slot.tag(), kExternalPointerNullTag);
      DCHECK(IsSharedExternalPointerType(slot.tag()));
      ExternalPointerHandle handle = slot.Relaxed_LoadHandle();
      table_->Mark(space_, handle, slot.address());
    }
    void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                       ObjectSlot end) override {
      UNREACHABLE();
    }
    void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                       MaybeObjectSlot end) override {
      UNREACHABLE();
    }
    void VisitInstructionStreamPointer(Tagged<Code> host,
                                       InstructionStreamSlot slot) override {
      UNREACHABLE();
    }
    void VisitCodeTarget(Tagged<InstructionStream> host,
                         RelocInfo* rinfo) override {
      UNREACHABLE();
    }
    void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) override {
      UNREACHABLE();
    }

   private:
    ExternalPointerTable* table_;
    ExternalPointerTable::Space* space_;
  };

  MarkExternalPointerTableVisitor visitor;
};
#endif

// Implementation of WeakObjectRetainer for mark compact GCs. All marked objects
// are retained.
class MarkCompactWeakObjectRetainer : public WeakObjectRetainer {
 public:
  explicit MarkCompactWeakObjectRetainer(MarkingState* marking_state)
      : marking_state_(marking_state) {}

  Tagged<Object> RetainAs(Tagged<Object> object) override {
    Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
    if (marking_state_->IsMarked(heap_object)) {
      return object;
    } else if (IsAllocationSite(object) &&
               !Cast<AllocationSite>(object)->IsZombie()) {
      // "dead" AllocationSites need to live long enough for a traversal of new
      // space. These sites get a one-time reprieve.

      Tagged<Object> nested = object;
      while (IsAllocationSite(nested)) {
        Tagged<AllocationSite> current_site = Cast<AllocationSite>(nested);
        // MarkZombie will override the nested_site, read it first before
        // marking
        nested = current_site->nested_site();
        current_site->MarkZombie();
        marking_state_->TryMarkAndAccountLiveBytes(current_site);
      }

      return object;
    } else {
      return Smi::zero();
    }
  }

 private:
  MarkingState* const marking_state_;
};

class RecordMigratedSlotVisitor : public ObjectVisitorWithCageBases {
 public:
  explicit RecordMigratedSlotVisitor(Heap* heap)
      : ObjectVisitorWithCageBases(heap->isolate()), heap_(heap) {}

  inline void VisitPointer(Tagged<HeapObject> host, ObjectSlot p) final {
    DCHECK(!HasWeakHeapObjectTag(p.load(cage_base())));
    RecordMigratedSlot(host, p.load(cage_base()), p.address());
  }

  inline void VisitMapPointer(Tagged<HeapObject> host) final {
    VisitPointer(host, host->map_slot());
  }

  inline void VisitPointer(Tagged<HeapObject> host, MaybeObjectSlot p) final {
    DCHECK(!MapWord::IsPacked(p.Relaxed_Load(cage_base()).ptr()));
    RecordMigratedSlot(host, p.load(cage_base()), p.address());
  }

  inline void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                            ObjectSlot end) final {
    while (start < end) {
      VisitPointer(host, start);
      ++start;
    }
  }

  inline void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                            MaybeObjectSlot end) final {
    while (start < end) {
      VisitPointer(host, start);
      ++start;
    }
  }

  inline void VisitInstructionStreamPointer(Tagged<Code> host,
                                            InstructionStreamSlot slot) final {
    // This code is similar to the implementation of VisitPointer() modulo
    // new kind of slot.
    DCHECK(!HasWeakHeapObjectTag(slot.load(code_cage_base())));
    Tagged<Object> code = slot.load(code_cage_base());
    RecordMigratedSlot(host, code, slot.address());
  }

  inline void VisitEphemeron(Tagged<HeapObject> host, int index, ObjectSlot key,
                             ObjectSlot value) override {
    DCHECK(IsEphemeronHashTable(host));
    DCHECK(!Heap::InYoungGeneration(host));

    // Simply record ephemeron keys in OLD_TO_NEW if it points into the young
    // generation instead of recording it in ephemeron_remembered_set here for
    // migrated objects. OLD_TO_NEW is per page and we can therefore easily
    // record in OLD_TO_NEW on different pages in parallel without merging. Both
    // sets are anyways guaranteed to be empty after a full GC.
    VisitPointer(host, key);
    VisitPointer(host, value);
  }

  inline void VisitCodeTarget(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) override {
    DCHECK(RelocInfo::IsCodeTargetMode(rinfo->rmode()));
    Tagged<InstructionStream> target =
        InstructionStream::FromTargetAddress(rinfo->target_address());
    // The target is always in old space, we don't have to record the slot in
    // the old-to-new remembered set.
    DCHECK(!Heap::InYoungGeneration(target));
    DCHECK(!InWritableSharedSpace(target));
    heap_->mark_compact_collector()->RecordRelocSlot(host, rinfo, target);
  }

  inline void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                                   RelocInfo* rinfo) override {
    DCHECK(RelocInfo::IsEmbeddedObjectMode(rinfo->rmode()));
    Tagged<HeapObject> object = rinfo->target_object(cage_base());
    GenerationalBarrierForCode(host, rinfo, object);
    WriteBarrier::Shared(host, rinfo, object);
    heap_->mark_compact_collector()->RecordRelocSlot(host, rinfo, object);
  }

  // Entries that are skipped for recording.
  inline void VisitExternalReference(Tagged<InstructionStream> host,
                                     RelocInfo* rinfo) final {}
  inline void VisitInternalReference(Tagged<InstructionStream> host,
                                     RelocInfo* rinfo) final {}
  inline void VisitExternalPointer(Tagged<HeapObject> host,
                                   ExternalPointerSlot slot) final {}

  inline void VisitIndirectPointer(Tagged<HeapObject> host,
                                   IndirectPointerSlot slot,
                                   IndirectPointerMode mode) final {}

  inline void VisitTrustedPointerTableEntry(Tagged<HeapObject> host,
                                            IndirectPointerSlot slot) final {}

  inline void VisitProtectedPointer(Tagged<TrustedObject> host,
                                    ProtectedPointerSlot slot) final {
    RecordMigratedSlot(host, slot.load(), slot.address());
  }

 protected:
  inline void RecordMigratedSlot(Tagged<HeapObject> host,
                                 Tagged<MaybeObject> value, Address slot) {
    if (value.IsStrongOrWeak()) {
      MemoryChunk* value_chunk = MemoryChunk::FromAddress(value.ptr());
      MemoryChunk* host_chunk = MemoryChunk::FromHeapObject(host);
      if (Heap::InYoungGeneration(value)) {
        MutablePageMetadata* host_metadata =
            MutablePageMetadata::cast(host_chunk->Metadata());
        DCHECK_IMPLIES(value_chunk->IsToPage(),
                       v8_flags.minor_ms || value_chunk->IsLargePage());
        DCHECK(host_metadata->SweepingDone());
        RememberedSet<OLD_TO_NEW>::Insert<AccessMode::NON_ATOMIC>(
            host_metadata, host_chunk->Offset(slot));
      } else if (value_chunk->IsEvacuationCandidate()) {
        MutablePageMetadata* host_metadata =
            MutablePageMetadata::cast(host_chunk->Metadata());
        if (value_chunk->IsFlagSet(MemoryChunk::IS_EXECUTABLE)) {
          RememberedSet<OLD_TO_CODE>::Insert<AccessMode::NON_ATOMIC>(
              host_metadata, host_chunk->Offset(slot));
        } else if (value_chunk->IsFlagSet(MemoryChunk::IS_TRUSTED) &&
                   host_chunk->IsFlagSet(MemoryChunk::IS_TRUSTED)) {
          // When the sandbox is disabled, we use plain tagged pointers to
          // reference trusted objects from untrusted ones. However, for these
          // references we want to use the OLD_TO_OLD remembered set, so here
          // we need to check that both the value chunk and the host chunk are
          // trusted space chunks.
          if (value_chunk->InWritableSharedSpace()) {
            RememberedSet<TRUSTED_TO_SHARED_TRUSTED>::Insert<
                AccessMode::NON_ATOMIC>(host_metadata,
                                        host_chunk->Offset(slot));
          } else {
            RememberedSet<TRUSTED_TO_TRUSTED>::Insert<AccessMode::NON_ATOMIC>(
                host_metadata, host_chunk->Offset(slot));
          }
        } else {
          RememberedSet<OLD_TO_OLD>::Insert<AccessMode::NON_ATOMIC>(
              host_metadata, host_chunk->Offset(slot));
        }
      } else if (value_chunk->InWritableSharedSpace() &&
                 !InWritableSharedSpace(host)) {
        MutablePageMetadata* host_metadata =
            MutablePageMetadata::cast(host_chunk->Metadata());
        if (value_chunk->IsFlagSet(MemoryChunk::IS_TRUSTED) &&
            host_chunk->IsFlagSet(MemoryChunk::IS_TRUSTED)) {
          RememberedSet<TRUSTED_TO_SHARED_TRUSTED>::Insert<
              AccessMode::NON_ATOMIC>(host_metadata, host_chunk->Offset(slot));
        } else {
          RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::NON_ATOMIC>(
              host_metadata, host_chunk->Offset(slot));
        }
      }
    }
  }

  Heap* const heap_;
};

class MigrationObserver {
 public:
  explicit MigrationObserver(Heap* heap) : heap_(heap) {}

  virtual ~MigrationObserver() = default;
  virtual void Move(AllocationSpace dest, Tagged<HeapObject> src,
                    Tagged<HeapObject> dst, int size) = 0;

 protected:
  Heap* heap_;
};

class ProfilingMigrationObserver final : public MigrationObserver {
 public:
  explicit ProfilingMigrationObserver(Heap* heap) : MigrationObserver(heap) {}

  inline void Move(AllocationSpace dest, Tagged<HeapObject> src,
                   Tagged<HeapObject> dst, int size) final {
    // Note this method is called in a concurrent setting. The current object
    // (src and dst) is somewhat safe to access without precautions, but other
    // objects may be subject to concurrent modification.
    if (dest == CODE_SPACE) {
      PROFILE(heap_->isolate(), CodeMoveEvent(Cast<InstructionStream>(src),
                                              Cast<InstructionStream>(dst)));
    } else if ((dest == OLD_SPACE || dest == TRUSTED_SPACE) &&
               IsBytecodeArray(dst)) {
      // TODO(saelo): remove `dest == OLD_SPACE` once BytecodeArrays are
      // allocated in trusted space.
      PROFILE(heap_->isolate(), BytecodeMoveEvent(Cast<BytecodeArray>(src),
                                                  Cast<BytecodeArray>(dst)));
    }
    heap_->OnMoveEvent(src, dst, size);
  }
};

class HeapObjectVisitor {
 public:
  virtual ~HeapObjectVisitor() = default;
  virtual bool Visit(Tagged<HeapObject> object, int size) = 0;
};

class EvacuateVisitorBase : public HeapObjectVisitor {
 public:
  void AddObserver(MigrationObserver* observer) {
    migration_function_ = RawMigrateObject<MigrationMode::kObserved>;
    observers_.push_back(observer);
  }

#if DEBUG
  void DisableAbortEvacuationAtAddress(MutablePageMetadata* chunk) {
    abort_evacuation_at_address_ = chunk->area_end();
  }

  void SetUpAbortEvacuationAtAddress(MutablePageMetadata* chunk) {
    if (v8_flags.stress_compaction || v8_flags.stress_compaction_random) {
      // Stress aborting of evacuation by aborting ~5% of evacuation candidates
      // when stress testing.
      const double kFraction = 0.05;

      if (rng_->NextDouble() < kFraction) {
        const double abort_evacuation_percentage = rng_->NextDouble();
        abort_evacuation_at_address_ =
            chunk->area_start() +
            abort_evacuation_percentage * chunk->area_size();
        return;
      }
    }

    abort_evacuation_at_address_ = chunk->area_end();
  }
#endif  // DEBUG

 protected:
  enum MigrationMode { kFast, kObserved };

  PtrComprCageBase cage_base() {
#if V8_COMPRESS_POINTERS
    return PtrComprCageBase{heap_->isolate()};
#else
    return PtrComprCageBase{};
#endif  // V8_COMPRESS_POINTERS
  }

  using MigrateFunction = void (*)(EvacuateVisitorBase* base,
                                   Tagged<HeapObject> dst,
                                   Tagged<HeapObject> src, int size,
                                   AllocationSpace dest);

  template <MigrationMode mode>
  static void RawMigrateObject(EvacuateVisitorBase* base,
                               Tagged<HeapObject> dst, Tagged<HeapObject> src,
                               int size, AllocationSpace dest) {
    Address dst_addr = dst.address();
    Address src_addr = src.address();
    PtrComprCageBase cage_base = base->cage_base();
    DCHECK(base->heap_->AllowedToBeMigrated(src->map(cage_base), src, dest));
    DCHECK_NE(dest, LO_SPACE);
    DCHECK_NE(dest, CODE_LO_SPACE);
    DCHECK_NE(dest, TRUSTED_LO_SPACE);
    if (dest == OLD_SPACE) {
      DCHECK_OBJECT_SIZE(size);
      DCHECK(IsAligned(size, kTaggedSize));
      base->heap_->CopyBlock(dst_addr, src_addr, size);
      if (mode != MigrationMode::kFast) {
        base->ExecuteMigrationObservers(dest, src, dst, size);
      }
      // In case the object's map gets relocated during GC we load the old map
      // here. This is fine since they store the same content.
      dst->IterateFast(dst->map(cage_base), size, base->record_visitor_);
    } else if (dest == SHARED_SPACE) {
      DCHECK_OBJECT_SIZE(size);
      DCHECK(IsAligned(size, kTaggedSize));
      base->heap_->CopyBlock(dst_addr, src_addr, size);
      if (mode != MigrationMode::kFast) {
        base->ExecuteMigrationObservers(dest, src, dst, size);
      }
      dst->IterateFast(dst->map(cage_base), size, base->record_visitor_);
    } else if (dest == TRUSTED_SPACE) {
      DCHECK_OBJECT_SIZE(size);
      DCHECK(IsAligned(size, kTaggedSize));
      base->heap_->CopyBlock(dst_addr, src_addr, size);
      if (mode != MigrationMode::kFast) {
        base->ExecuteMigrationObservers(dest, src, dst, size);
      }
      // In case the object's map gets relocated during GC we load the old map
      // here. This is fine since they store the same content.
      dst->IterateFast(dst->map(cage_base), size, base->record_visitor_);
    } else if (dest == CODE_SPACE) {
      DCHECK_CODEOBJECT_SIZE(size);
      {
        WritableJitAllocation writable_allocation =
            ThreadIsolation::RegisterInstructionStreamAllocation(dst_addr,
                                                                 size);
        base->heap_->CopyBlock(dst_addr, src_addr, size);
        Tagged<InstructionStream> istream = Cast<InstructionStream>(dst);
        istream->Relocate(writable_allocation, dst_addr - src_addr);
      }
      if (mode != MigrationMode::kFast) {
        base->ExecuteMigrationObservers(dest, src, dst, size);
      }
      // In case the object's map gets relocated during GC we load the old map
      // here. This is fine since they store the same content.
      dst->IterateFast(dst->map(cage_base), size, base->record_visitor_);
    } else {
      DCHECK_OBJECT_SIZE(size);
      DCHECK(dest == NEW_SPACE);
      base->heap_->CopyBlock(dst_addr, src_addr, size);
      if (mode != MigrationMode::kFast) {
        base->ExecuteMigrationObservers(dest, src, dst, size);
      }
    }

    if (dest == CODE_SPACE) {
      WritableJitAllocation jit_allocation =
          WritableJitAllocation::ForInstructionStream(
              Cast<InstructionStream>(src));
      jit_allocation.WriteHeaderSlot<MapWord, HeapObject::kMapOffset>(
          MapWord::FromForwardingAddress(src, dst));
    } else {
      src->set_map_word_forwarded(dst, kRelaxedStore);
    }
  }

  EvacuateVisitorBase(Heap* heap, EvacuationAllocator* local_allocator,
                      RecordMigratedSlotVisitor* record_visitor)
      : heap_(heap),
        local_allocator_(local_allocator),
        record_visitor_(record_visitor),
        shared_string_table_(v8_flags.shared_string_table &&
                             heap->isolate()->has_shared_space()) {
    migration_function_ = RawMigrateObject<MigrationMode::kFast>;
#if DEBUG
    rng_.emplace(heap_->isolate()->fuzzer_rng()->NextInt64());
#endif  // DEBUG
  }

  inline bool TryEvacuateObject(AllocationSpace target_space,
                                Tagged<HeapObject> object, int size,
                                Tagged<HeapObject>* target_object) {
#if DEBUG
    DCHECK_LE(abort_evacuation_at_address_,
              MutablePageMetadata::FromHeapObject(object)->area_end());
    DCHECK_GE(abort_evacuation_at_address_,
              MutablePageMetadata::FromHeapObject(object)->area_start());

    if (V8_UNLIKELY(object.address() >= abort_evacuation_at_address_)) {
      return false;
    }
#endif  // DEBUG

    Tagged<Map> map = object->map(cage_base());
    AllocationAlignment alignment = HeapObject::RequiredAlignment(map);
    AllocationResult allocation;
    if (target_space == OLD_SPACE && ShouldPromoteIntoSharedHeap(map)) {
      allocation = local_allocator_->Allocate(SHARED_SPACE, size, alignment);
    } else {
      allocation = local_allocator_->Allocate(target_space, size, alignment);
    }
    if (allocation.To(target_object)) {
      MigrateObject(*target_object, object, size, target_space);
      return true;
    }
    return false;
  }

  inline bool ShouldPromoteIntoSharedHeap(Tagged<Map> map) {
    if (shared_string_table_) {
      return String::IsInPlaceInternalizableExcludingExternal(
          map->instance_type());
    }
    return false;
  }

  inline void ExecuteMigrationObservers(AllocationSpace dest,
                                        Tagged<HeapObject> src,
                                        Tagged<HeapObject> dst, int size) {
    for (MigrationObserver* obs : observers_) {
      obs->Move(dest, src, dst, size);
    }
  }

  inline void MigrateObject(Tagged<HeapObject> dst, Tagged<HeapObject> src,
                            int size, AllocationSpace dest) {
    migration_function_(this, dst, src, size, dest);
  }

  Heap* heap_;
  EvacuationAllocator* local_allocator_;
  RecordMigratedSlotVisitor* record_visitor_;
  std::vector<MigrationObserver*> observers_;
  MigrateFunction migration_function_;
  const bool shared_string_table_;
#if DEBUG
  Address abort_evacuation_at_address_{kNullAddress};
#endif  // DEBUG
  std::optional<base::RandomNumberGenerator> rng_;
};

class EvacuateNewSpaceVisitor final : public EvacuateVisitorBase {
 public:
  explicit EvacuateNewSpaceVisitor(
      Heap* heap, EvacuationAllocator* local_allocator,
      RecordMigratedSlotVisitor* record_visitor,
      PretenuringHandler::PretenuringFeedbackMap* local_pretenuring_feedback)
      : EvacuateVisitorBase(heap, local_allocator, record_visitor),
        promoted_size_(0),
        semispace_copied_size_(0),
        pretenuring_handler_(heap_->pretenuring_handler()),
        local_pretenuring_feedback_(local_pretenuring_feedback),
        is_incremental_marking_(heap->incremental_marking()->IsMarking()),
        shortcut_strings_(!heap_->IsGCWithStack() ||
                          v8_flags.shortcut_strings_with_stack) {
    DCHECK_IMPLIES(is_incremental_marking_,
                   heap->incremental_marking()->IsMajorMarking());
  }

  inline bool Visit(Tagged<HeapObject> object, int size) override {
    if (TryEvacuateWithoutCopy(object)) return true;
    Tagged<HeapObject> target_object;

    pretenuring_handler_->UpdateAllocationSite(object->map(), object,
                                               local_pretenuring_feedback_);

    if (!TryEvacuateObject(OLD_SPACE, object, size, &target_object)) {
      heap_->FatalProcessOutOfMemory(
          "MarkCompactCollector: young object promotion failed");
    }

    promoted_size_ += size;
    return true;
  }

  intptr_t promoted_size() { return promoted_size_; }
  intptr_t semispace_copied_size() { return semispace_copied_size_; }

 private:
  inline bool TryEvacuateWithoutCopy(Tagged<HeapObject> object) {
    DCHECK(!is_incremental_marking_);

    if (!shortcut_strings_) return false;

    Tagged<Map> map = object->map();

    // Some objects can be evacuated without creating a copy.
    if (map->visitor_id() == kVisitThinString) {
      Tagged<HeapObject> actual = Cast<ThinString>(object)->unchecked_actual();
      if (MarkCompactCollector::IsOnEvacuationCandidate(actual)) return false;
      object->set_map_word_forwarded(actual, kRelaxedStore);
      return true;
    }
    // TODO(mlippautz): Handle ConsString.

    return false;
  }

  inline AllocationSpace AllocateTargetObject(
      Tagged<HeapObject> old_object, int size,
      Tagged<HeapObject>* target_object) {
    AllocationAlignment alignment =
        HeapObject::RequiredAlignment(old_object->map());
    AllocationSpace space_allocated_in = NEW_SPACE;
    AllocationResult allocation =
        local_allocator_->Allocate(NEW_SPACE, size, alignment);
    if (allocation.IsFailure()) {
      allocation = AllocateInOldSpace(size, alignment);
      space_allocated_in = OLD_SPACE;
    }
    bool ok = allocation.To(target_object);
    DCHECK(ok);
    USE(ok);
    return space_allocated_in;
  }

  inline AllocationResult AllocateInOldSpace(int size_in_bytes,
                                             AllocationAlignment alignment) {
    AllocationResult allocation =
        local_allocator_->Allocate(OLD_SPACE, size_in_bytes, alignment);
    if (allocation.IsFailure()) {
      heap_->FatalProcessOutOfMemory(
          "MarkCompactCollector: semi-space copy, fallback in old gen");
    }
    return allocation;
  }

  intptr_t promoted_size_;
  intptr_t semispace_copied_size_;
  PretenuringHandler* const pretenuring_handler_;
  PretenuringHandler::PretenuringFeedbackMap* local_pretenuring_feedback_;
  bool is_incremental_marking_;
  const bool shortcut_strings_;
};

class EvacuateNewToOldSpacePageVisitor final : public HeapObjectVisitor {
 public:
  explicit EvacuateNewToOldSpacePageVisitor(
      Heap* heap, RecordMigratedSlotVisitor* record_visitor,
      PretenuringHandler::PretenuringFeedbackMap* local_pretenuring_feedback)
      : heap_(heap),
        record_visitor_(record_visitor),
        moved_bytes_(0),
        pretenuring_handler_(heap_->pretenuring_handler()),
        local_pretenuring_feedback_(local_pretenuring_feedback) {}

  static void Move(PageMetadata* page) {
    page->heap()->new_space()->PromotePageToOldSpace(page);
  }

  inline bool Visit(Tagged<HeapObject> object, int size) override {
    if (v8_flags.minor_ms) {
      pretenuring_handler_->UpdateAllocationSite(object->map(), object,
                                                 local_pretenuring_feedback_);
    }
    DCHECK(!IsCodeSpaceObject(object));
    PtrComprCageBase cage_base = GetPtrComprCageBase(object);
    object->IterateFast(cage_base, record_visitor_);
    return true;
  }

  intptr_t moved_bytes() { return moved_bytes_; }
  void account_moved_bytes(intptr_t bytes) { moved_bytes_ += bytes; }

 private:
  Heap* heap_;
  RecordMigratedSlotVisitor* record_visitor_;
  intptr_t moved_bytes_;
  PretenuringHandler* const pretenuring_handler_;
  PretenuringHandler::PretenuringFeedbackMap* local_pretenuring_feedback_;
};

class EvacuateOldSpaceVisitor final : public EvacuateVisitorBase {
 public:
  EvacuateOldSpaceVisitor(Heap* heap, EvacuationAllocator* local_allocator,
                          RecordMigratedSlotVisitor* record_visitor)
      : EvacuateVisitorBase(heap, local_allocator, record_visitor) {}

  inline bool Visit(Tagged<HeapObject> object, int size) override {
    Tagged<HeapObject> target_object;
    if (TryEvacuateObject(
            PageMetadata::FromHeapObject(object)->owner_identity(), object,
            size, &target_object)) {
      DCHECK(object->map_word(heap_->isolate(), kRelaxedLoad)
                 .IsForwardingAddress());
      return true;
    }
    return false;
  }
};

class EvacuateRecordOnlyVisitor final : public HeapObjectVisitor {
 public:
  explicit EvacuateRecordOnlyVisitor(Heap* heap)
      : heap_(heap), cage_base_(heap->isolate()) {}

  bool Visit(Tagged<HeapObject> object, int size) override {
    RecordMigratedSlotVisitor visitor(heap_);
    Tagged<Map> map = object->map(cage_base_);
    // Instead of calling object.IterateFast(cage_base(), &visitor) here
    // we can shortcut and use the precomputed size value passed to the visitor.
    DCHECK_EQ(object->SizeFromMap(map), size);
    live_object_size_ += ALIGN_TO_ALLOCATION_ALIGNMENT(size);
    object->IterateFast(map, size, &visitor);
    return true;
  }

  size_t live_object_size() const { return live_object_size_; }

 private:
  Heap* heap_;
  const PtrComprCageBase cage_base_;
  size_t live_object_size_ = 0;
};

// static
bool MarkCompactCollector::IsUnmarkedHeapObject(Heap* heap, FullObjectSlot p) {
  Tagged<Object> o = *p;
  if (!IsHeapObject(o)) return false;
  Tagged<HeapObject> heap_object = Cast<HeapObject>(o);
  if (InReadOnlySpace(heap_object)) return false;
  MarkCompactCollector* collector = heap->mark_compact_collector();
  if (V8_UNLIKELY(collector->uses_shared_heap_) &&
      !collector->is_shared_space_isolate_) {
    if (InWritableSharedSpace(heap_object)) return false;
  }
  return collector->non_atomic_marking_state_->IsUnmarked(heap_object);
}

// static
bool MarkCompactCollector::IsUnmarkedSharedHeapObject(Heap* heap,
                                                      FullObjectSlot p) {
  Tagged<Object> o = *p;
  if (!IsHeapObject(o)) return false;
  Tagged<HeapObject> heap_object = Cast<HeapObject>(o);
  Isolate* shared_space_isolate = heap->isolate()->shared_space_isolate();
  MarkCompactCollector* collector =
      shared_space_isolate->heap()->mark_compact_collector();
  if (!InWritableSharedSpace(heap_object)) return false;
  return collector->non_atomic_marking_state_->IsUnmarked(heap_object);
}

void MarkCompactCollector::MarkRoots(RootVisitor* root_visitor) {
  Isolate* const isolate = heap_->isolate();

  // Mark the heap roots including global variables, stack variables,
  // etc., and all objects reachable from them.
  heap_->IterateRoots(
      root_visitor,
      base::EnumSet<SkipRoot>{SkipRoot::kWeak, SkipRoot::kTracedHandles,
                              SkipRoot::kConservativeStack,
                              SkipRoot::kReadOnlyBuiltins});

  // Custom marking for top optimized frame.
  CustomRootBodyMarkingVisitor custom_root_body_visitor(this);
  ProcessTopOptimizedFrame(&custom_root_body_visitor, isolate);

  if (isolate->is_shared_space_isolate()) {
    ClientRootVisitor<> client_root_visitor(root_visitor);
    ClientObjectVisitor<> client_custom_root_body_visitor(
        &custom_root_body_visitor);

    isolate->global_safepoint()->IterateClientIsolates(
        [this, &client_root_visitor,
         &client_custom_root_body_visitor](Isolate* client) {
          client->heap()->IterateRoots(
              &client_root_visitor,
              base::EnumSet<SkipRoot>{SkipRoot::kWeak,
                                      SkipRoot::kConservativeStack,
                                      SkipRoot::kReadOnlyBuiltins});
          ProcessTopOptimizedFrame(&client_custom_root_body_visitor, client);
        });
  }
}

void MarkCompactCollector::MarkRootsFromConservativeStack(
    RootVisitor* root_visitor) {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::CONSERVATIVE_STACK_SCANNING);
  heap_->IterateConservativeStackRoots(root_visitor,
                                       Heap::IterateRootsMode::kMainIsolate);

  Isolate* const isolate = heap_->isolate();
  if (isolate->is_shared_space_isolate()) {
    ClientRootVisitor<> client_root_visitor(root_visitor);
    // For client isolates, use the stack marker to conservatively scan the
    // stack.
    isolate->global_safepoint()->IterateClientIsolates(
        [v = &client_root_visitor](Isolate* client) {
          client->heap()->IterateConservativeStackRoots(
              v, Heap::IterateRootsMode::kClientIsolate);
        });
  }
}

void MarkCompactCollector::MarkObjectsFromClientHeaps() {
  Isolate* const isolate = heap_->isolate();
  if (!isolate->is_shared_space_isolate()) return;

  isolate->global_safepoint()->IterateClientIsolates(
      [collector = this](Isolate* client) {
        collector->MarkObjectsFromClientHeap(client);
      });
}

void MarkCompactCollector::MarkObjectsFromClientHeap(Isolate* client) {
  // There is no OLD_TO_SHARED remembered set for the young generation. We
  // therefore need to iterate each object and check whether it points into the
  // shared heap. As an optimization and to avoid a second heap iteration in the
  // "update pointers" phase, all pointers into the shared heap are recorded in
  // the OLD_TO_SHARED remembered set as well.
  SharedHeapObjectVisitor visitor(this);

  PtrComprCageBase cage_base(client);
  Heap* heap = client->heap();

  // Finish sweeping for new space in order to iterate objects in it.
  heap->sweeper()->FinishMinorJobs();
  // Finish sweeping for old generation in order to iterate OLD_TO_SHARED.
  heap->sweeper()->FinishMajorJobs();

  if (auto* new_space = heap->new_space()) {
    DCHECK(!heap->allocator()->new_space_allocator()->IsLabValid());
    for (PageMetadata* page : *new_space) {
      for (Tagged<HeapObject> obj : HeapObjectRange(page)) {
        obj->IterateFast(cage_base, &visitor);
      }
    }
  }

  if (heap->new_lo_space()) {
    std::unique_ptr<ObjectIterator> iterator =
        heap->new_lo_space()->GetObjectIterator(heap);
    for (Tagged<HeapObject> obj = iterator->Next(); !obj.is_null();
         obj = iterator->Next()) {
      obj->IterateFast(cage_base, &visitor);
    }
  }

  // In the old generation we can simply use the OLD_TO_SHARED remembered set to
  // find all incoming pointers into the shared heap.
  OldGenerationMemoryChunkIterator chunk_iterator(heap);

  // Tracking OLD_TO_SHARED requires the write barrier.
  DCHECK(!v8_flags.disable_write_barriers);

  for (MutablePageMetadata* chunk = chunk_iterator.next(); chunk;
       chunk = chunk_iterator.next()) {
    const auto slot_count = RememberedSet<OLD_TO_SHARED>::Iterate(
        chunk,
        [collector = this, cage_base](MaybeObjectSlot slot) {
          Tagged<MaybeObject> obj = slot.Relaxed_Load(cage_base);
          Tagged<HeapObject> heap_object;

          if (obj.GetHeapObject(&heap_object) &&
              InWritableSharedSpace(heap_object)) {
            collector->MarkRootObject(Root::kClientHeap, heap_object,
                                      MarkingHelper::WorklistTarget::kRegular);
            return KEEP_SLOT;
          } else {
            return REMOVE_SLOT;
          }
        },
        SlotSet::FREE_EMPTY_BUCKETS);
    if (slot_count == 0) {
      chunk->ReleaseSlotSet(OLD_TO_SHARED);
    }

    const auto typed_slot_count = RememberedSet<OLD_TO_SHARED>::IterateTyped(
        chunk, [collector = this, heap](SlotType slot_type, Address slot) {
          Tagged<HeapObject> heap_object =
              UpdateTypedSlotHelper::GetTargetObject(heap, slot_type, slot);
          if (InWritableSharedSpace(heap_object)) {
            collector->MarkRootObject(Root::kClientHeap, heap_object,
                                      MarkingHelper::WorklistTarget::kRegular);
            return KEEP_SLOT;
          } else {
            return REMOVE_SLOT;
          }
        });
    if (typed_slot_count == 0) {
      chunk->ReleaseTypedSlotSet(OLD_TO_SHARED);
    }

    const auto protected_slot_count =
        RememberedSet<TRUSTED_TO_SHARED_TRUSTED>::Iterate(
            chunk,
            [collector = this](MaybeObjectSlot slot) {
              ProtectedPointerSlot protected_slot(slot.address());
              Tagged<MaybeObject> obj = protected_slot.Relaxed_Load();
              Tagged<HeapObject> heap_object;

              if (obj.GetHeapObject(&heap_object) &&
                  InWritableSharedSpace(heap_object)) {
                collector->MarkRootObject(
                    Root::kClientHeap, heap_object,
                    MarkingHelper::WorklistTarget::kRegular);
                return KEEP_SLOT;
              } else {
                return REMOVE_SLOT;
              }
            },
            SlotSet::FREE_EMPTY_BUCKETS);
    if (protected_slot_count == 0) {
      chunk->ReleaseSlotSet(TRUSTED_TO_SHARED_TRUSTED);
    }
  }

#ifdef V8_ENABLE_SANDBOX
  DCHECK(IsSharedExternalPointerType(kExternalStringResourceTag));
  DCHECK(IsSharedExternalPointerType(kExternalStringResourceDataTag));
  // All ExternalString resources are stored in the shared external pointer
  // table. Mark entries from client heaps.
  ExternalPointerTable& shared_table = client->shared_external_pointer_table();
  ExternalPointerTable::Space* shared_space =
      client->shared_external_pointer_space();
  MarkExternalPointerFromExternalStringTable external_string_visitor(
      &shared_table, shared_space);
  heap->external_string_table_.IterateAll(&external_string_visitor);

  TrustedPointerTable* const tpt = &client->trusted_pointer_table();
  tpt->IterateActiveEntriesIn(
      client->heap()->trusted_pointer_space(),
      [collector = this](TrustedPointerHandle handle, Address content) {
        Tagged<HeapObject> heap_obj = Cast<HeapObject>(Tagged<Object>(content));
        DCHECK(IsExposedTrustedObject(heap_obj));
        if (!InWritableSharedSpace(heap_obj)) return;
        collector->MarkRootObject(Root::kClientHeap, heap_obj,
                                  MarkingHelper::WorklistTarget::kRegular);
      });
#endif  // V8_ENABLE_SANDBOX
}

bool MarkCompactCollector::MarkTransitiveClosureUntilFixpoint() {
  int iterations = 0;
  int max_iterations = v8_flags.ephemeron_fixpoint_iterations;

  bool another_ephemeron_iteration_main_thread;

  do {
    PerformWrapperTracing();

    if (iterations >= max_iterations) {
      // Give up fixpoint iteration and switch to linear algorithm.
      return false;
    }

    // Move ephemerons from next_ephemerons into current_ephemerons to
    // drain them in this iteration.
    DCHECK(
        local_weak_objects()->current_ephemerons_local.IsLocalAndGlobalEmpty());
    weak_objects_.current_ephemerons.Merge(weak_objects_.next_ephemerons);
    heap_->concurrent_marking()->set_another_ephemeron_iteration(false);

    {
      TRACE_GC(heap_->tracer(),
               GCTracer::Scope::MC_MARK_WEAK_CLOSURE_EPHEMERON_MARKING);
      another_ephemeron_iteration_main_thread = ProcessEphemerons();
    }

    // Can only check for local emptiness here as parallel marking tasks may
    // still be running. The caller performs the CHECKs for global emptiness.
    CHECK(local_weak_objects()->current_ephemerons_local.IsLocalEmpty());
    CHECK(local_weak_objects()->discovered_ephemerons_local.IsLocalEmpty());

    ++iterations;
  } while (another_ephemeron_iteration_main_thread ||
           heap_->concurrent_marking()->another_ephemeron_iteration() ||
           !local_marking_worklists_->IsEmpty() ||
           !IsCppHeapMarkingFinished(heap_, local_marking_worklists_.get()));

  return true;
}

bool MarkCompactCollector::ProcessEphemerons() {
  Ephemeron ephemeron;
  bool another_ephemeron_iteration = false;

  // Drain current_ephemerons and push ephemerons where key and value are still
  // unreachable into next_ephemerons.
  while (local_weak_objects()->current_ephemerons_local.Pop(&ephemeron)) {
    if (ProcessEphemeron(ephemeron.key, ephemeron.value)) {
      another_ephemeron_iteration = true;
    }
  }

  // Drain marking worklist and push discovered ephemerons into
  // discovered_ephemerons.
  size_t objects_processed;
  std::tie(std::ignore, objects_processed) =
      ProcessMarkingWorklist(v8::base::TimeDelta::Max(), SIZE_MAX,
                             MarkingWorklistProcessingMode::kDefault);

  // As soon as a single object was processed and potentially marked another
  // object we need another iteration. Otherwise we might miss to apply
  // ephemeron semantics on it.
  if (objects_processed > 0) another_ephemeron_iteration = true;

  // Drain discovered_ephemerons (filled in the drain MarkingWorklist-phase
  // before) and push ephemerons where key and value are still unreachable into
  // next_ephemerons.
  while (local_weak_objects()->discovered_ephemerons_local.Pop(&ephemeron)) {
    if (ProcessEphemeron(ephemeron.key, ephemeron.value)) {
      another_ephemeron_iteration = true;
    }
  }

  // Flush local ephemerons for main task to global pool.
  local_weak_objects()->ephemeron_hash_tables_local.Publish();
  local_weak_objects()->next_ephemerons_local.Publish();

  return another_ephemeron_iteration;
}

void MarkCompactCollector::MarkTransitiveClosureLinear() {
  TRACE_GC(heap_->tracer(),
           GCTracer::Scope::MC_MARK_WEAK_CLOSURE_EPHEMERON_LINEAR);
  // This phase doesn't support parallel marking.
  DCHECK(heap_->concurrent_marking()->IsStopped());
  // We must use the full pointer comparison here as this map will be queried
  // with objects from different cages (e.g. code- or trusted cage).
  std::unordered_multimap<Tagged<HeapObject>, Tagged<HeapObject>,
                          Object::Hasher, Object::KeyEqualSafe>
      key_to_values;
  Ephemeron ephemeron;

  DCHECK(
      local_weak_objects()->current_ephemerons_local.IsLocalAndGlobalEmpty());
  weak_objects_.current_ephemerons.Merge(weak_objects_.next_ephemerons);
  while (local_weak_objects()->current_ephemerons_local.Pop(&ephemeron)) {
    ProcessEphemeron(ephemeron.key, ephemeron.value);

    if (non_atomic_marking_state_->IsUnmarked(ephemeron.value)) {
      key_to_values.insert(std::make_pair(ephemeron.key, ephemeron.value));
    }
  }

  ephemeron_marking_.newly_discovered_limit = key_to_values.size();
  bool work_to_do = true;

  while (work_to_do) {
    PerformWrapperTracing();

    ResetNewlyDiscovered();
    ephemeron_marking_.newly_discovered_limit = key_to_values.size();

    {
      TRACE_GC(heap_->tracer(),
               GCTracer::Scope::MC_MARK_WEAK_CLOSURE_EPHEMERON_MARKING);
      // Drain marking worklist and push all discovered objects into
      // newly_discovered.
      ProcessMarkingWorklist(
          v8::base::TimeDelta::Max(), SIZE_MAX,
          MarkingWorklistProcessingMode::kTrackNewlyDiscoveredObjects);
    }

    while (local_weak_objects()->discovered_ephemerons_local.Pop(&ephemeron)) {
      ProcessEphemeron(ephemeron.key, ephemeron.value);

      if (non_atomic_marking_state_->IsUnmarked(ephemeron.value)) {
        key_to_values.insert(std::make_pair(ephemeron.key, ephemeron.value));
      }
    }

    if (ephemeron_marking_.newly_discovered_overflowed) {
      // If newly_discovered was overflowed just visit all ephemerons in
      // next_ephemerons.
      local_weak_objects()->next_ephemerons_local.Publish();
      weak_objects_.next_ephemerons.Iterate([&](Ephemeron ephemeron) {
        if (non_atomic_marking_state_->IsMarked(ephemeron.key)) {
          DCHECK(MarkingHelper::ShouldMarkObject(heap_, ephemeron.value));
          MarkingHelper::TryMarkAndPush(
              heap_, local_marking_worklists_.get(), non_atomic_marking_state_,
              MarkingHelper::WorklistTarget::kRegular, ephemeron.value);
        }
      });

    } else {
      // This is the good case: newly_discovered stores all discovered
      // objects. Now use key_to_values to see if discovered objects keep more
      // objects alive due to ephemeron semantics.
      for (Tagged<HeapObject> object : ephemeron_marking_.newly_discovered) {
        auto range = key_to_values.equal_range(object);
        for (auto it = range.first; it != range.second; ++it) {
          Tagged<HeapObject> value = it->second;
          const auto target_worklist =
              MarkingHelper::ShouldMarkObject(heap_, value);
          DCHECK(target_worklist);
          MarkObject(object, value, target_worklist.value());
        }
      }
    }

    // Do NOT drain marking worklist here, otherwise the current checks
    // for work_to_do are not sufficient for determining if another iteration
    // is necessary.

    work_to_do =
        !local_marking_worklists_->IsEmpty() ||
        !IsCppHeapMarkingFinished(heap_, local_marking_worklists_.get());
    CHECK(local_weak_objects()
              ->discovered_ephemerons_local.IsLocalAndGlobalEmpty());
  }

  ResetNewlyDiscovered();
  ephemeron_marking_.newly_discovered.shrink_to_fit();

  CHECK(local_marking_worklists_->IsEmpty());

  CHECK(weak_objects_.current_ephemerons.IsEmpty());
  CHECK(weak_objects_.discovered_ephemerons.IsEmpty());

  // Flush local ephemerons for main task to global pool.
  local_weak_objects()->ephemeron_hash_tables_local.Publish();
  local_weak_objects()->next_ephemerons_local.Publish();
}

void MarkCompactCollector::PerformWrapperTracing() {
  auto* cpp_heap = CppHeap::From(heap_->cpp_heap_);
  if (!cpp_heap) return;

  TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_EMBEDDER_TRACING);
  cpp_heap->AdvanceTracing(v8::base::TimeDelta::Max());
}

namespace {

constexpr size_t kDeadlineCheckInterval = 128u;

}  // namespace

std::pair<size_t, size_t> MarkCompactCollector::ProcessMarkingWorklist(
    v8::base::TimeDelta max_duration, size_t max_bytes_to_process,
    MarkingWorklistProcessingMode mode) {
  Tagged<HeapObject> object;
  size_t bytes_processed = 0;
  size_t objects_processed = 0;
  bool is_per_context_mode = local_marking_worklists_->IsPerContextMode();
  Isolate* const isolate = heap_->isolate();
  const auto start = v8::base::TimeTicks::Now();
  PtrComprCageBase cage_base(isolate);

  if (parallel_marking_ && UseBackgroundThreadsInCycle()) {
    heap_->concurrent_marking()->RescheduleJobIfNeeded(
        GarbageCollector::MARK_COMPACTOR, TaskPriority::kUserBlocking);
  }

  while (local_marking_worklists_->Pop(&object) ||
         local_marking_worklists_->PopOnHold(&object)) {
    // The marking worklist should never contain filler objects.
    CHECK(!IsFreeSpaceOrFiller(object, cage_base));
    DCHECK(IsHeapObject(object));
    DCHECK(!InReadOnlySpace(object));
    DCHECK_EQ(GetIsolateFromWritableObject(object), isolate);
    DCHECK(heap_->Contains(object));
    DCHECK(!(marking_state_->IsUnmarked(object)));
    if (mode == MarkCompactCollector::MarkingWorklistProcessingMode::
                    kTrackNewlyDiscoveredObjects) {
      AddNewlyDiscovered(object);
    }
    Tagged<Map> map = object->map(cage_base);
    if (is_per_context_mode) {
      Address context;
      if (native_context_inferrer_.Infer(cage_base, map, object, &context)) {
        local_marking_worklists_->SwitchToContext(context);
      }
    }
    const auto visited_size = marking_visitor_->Visit(map, object);
    if (visited_size) {
      MutablePageMetadata::FromHeapObject(object)->IncrementLiveBytesAtomically(
          ALIGN_TO_ALLOCATION_ALIGNMENT(visited_size));
    }
    if (is_per_context_mode) {
      native_context_stats_.IncrementSize(local_marking_worklists_->Context(),
                                          map, object, visited_size);
    }
    bytes_processed += visited_size;
    objects_processed++;
    static_assert(base::bits::IsPowerOfTwo(kDeadlineCheckInterval),
                  "kDeadlineCheckInterval must be power of 2");
    // The below check is an optimized version of
    // `(objects_processed % kDeadlineCheckInterval) == 0`
    if ((objects_processed & (kDeadlineCheckInterval -1)) == 0 &&
        ((v8::base::TimeTicks::Now() - start) > max_duration)) {
      break;
    }
    if (bytes_processed >= max_bytes_to_process) {
      break;
    }
  }
  return std::make_pair(bytes_processed, objects_processed);
}

bool MarkCompactCollector::ProcessEphemeron(Tagged<HeapObject> key,
                                            Tagged<HeapObject> value) {
  // Objects in the shared heap are prohibited from being used as keys in
  // WeakMaps and WeakSets and therefore cannot be ephemeron keys, because that
  // would enable thread local -> shared heap edges.
  DCHECK(!InWritableSharedSpace(key));
  // Usually values that should not be marked are not added to the ephemeron
  // worklist. However, minor collection during incremental marking may promote
  // strings from the younger generation into the shared heap. This
  // ShouldMarkObject call catches those cases.
  const auto target_worklist = MarkingHelper::ShouldMarkObject(heap_, value);
  if (!target_worklist) {
    return false;
  }
  if (marking_state_->IsMarked(key)) {
    const auto target_worklist = MarkingHelper::ShouldMarkObject(heap_, value);
    if (MarkingHelper::TryMarkAndPush(heap_, local_marking_worklists_.get(),
                                      marking_state_, target_worklist.value(),
                                      value)) {
      return true;
    }
  } else if (marking_state_->IsUnmarked(value)) {
    local_weak_objects()->next_ephemerons_local.Push(Ephemeron{key, value});
  }
  return false;
}

void MarkCompactCollector::VerifyEphemeronMarking() {
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    Ephemeron ephemeron;

    DCHECK(
        local_weak_objects()->current_ephemerons_local.IsLocalAndGlobalEmpty());
    weak_objects_.current_ephemerons.Merge(weak_objects_.next_ephemerons);
    while (local_weak_objects()->current_ephemerons_local.Pop(&ephemeron)) {
      CHECK(!ProcessEphemeron(ephemeron.key, ephemeron.value));
    }
  }
#endif  // VERIFY_HEAP
}

void MarkCompactCollector::MarkTransitiveClosure() {
  // Incremental marking might leave ephemerons in main task's local
  // buffer, flush it into global pool.
  local_weak_objects()->next_ephemerons_local.Publish();

  if (!MarkTransitiveClosureUntilFixpoint()) {
    // Fixpoint iteration needed too many iterations and was cancelled. Use the
    // guaranteed linear algorithm. But only in the final single-thread marking
    // phase.
    if (!parallel_marking_) MarkTransitiveClosureLinear();
  }
}

void MarkCompactCollector::ProcessTopOptimizedFrame(ObjectVisitor* visitor,
                                                    Isolate* isolate) {
  for (StackFrameIterator it(isolate, isolate->thread_local_top()); !it.done();
       it.Advance()) {
    if (it.frame()->is_unoptimized()) return;
    if (it.frame()->is_optimized()) {
      Tagged<GcSafeCode> lookup_result = it.frame()->GcSafeLookupCode();
      if (!lookup_result->has_instruction_stream()) return;
      if (!lookup_result->CanDeoptAt(isolate,
                                     it.frame()->maybe_unauthenticated_pc())) {
        Tagged<InstructionStream> istream = UncheckedCast<InstructionStream>(
            lookup_result->raw_instruction_stream());
        PtrComprCageBase cage_base(isolate);
        InstructionStream::BodyDescriptor::IterateBody(istream->map(cage_base),
                                                       istream, visitor);
      }
      return;
    }
  }
}

void MarkCompactCollector::RecordObjectStats() {
  if (V8_LIKELY(!TracingFlags::is_gc_stats_enabled())) return;
  // Cannot run during bootstrapping due to incomplete objects.
  if (heap_->isolate()->bootstrapper()->IsActive()) return;
  TRACE_EVENT0(TRACE_GC_CATEGORIES, "V8.GC_OBJECT_DUMP_STATISTICS");
  heap_->CreateObjectStats();
  ObjectStatsCollector collector(heap_, heap_->live_object_stats_.get(),
                                 heap_->dead_object_stats_.get());
  collector.Collect();
  if (V8_UNLIKELY(TracingFlags::gc_stats.load(std::memory_order_relaxed) &
                  v8::tracing::TracingCategoryObserver::ENABLED_BY_TRACING)) {
    std::stringstream live, dead;
    heap_->live_object_stats_->Dump(live);
    heap_->dead_object_stats_->Dump(dead);
    TRACE_EVENT_INSTANT2(TRACE_DISABLED_BY_DEFAULT("v8.gc_stats"),
                         "V8.GC_Objects_Stats", TRACE_EVENT_SCOPE_THREAD,
                         "live", TRACE_STR_COPY(live.str().c_str()), "dead",
                         TRACE_STR_COPY(dead.str().c_str()));
  }
  if (v8_flags.trace_gc_object_stats) {
    heap_->live_object_stats_->PrintJSON("live");
    heap_->dead_object_stats_->PrintJSON("dead");
  }
  heap_->live_object_stats_->CheckpointObjectStats();
  heap_->dead_object_stats_->ClearObjectStats();
}

namespace {

bool ShouldRetainMap(MarkingState* marking_state, Tagged<Map> map, int age) {
  if (age == 0) {
    // The map has aged. Do not retain this map.
    return false;
  }
  Tagged<Object> constructor = map->GetConstructor();
  if (!IsHeapObject(constructor) ||
      (!InReadOnlySpace(Cast<HeapObject>(constructor)) &&
       marking_state->IsUnmarked(Cast<HeapObject>(constructor)))) {
    // The constructor is dead, no new objects with this map can
    // be created. Do not retain this map.
    return false;
  }
  return true;
}

}  // namespace

void MarkCompactCollector::RetainMaps() {
  // Retaining maps increases the chances of reusing map transitions at some
  // memory cost, hence disable it when trying to reduce memory footprint more
  // aggressively.
  const bool should_retain_maps =
      !heap_->ShouldReduceMemory() && v8_flags.retain_maps_for_n_gc != 0;

  for (Tagged<WeakArrayList> retained_maps : heap_->FindAllRetainedMaps()) {
    DCHECK_EQ(0, retained_maps->length() % 2);
    for (int i = 0; i < retained_maps->length(); i += 2) {
      Tagged<MaybeObject> value = retained_maps->Get(i);
      Tagged<HeapObject> map_heap_object;
      if (!value.GetHeapObjectIfWeak(&map_heap_object)) {
        continue;
      }
      int age = retained_maps->Get(i + 1).ToSmi().value();
      int new_age;
      Tagged<Map> map = Cast<Map>(map_heap_object);
      if (should_retain_maps && marking_state_->IsUnmarked(map)) {
        if (ShouldRetainMap(marking_state_, map, age)) {
          DCHECK(MarkingHelper::ShouldMarkObject(heap_, map));
          MarkingHelper::TryMarkAndPush(
              heap_, local_marking_worklists_.get(), marking_state_,
              MarkingHelper::WorklistTarget::kRegular, map);
        }
        Tagged<Object> prototype = map->prototype();
        if (age > 0 && IsHeapObject(prototype) &&
            (!InReadOnlySpace(Cast<HeapObject>(prototype)) &&
             marking_state_->IsUnmarked(Cast<HeapObject>(prototype)))) {
          // The prototype is not marked, age the map.
          new_age = age - 1;
        } else {
          // The prototype and the constructor are marked, this map keeps only
          // transition tree alive, not JSObjects. Do not age the map.
          new_age = age;
        }
      } else {
        new_age = v8_flags.retain_maps_for_n_gc;
      }
      // Compact the array and update the age.
      if (new_age != age) {
        retained_maps->Set(i + 1, Smi::FromInt(new_age));
      }
    }
  }
}

void MarkCompactCollector::MarkLiveObjects() {
  TRACE_GC_ARG1(heap_->tracer(), GCTracer::Scope::MC_MARK,
                "UseBackgroundThreads", UseBackgroundThreadsInCycle());

  const bool was_marked_incrementally =
      !heap_->incremental_marking()->IsStopped();
  if (was_marked_incrementally) {
    auto* incremental_marking = heap_->incremental_marking();
    TRACE_GC_WITH_FLOW(
        heap_->tracer(), GCTracer::Scope::MC_MARK_FINISH_INCREMENTAL,
        incremental_marking->current_trace_id(), TRACE_EVENT_FLAG_FLOW_IN);
    DCHECK(incremental_marking->IsMajorMarking());
    incremental_marking->Stop();
    MarkingBarrier::PublishAll(heap_);
  }

#ifdef DEBUG
  DCHECK(state_ == PREPARE_GC);
  state_ = MARK_LIVE_OBJECTS;
#endif

  if (heap_->cpp_heap_) {
    CppHeap::From(heap_->cpp_heap_)
        ->EnterFinalPause(heap_->embedder_stack_state_);
  }

  RootMarkingVisitor root_visitor(this);

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_ROOTS);
    MarkRoots(&root_visitor);
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_CLIENT_HEAPS);
    MarkObjectsFromClientHeaps();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_RETAIN_MAPS);
    RetainMaps();
  }

  if (v8_flags.parallel_marking && UseBackgroundThreadsInCycle()) {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_FULL_CLOSURE_PARALLEL);
    parallel_marking_ = true;
    heap_->concurrent_marking()->RescheduleJobIfNeeded(
        GarbageCollector::MARK_COMPACTOR, TaskPriority::kUserBlocking);
    MarkTransitiveClosure();
    {
      TRACE_GC(heap_->tracer(),
               GCTracer::Scope::MC_MARK_FULL_CLOSURE_PARALLEL_JOIN);
      FinishConcurrentMarking();
    }
    parallel_marking_ = false;
  } else {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_FULL_CLOSURE_SERIAL);
    MarkTransitiveClosure();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_ROOTS);
    MarkRootsFromConservativeStack(&root_visitor);
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_MARK_FULL_CLOSURE);
    // Complete the transitive closure single-threaded to avoid races with
    // multiple threads when processing weak maps and embedder heaps.
    CHECK(heap_->concurrent_marking()->IsStopped());
    if (auto* cpp_heap = CppHeap::From(heap_->cpp_heap())) {
      cpp_heap->EnterProcessGlobalAtomicPause();
    }
    MarkTransitiveClosure();
    CHECK(local_marking_worklists_->IsEmpty());
    CHECK(
        local_weak_objects()->current_ephemerons_local.IsLocalAndGlobalEmpty());
    CHECK(local_weak_objects()
              ->discovered_ephemerons_local.IsLocalAndGlobalEmpty());
    CHECK(IsCppHeapMarkingFinished(heap_, local_marking_worklists_.get()));
    VerifyEphemeronMarking();
  }

  if (was_marked_incrementally) {
    // Disable the marking barrier after concurrent/parallel marking has
    // finished as it will reset page flags that share the same bitmap as
    // the evacuation candidate bit.
    MarkingBarrier::DeactivateAll(heap_);
    heap_->isolate()->traced_handles()->SetIsMarking(false);
  }

  epoch_++;
}

namespace {

class ParallelClearingJob final : public v8::JobTask {
 public:
  class ClearingItem {
   public:
    virtual ~ClearingItem() = default;
    virtual void Run(JobDelegate* delegate) = 0;
  };

  explicit ParallelClearingJob(MarkCompactCollector* collector)
      : collector_(collector) {}
  ~ParallelClearingJob() override = default;
  ParallelClearingJob(const ParallelClearingJob&) = delete;
  ParallelClearingJob& operator=(const ParallelClearingJob&) = delete;

  // v8::JobTask overrides.
  void Run(JobDelegate* delegate) override {
    std::unique_ptr<ClearingItem> item;
    {
      base::MutexGuard guard(&items_mutex_);
      item = std::move(items_.back());
      items_.pop_back();
    }
    item->Run(delegate);
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    base::MutexGuard guard(&items_mutex_);
    if (!v8_flags.parallel_weak_ref_clearing ||
        !collector_->UseBackgroundThreadsInCycle()) {
      return std::min<size_t>(items_.size(), 1);
    }
    return items_.size();
  }

  void Add(std::unique_ptr<ClearingItem> item) {
    items_.push_back(std::move(item));
  }

 private:
  MarkCompactCollector* collector_;
  mutable base::Mutex items_mutex_;
  std::vector<std::unique_ptr<ClearingItem>> items_;
};

class ClearStringTableJobItem final : public ParallelClearingJob::ClearingItem {
 public:
  explicit ClearStringTableJobItem(Isolate* isolate)
      : isolate_(isolate),
        trace_id_(reinterpret_cast<uint64_t>(this) ^
                  isolate->heap()->tracer()->CurrentEpoch(
                      GCTracer::Scope::MC_CLEAR_STRING_TABLE)) {}

  void Run(JobDelegate* delegate) final {
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate_);

    if (isolate_->OwnsStringTables()) {
      TRACE_GC1_WITH_FLOW(isolate_->heap()->tracer(),
                          GCTracer::Scope::MC_CLEAR_STRING_TABLE,
                          delegate->IsJoiningThread() ? ThreadKind::kMain
                                                      : ThreadKind::kBackground,
                          trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
      // Prune the string table removing all strings only pointed to by the
      // string table.  Cannot use string_table() here because the string
      // table is marked.
      StringTable* string_table = isolate_->string_table();
      InternalizedStringTableCleaner internalized_visitor(isolate_->heap());
      string_table->DropOldData();
      string_table->IterateElements(&internalized_visitor);
      string_table->NotifyElementsRemoved(
          internalized_visitor.PointersRemoved());
    }
  }

  uint64_t trace_id() const { return trace_id_; }

 private:
  Isolate* const isolate_;
  const uint64_t trace_id_;
};

}  // namespace

class FullStringForwardingTableCleaner final
    : public StringForwardingTableCleanerBase {
 public:
  explicit FullStringForwardingTableCleaner(Heap* heap)
      : StringForwardingTableCleanerBase(heap), heap_(heap) {
    USE(heap_);
  }

  // Transition all strings in the forwarding table to
  // ThinStrings/ExternalStrings and clear the table afterwards.
  void TransitionStrings() {
    DCHECK(!heap_->IsGCWithStack() ||
           v8_flags.transition_strings_during_gc_with_stack);
    StringForwardingTable* forwarding_table =
        isolate_->string_forwarding_table();
    forwarding_table->IterateElements(
        [&](StringForwardingTable::Record* record) {
          TransitionStrings(record);
        });
    forwarding_table->Reset();
  }

  // When performing GC with a stack, we conservatively assume that
  // the GC could have been triggered by optimized code. Optimized code
  // assumes that flat strings don't transition during GCs, so we are not
  // allowed to transition strings to ThinString/ExternalString in that
  // case.
  // Instead we mark forward objects to keep them alive and update entries
  // of evacuated objects later.
  void ProcessFullWithStack() {
    DCHECK(heap_->IsGCWithStack() &&
           !v8_flags.transition_strings_during_gc_with_stack);
    StringForwardingTable* forwarding_table =
        isolate_->string_forwarding_table();
    forwarding_table->IterateElements(
        [&](StringForwardingTable::Record* record) {
          MarkForwardObject(record);
        });
  }

 private:
  void MarkForwardObject(StringForwardingTable::Record* record) {
    Tagged<Object> original = record->OriginalStringObject(isolate_);
    if (!IsHeapObject(original)) {
      DCHECK_EQ(original, StringForwardingTable::deleted_element());
      return;
    }
    Tagged<String> original_string = Cast<String>(original);
    if (marking_state_->IsMarked(original_string)) {
      Tagged<Object> forward = record->ForwardStringObjectOrHash(isolate_);
      if (!IsHeapObject(forward) ||
          InReadOnlySpace(Cast<HeapObject>(forward))) {
        return;
      }
      marking_state_->TryMarkAndAccountLiveBytes(Cast<HeapObject>(forward));
    } else {
      DisposeExternalResource(record);
      record->set_original_string(StringForwardingTable::deleted_element());
    }
  }

  void TransitionStrings(StringForwardingTable::Record* record) {
    Tagged<Object> original = record->OriginalStringObject(isolate_);
    if (!IsHeapObject(original)) {
      DCHECK_EQ(original, StringForwardingTable::deleted_element());
      return;
    }
    if (marking_state_->IsMarked(Cast<HeapObject>(original))) {
      Tagged<String> original_string = Cast<String>(original);
      if (IsThinString(original_string)) {
        original_string = Cast<ThinString>(original_string)->actual();
      }
      TryExternalize(original_string, record);
      TryInternalize(original_string, record);
      original_string->set_raw_hash_field(record->raw_hash(isolate_));
    } else {
      DisposeExternalResource(record);
    }
  }

  void TryExternalize(Tagged<String> original_string,
                      StringForwardingTable::Record* record) {
    // If the string is already external, dispose the resource.
    if (IsExternalString(original_string)) {
      record->DisposeUnusedExternalResource(original_string);
      return;
    }

    bool is_one_byte;
    v8::String::ExternalStringResourceBase* external_resource =
        record->external_resource(&is_one_byte);
    if (external_resource == nullptr) return;

    if (is_one_byte) {
      original_string->MakeExternalDuringGC(
          isolate_,
          reinterpret_cast<v8::String::ExternalOneByteStringResource*>(
              external_resource));
    } else {
      original_string->MakeExternalDuringGC(
          isolate_, reinterpret_cast<v8::String::ExternalStringResource*>(
                        external_resource));
    }
  }

  void TryInternalize(Tagged<String> original_string,
                      StringForwardingTable::Record* record) {
    if (IsInternalizedString(original_string)) return;
    Tagged<Object> forward = record->ForwardStringObjectOrHash(isolate_);
    if (!IsHeapObject(forward)) {
      return;
    }
    Tagged<String> forward_string = Cast<String>(forward);

    // Mark the forwarded string to keep it alive.
    if (!InReadOnlySpace(forward_string)) {
      marking_state_->TryMarkAndAccountLiveBytes(forward_string);
    }
    // Transition the original string to a ThinString and override the
    // forwarding index with the correct hash.
    original_string->MakeThin(isolate_, forward_string);
    // Record the slot in the old-to-old remembered set. This is
    // required as the internalized string could be relocated during
    // compaction.
    ObjectSlot slot(&Cast<ThinString>(original_string)->actual_);
    MarkCompactCollector::RecordSlot(original_string, slot, forward_string);
  }

  Heap* const heap_;
};

namespace {

class SharedStructTypeRegistryCleaner final : public RootVisitor {
 public:
  explicit SharedStructTypeRegistryCleaner(Heap* heap) : heap_(heap) {}

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    UNREACHABLE();
  }

  void VisitRootPointers(Root root, const char* description,
                         OffHeapObjectSlot start,
                         OffHeapObjectSlot end) override {
    DCHECK_EQ(root, Root::kSharedStructTypeRegistry);
    // The SharedStructTypeRegistry holds the canonical SharedStructType
    // instance maps weakly. Visit all Map pointers in [start, end), deleting
    // it if unmarked.
    auto* marking_state = heap_->marking_state();
    Isolate* const isolate = heap_->isolate();
    for (OffHeapObjectSlot p = start; p < end; p++) {
      Tagged<Object> o = p.load(isolate);
      DCHECK(!IsString(o));
      if (IsMap(o)) {
        Tagged<HeapObject> map = Cast<Map>(o);
        DCHECK(InAnySharedSpace(map));
        if (!InReadOnlySpace(map) && marking_state->IsUnmarked(map)) {
          elements_removed_++;
          p.store(SharedStructTypeRegistry::deleted_element());
        }
      }
    }
  }

  int ElementsRemoved() const { return elements_removed_; }

 private:
  Heap* heap_;
  int elements_removed_ = 0;
};

class ClearSharedStructTypeRegistryJobItem final
    : public ParallelClearingJob::ClearingItem {
 public:
  explicit ClearSharedStructTypeRegistryJobItem(Isolate* isolate)
      : isolate_(isolate) {
    DCHECK(isolate->is_shared_space_isolate());
    DCHECK_NOT_NULL(isolate->shared_struct_type_registry());
  }

  void Run(JobDelegate* delegate) final {
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate_);

    auto* registry = isolate_->shared_struct_type_registry();
    SharedStructTypeRegistryCleaner cleaner(isolate_->heap());
    registry->IterateElements(isolate_, &cleaner);
    registry->NotifyElementsRemoved(cleaner.ElementsRemoved());
  }

 private:
  Isolate* const isolate_;
};

}  // namespace

class MarkCompactCollector::ClearTrivialWeakRefJobItem final
    : public ParallelClearingJob::ClearingItem {
 public:
  explicit ClearTrivialWeakRefJobItem(MarkCompactCollector* collector)
      : collector_(collector),
        trace_id_(reinterpret_cast<uint64_t>(this) ^
                  collector->heap()->tracer()->CurrentEpoch(
                      GCTracer::Scope::MC_CLEAR_WEAK_REFERENCES_TRIVIAL)) {}

  void Run(JobDelegate* delegate) final {
    Heap* heap = collector_->heap();

    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(heap->isolate());

    TRACE_GC1_WITH_FLOW(heap->tracer(),
                        GCTracer::Scope::MC_CLEAR_WEAK_REFERENCES_TRIVIAL,
                        delegate->IsJoiningThread() ? ThreadKind::kMain
                                                    : ThreadKind::kBackground,
                        trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
    collector_->ClearTrivialWeakReferences();
  }

  uint64_t trace_id() const { return trace_id_; }

 private:
  MarkCompactCollector* collector_;
  const uint64_t trace_id_;
};

class MarkCompactCollector::FilterNonTrivialWeakRefJobItem final
    : public ParallelClearingJob::ClearingItem {
 public:
  explicit FilterNonTrivialWeakRefJobItem(MarkCompactCollector* collector)
      : collector_(collector),
        trace_id_(
            reinterpret_cast<uint64_t>(this) ^
            collector->heap()->tracer()->CurrentEpoch(
                GCTracer::Scope::MC_CLEAR_WEAK_REFERENCES_FILTER_NON_TRIVIAL)) {
  }

  void Run(JobDelegate* delegate) final {
    Heap* heap = collector_->heap();

    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(heap->isolate());

    TRACE_GC1_WITH_FLOW(
        heap->tracer(),
        GCTracer::Scope::MC_CLEAR_WEAK_REFERENCES_FILTER_NON_TRIVIAL,
        delegate->IsJoiningThread() ? ThreadKind::kMain
                                    : ThreadKind::kBackground,
        trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
    collector_->FilterNonTrivialWeakReferences();
  }

  uint64_t trace_id() const { return trace_id_; }

 private:
  MarkCompactCollector* collector_;
  const uint64_t trace_id_;
};

void MarkCompactCollector::ClearNonLiveReferences() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR);

  Isolate* const isolate = heap_->isolate();
  if (isolate->OwnsStringTables()) {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MC_CLEAR_STRING_FORWARDING_TABLE);
    // Clear string forwarding table. Live strings are transitioned to
    // ThinStrings/ExternalStrings in the cleanup process, if this is a GC
    // without stack.
    // Clearing the string forwarding table must happen before clearing the
    // string table, as entries in the forwarding table can keep internalized
    // strings alive.
    FullStringForwardingTableCleaner forwarding_table_cleaner(heap_);
    if (!heap_->IsGCWithStack() ||
        v8_flags.transition_strings_during_gc_with_stack) {
      forwarding_table_cleaner.TransitionStrings();
    } else {
      forwarding_table_cleaner.ProcessFullWithStack();
    }
  }

  {
    // Clear Isolate::topmost_script_having_context slot if it's not alive.
    Tagged<Object> maybe_caller_context =
        isolate->topmost_script_having_context();
    if (maybe_caller_context.IsHeapObject() &&
        !marking_state_->IsMarked(Cast<HeapObject>(maybe_caller_context))) {
      isolate->clear_topmost_script_having_context();
    }
  }

  std::unique_ptr<JobHandle> clear_string_table_job_handle;
  {
    auto job = std::make_unique<ParallelClearingJob>(this);
    auto job_item = std::make_unique<ClearStringTableJobItem>(isolate);
    const uint64_t trace_id = job_item->trace_id();
    job->Add(std::move(job_item));
    TRACE_GC_NOTE_WITH_FLOW("ClearStringTableJob started", trace_id,
                            TRACE_EVENT_FLAG_FLOW_OUT);
    if (isolate->is_shared_space_isolate() &&
        isolate->shared_struct_type_registry()) {
      auto registry_job_item =
          std::make_unique<ClearSharedStructTypeRegistryJobItem>(isolate);
      job->Add(std::move(registry_job_item));
    }
    clear_string_table_job_handle = V8::GetCurrentPlatform()->CreateJob(
        TaskPriority::kUserBlocking, std::move(job));
  }
  if (v8_flags.parallel_weak_ref_clearing && UseBackgroundThreadsInCycle()) {
    clear_string_table_job_handle->NotifyConcurrencyIncrease();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_EXTERNAL_STRING_TABLE);
    ExternalStringTableCleanerVisitor<ExternalStringTableCleaningMode::kAll>
        external_visitor(heap_);
    heap_->external_string_table_.IterateAll(&external_visitor);
    heap_->external_string_table_.CleanUpAll();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_WEAK_GLOBAL_HANDLES);
    // We depend on `IterateWeakRootsForPhantomHandles()` being called before
    // `ProcessOldCodeCandidates()` in order to identify flushed bytecode in the
    // CPU profiler.
    isolate->global_handles()->IterateWeakRootsForPhantomHandles(
        &IsUnmarkedHeapObject);
    isolate->traced_handles()->ResetDeadNodes(&IsUnmarkedHeapObject);

    if (isolate->is_shared_space_isolate()) {
      isolate->global_safepoint()->IterateClientIsolates([](Isolate* client) {
        client->global_handles()->IterateWeakRootsForPhantomHandles(
            &IsUnmarkedSharedHeapObject);
        // No need to reset traced handles since they are always strong.
      });
    }
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_FLUSHABLE_BYTECODE);
    // `ProcessFlushedBaselineCandidates()` must be called after
    // `ProcessOldCodeCandidates()` so that we correctly set the code object on
    // the JSFunction after flushing.
    ProcessOldCodeCandidates();
    ProcessFlushedBaselineCandidates();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_FLUSHED_JS_FUNCTIONS);
    ClearFlushedJsFunctions();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_WEAK_LISTS);
    // Process the weak references.
    MarkCompactWeakObjectRetainer mark_compact_object_retainer(marking_state_);
    heap_->ProcessAllWeakReferences(&mark_compact_object_retainer);
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_MAPS);
    // ClearFullMapTransitions must be called before weak references are
    // cleared.
    ClearFullMapTransitions();
    // Weaken recorded strong DescriptorArray objects. This phase can
    // potentially move everywhere after `ClearFullMapTransitions()`.
    WeakenStrongDescriptorArrays();
  }

  // Start two parallel jobs: one for clearing trivial weak references and one
  // for filtering out non-trivial weak references that will not be cleared.
  // Both jobs read the values of weak references and the corresponding
  // mark bits. They cannot start before the following methods have finished,
  // because these may change the values of weak references and/or mark more
  // objects, thus creating data races:
  //   - ProcessOldCodeCandidates
  //   - ProcessAllWeakReferences
  //   - ClearFullMapTransitions
  //   - WeakenStrongDescriptorArrays
  // The two jobs could be merged but it's convenient to keep them separate,
  // as they are joined at different times. The filtering job must be joined
  // before proceeding to the actual clearing of non-trivial weak references,
  // whereas the job for clearing trivial weak references can be joined at the
  // end of this method.
  std::unique_ptr<JobHandle> clear_trivial_weakrefs_job_handle;
  {
    auto job = std::make_unique<ParallelClearingJob>(this);
    auto job_item = std::make_unique<ClearTrivialWeakRefJobItem>(this);
    const uint64_t trace_id = job_item->trace_id();
    job->Add(std::move(job_item));
    TRACE_GC_NOTE_WITH_FLOW("ClearTrivialWeakRefJob started", trace_id,
                            TRACE_EVENT_FLAG_FLOW_OUT);
    clear_trivial_weakrefs_job_handle = V8::GetCurrentPlatform()->CreateJob(
        TaskPriority::kUserBlocking, std::move(job));
  }
  std::unique_ptr<JobHandle> filter_non_trivial_weakrefs_job_handle;
  {
    auto job = std::make_unique<ParallelClearingJob>(this);
    auto job_item = std::make_unique<FilterNonTrivialWeakRefJobItem>(this);
    const uint64_t trace_id = job_item->trace_id();
    job->Add(std::move(job_item));
    TRACE_GC_NOTE_WITH_FLOW("FilterNonTrivialWeakRefJob started", trace_id,
                            TRACE_EVENT_FLAG_FLOW_OUT);
    filter_non_trivial_weakrefs_job_handle =
        V8::GetCurrentPlatform()->CreateJob(TaskPriority::kUserBlocking,
                                            std::move(job));
  }
  if (v8_flags.parallel_weak_ref_clearing && UseBackgroundThreadsInCycle()) {
    clear_trivial_weakrefs_job_handle->NotifyConcurrencyIncrease();
    filter_non_trivial_weakrefs_job_handle->NotifyConcurrencyIncrease();
  }

#ifdef V8_COMPRESS_POINTERS
  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_SWEEP_EXTERNAL_POINTER_TABLE);
    // External pointer table sweeping needs to happen before evacuating live
    // objects as it may perform table compaction, which requires objects to
    // still be at the same location as during marking.
    //
    // Note we explicitly do NOT run SweepAndCompact on
    // read_only_external_pointer_space since these entries are all immortal by
    // definition.
    isolate->external_pointer_table().EvacuateAndSweepAndCompact(
        isolate->heap()->old_external_pointer_space(),
        isolate->heap()->young_external_pointer_space(), isolate->counters());
    isolate->heap()->young_external_pointer_space()->AssertEmpty();
    if (isolate->owns_shareable_data()) {
      isolate->shared_external_pointer_table().SweepAndCompact(
          isolate->shared_external_pointer_space(), isolate->counters());
    }
    isolate->cpp_heap_pointer_table().SweepAndCompact(
        isolate->heap()->cpp_heap_pointer_space(), isolate->counters());
  }
#endif  // V8_COMPRESS_POINTERS

#ifdef V8_ENABLE_SANDBOX
  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_SWEEP_TRUSTED_POINTER_TABLE);
    isolate->trusted_pointer_table().Sweep(heap_->trusted_pointer_space(),
                                           isolate->counters());
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_SWEEP_CODE_POINTER_TABLE);
    GetProcessWideCodePointerTable()->Sweep(heap_->code_pointer_space(),
                                            isolate->counters());
  }
#endif  // V8_ENABLE_SANDBOX

#ifdef V8_ENABLE_LEAPTIERING
  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_SWEEP_JS_DISPATCH_TABLE);
    GetProcessWideJSDispatchTable()->Sweep(heap_->js_dispatch_table_space(),
                                           isolate->counters());
  }
#endif  // V8_ENABLE_LEAPTIERING

  {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MC_CLEAR_WEAK_REFERENCES_JOIN_FILTER_JOB);
    filter_non_trivial_weakrefs_job_handle->Join();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_WEAKNESS_HANDLING);
    ClearNonTrivialWeakReferences();
    ClearWeakCollections();
    ClearJSWeakRefs();
  }

  PROFILE(heap_->isolate(), WeakCodeClearEvent());

  {
    // This method may be called from within a DisallowDeoptimizations scope.
    // Temporarily allow deopts for marking code for deopt. This is not doing
    // the deopt yet and the actual deopts will be bailed out on later if the
    // current safepoint is not safe for deopts.
    // TODO(357636610): Reconsider whether the DisallowDeoptimization scopes are
    // truly needed.
    AllowDeoptimization allow_deoptimization(heap_->isolate());
    MarkDependentCodeForDeoptimization();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_JOIN_JOB);
    clear_string_table_job_handle->Join();
    clear_trivial_weakrefs_job_handle->Join();
  }

  if (v8_flags.sticky_mark_bits) {
    // TODO(333906585): Consider adjusting the dchecks that happen on clearing
    // and move this phase into MarkingBarrier::DeactivateAll.
    heap()->DeactivateMajorGCInProgressFlag();
  }

  DCHECK(weak_objects_.transition_arrays.IsEmpty());
  DCHECK(weak_objects_.weak_references_trivial.IsEmpty());
  DCHECK(weak_objects_.weak_references_non_trivial.IsEmpty());
  DCHECK(weak_objects_.weak_references_non_trivial_unmarked.IsEmpty());
  DCHECK(weak_objects_.weak_objects_in_code.IsEmpty());
  DCHECK(weak_objects_.js_weak_refs.IsEmpty());
  DCHECK(weak_objects_.weak_cells.IsEmpty());
  DCHECK(weak_objects_.code_flushing_candidates.IsEmpty());
  DCHECK(weak_objects_.baseline_flushing_candidates.IsEmpty());
  DCHECK(weak_objects_.flushed_js_functions.IsEmpty());
}

void MarkCompactCollector::MarkDependentCodeForDeoptimization() {
  HeapObjectAndCode weak_object_in_code;
  while (local_weak_objects()->weak_objects_in_code_local.Pop(
      &weak_object_in_code)) {
    Tagged<HeapObject> object = weak_object_in_code.heap_object;
    Tagged<Code> code = weak_object_in_code.code;
    if (!non_atomic_marking_state_->IsMarked(object) &&
        !code->embedded_objects_cleared()) {
      if (!code->marked_for_deoptimization()) {
        code->SetMarkedForDeoptimization(heap_->isolate(), "weak objects");
        have_code_to_deoptimize_ = true;
      }
      code->ClearEmbeddedObjects(heap_);
      DCHECK(code->embedded_objects_cleared());
    }
  }
}

void MarkCompactCollector::ClearPotentialSimpleMapTransition(
    Tagged<Map> dead_target) {
  DCHECK(non_atomic_marking_state_->IsUnmarked(dead_target));
  Tagged<Object> potential_parent = dead_target->constructor_or_back_pointer();
  if (IsMap(potential_parent)) {
    Tagged<Map> parent = Cast<Map>(potential_parent);
    DisallowGarbageCollection no_gc_obviously;
    if (non_atomic_marking_state_->IsMarked(parent) &&
        TransitionsAccessor(heap_->isolate(), parent)
            .HasSimpleTransitionTo(dead_target)) {
      ClearPotentialSimpleMapTransition(parent, dead_target);
    }
  }
}

void MarkCompactCollector::ClearPotentialSimpleMapTransition(
    Tagged<Map> map, Tagged<Map> dead_target) {
  DCHECK(!map->is_prototype_map());
  DCHECK(!dead_target->is_prototype_map());
  DCHECK_EQ(map->raw_transitions(), MakeWeak(dead_target));
  // Take ownership of the descriptor array.
  int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  Tagged<DescriptorArray> descriptors =
      map->instance_descriptors(heap_->isolate());
  if (descriptors == dead_target->instance_descriptors(heap_->isolate()) &&
      number_of_own_descriptors > 0) {
    TrimDescriptorArray(map, descriptors);
    DCHECK(descriptors->number_of_descriptors() == number_of_own_descriptors);
  }
}

bool MarkCompactCollector::SpecialClearMapSlot(Tagged<HeapObject> host,
                                               Tagged<Map> map,
                                               HeapObjectSlot slot) {
  ClearPotentialSimpleMapTransition(map);

  // Special handling for clearing field type entries, identified by their host
  // being a descriptor array.
  // TODO(olivf): This whole special handling of field-type clearing
  // could be replaced by eagerly triggering field type dependencies and
  // generalizing field types, as soon as a field-type map becomes
  // unstable.
  if (IsDescriptorArray(host)) {
    // We want to distinguish two cases:
    // 1. There are no instances of the descriptor owner's map left.
    // 2. The field type is not up to date because the stored object
    //    migrated away to a different map.
    // In case (1) it makes sense to clear the field type such that we
    // can learn a new one should we ever start creating instances
    // again.
    // In case (2) we must not re-learn a new field type. Doing so could
    // lead us to learning a field type that is not consistent with
    // still existing object's contents. To conservatively identify case
    // (1) we check the stability of the dead map.
    MaybeObjectSlot location(slot);
    if (map->is_stable() && FieldType::kFieldTypesCanBeClearedOnGC) {
      location.store(FieldType::None());
    } else {
      location.store(FieldType::Any());
    }
    return true;
  }
  return false;
}

void MarkCompactCollector::FlushBytecodeFromSFI(
    Tagged<SharedFunctionInfo> shared_info) {
  DCHECK(shared_info->HasBytecodeArray());

  // Retain objects required for uncompiled data.
  Tagged<String> inferred_name = shared_info->inferred_name();
  int start_position = shared_info->StartPosition();
  int end_position = shared_info->EndPosition();

  shared_info->DiscardCompiledMetadata(
      heap_->isolate(),
      [](Tagged<HeapObject> object, ObjectSlot slot,
         Tagged<HeapObject> target) { RecordSlot(object, slot, target); });

  // The size of the bytecode array should always be larger than an
  // UncompiledData object.
  static_assert(BytecodeArray::SizeFor(0) >=
                UncompiledDataWithoutPreparseData::kSize);

  // Replace the bytecode with an uncompiled data object.
  Tagged<BytecodeArray> bytecode_array =
      shared_info->GetBytecodeArray(heap_->isolate());

#ifdef V8_ENABLE_SANDBOX
  // Zap the old entry in the trusted pointer table.
  TrustedPointerTable& table = heap_->isolate()->trusted_pointer_table();
  IndirectPointerSlot self_indirect_pointer_slot =
      bytecode_array->RawIndirectPointerField(
          BytecodeArray::kSelfIndirectPointerOffset,
          kBytecodeArrayIndirectPointerTag);
  table.Zap(self_indirect_pointer_slot.Relaxed_LoadHandle());
#endif

  Tagged<HeapObject> compiled_data = bytecode_array;
  Address compiled_data_start = compiled_data.address();
  int compiled_data_size = ALIGN_TO_ALLOCATION_ALIGNMENT(compiled_data->Size());
  MutablePageMetadata* chunk =
      MutablePageMetadata::FromAddress(compiled_data_start);

  // Clear any recorded slots for the compiled data as being invalid.
  RememberedSet<OLD_TO_NEW>::RemoveRange(
      chunk, compiled_data_start, compiled_data_start + compiled_data_size,
      SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<OLD_TO_NEW_BACKGROUND>::RemoveRange(
      chunk, compiled_data_start, compiled_data_start + compiled_data_size,
      SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<OLD_TO_SHARED>::RemoveRange(
      chunk, compiled_data_start, compiled_data_start + compiled_data_size,
      SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<OLD_TO_OLD>::RemoveRange(
      chunk, compiled_data_start, compiled_data_start + compiled_data_size,
      SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<TRUSTED_TO_TRUSTED>::RemoveRange(
      chunk, compiled_data_start, compiled_data_start + compiled_data_size,
      SlotSet::FREE_EMPTY_BUCKETS);

  // Swap the map, using set_map_after_allocation to avoid verify heap checks
  // which are not necessary since we are doing this during the GC atomic pause.
  compiled_data->set_map_after_allocation(
      ReadOnlyRoots(heap_).uncompiled_data_without_preparse_data_map(),
      SKIP_WRITE_BARRIER);

  // Create a filler object for any left over space in the bytecode array.
  if (!heap_->IsLargeObject(compiled_data)) {
    const int aligned_filler_offset =
        ALIGN_TO_ALLOCATION_ALIGNMENT(UncompiledDataWithoutPreparseData::kSize);
    heap_->CreateFillerObjectAt(compiled_data.address() + aligned_filler_offset,
                                compiled_data_size - aligned_filler_offset);
  }

  // Initialize the uncompiled data.
  Tagged<UncompiledData> uncompiled_data = Cast<UncompiledData>(compiled_data);

  uncompiled_data->InitAfterBytecodeFlush(
      heap_->isolate(), inferred_name, start_position, end_position,
      [](Tagged<HeapObject> object, ObjectSlot slot,
         Tagged<HeapObject> target) { RecordSlot(object, slot, target); });

  // Mark the uncompiled data as black, and ensure all fields have already been
  // marked.
  DCHECK(MarkingHelper::GetLivenessMode(heap_, inferred_name) ==
             MarkingHelper::LivenessMode::kAlwaysLive ||
         marking_state_->IsMarked(inferred_name));
  marking_state_->TryMarkAndAccountLiveBytes(uncompiled_data);

#ifdef V8_ENABLE_SANDBOX
  // Mark the new entry in the trusted pointer table as alive.
  TrustedPointerTable::Space* space = heap_->trusted_pointer_space();
  table.Mark(space, self_indirect_pointer_slot.Relaxed_LoadHandle());
#endif

  shared_info->set_uncompiled_data(uncompiled_data);
  DCHECK(!shared_info->is_compiled());
}

void MarkCompactCollector::ProcessOldCodeCandidates() {
  DCHECK(v8_flags.flush_bytecode || v8_flags.flush_baseline_code ||
         weak_objects_.code_flushing_candidates.IsEmpty());
  Tagged<SharedFunctionInfo> flushing_candidate;
  int number_of_flushed_sfis = 0;
  while (local_weak_objects()->code_flushing_candidates_local.Pop(
      &flushing_candidate)) {
    bool is_bytecode_live;
    if (v8_flags.flush_baseline_code && flushing_candidate->HasBaselineCode()) {
      is_bytecode_live = ProcessOldBaselineSFI(flushing_candidate);
    } else {
      is_bytecode_live = ProcessOldBytecodeSFI(flushing_candidate);
    }

    if (!is_bytecode_live) number_of_flushed_sfis++;

    // Now record the data slots, which have been updated to an uncompiled
    // data, Baseline code or BytecodeArray which is still alive.
#ifndef V8_ENABLE_SANDBOX
    // If the sandbox is enabled, the slot contains an indirect pointer which
    // does not need to be updated during mark-compact (because the pointer in
    // the pointer table will be updated), so no action is needed here.
    ObjectSlot slot = flushing_candidate->RawField(
        SharedFunctionInfo::kTrustedFunctionDataOffset);
    if (IsHeapObject(*slot)) {
      RecordSlot(flushing_candidate, slot, Cast<HeapObject>(*slot));
    }
#endif
  }

  if (v8_flags.trace_flush_code) {
    PrintIsolate(heap_->isolate(), "%d flushed SharedFunctionInfo(s)\n",
                 number_of_flushed_sfis);
  }
}

bool MarkCompactCollector::ProcessOldBytecodeSFI(
    Tagged<SharedFunctionInfo> flushing_candidate) {
  // During flushing a BytecodeArray is transformed into an UncompiledData
  // in place. Seeing an UncompiledData here implies that another
  // SharedFunctionInfo had a reference to the same BytecodeArray and
  // flushed it before processing this candidate. This can happen when using
  // CloneSharedFunctionInfo().
  Isolate* const isolate = heap_->isolate();
  const bool bytecode_already_decompiled =
      flushing_candidate->HasUncompiledData();
  const bool is_bytecode_live =
      !bytecode_already_decompiled &&
      non_atomic_marking_state_->IsMarked(
          flushing_candidate->GetBytecodeArray(isolate));

  if (!is_bytecode_live) {
    FlushSFI(flushing_candidate, bytecode_already_decompiled);
  }
  return is_bytecode_live;
}

bool MarkCompactCollector::ProcessOldBaselineSFI(
    Tagged<SharedFunctionInfo> flushing_candidate) {
  Tagged<Code> baseline_code = flushing_candidate->baseline_code(kAcquireLoad);
  // Safe to do a relaxed load here since the Code was acquire-loaded.
  Tagged<InstructionStream> baseline_istream =
      baseline_code->instruction_stream(baseline_code->code_cage_base(),
                                        kRelaxedLoad);
  Tagged<HeapObject> baseline_bytecode_or_interpreter_data =
      baseline_code->bytecode_or_interpreter_data();

  // During flushing a BytecodeArray is transformed into an UncompiledData
  // in place. Seeing an UncompiledData here implies that another
  // SharedFunctionInfo had a reference to the same BytecodeArray and
  // flushed it before processing this candidate. This can happen when using
  // CloneSharedFunctionInfo().
  const bool bytecode_already_decompiled =
      IsUncompiledData(baseline_bytecode_or_interpreter_data, heap_->isolate());
  const bool is_bytecode_live =
      !bytecode_already_decompiled &&
      non_atomic_marking_state_->IsMarked(
          flushing_candidate->GetBytecodeArray(heap_->isolate()));

  if (non_atomic_marking_state_->IsMarked(baseline_istream)) {
    // Currently baseline code holds bytecode array strongly and it is
    // always ensured that bytecode is live if baseline code is live. Hence
    // baseline code can safely load bytecode array without any additional
    // checks. In future if this changes we need to update these checks to
    // flush code if the bytecode is not live and also update baseline code
    // to bailout if there is no bytecode.
    DCHECK(is_bytecode_live);

    // Regardless of whether the Code is a Code or
    // the InstructionStream itself, if the InstructionStream is live then
    // the Code has to be live and will have been marked via
    // the owning JSFunction.
    DCHECK(non_atomic_marking_state_->IsMarked(baseline_code));
  } else if (is_bytecode_live || bytecode_already_decompiled) {
    // Reset the function_data field to the BytecodeArray, InterpreterData,
    // or UncompiledData found on the baseline code. We can skip this step
    // if the BytecodeArray is not live and not already decompiled, because
    // FlushBytecodeFromSFI below will set the function_data field.
    flushing_candidate->FlushBaselineCode();
  }

  if (!is_bytecode_live) {
    FlushSFI(flushing_candidate, bytecode_already_decompiled);
  }
  return is_bytecode_live;
}

void MarkCompactCollector::FlushSFI(Tagged<SharedFunctionInfo> sfi,
                                    bool bytecode_already_decompiled) {
  // If baseline code flushing is disabled we should only flush bytecode
  // from functions that don't have baseline data.
  DCHECK(v8_flags.flush_baseline_code || !sfi->HasBaselineCode());

  if (bytecode_already_decompiled) {
    sfi->DiscardCompiledMetadata(
        heap_->isolate(),
        [](Tagged<HeapObject> object, ObjectSlot slot,
           Tagged<HeapObject> target) { RecordSlot(object, slot, target); });
  } else {
    // If the BytecodeArray is dead, flush it, which will replace the field
    // with an uncompiled data object.
    FlushBytecodeFromSFI(sfi);
  }
}

void MarkCompactCollector::ClearFlushedJsFunctions() {
  DCHECK(v8_flags.flush_bytecode ||
         weak_objects_.flushed_js_functions.IsEmpty());
  Tagged<JSFunction> flushed_js_function;
  while (local_weak_objects()->flushed_js_functions_local.Pop(
      &flushed_js_function)) {
    auto gc_notify_updated_slot = [](Tagged<HeapObject> object, ObjectSlot slot,
                                     Tagged<Object> target) {
      RecordSlot(object, slot, Cast<HeapObject>(target));
    };
    flushed_js_function->ResetIfCodeFlushed(heap_->isolate(),
                                            gc_notify_updated_slot);
  }
}

void MarkCompactCollector::ProcessFlushedBaselineCandidates() {
  DCHECK(v8_flags.flush_baseline_code ||
         weak_objects_.baseline_flushing_candidates.IsEmpty());
  Tagged<JSFunction> flushed_js_function;
  while (local_weak_objects()->baseline_flushing_candidates_local.Pop(
      &flushed_js_function)) {
    auto gc_notify_updated_slot = [](Tagged<HeapObject> object, ObjectSlot slot,
                                     Tagged<Object> target) {
      RecordSlot(object, slot, Cast<HeapObject>(target));
    };
    flushed_js_function->ResetIfCodeFlushed(heap_->isolate(),
                                            gc_notify_updated_slot);

#ifndef V8_ENABLE_SANDBOX
    // Record the code slot that has been updated either to CompileLazy,
    // InterpreterEntryTrampoline or baseline code.
    // This is only necessary when the sandbox is not enabled. If it is, the
    // Code objects are referenced through a pointer table indirection and so
    // remembered slots are not necessary as the Code object will update its
    // entry in the pointer table when it is relocated.
    ObjectSlot slot = flushed_js_function->RawField(JSFunction::kCodeOffset);
    RecordSlot(flushed_js_function, slot, Cast<HeapObject>(*slot));
#endif
  }
}

void MarkCompactCollector::ClearFullMapTransitions() {
  Tagged<TransitionArray> array;
  Isolate* const isolate = heap_->isolate();
  ReadOnlyRoots roots(isolate);
  while (local_weak_objects()->transition_arrays_local.Pop(&array)) {
    int num_transitions = array->number_of_entries();
    if (num_transitions > 0) {
        Tagged<Map> map;
        // The array might contain "undefined" elements because it's not yet
        // filled. Allow it.
        if (array->GetTargetIfExists(0, isolate, &map)) {
          DCHECK(!map.is_null());  // Weak pointers aren't cleared yet.
          Tagged<Object> constructor_or_back_pointer =
              map->constructor_or_back_pointer();
          if (IsSmi(constructor_or_back_pointer)) {
            DCHECK(isolate->has_active_deserializer());
            DCHECK_EQ(constructor_or_back_pointer,
                      Smi::uninitialized_deserialization_value());
            continue;
          }
          Tagged<Map> parent = Cast<Map>(map->constructor_or_back_pointer());
          bool parent_is_alive = non_atomic_marking_state_->IsMarked(parent);
          Tagged<DescriptorArray> descriptors =
              parent_is_alive ? parent->instance_descriptors(isolate)
                              : Tagged<DescriptorArray>();
          bool descriptors_owner_died =
              CompactTransitionArray(parent, array, descriptors);
          if (descriptors_owner_died) {
            TrimDescriptorArray(parent, descriptors);
          }
        }
      }
  }
}

// Returns false if no maps have died, or if the transition array is
// still being deserialized.
bool MarkCompactCollector::TransitionArrayNeedsCompaction(
    Tagged<TransitionArray> transitions, int num_transitions) {
  ReadOnlyRoots roots(heap_->isolate());
  for (int i = 0; i < num_transitions; ++i) {
    Tagged<MaybeObject> raw_target = transitions->GetRawTarget(i);
    if (raw_target.IsSmi()) {
      // This target is still being deserialized,
      DCHECK(heap_->isolate()->has_active_deserializer());
      DCHECK_EQ(raw_target.ToSmi(), Smi::uninitialized_deserialization_value());
#ifdef DEBUG
      // Targets can only be dead iff this array is fully deserialized.
      for (int j = 0; j < num_transitions; ++j) {
        DCHECK_IMPLIES(
            !transitions->GetRawTarget(j).IsSmi(),
            !non_atomic_marking_state_->IsUnmarked(transitions->GetTarget(j)));
      }
#endif
      return false;
    } else if (non_atomic_marking_state_->IsUnmarked(
                   TransitionsAccessor::GetTargetFromRaw(raw_target))) {
#ifdef DEBUG
      // Targets can only be dead iff this array is fully deserialized.
      for (int j = 0; j < num_transitions; ++j) {
        DCHECK(!transitions->GetRawTarget(j).IsSmi());
      }
#endif
      return true;
    }
  }
  return false;
}

bool MarkCompactCollector::CompactTransitionArray(
    Tagged<Map> map, Tagged<TransitionArray> transitions,
    Tagged<DescriptorArray> descriptors) {
  DCHECK(!map->is_prototype_map());
  int num_transitions = transitions->number_of_entries();
  if (!TransitionArrayNeedsCompaction(transitions, num_transitions)) {
    return false;
  }
  ReadOnlyRoots roots(heap_->isolate());
  bool descriptors_owner_died = false;
  int transition_index = 0;
  // Compact all live transitions to the left.
  for (int i = 0; i < num_transitions; ++i) {
    Tagged<Map> target = transitions->GetTarget(i);
    DCHECK_EQ(target->constructor_or_back_pointer(), map);

    if (non_atomic_marking_state_->IsUnmarked(target)) {
      if (!descriptors.is_null() &&
          target->instance_descriptors(heap_->isolate()) == descriptors) {
        DCHECK(!target->is_prototype_map());
        descriptors_owner_died = true;
      }
      continue;
    }

    if (i != transition_index) {
      Tagged<Name> key = transitions->GetKey(i);
      transitions->SetKey(transition_index, key);
      HeapObjectSlot key_slot = transitions->GetKeySlot(transition_index);
      RecordSlot(transitions, key_slot, key);
      Tagged<MaybeObject> raw_target = transitions->GetRawTarget(i);
      transitions->SetRawTarget(transition_index, raw_target);
      HeapObjectSlot target_slot = transitions->GetTargetSlot(transition_index);
      RecordSlot(transitions, target_slot, raw_target.GetHeapObject());
    }
    transition_index++;
  }
  // If there are no transitions to be cleared, return.
  if (transition_index == num_transitions) {
    DCHECK(!descriptors_owner_died);
    return false;
  }
  // Note that we never eliminate a transition array, though we might right-trim
  // such that number_of_transitions() == 0. If this assumption changes,
  // TransitionArray::Insert() will need to deal with the case that a transition
  // array disappeared during GC.
  int old_capacity_in_entries = transitions->Capacity();
  if (transition_index < old_capacity_in_entries) {
    int old_capacity = transitions->length();
    static_assert(TransitionArray::kEntryKeyIndex == 0);
    DCHECK_EQ(TransitionArray::ToKeyIndex(old_capacity_in_entries),
              old_capacity);
    int new_capacity = TransitionArray::ToKeyIndex(transition_index);
    heap_->RightTrimArray(transitions, new_capacity, old_capacity);
    transitions->SetNumberOfTransitions(transition_index);
  }
  return descriptors_owner_died;
}

void MarkCompactCollector::RightTrimDescriptorArray(
    Tagged<DescriptorArray> array, int descriptors_to_trim) {
  int old_nof_all_descriptors = array->number_of_all_descriptors();
  int new_nof_all_descriptors = old_nof_all_descriptors - descriptors_to_trim;
  DCHECK_LT(0, descriptors_to_trim);
  DCHECK_LE(0, new_nof_all_descriptors);
  Address start = array->GetDescriptorSlot(new_nof_all_descriptors).address();
  Address end = array->GetDescriptorSlot(old_nof_all_descriptors).address();
  MutablePageMetadata* chunk = MutablePageMetadata::FromHeapObject(array);
  RememberedSet<OLD_TO_NEW>::RemoveRange(chunk, start, end,
                                         SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<OLD_TO_NEW_BACKGROUND>::RemoveRange(
      chunk, start, end, SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<OLD_TO_SHARED>::RemoveRange(chunk, start, end,
                                            SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<OLD_TO_OLD>::RemoveRange(chunk, start, end,
                                         SlotSet::FREE_EMPTY_BUCKETS);
  if (V8_COMPRESS_POINTERS_8GB_BOOL) {
    Address aligned_start = ALIGN_TO_ALLOCATION_ALIGNMENT(start);
    Address aligned_end = ALIGN_TO_ALLOCATION_ALIGNMENT(end);
    if (aligned_start < aligned_end) {
      heap_->CreateFillerObjectAt(
          aligned_start, static_cast<int>(aligned_end - aligned_start));
    }
    if (heap::ShouldZapGarbage()) {
      Address zap_end = std::min(aligned_start, end);
      MemsetTagged(ObjectSlot(start),
                   Tagged<Object>(static_cast<Address>(kZapValue)),
                   (zap_end - start) >> kTaggedSizeLog2);
    }
  } else {
    heap_->CreateFillerObjectAt(start, static_cast<int>(end - start));
  }
  array->set_number_of_all_descriptors(new_nof_all_descriptors);
}

void MarkCompactCollector::RecordStrongDescriptorArraysForWeakening(
    GlobalHandleVector<DescriptorArray> strong_descriptor_arrays) {
  DCHECK(heap_->incremental_marking()->IsMajorMarking());
  base::MutexGuard guard(&strong_descriptor_arrays_mutex_);
  strong_descriptor_arrays_.push_back(std::move(strong_descriptor_arrays));
}

void MarkCompactCollector::WeakenStrongDescriptorArrays() {
  Tagged<Map> descriptor_array_map =
      ReadOnlyRoots(heap_->isolate()).descriptor_array_map();
  for (auto vec : strong_descriptor_arrays_) {
    for (auto it = vec.begin(); it != vec.end(); ++it) {
      Tagged<DescriptorArray> raw = it.raw();
      DCHECK(IsStrongDescriptorArray(raw));
      raw->set_map_safe_transition_no_write_barrier(descriptor_array_map);
      DCHECK_EQ(raw->raw_gc_state(kRelaxedLoad), 0);
    }
  }
  strong_descriptor_arrays_.clear();
}

void MarkCompactCollector::TrimDescriptorArray(
    Tagged<Map> map, Tagged<DescriptorArray> descriptors) {
  int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  if (number_of_own_descriptors == 0) {
    DCHECK(descriptors == ReadOnlyRoots(heap_).empty_descriptor_array());
    return;
  }
  int to_trim =
      descriptors->number_of_all_descriptors() - number_of_own_descriptors;
  if (to_trim > 0) {
    descriptors->set_number_of_descriptors(number_of_own_descriptors);
    RightTrimDescriptorArray(descriptors, to_trim);

    TrimEnumCache(map, descriptors);
    descriptors->Sort();
  }
  DCHECK(descriptors->number_of_descriptors() == number_of_own_descriptors);
  map->set_owns_descriptors(true);
}

void MarkCompactCollector::TrimEnumCache(Tagged<Map> map,
                                         Tagged<DescriptorArray> descriptors) {
  int live_enum = map->EnumLength();
  if (live_enum == kInvalidEnumCacheSentinel) {
    live_enum = map->NumberOfEnumerableProperties();
  }
  if (live_enum == 0) return descriptors->ClearEnumCache();
  Tagged<EnumCache> enum_cache = descriptors->enum_cache();

  Tagged<FixedArray> keys = enum_cache->keys();
  int keys_length = keys->length();
  if (live_enum >= keys_length) return;
  heap_->RightTrimArray(keys, live_enum, keys_length);

  Tagged<FixedArray> indices = enum_cache->indices();
  int indices_length = indices->length();
  if (live_enum >= indices_length) return;
  heap_->RightTrimArray(indices, live_enum, indices_length);
}

void MarkCompactCollector::ClearWeakCollections() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_WEAK_COLLECTIONS);
  Tagged<EphemeronHashTable> table;
  while (local_weak_objects()->ephemeron_hash_tables_local.Pop(&table)) {
    for (InternalIndex i : table->IterateEntries()) {
      Tagged<HeapObject> key = Cast<HeapObject>(table->KeyAt(i));
#ifdef VERIFY_HEAP
      if (v8_flags.verify_heap) {
        Tagged<Object> value = table->ValueAt(i);
        if (IsHeapObject(value)) {
          Tagged<HeapObject> heap_object = Cast<HeapObject>(value);

          CHECK_IMPLIES(MarkingHelper::GetLivenessMode(heap_, key) ==
                                MarkingHelper::LivenessMode::kAlwaysLive ||
                            non_atomic_marking_state_->IsMarked(key),
                        MarkingHelper::GetLivenessMode(heap_, heap_object) ==
                                MarkingHelper::LivenessMode::kAlwaysLive ||
                            non_atomic_marking_state_->IsMarked(heap_object));
        }
      }
#endif  // VERIFY_HEAP
      if (MarkingHelper::GetLivenessMode(heap_, key) ==
              MarkingHelper::LivenessMode::kMarkbit &&
          !non_atomic_marking_state_->IsMarked(key)) {
        table->RemoveEntry(i);
      }
    }
  }
  auto* table_map = heap_->ephemeron_remembered_set()->tables();
  for (auto it = table_map->begin(); it != table_map->end();) {
    if (!non_atomic_marking_state_->IsMarked(it->first)) {
      it = table_map->erase(it);
    } else {
      ++it;
    }
  }
}

void MarkCompactCollector::ClearTrivialWeakReferences() {
  HeapObjectAndSlot slot;
  Tagged<HeapObjectReference> cleared_weak_ref = ClearedValue(heap_->isolate());
  while (local_weak_objects()->weak_references_trivial_local.Pop(&slot)) {
    Tagged<HeapObject> value;
    // The slot could have been overwritten, so we have to treat it
    // as MaybeObjectSlot.
    MaybeObjectSlot location(slot.slot);
    if ((*location).GetHeapObjectIfWeak(&value)) {
      DCHECK(!IsWeakCell(value));
      // Values in RO space have already been filtered, but a non-RO value may
      // have been overwritten by a RO value since marking.
      if (InReadOnlySpace(value) ||
          non_atomic_marking_state_->IsMarked(value)) {
        // The value of the weak reference is alive.
        RecordSlot(slot.heap_object, HeapObjectSlot(location), value);
      } else {
        DCHECK(MainMarkingVisitor::IsTrivialWeakReferenceValue(slot.heap_object,
                                                               value));
        // The value of the weak reference is non-live.
        // This is a non-atomic store, which is fine as long as we only have a
        // single clearing job.
        location.store(cleared_weak_ref);
      }
    }
  }
}

void MarkCompactCollector::FilterNonTrivialWeakReferences() {
  HeapObjectAndSlot slot;
  while (local_weak_objects()->weak_references_non_trivial_local.Pop(&slot)) {
    Tagged<HeapObject> value;
    // The slot could have been overwritten, so we have to treat it
    // as MaybeObjectSlot.
    MaybeObjectSlot location(slot.slot);
    if ((*location).GetHeapObjectIfWeak(&value)) {
      DCHECK(!IsWeakCell(value));
      // Values in RO space have already been filtered, but a non-RO value may
      // have been overwritten by a RO value since marking.
      if (InReadOnlySpace(value) ||
          non_atomic_marking_state_->IsMarked(value)) {
        // The value of the weak reference is alive.
        RecordSlot(slot.heap_object, HeapObjectSlot(location), value);
      } else {
        DCHECK(!MainMarkingVisitor::IsTrivialWeakReferenceValue(
            slot.heap_object, value));
        // The value is non-live, defer the actual clearing.
        // This is non-atomic, which is fine as long as we only have a single
        // filtering job.
        local_weak_objects_->weak_references_non_trivial_unmarked_local.Push(
            slot);
      }
    }
  }
}

void MarkCompactCollector::ClearNonTrivialWeakReferences() {
  TRACE_GC(heap_->tracer(),
           GCTracer::Scope::MC_CLEAR_WEAK_REFERENCES_NON_TRIVIAL);
  HeapObjectAndSlot slot;
  Tagged<HeapObjectReference> cleared_weak_ref = ClearedValue(heap_->isolate());
  while (local_weak_objects()->weak_references_non_trivial_unmarked_local.Pop(
      &slot)) {
    // The slot may not have been overwritten since it was filtered, so we can
    // directly read its value.
    Tagged<HeapObject> value = (*slot.slot).GetHeapObjectAssumeWeak();
    DCHECK(!IsWeakCell(value));
    DCHECK(!InReadOnlySpace(value));
    DCHECK(!non_atomic_marking_state_->IsMarked(value));
    DCHECK(!MainMarkingVisitor::IsTrivialWeakReferenceValue(slot.heap_object,
                                                            value));
    if (!SpecialClearMapSlot(slot.heap_object, Cast<Map>(value), slot.slot)) {
      slot.slot.store(cleared_weak_ref);
    }
  }
}

void MarkCompactCollector::ClearJSWeakRefs() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_CLEAR_JS_WEAK_REFERENCES);
  Tagged<JSWeakRef> weak_ref;
  Isolate* const isolate = heap_->isolate();
  while (local_weak_objects()->js_weak_refs_local.Pop(&weak_ref)) {
    Tagged<HeapObject> target = Cast<HeapObject>(weak_ref->target());
    if (!InReadOnlySpace(target) &&
        !non_atomic_marking_state_->IsMarked(target)) {
      weak_ref->set_target(ReadOnlyRoots(isolate).undefined_value());
    } else {
      // The value of the JSWeakRef is alive.
      ObjectSlot slot = weak_ref->RawField(JSWeakRef::kTargetOffset);
      RecordSlot(weak_ref, slot, target);
    }
  }
  Tagged<WeakCell> weak_cell;
  while (local_weak_objects()->weak_cells_local.Pop(&weak_cell)) {
    auto gc_notify_updated_slot = [](Tagged<HeapObject> object, ObjectSlot slot,
                                     Tagged<Object> target) {
      if (IsHeapObject(target)) {
        RecordSlot(object, slot, Cast<HeapObject>(target));
      }
    };
    Tagged<HeapObject> target = Cast<HeapObject>(weak_cell->target());
    if (!InReadOnlySpace(target) &&
        !non_atomic_marking_state_->IsMarked(target)) {
      DCHECK(Object::CanBeHeldWeakly(target));
      // The value of the WeakCell is dead.
      Tagged<JSFinalizationRegistry> finalization_registry =
          Cast<JSFinalizationRegistry>(weak_cell->finalization_registry());
      if (!finalization_registry->scheduled_for_cleanup()) {
        heap_->EnqueueDirtyJSFinalizationRegistry(finalization_registry,
                                                  gc_notify_updated_slot);
      }
      // We're modifying the pointers in WeakCell and JSFinalizationRegistry
      // during GC; thus we need to record the slots it writes. The normal write
      // barrier is not enough, since it's disabled before GC.
      weak_cell->Nullify(isolate, gc_notify_updated_slot);
      DCHECK(finalization_registry->NeedsCleanup());
      DCHECK(finalization_registry->scheduled_for_cleanup());
    } else {
      // The value of the WeakCell is alive.
      ObjectSlot slot = weak_cell->RawField(WeakCell::kTargetOffset);
      RecordSlot(weak_cell, slot, Cast<HeapObject>(*slot));
    }

    Tagged<HeapObject> unregister_token = weak_cell->unregister_token();
    if (!InReadOnlySpace(unregister_token) &&
        !non_atomic_marking_state_->IsMarked(unregister_token)) {
      DCHECK(Object::CanBeHeldWeakly(unregister_token));
      // The unregister token is dead. Remove any corresponding entries in the
      // key map. Multiple WeakCell with the same token will have all their
      // unregister_token field set to undefined when processing the first
      // WeakCell. Like above, we're modifying pointers during GC, so record the
      // slots.
      Tagged<JSFinalizationRegistry> finalization_registry =
          Cast<JSFinalizationRegistry>(weak_cell->finalization_registry());
      finalization_registry->RemoveUnregisterToken(
          unregister_token, isolate,
          JSFinalizationRegistry::kKeepMatchedCellsInRegistry,
          gc_notify_updated_slot);
    } else {
      // The unregister_token is alive.
      ObjectSlot slot = weak_cell->RawField(WeakCell::kUnregisterTokenOffset);
      RecordSlot(weak_cell, slot, Cast<HeapObject>(*slot));
    }
  }
  heap_->PostFinalizationRegistryCleanupTaskIfNeeded();
}

// static
bool MarkCompactCollector::ShouldRecordRelocSlot(Tagged<InstructionStream> host,
                                                 RelocInfo* rinfo,
                                                 Tagged<HeapObject> target) {
  MemoryChunk* source_chunk = MemoryChunk::FromHeapObject(host);
  MemoryChunk* target_chunk = MemoryChunk::FromHeapObject(target);
  return target_chunk->IsEvacuationCandidate() &&
         !source_chunk->ShouldSkipEvacuationSlotRecording();
}

// static
MarkCompactCollector::RecordRelocSlotInfo
MarkCompactCollector::ProcessRelocInfo(Tagged<InstructionStream> host,
                                       RelocInfo* rinfo,
                                       Tagged<HeapObject> target) {
  RecordRelocSlotInfo result;
  const RelocInfo::Mode rmode = rinfo->rmode();
  Address addr;
  SlotType slot_type;

  if (rinfo->IsInConstantPool()) {
    addr = rinfo->constant_pool_entry_address();

    if (RelocInfo::IsCodeTargetMode(rmode)) {
      slot_type = SlotType::kConstPoolCodeEntry;
    } else if (RelocInfo::IsCompressedEmbeddedObject(rmode)) {
      slot_type = SlotType::kConstPoolEmbeddedObjectCompressed;
    } else {
      DCHECK(RelocInfo::IsFullEmbeddedObject(rmode));
      slot_type = SlotType::kConstPoolEmbeddedObjectFull;
    }
  } else {
    addr = rinfo->pc();

    if (RelocInfo::IsCodeTargetMode(rmode)) {
      slot_type = SlotType::kCodeEntry;
    } else if (RelocInfo::IsFullEmbeddedObject(rmode)) {
      slot_type = SlotType::kEmbeddedObjectFull;
    } else {
      DCHECK(RelocInfo::IsCompressedEmbeddedObject(rmode));
      slot_type = SlotType::kEmbeddedObjectCompressed;
    }
  }

  MemoryChunk* const source_chunk = MemoryChunk::FromHeapObject(host);
  MutablePageMetadata* const source_page_metadata =
      MutablePageMetadata::cast(source_chunk->Metadata());
  const uintptr_t offset = source_chunk->Offset(addr);
  DCHECK_LT(offset, static_cast<uintptr_t>(TypedSlotSet::kMaxOffset));
  result.page_metadata = source_page_metadata;
  result.slot_type = slot_type;
  result.offset = static_cast<uint32_t>(offset);

  return result;
}

// static
void MarkCompactCollector::RecordRelocSlot(Tagged<InstructionStream> host,
                                           RelocInfo* rinfo,
                                           Tagged<HeapObject> target) {
  if (!ShouldRecordRelocSlot(host, rinfo, target)) return;
  RecordRelocSlotInfo info = ProcessRelocInfo(host, rinfo, target);

  // Access to TypeSlots need to be protected, since LocalHeaps might
  // publish code in the background thread.
  std::optional<base::MutexGuard> opt_guard;
  if (v8_flags.concurrent_sparkplug) {
    opt_guard.emplace(info.page_metadata->mutex());
  }
  RememberedSet<OLD_TO_OLD>::InsertTyped(info.page_metadata, info.slot_type,
                                         info.offset);
}

namespace {

// Missing specialization MakeSlotValue<FullObjectSlot, WEAK>() will turn
// attempt to store a weak reference to strong-only slot to a compilation error.
template <typename TSlot, HeapObjectReferenceType reference_type>
typename TSlot::TObject MakeSlotValue(Tagged<HeapObject> heap_object);

template <>
Tagged<Object> MakeSlotValue<ObjectSlot, HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}

template <>
Tagged<MaybeObject>
MakeSlotValue<MaybeObjectSlot, HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}

template <>
Tagged<MaybeObject>
MakeSlotValue<MaybeObjectSlot, HeapObjectReferenceType::WEAK>(
    Tagged<HeapObject> heap_object) {
  return MakeWeak(heap_object);
}

template <>
Tagged<Object>
MakeSlotValue<WriteProtectedSlot<ObjectSlot>, HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}

#ifdef V8_ENABLE_SANDBOX
template <>
Tagged<Object> MakeSlotValue<WriteProtectedSlot<ProtectedPointerSlot>,
                             HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}
#endif

template <>
Tagged<Object>
MakeSlotValue<OffHeapObjectSlot, HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}

#ifdef V8_COMPRESS_POINTERS
template <>
Tagged<Object> MakeSlotValue<FullObjectSlot, HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}

template <>
Tagged<MaybeObject>
MakeSlotValue<FullMaybeObjectSlot, HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}

template <>
Tagged<MaybeObject>
MakeSlotValue<FullMaybeObjectSlot, HeapObjectReferenceType::WEAK>(
    Tagged<HeapObject> heap_object) {
  return MakeWeak(heap_object);
}

#ifdef V8_EXTERNAL_CODE_SPACE
template <>
Tagged<Object>
MakeSlotValue<InstructionStreamSlot, HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}
#endif  // V8_EXTERNAL_CODE_SPACE

#ifdef V8_ENABLE_SANDBOX
template <>
Tagged<Object>
MakeSlotValue<ProtectedPointerSlot, HeapObjectReferenceType::STRONG>(
    Tagged<HeapObject> heap_object) {
  return heap_object;
}
#endif  // V8_ENABLE_SANDBOX

// The following specialization
//   MakeSlotValue<FullMaybeObjectSlot, HeapObjectReferenceType::WEAK>()
// is not used.
#endif  // V8_COMPRESS_POINTERS

template <HeapObjectReferenceType reference_type, typename TSlot>
static inline void UpdateSlot(PtrComprCageBase cage_base, TSlot slot,
                              Tagged<HeapObject> heap_obj) {
  static_assert(
      std::is_same<TSlot, FullObjectSlot>::value ||
          std::is_same<TSlot, ObjectSlot>::value ||
          std::is_same<TSlot, FullMaybeObjectSlot>::value ||
          std::is_same<TSlot, MaybeObjectSlot>::value ||
          std::is_same<TSlot, OffHeapObjectSlot>::value ||
          std::is_same<TSlot, InstructionStreamSlot>::value ||
          std::is_same<TSlot, ProtectedPointerSlot>::value ||
          std::is_same<TSlot, WriteProtectedSlot<ObjectSlot>>::value ||
          std::is_same<TSlot, WriteProtectedSlot<ProtectedPointerSlot>>::value,
      "Only [Full|OffHeap]ObjectSlot, [Full]MaybeObjectSlot, "
      "InstructionStreamSlot, ProtectedPointerSlot, or WriteProtectedSlot are "
      "expected here");
  MapWord map_word = heap_obj->map_word(cage_base, kRelaxedLoad);
  if (!map_word.IsForwardingAddress()) return;
  DCHECK_IMPLIES((!v8_flags.minor_ms && !Heap::InFromPage(heap_obj)),
                 MarkCompactCollector::IsOnEvacuationCandidate(heap_obj) ||
                     MemoryChunk::FromHeapObject(heap_obj)->IsFlagSet(
                         MemoryChunk::COMPACTION_WAS_ABORTED));
  typename TSlot::TObject target = MakeSlotValue<TSlot, reference_type>(
      map_word.ToForwardingAddress(heap_obj));
  // Needs to be atomic for map space compaction: This slot could be a map
  // word which we update while loading the map word for updating the slot
  // on another page.
  slot.Relaxed_Store(target);
  DCHECK_IMPLIES(!v8_flags.sticky_mark_bits, !Heap::InFromPage(target));
  DCHECK(!MarkCompactCollector::IsOnEvacuationCandidate(target));
}

template <typename TSlot>
static inline void UpdateSlot(PtrComprCageBase cage_base, TSlot slot) {
  typename TSlot::TObject obj = slot.Relaxed_Load(cage_base);
  Tagged<HeapObject> heap_obj;
  if constexpr (TSlot::kCanBeWeak) {
    if (obj.GetHeapObjectIfWeak(&heap_obj)) {
      return UpdateSlot<HeapObjectReferenceType::WEAK>(cage_base, slot,
                                                       heap_obj);
    }
  }
  if (obj.GetHeapObjectIfStrong(&heap_obj)) {
    UpdateSlot<HeapObjectReferenceType::STRONG>(cage_base, slot, heap_obj);
  }
}

template <typename TSlot>
static inline SlotCallbackResult UpdateOldToSharedSlot(
    PtrComprCageBase cage_base, TSlot slot) {
  typename TSlot::TObject obj = slot.Relaxed_Load(cage_base);
  Tagged<HeapObject> heap_obj;

  if constexpr (TSlot::kCanBeWeak) {
    if (obj.GetHeapObjectIfWeak(&heap_obj)) {
      UpdateSlot<HeapObjectReferenceType::WEAK>(cage_base, slot, heap_obj);
      return InWritableSharedSpace(heap_obj) ? KEEP_SLOT : REMOVE_SLOT;
    }
  }

  if (obj.GetHeapObjectIfStrong(&heap_obj)) {
    UpdateSlot<HeapObjectReferenceType::STRONG>(cage_base, slot, heap_obj);
    return InWritableSharedSpace(heap_obj) ? KEEP_SLOT : REMOVE_SLOT;
  }

  return REMOVE_SLOT;
}

template <typename TSlot>
static inline void UpdateStrongSlot(PtrComprCageBase cage_base, TSlot slot) {
  typename TSlot::TObject obj = slot.Relaxed_Load(cage_base);
#ifdef V8_ENABLE_DIRECT_HANDLE
  if (obj.ptr() == kTaggedNullAddress) return;
#endif
  DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(obj.ptr()));
  Tagged<HeapObject> heap_obj;
  if (obj.GetHeapObject(&heap_obj)) {
    UpdateSlot<HeapObjectReferenceType::STRONG>(cage_base, slot, heap_obj);
  }
}

static inline SlotCallbackResult UpdateStrongOldToSharedSlot(
    PtrComprCageBase cage_base, FullMaybeObjectSlot slot) {
  Tagged<MaybeObject> obj = slot.Relaxed_Load(cage_base);
#ifdef V8_ENABLE_DIRECT_HANDLE
  if (obj.ptr() == kTaggedNullAddress) return REMOVE_SLOT;
#endif
  DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(obj.ptr()));
  Tagged<HeapObject> heap_obj;
  if (obj.GetHeapObject(&heap_obj)) {
    UpdateSlot<HeapObjectReferenceType::STRONG>(cage_base, slot, heap_obj);
    return InWritableSharedSpace(heap_obj) ? KEEP_SLOT : REMOVE_SLOT;
  }

  return REMOVE_SLOT;
}

static inline void UpdateStrongCodeSlot(Tagged<HeapObject> host,
                                        PtrComprCageBase cage_base,
                                        PtrComprCageBase code_cage_base,
                                        InstructionStreamSlot slot) {
  Tagged<Object> obj = slot.Relaxed_Load(code_cage_base);
  DCHECK(!HAS_WEAK_HEAP_OBJECT_TAG(obj.ptr()));
  Tagged<HeapObject> heap_obj;
  if (obj.GetHeapObject(&heap_obj)) {
    UpdateSlot<HeapObjectReferenceType::STRONG>(cage_base, slot, heap_obj);

    Tagged<Code> code = Cast<Code>(HeapObject::FromAddress(
        slot.address() - Code::kInstructionStreamOffset));
    Tagged<InstructionStream> instruction_stream =
        code->instruction_stream(code_cage_base);
    code->UpdateInstructionStart(GetIsolateForSandbox(host),
                                 instruction_stream);
  }
}

}  // namespace

// Visitor for updating root pointers and to-space pointers.
// It does not expect to encounter pointers to dead objects.
class PointersUpdatingVisitor final : public ObjectVisitorWithCageBases,
                                      public RootVisitor {
 public:
  explicit PointersUpdatingVisitor(Heap* heap)
      : ObjectVisitorWithCageBases(heap) {}

  void VisitPointer(Tagged<HeapObject> host, ObjectSlot p) override {
    UpdateStrongSlotInternal(cage_base(), p);
  }

  void VisitPointer(Tagged<HeapObject> host, MaybeObjectSlot p) override {
    UpdateSlotInternal(cage_base(), p);
  }

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override {
    for (ObjectSlot p = start; p < end; ++p) {
      UpdateStrongSlotInternal(cage_base(), p);
    }
  }

  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) final {
    for (MaybeObjectSlot p = start; p < end; ++p) {
      UpdateSlotInternal(cage_base(), p);
    }
  }

  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    UpdateStrongCodeSlot(host, cage_base(), code_cage_base(), slot);
  }

  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) override {
    DCHECK(!MapWord::IsPacked(p.Relaxed_Load().ptr()));
    UpdateRootSlotInternal(cage_base(), p);
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot p = start; p < end; ++p) {
      UpdateRootSlotInternal(cage_base(), p);
    }
  }

  void VisitRootPointers(Root root, const char* description,
                         OffHeapObjectSlot start,
                         OffHeapObjectSlot end) override {
    for (OffHeapObjectSlot p = start; p < end; ++p) {
      UpdateRootSlotInternal(cage_base(), p);
    }
  }

  void VisitCodeTarget(Tagged<InstructionStream> host,
                       RelocInfo* rinfo) override {
    // This visitor nevers visits code objects.
    UNREACHABLE();
  }

  void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                            RelocInfo* rinfo) override {
    // This visitor nevers visits code objects.
    UNREACHABLE();
  }

 private:
  static inline void UpdateRootSlotInternal(PtrComprCageBase cage_base,
                                            FullObjectSlot slot) {
    UpdateStrongSlot(cage_base, slot);
  }

  static inline void UpdateRootSlotInternal(PtrComprCageBase cage_base,
                                            OffHeapObjectSlot slot) {
    UpdateStrongSlot(cage_base, slot);
  }

  static inline void UpdateStrongMaybeObjectSlotInternal(
      PtrComprCageBase cage_base, MaybeObjectSlot slot) {
    UpdateStrongSlot(cage_base, slot);
  }

  static inline void UpdateStrongSlotInternal(PtrComprCageBase cage_base,
                                              ObjectSlot slot) {
    UpdateStrongSlot(cage_base, slot);
  }

  static inline void UpdateSlotInternal(PtrComprCageBase cage_base,
                                        MaybeObjectSlot slot) {
    UpdateSlot(cage_base, slot);
  }
};

static Tagged<String> UpdateReferenceInExternalStringTableEntry(
    Heap* heap, FullObjectSlot p) {
  Tagged<HeapObject> old_string = Cast<HeapObject>(*p);
  MapWord map_word = old_string->map_word(kRelaxedLoad);

  if (map_word.IsForwardingAddress()) {
    Tagged<String> new_string =
        Cast<String>(map_word.ToForwardingAddress(old_string));

    if (IsExternalString(new_string)) {
      MutablePageMetadata::MoveExternalBackingStoreBytes(
          ExternalBackingStoreType::kExternalString,
          PageMetadata::FromAddress((*p).ptr()),
          PageMetadata::FromHeapObject(new_string),
          Cast<ExternalString>(new_string)->ExternalPayloadSize());
    }
    return new_string;
  }

  return Cast<String>(*p);
}

void MarkCompactCollector::EvacuatePrologue() {
  // New space.
  if (NewSpace* new_space = heap_->new_space()) {
    DCHECK(new_space_evacuation_pages_.empty());
    std::copy_if(new_space->begin(), new_space->end(),
                 std::back_inserter(new_space_evacuation_pages_),
                 [](PageMetadata* p) { return p->live_bytes() > 0; });
    if (!v8_flags.minor_ms) {
      SemiSpaceNewSpace::From(new_space)->EvacuatePrologue();
    }
  }

  // Large new space.
  if (NewLargeObjectSpace* new_lo_space = heap_->new_lo_space()) {
    new_lo_space->Flip();
    new_lo_space->ResetPendingObject();
  }

  // Old space.
  DCHECK(old_space_evacuation_pages_.empty());
  old_space_evacuation_pages_ = std::move(evacuation_candidates_);
  evacuation_candidates_.clear();
  DCHECK(evacuation_candidates_.empty());
}

void MarkCompactCollector::EvacuateEpilogue() {
  aborted_evacuation_candidates_due_to_oom_.clear();
  aborted_evacuation_candidates_due_to_flags_.clear();

  // New space.
  if (heap_->new_space()) {
    DCHECK_EQ(0, heap_->new_space()->Size());
  }

  // Old generation. Deallocate evacuated candidate pages.
  ReleaseEvacuationCandidates();

#ifdef DEBUG
  VerifyRememberedSetsAfterEvacuation(heap_, GarbageCollector::MARK_COMPACTOR);
#endif  // DEBUG
}

class Evacuator final : public Malloced {
 public:
  enum EvacuationMode {
    kObjectsNewToOld,
    kPageNewToOld,
    kObjectsOldToOld,
  };

  static const char* EvacuationModeName(EvacuationMode mode) {
    switch (mode) {
      case kObjectsNewToOld:
        return "objects-new-to-old";
      case kPageNewToOld:
        return "page-new-to-old";
      case kObjectsOldToOld:
        return "objects-old-to-old";
    }
  }

  static inline EvacuationMode ComputeEvacuationMode(MemoryChunk* chunk) {
    // Note: The order of checks is important in this function.
    if (chunk->IsFlagSet(MemoryChunk::PAGE_NEW_OLD_PROMOTION))
      return kPageNewToOld;
    if (chunk->InYoungGeneration()) return kObjectsNewToOld;
    return kObjectsOldToOld;
  }

  explicit Evacuator(Heap* heap)
      : heap_(heap),
        local_pretenuring_feedback_(
            PretenuringHandler::kInitialFeedbackCapacity),
        local_allocator_(heap_,
                         CompactionSpaceKind::kCompactionSpaceForMarkCompact),
        record_visitor_(heap_),
        new_space_visitor_(heap_, &local_allocator_, &record_visitor_,
                           &local_pretenuring_feedback_),
        new_to_old_page_visitor_(heap_, &record_visitor_,
                                 &local_pretenuring_feedback_),

        old_space_visitor_(heap_, &local_allocator_, &record_visitor_),
        duration_(0.0),
        bytes_compacted_(0) {}

  void EvacuatePage(MutablePageMetadata* chunk);

  void AddObserver(MigrationObserver* observer) {
    new_space_visitor_.AddObserver(observer);
    old_space_visitor_.AddObserver(observer);
  }

  // Merge back locally cached info sequentially. Note that this method needs
  // to be called from the main thread.
  void Finalize();

 private:
  // |saved_live_bytes| returns the live bytes of the page that was processed.
  bool RawEvacuatePage(MutablePageMetadata* chunk);

  inline Heap* heap() { return heap_; }

  void ReportCompactionProgress(double duration, intptr_t bytes_compacted) {
    duration_ += duration;
    bytes_compacted_ += bytes_compacted;
  }

  Heap* heap_;

  PretenuringHandler::PretenuringFeedbackMap local_pretenuring_feedback_;

  // Locally cached collector data.
  EvacuationAllocator local_allocator_;

  RecordMigratedSlotVisitor record_visitor_;

  // Visitors for the corresponding spaces.
  EvacuateNewSpaceVisitor new_space_visitor_;
  EvacuateNewToOldSpacePageVisitor new_to_old_page_visitor_;
  EvacuateOldSpaceVisitor old_space_visitor_;

  // Book keeping info.
  double duration_;
  intptr_t bytes_compacted_;
};

void Evacuator::EvacuatePage(MutablePageMetadata* page) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"), "Evacuator::EvacuatePage");
  DCHECK(page->SweepingDone());
  intptr_t saved_live_bytes = page->live_bytes();
  double evacuation_time = 0.0;
  bool success = false;
  {
    TimedScope timed_scope(&evacuation_time);
    success = RawEvacuatePage(page);
  }
  ReportCompactionProgress(evacuation_time, saved_live_bytes);
  if (v8_flags.trace_evacuation) {
    MemoryChunk* chunk = page->Chunk();
    PrintIsolate(heap_->isolate(),
                 "evacuation[%p]: page=%p new_space=%d "
                 "page_evacuation=%d executable=%d can_promote=%d "
                 "live_bytes=%" V8PRIdPTR " time=%f success=%d\n",
                 static_cast<void*>(this), static_cast<void*>(page),
                 chunk->InNewSpace(),
                 chunk->IsFlagSet(MemoryChunk::PAGE_NEW_OLD_PROMOTION),
                 chunk->IsFlagSet(MemoryChunk::IS_EXECUTABLE),
                 heap_->new_space()->IsPromotionCandidate(page),
                 saved_live_bytes, evacuation_time, success);
  }
}

void Evacuator::Finalize() {
  local_allocator_.Finalize();
  heap_->tracer()->AddCompactionEvent(duration_, bytes_compacted_);
  heap_->IncrementPromotedObjectsSize(new_space_visitor_.promoted_size() +
                                      new_to_old_page_visitor_.moved_bytes());
  heap_->IncrementNewSpaceSurvivingObjectSize(
      new_space_visitor_.semispace_copied_size());
  heap_->IncrementYoungSurvivorsCounter(
      new_space_visitor_.promoted_size() +
      new_space_visitor_.semispace_copied_size() +
      new_to_old_page_visitor_.moved_bytes());
  heap_->pretenuring_handler()->MergeAllocationSitePretenuringFeedback(
      local_pretenuring_feedback_);
}

class LiveObjectVisitor final : AllStatic {
 public:
  // Visits marked objects using `bool Visitor::Visit(HeapObject object, size_t
  // size)` as long as the return value is true.
  //
  // Returns whether all objects were successfully visited. Upon returning
  // false, also sets `failed_object` to the object for which the visitor
  // returned false.
  template <class Visitor>
  static bool VisitMarkedObjects(PageMetadata* page, Visitor* visitor,
                                 Tagged<HeapObject>* failed_object);

  // Visits marked objects using `bool Visitor::Visit(HeapObject object, size_t
  // size)` as long as the return value is true. Assumes that the return value
  // is always true (success).
  template <class Visitor>
  static void VisitMarkedObjectsNoFail(PageMetadata* page, Visitor* visitor);
};

template <class Visitor>
bool LiveObjectVisitor::VisitMarkedObjects(PageMetadata* page, Visitor* visitor,
                                           Tagged<HeapObject>* failed_object) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
               "LiveObjectVisitor::VisitMarkedObjects");
  for (auto [object, size] : LiveObjectRange(page)) {
    if (!visitor->Visit(object, size)) {
      *failed_object = object;
      return false;
    }
  }
  return true;
}

template <class Visitor>
void LiveObjectVisitor::VisitMarkedObjectsNoFail(PageMetadata* page,
                                                 Visitor* visitor) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
               "LiveObjectVisitor::VisitMarkedObjectsNoFail");
  for (auto [object, size] : LiveObjectRange(page)) {
    const bool success = visitor->Visit(object, size);
    USE(success);
    DCHECK(success);
  }
}

bool Evacuator::RawEvacuatePage(MutablePageMetadata* page) {
  MemoryChunk* chunk = page->Chunk();
  const EvacuationMode evacuation_mode = ComputeEvacuationMode(chunk);
  TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
               "FullEvacuator::RawEvacuatePage", "evacuation_mode",
               EvacuationModeName(evacuation_mode), "live_bytes",
               page->live_bytes());
  switch (evacuation_mode) {
    case kObjectsNewToOld:
#if DEBUG
      new_space_visitor_.DisableAbortEvacuationAtAddress(page);
#endif  // DEBUG
      LiveObjectVisitor::VisitMarkedObjectsNoFail(PageMetadata::cast(page),
                                                  &new_space_visitor_);
      page->ClearLiveness();
      break;
    case kPageNewToOld:
      if (chunk->IsLargePage()) {
        auto object = LargePageMetadata::cast(page)->GetObject();
        bool success = new_to_old_page_visitor_.Visit(object, object->Size());
        USE(success);
        DCHECK(success);
      } else {
        LiveObjectVisitor::VisitMarkedObjectsNoFail(PageMetadata::cast(page),
                                                    &new_to_old_page_visitor_);
      }
      new_to_old_page_visitor_.account_moved_bytes(page->live_bytes());
      break;
    case kObjectsOldToOld: {
#if DEBUG
      old_space_visitor_.SetUpAbortEvacuationAtAddress(page);
#endif  // DEBUG
      Tagged<HeapObject> failed_object;
      if (LiveObjectVisitor::VisitMarkedObjects(
              PageMetadata::cast(page), &old_space_visitor_, &failed_object)) {
        page->ClearLiveness();
      } else {
        // Aborted compaction page. Actual processing happens on the main
        // thread for simplicity reasons.
        heap_->mark_compact_collector()
            ->ReportAbortedEvacuationCandidateDueToOOM(
                failed_object.address(), static_cast<PageMetadata*>(page));
        return false;
      }
      break;
    }
  }

  return true;
}

class PageEvacuationJob : public v8::JobTask {
 public:
  PageEvacuationJob(
      Isolate* isolate, MarkCompactCollector* collector,
      std::vector<std::unique_ptr<Evacuator>>* evacuators,
      std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
          evacuation_items)
      : collector_(collector),
        evacuators_(evacuators),
        evacuation_items_(std::move(evacuation_items)),
        remaining_evacuation_items_(evacuation_items_.size()),
        generator_(evacuation_items_.size()),
        tracer_(isolate->heap()->tracer()),
        trace_id_(reinterpret_cast<uint64_t>(this) ^
                  tracer_->CurrentEpoch(GCTracer::Scope::MC_EVACUATE)) {}

  void Run(JobDelegate* delegate) override {
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(
        collector_->heap()->isolate());

    Evacuator* evacuator = (*evacuators_)[delegate->GetTaskId()].get();
    if (delegate->IsJoiningThread()) {
      TRACE_GC_WITH_FLOW(tracer_, GCTracer::Scope::MC_EVACUATE_COPY_PARALLEL,
                         trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
      ProcessItems(delegate, evacuator);
    } else {
      TRACE_GC_EPOCH_WITH_FLOW(
          tracer_, GCTracer::Scope::MC_BACKGROUND_EVACUATE_COPY,
          ThreadKind::kBackground, trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
      ProcessItems(delegate, evacuator);
    }
  }

  void ProcessItems(JobDelegate* delegate, Evacuator* evacuator) {
    while (remaining_evacuation_items_.load(std::memory_order_relaxed) > 0) {
      std::optional<size_t> index = generator_.GetNext();
      if (!index) return;
      for (size_t i = *index; i < evacuation_items_.size(); ++i) {
        auto& work_item = evacuation_items_[i];
        if (!work_item.first.TryAcquire()) break;
        evacuator->EvacuatePage(work_item.second);
        if (remaining_evacuation_items_.fetch_sub(
                1, std::memory_order_relaxed) <= 1) {
          return;
        }
      }
    }
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    const size_t kItemsPerWorker = std::max(1, MB / PageMetadata::kPageSize);
    // Ceiling division to ensure enough workers for all
    // |remaining_evacuation_items_|
    size_t wanted_num_workers =
        (remaining_evacuation_items_.load(std::memory_order_relaxed) +
         kItemsPerWorker - 1) /
        kItemsPerWorker;
    wanted_num_workers =
        std::min<size_t>(wanted_num_workers, evacuators_->size());
    if (!collector_->UseBackgroundThreadsInCycle()) {
      return std::min<size_t>(wanted_num_workers, 1);
    }
    return wanted_num_workers;
  }

  uint64_t trace_id() const { return trace_id_; }

 private:
  MarkCompactCollector* collector_;
  std::vector<std::unique_ptr<Evacuator>>* evacuators_;
  std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
      evacuation_items_;
  std::atomic<size_t> remaining_evacuation_items_{0};
  IndexGenerator generator_;

  GCTracer* tracer_;
  const uint64_t trace_id_;
};

namespace {
size_t CreateAndExecuteEvacuationTasks(
    Heap* heap, MarkCompactCollector* collector,
    std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
        evacuation_items) {
  std::optional<ProfilingMigrationObserver> profiling_observer;
  if (heap->isolate()->log_object_relocation()) {
    profiling_observer.emplace(heap);
  }
  std::vector<std::unique_ptr<v8::internal::Evacuator>> evacuators;
  const int wanted_num_tasks = NumberOfParallelCompactionTasks(heap);
  for (int i = 0; i < wanted_num_tasks; i++) {
    auto evacuator = std::make_unique<Evacuator>(heap);
    if (profiling_observer) {
      evacuator->AddObserver(&profiling_observer.value());
    }
    evacuators.push_back(std::move(evacuator));
  }
  auto page_evacuation_job = std::make_unique<PageEvacuationJob>(
      heap->isolate(), collector, &evacuators, std::move(evacuation_items));
  TRACE_GC_NOTE_WITH_FLOW("PageEvacuationJob started",
                          page_evacuation_job->trace_id(),
                          TRACE_EVENT_FLAG_FLOW_OUT);
  V8::GetCurrentPlatform()
      ->CreateJob(v8::TaskPriority::kUserBlocking,
                  std::move(page_evacuation_job))
      ->Join();
  for (auto& evacuator : evacuators) {
    evacuator->Finalize();
  }
  return wanted_num_tasks;
}

enum class MemoryReductionMode { kNone, kShouldReduceMemory };

// NewSpacePages with more live bytes than this threshold qualify for fast
// evacuation.
intptr_t NewSpacePageEvacuationThreshold() {
  return v8_flags.page_promotion_threshold *
         MemoryChunkLayout::AllocatableMemoryInDataPage() / 100;
}

bool ShouldMovePage(PageMetadata* p, intptr_t live_bytes,
                    MemoryReductionMode memory_reduction_mode) {
  Heap* heap = p->heap();
  DCHECK(!p->Chunk()->NeverEvacuate());
  const bool should_move_page =
      v8_flags.page_promotion &&
      (memory_reduction_mode == MemoryReductionMode::kNone) &&
      (live_bytes > NewSpacePageEvacuationThreshold()) &&
      heap->CanExpandOldGeneration(live_bytes);
  if (v8_flags.trace_page_promotions) {
    PrintIsolate(heap->isolate(),
                 "[Page Promotion] %p: collector=mc, should move: %d"
                 ", live bytes = %zu, promotion threshold = %zu"
                 ", allocated labs size = %zu\n",
                 p, should_move_page, live_bytes,
                 NewSpacePageEvacuationThreshold(), p->AllocatedLabSize());
  }
  return should_move_page;
}

void TraceEvacuation(Isolate* isolate, size_t pages_count,
                     size_t wanted_num_tasks, size_t live_bytes,
                     size_t aborted_pages) {
  DCHECK(v8_flags.trace_evacuation);
  PrintIsolate(
      isolate,
      "%8.0f ms: evacuation-summary: parallel=%s pages=%zu "
      "wanted_tasks=%zu cores=%d live_bytes=%" V8PRIdPTR
      " compaction_speed=%.f aborted=%zu\n",
      isolate->time_millis_since_init(),
      v8_flags.parallel_compaction ? "yes" : "no", pages_count,
      wanted_num_tasks, V8::GetCurrentPlatform()->NumberOfWorkerThreads() + 1,
      live_bytes,
      isolate->heap()->tracer()->CompactionSpeedInBytesPerMillisecond(),
      aborted_pages);
}

}  // namespace

void MarkCompactCollector::EvacuatePagesInParallel() {
  std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
      evacuation_items;
  intptr_t live_bytes = 0;

  // Evacuation of new space pages cannot be aborted, so it needs to run
  // before old space evacuation.
  bool force_page_promotion =
      heap_->IsGCWithStack() && !v8_flags.compact_with_stack;
  for (PageMetadata* page : new_space_evacuation_pages_) {
    intptr_t live_bytes_on_page = page->live_bytes();
    DCHECK_LT(0, live_bytes_on_page);
    live_bytes += live_bytes_on_page;
    MemoryReductionMode memory_reduction_mode =
        heap_->ShouldReduceMemory() ? MemoryReductionMode::kShouldReduceMemory
                                    : MemoryReductionMode::kNone;
    if (ShouldMovePage(page, live_bytes_on_page, memory_reduction_mode) ||
        force_page_promotion) {
      EvacuateNewToOldSpacePageVisitor::Move(page);
      page->Chunk()->SetFlagNonExecutable(MemoryChunk::PAGE_NEW_OLD_PROMOTION);
      DCHECK_EQ(heap_->old_space(), page->owner());
      // The move added page->allocated_bytes to the old space, but we are
      // going to sweep the page and add page->live_byte_count.
      heap_->old_space()->DecreaseAllocatedBytes(page->allocated_bytes(), page);
    }
    evacuation_items.emplace_back(ParallelWorkItem{}, page);
  }

  if (heap_->IsGCWithStack()) {
    if (!v8_flags.compact_with_stack) {
      for (PageMetadata* page : old_space_evacuation_pages_) {
        ReportAbortedEvacuationCandidateDueToFlags(page->area_start(), page);
      }
    } else if (!v8_flags.compact_code_space_with_stack ||
               heap_->isolate()->InFastCCall()) {
      // For fast C calls we cannot patch the return address in the native stack
      // frame if we would relocate InstructionStream objects.
      for (PageMetadata* page : old_space_evacuation_pages_) {
        if (page->owner_identity() != CODE_SPACE) continue;
        ReportAbortedEvacuationCandidateDueToFlags(page->area_start(), page);
      }
    }
  } else {
    // There should always be a stack when we are in a fast c call.
    DCHECK(!heap_->isolate()->InFastCCall());
  }

  if (v8_flags.stress_compaction || v8_flags.stress_compaction_random) {
    // Stress aborting of evacuation by aborting ~10% of evacuation candidates
    // when stress testing.
    const double kFraction = 0.05;

    for (PageMetadata* page : old_space_evacuation_pages_) {
      MemoryChunk* chunk = page->Chunk();
      if (chunk->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED)) continue;

      if (heap_->isolate()->fuzzer_rng()->NextDouble() < kFraction) {
        ReportAbortedEvacuationCandidateDueToFlags(page->area_start(), page);
      }
    }
  }

  for (PageMetadata* page : old_space_evacuation_pages_) {
    MemoryChunk* chunk = page->Chunk();
    if (chunk->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED)) continue;

    live_bytes += page->live_bytes();
    evacuation_items.emplace_back(ParallelWorkItem{}, page);
  }

  // Promote young generation large objects.
  if (auto* new_lo_space = heap_->new_lo_space()) {
    for (auto it = new_lo_space->begin(); it != new_lo_space->end();) {
      LargePageMetadata* current = *(it++);
      Tagged<HeapObject> object = current->GetObject();
      if (marking_state_->IsMarked(object)) {
        heap_->lo_space()->PromoteNewLargeObject(current);
        current->Chunk()->SetFlagNonExecutable(
            MemoryChunk::PAGE_NEW_OLD_PROMOTION);
        promoted_large_pages_.push_back(current);
        evacuation_items.emplace_back(ParallelWorkItem{}, current);
      }
    }
    new_lo_space->set_objects_size(0);
  }

  const size_t pages_count = evacuation_items.size();
  size_t wanted_num_tasks = 0;
  if (!evacuation_items.empty()) {
    TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                 "MarkCompactCollector::EvacuatePagesInParallel", "pages",
                 evacuation_items.size());

    wanted_num_tasks = CreateAndExecuteEvacuationTasks(
        heap_, this, std::move(evacuation_items));
  }

  const size_t aborted_pages = PostProcessAbortedEvacuationCandidates();

  if (v8_flags.trace_evacuation) {
    TraceEvacuation(heap_->isolate(), pages_count, wanted_num_tasks, live_bytes,
                    aborted_pages);
  }
}

class EvacuationWeakObjectRetainer : public WeakObjectRetainer {
 public:
  Tagged<Object> RetainAs(Tagged<Object> object) override {
    if (object.IsHeapObject()) {
      Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
      MapWord map_word = heap_object->map_word(kRelaxedLoad);
      if (map_word.IsForwardingAddress()) {
        return map_word.ToForwardingAddress(heap_object);
      }
    }
    return object;
  }
};

void MarkCompactCollector::Evacuate() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_EVACUATE);
  base::MutexGuard guard(heap_->relocation_mutex());

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_EVACUATE_PROLOGUE);
    EvacuatePrologue();
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_EVACUATE_COPY);
    EvacuatePagesInParallel();
  }

  UpdatePointersAfterEvacuation();

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_EVACUATE_CLEAN_UP);

    for (PageMetadata* p : new_space_evacuation_pages_) {
      MemoryChunk* chunk = p->Chunk();
      if (chunk->IsFlagSet(MemoryChunk::PAGE_NEW_OLD_PROMOTION)) {
        chunk->ClearFlagNonExecutable(MemoryChunk::PAGE_NEW_OLD_PROMOTION);
        DCHECK_EQ(OLD_SPACE, p->owner_identity());
        sweeper_->AddPage(OLD_SPACE, p);
      } else if (v8_flags.minor_ms) {
        // Sweep non-promoted pages to add them back to the free list.
        DCHECK_EQ(NEW_SPACE, p->owner_identity());
        DCHECK_EQ(0, p->live_bytes());
        DCHECK(p->SweepingDone());
        PagedNewSpace* space = heap_->paged_new_space();
        if (space->ShouldReleaseEmptyPage()) {
          space->ReleasePage(p);
        } else {
          sweeper_->SweepEmptyNewSpacePage(p);
        }
      }
    }
    new_space_evacuation_pages_.clear();

    for (LargePageMetadata* p : promoted_large_pages_) {
      MemoryChunk* chunk = p->Chunk();
      DCHECK(chunk->IsFlagSet(MemoryChunk::PAGE_NEW_OLD_PROMOTION));
      chunk->ClearFlagNonExecutable(MemoryChunk::PAGE_NEW_OLD_PROMOTION);
      Tagged<HeapObject> object = p->GetObject();
      if (!v8_flags.sticky_mark_bits) {
        MarkBit::From(object).Clear();
        p->SetLiveBytes(0);
      }
      p->ProgressBar().ResetIfEnabled();
    }
    promoted_large_pages_.clear();

    for (PageMetadata* p : old_space_evacuation_pages_) {
      MemoryChunk* chunk = p->Chunk();
      if (chunk->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED)) {
        sweeper_->AddPage(p->owner_identity(), p);
        chunk->ClearFlagSlow(MemoryChunk::COMPACTION_WAS_ABORTED);
      }
    }
  }

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_EVACUATE_EPILOGUE);
    EvacuateEpilogue();
  }

#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap && !sweeper_->sweeping_in_progress()) {
    EvacuationVerifier verifier(heap_);
    verifier.Run();
  }
#endif  // VERIFY_HEAP
}

class UpdatingItem : public ParallelWorkItem {
 public:
  virtual ~UpdatingItem() = default;
  virtual void Process() = 0;
};

class PointersUpdatingJob : public v8::JobTask {
 public:
  explicit PointersUpdatingJob(
      Isolate* isolate, MarkCompactCollector* collector,
      std::vector<std::unique_ptr<UpdatingItem>> updating_items)
      : collector_(collector),
        updating_items_(std::move(updating_items)),
        remaining_updating_items_(updating_items_.size()),
        generator_(updating_items_.size()),
        tracer_(isolate->heap()->tracer()),
        trace_id_(reinterpret_cast<uint64_t>(this) ^
                  tracer_->CurrentEpoch(GCTracer::Scope::MC_EVACUATE)) {}

  void Run(JobDelegate* delegate) override {
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(
        collector_->heap()->isolate());

    if (delegate->IsJoiningThread()) {
      TRACE_GC_WITH_FLOW(tracer_,
                         GCTracer::Scope::MC_EVACUATE_UPDATE_POINTERS_PARALLEL,
                         trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
      UpdatePointers(delegate);
    } else {
      TRACE_GC_EPOCH_WITH_FLOW(
          tracer_, GCTracer::Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS,
          ThreadKind::kBackground, trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
      UpdatePointers(delegate);
    }
  }

  void UpdatePointers(JobDelegate* delegate) {
    while (remaining_updating_items_.load(std::memory_order_relaxed) > 0) {
      std::optional<size_t> index = generator_.GetNext();
      if (!index) return;
      for (size_t i = *index; i < updating_items_.size(); ++i) {
        auto& work_item = updating_items_[i];
        if (!work_item->TryAcquire()) break;
        work_item->Process();
        if (remaining_updating_items_.fetch_sub(1, std::memory_order_relaxed) <=
            1) {
          return;
        }
      }
    }
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    size_t items = remaining_updating_items_.load(std::memory_order_relaxed);
    if (!v8_flags.parallel_pointer_update ||
        !collector_->UseBackgroundThreadsInCycle()) {
      return std::min<size_t>(items, 1);
    }
    const size_t kMaxPointerUpdateTasks = 8;
    size_t max_concurrency = std::min<size_t>(kMaxPointerUpdateTasks, items);
    DCHECK_IMPLIES(items > 0, max_concurrency > 0);
    return max_concurrency;
  }

  uint64_t trace_id() const { return trace_id_; }

 private:
  MarkCompactCollector* collector_;
  std::vector<std::unique_ptr<UpdatingItem>> updating_items_;
  std::atomic<size_t> remaining_updating_items_{0};
  IndexGenerator generator_;

  GCTracer* tracer_;
  const uint64_t trace_id_;
};

namespace {

class RememberedSetUpdatingItem : public UpdatingItem {
 public:
  explicit RememberedSetUpdatingItem(Heap* heap, MutablePageMetadata* chunk)
      : heap_(heap),
        marking_state_(heap_->non_atomic_marking_state()),
        chunk_(chunk),
        record_old_to_shared_slots_(heap->isolate()->has_shared_space() &&
                                    !chunk->Chunk()->InWritableSharedSpace()) {}
  ~RememberedSetUpdatingItem() override = default;

  void Process() override {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                 "RememberedSetUpdatingItem::Process");
    UpdateUntypedPointers();
    UpdateTypedPointers();
  }

 private:
  template <typename TSlot>
  inline void CheckSlotForOldToSharedUntyped(PtrComprCageBase cage_base,
                                             MutablePageMetadata* page,
                                             TSlot slot) {
    Tagged<HeapObject> heap_object;

    if (!slot.load(cage_base).GetHeapObject(&heap_object)) {
      return;
    }

    if (InWritableSharedSpace(heap_object)) {
      RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::NON_ATOMIC>(
          page, page->Offset(slot.address()));
    }
  }

  inline void CheckSlotForOldToSharedTyped(
      MutablePageMetadata* page, SlotType slot_type, Address addr,
      WritableJitAllocation& jit_allocation) {
    Tagged<HeapObject> heap_object =
        UpdateTypedSlotHelper::GetTargetObject(page->heap(), slot_type, addr);

#if DEBUG
    UpdateTypedSlotHelper::UpdateTypedSlot(
        jit_allocation, page->heap(), slot_type, addr,
        [heap_object](FullMaybeObjectSlot slot) {
          DCHECK_EQ((*slot).GetHeapObjectAssumeStrong(), heap_object);
          return KEEP_SLOT;
        });
#endif  // DEBUG

    if (InWritableSharedSpace(heap_object)) {
      const uintptr_t offset = page->Offset(addr);
      DCHECK_LT(offset, static_cast<uintptr_t>(TypedSlotSet::kMaxOffset));
      RememberedSet<OLD_TO_SHARED>::InsertTyped(page, slot_type,
                                                static_cast<uint32_t>(offset));
    }
  }

  template <typename TSlot>
  inline void CheckAndUpdateOldToNewSlot(TSlot slot,
                                         const PtrComprCageBase cage_base) {
    static_assert(
        std::is_same<TSlot, FullMaybeObjectSlot>::value ||
            std::is_same<TSlot, MaybeObjectSlot>::value,
        "Only FullMaybeObjectSlot and MaybeObjectSlot are expected here");
    Tagged<HeapObject> heap_object;
    if (!(*slot).GetHeapObject(&heap_object)) return;
    if (!Heap::InYoungGeneration(heap_object)) return;

    if (!v8_flags.sticky_mark_bits) {
      DCHECK_IMPLIES(v8_flags.minor_ms && !Heap::IsLargeObject(heap_object),
                     Heap::InToPage(heap_object));
      DCHECK_IMPLIES(!v8_flags.minor_ms || Heap::IsLargeObject(heap_object),
                     Heap::InFromPage(heap_object));
    }

    // OLD_TO_NEW slots are recorded in dead memory, so they might point to
    // dead objects.
    DCHECK_IMPLIES(!heap_object->map_word(kRelaxedLoad).IsForwardingAddress(),
                   !marking_state_->IsMarked(heap_object));
    UpdateSlot(cage_base, slot);
  }

  void UpdateUntypedPointers() {
    UpdateUntypedOldToNewPointers<OLD_TO_NEW>();
    UpdateUntypedOldToNewPointers<OLD_TO_NEW_BACKGROUND>();
    UpdateUntypedOldToOldPointers();
    UpdateUntypedOldToCodePointers();
    UpdateUntypedTrustedToTrustedPointers();
  }

  template <RememberedSetType old_to_new_type>
  void UpdateUntypedOldToNewPointers() {
    if (!chunk_->slot_set<old_to_new_type, AccessMode::NON_ATOMIC>()) return;

    const PtrComprCageBase cage_base = heap_->isolate();
    // Marking bits are cleared already when the page is already swept. This
    // is fine since in that case the sweeper has already removed dead invalid
    // objects as well.
    RememberedSet<old_to_new_type>::Iterate(
        chunk_,
        [this, cage_base](MaybeObjectSlot slot) {
          CheckAndUpdateOldToNewSlot(slot, cage_base);
          // A new space string might have been promoted into the shared heap
          // during GC.
          if (record_old_to_shared_slots_) {
            CheckSlotForOldToSharedUntyped(cage_base, chunk_, slot);
          }
          // Always keep slot since all slots are dropped at once after
          // iteration.
          return KEEP_SLOT;
        },
        SlotSet::KEEP_EMPTY_BUCKETS);

    // Full GCs will empty new space, so [old_to_new_type] is empty.
    chunk_->ReleaseSlotSet(old_to_new_type);
  }

  void UpdateUntypedOldToOldPointers() {
    if (!chunk_->slot_set<OLD_TO_OLD, AccessMode::NON_ATOMIC>()) return;

    const PtrComprCageBase cage_base = heap_->isolate();
    if (chunk_->Chunk()->executable()) {
      // When updating pointer in an InstructionStream (in particular, the
      // pointer to relocation info), we need to use WriteProtectedSlots that
      // ensure that the code page is unlocked.
      WritableJitPage jit_page(chunk_->area_start(), chunk_->area_size());
      RememberedSet<OLD_TO_OLD>::Iterate(
          chunk_,
          [&](MaybeObjectSlot slot) {
            WritableJitAllocation jit_allocation =
                jit_page.LookupAllocationContaining(slot.address());
            UpdateSlot(cage_base, WriteProtectedSlot<ObjectSlot>(
                                      jit_allocation, slot.address()));
            // Always keep slot since all slots are dropped at once after
            // iteration.
            return KEEP_SLOT;
          },
          SlotSet::KEEP_EMPTY_BUCKETS);
    } else {
      RememberedSet<OLD_TO_OLD>::Iterate(
          chunk_,
          [&](MaybeObjectSlot slot) {
            UpdateSlot(cage_base, slot);
            // A string might have been promoted into the shared heap during
            // GC.
            if (record_old_to_shared_slots_) {
              CheckSlotForOldToSharedUntyped(cage_base, chunk_, slot);
            }
            // Always keep slot since all slots are dropped at once after
            // iteration.
            return KEEP_SLOT;
          },
          SlotSet::KEEP_EMPTY_BUCKETS);
    }

    chunk_->ReleaseSlotSet(OLD_TO_OLD);
  }

  void UpdateUntypedOldToCodePointers() {
    if (!chunk_->slot_set<OLD_TO_CODE, AccessMode::NON_ATOMIC>()) return;

    const PtrComprCageBase cage_base = heap_->isolate();
#ifdef V8_EXTERNAL_CODE_SPACE
    const PtrComprCageBase code_cage_base(heap_->isolate()->code_cage_base());
#else
    const PtrComprCageBase code_cage_base = cage_base;
#endif
    RememberedSet<OLD_TO_CODE>::Iterate(
        chunk_,
        [=](MaybeObjectSlot slot) {
          Tagged<HeapObject> host = HeapObject::FromAddress(
              slot.address() - Code::kInstructionStreamOffset);
          DCHECK(IsCode(host, cage_base));
          UpdateStrongCodeSlot(host, cage_base, code_cage_base,
                               InstructionStreamSlot(slot.address()));
          // Always keep slot since all slots are dropped at once after
          // iteration.
          return KEEP_SLOT;
        },
        SlotSet::FREE_EMPTY_BUCKETS);

    chunk_->ReleaseSlotSet(OLD_TO_CODE);
  }

  void UpdateUntypedTrustedToTrustedPointers() {
#ifdef V8_ENABLE_SANDBOX
    // When the sandbox is enabled, we must not process the TRUSTED_TO_TRUSTED
    // remembered set on any chunk that is located inside the sandbox (in which
    // case the set should be unused). This is because an attacker could either
    // directly modify the TRUSTED_TO_TRUSTED set on such a chunk, or trick the
    // GC into populating it with invalid pointers, both of which may lead to
    // memory corruption inside the trusted space here.
    if (InsideSandbox(chunk_->ChunkAddress())) return;
#endif
    if (!chunk_->slot_set<TRUSTED_TO_TRUSTED, AccessMode::NON_ATOMIC>()) return;

    // TODO(saelo) we can probably drop all the cage_bases here once we no
    // longer need to pass them into our slot implementations.
    const PtrComprCageBase unused_cage_base(kNullAddress);

    if (chunk_->Chunk()->executable()) {
      // When updating the InstructionStream -> Code pointer, we need to use
      // WriteProtectedSlots that ensure that the code page is unlocked.
      WritableJitPage jit_page(chunk_->area_start(), chunk_->area_size());

      RememberedSet<TRUSTED_TO_TRUSTED>::Iterate(
          chunk_,
          [&](MaybeObjectSlot slot) {
            WritableJitAllocation jit_allocation =
                jit_page.LookupAllocationContaining(slot.address());
            UpdateStrongSlot(unused_cage_base,
                             WriteProtectedSlot<ProtectedPointerSlot>(
                                 jit_allocation, slot.address()));
            // Always keep slot since all slots are dropped at once after
            // iteration.
            return KEEP_SLOT;
          },
          SlotSet::FREE_EMPTY_BUCKETS);
    } else {
      RememberedSet<TRUSTED_TO_TRUSTED>::Iterate(
          chunk_,
          [&](MaybeObjectSlot slot) {
            UpdateStrongSlot(unused_cage_base,
                             ProtectedPointerSlot(slot.address()));
            // Always keep slot since all slots are dropped at once after
            // iteration.
            return KEEP_SLOT;
          },
          SlotSet::FREE_EMPTY_BUCKETS);
    }

    chunk_->ReleaseSlotSet(TRUSTED_TO_TRUSTED);
  }

  void UpdateTypedPointers() {
    if (!chunk_->Chunk()->executable()) {
      DCHECK_NULL((chunk_->typed_slot_set<OLD_TO_NEW>()));
      DCHECK_NULL((chunk_->typed_slot_set<OLD_TO_OLD>()));
      return;
    }

    WritableJitPage jit_page = ThreadIsolation::LookupWritableJitPage(
        chunk_->area_start(), chunk_->area_size());
    UpdateTypedOldToNewPointers(jit_page);
    UpdateTypedOldToOldPointers(jit_page);
  }

  void UpdateTypedOldToNewPointers(WritableJitPage& jit_page) {
    if (chunk_->typed_slot_set<OLD_TO_NEW, AccessMode::NON_ATOMIC>() == nullptr)
      return;
    const PtrComprCageBase cage_base = heap_->isolate();
    const auto check_and_update_old_to_new_slot_fn =
        [this, cage_base](FullMaybeObjectSlot slot) {
          CheckAndUpdateOldToNewSlot(slot, cage_base);
          return KEEP_SLOT;
        };

    RememberedSet<OLD_TO_NEW>::IterateTyped(
        chunk_, [this, &check_and_update_old_to_new_slot_fn, &jit_page](
                    SlotType slot_type, Address slot) {
          WritableJitAllocation jit_allocation =
              jit_page.LookupAllocationContaining(slot);
          UpdateTypedSlotHelper::UpdateTypedSlot(
              jit_allocation, heap_, slot_type, slot,
              check_and_update_old_to_new_slot_fn);
          // A new space string might have been promoted into the shared heap
          // during GC.
          if (record_old_to_shared_slots_) {
            CheckSlotForOldToSharedTyped(chunk_, slot_type, slot,
                                         jit_allocation);
          }
          // Always keep slot since all slots are dropped at once after
          // iteration.
          return KEEP_SLOT;
        });
    // Full GCs will empty new space, so OLD_TO_NEW is empty.
    chunk_->ReleaseTypedSlotSet(OLD_TO_NEW);
    // OLD_TO_NEW_BACKGROUND typed slots set should always be empty.
    DCHECK_NULL(chunk_->typed_slot_set<OLD_TO_NEW_BACKGROUND>());
  }

  void UpdateTypedOldToOldPointers(WritableJitPage& jit_page) {
    if (chunk_->typed_slot_set<OLD_TO_OLD, AccessMode::NON_ATOMIC>() == nullptr)
      return;
    PtrComprCageBase cage_base = heap_->isolate();
    RememberedSet<OLD_TO_OLD>::IterateTyped(
        chunk_, [this, cage_base, &jit_page](SlotType slot_type, Address slot) {
          // Using UpdateStrongSlot is OK here, because there are no weak
          // typed slots.
          WritableJitAllocation jit_allocation =
              jit_page.LookupAllocationContaining(slot);
          SlotCallbackResult result = UpdateTypedSlotHelper::UpdateTypedSlot(
              jit_allocation, heap_, slot_type, slot,
              [cage_base](FullMaybeObjectSlot slot) {
                UpdateStrongSlot(cage_base, slot);
                // Always keep slot since all slots are dropped at once after
                // iteration.
                return KEEP_SLOT;
              });
          // A string might have been promoted into the shared heap during GC.
          if (record_old_to_shared_slots_) {
            CheckSlotForOldToSharedTyped(chunk_, slot_type, slot,
                                         jit_allocation);
          }
          return result;
        });
    chunk_->ReleaseTypedSlotSet(OLD_TO_OLD);
  }

  Heap* heap_;
  NonAtomicMarkingState* marking_state_;
  MutablePageMetadata* chunk_;
  const bool record_old_to_shared_slots_;
};

}  // namespace

namespace {
template <typename IterateableSpace>
void CollectRememberedSetUpdatingItems(
    std::vector<std::unique_ptr<UpdatingItem>>* items,
    IterateableSpace* space) {
  for (MutablePageMetadata* page : *space) {
    // No need to update pointers on evacuation candidates. Evacuated pages will
    // be released after this phase.
    if (page->Chunk()->IsEvacuationCandidate()) continue;
    if (page->ContainsAnySlots()) {
      items->emplace_back(
          std::make_unique<RememberedSetUpdatingItem>(space->heap(), page));
    }
  }
}
}  // namespace

class EphemeronTableUpdatingItem : public UpdatingItem {
 public:
  enum EvacuationState { kRegular, kAborted };

  explicit EphemeronTableUpdatingItem(Heap* heap) : heap_(heap) {}
  ~EphemeronTableUpdatingItem() override = default;

  void Process() override {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                 "EphemeronTableUpdatingItem::Process");
    PtrComprCageBase cage_base(heap_->isolate());

    auto* table_map = heap_->ephemeron_remembered_set()->tables();
    for (auto it = table_map->begin(); it != table_map->end(); it++) {
      Tagged<EphemeronHashTable> table = it->first;
      auto& indices = it->second;
      if (table->map_word(cage_base, kRelaxedLoad).IsForwardingAddress()) {
        // The object has moved, so ignore slots in dead memory here.
        continue;
      }
      DCHECK(IsMap(table->map(cage_base), cage_base));
      DCHECK(IsEphemeronHashTable(table, cage_base));
      for (auto iti = indices.begin(); iti != indices.end(); ++iti) {
        // EphemeronHashTable keys must be heap objects.
        ObjectSlot key_slot(table->RawFieldOfElementAt(
            EphemeronHashTable::EntryToIndex(InternalIndex(*iti))));
        Tagged<Object> key_object = key_slot.Relaxed_Load();
        Tagged<HeapObject> key;
        CHECK(key_object.GetHeapObject(&key));
        MapWord map_word = key->map_word(cage_base, kRelaxedLoad);
        if (map_word.IsForwardingAddress()) {
          key = map_word.ToForwardingAddress(key);
          key_slot.Relaxed_Store(key);
        }
      }
    }
    table_map->clear();
  }

 private:
  Heap* const heap_;
};

void MarkCompactCollector::UpdatePointersAfterEvacuation() {
  TRACE_GC(heap_->tracer(), GCTracer::Scope::MC_EVACUATE_UPDATE_POINTERS);

  {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MC_EVACUATE_UPDATE_POINTERS_TO_NEW_ROOTS);
    // The external string table is updated at the end.
    PointersUpdatingVisitor updating_visitor(heap_);
    heap_->IterateRootsIncludingClients(
        &updating_visitor,
        base::EnumSet<SkipRoot>{SkipRoot::kExternalStringTable,
                                SkipRoot::kConservativeStack,
                                SkipRoot::kReadOnlyBuiltins});
  }

  {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MC_EVACUATE_UPDATE_POINTERS_CLIENT_HEAPS);
    UpdatePointersInClientHeaps();
  }

  {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MC_EVACUATE_UPDATE_POINTERS_SLOTS_MAIN);
    std::vector<std::unique_ptr<UpdatingItem>> updating_items;

    CollectRememberedSetUpdatingItems(&updating_items, heap_->old_space());
    CollectRememberedSetUpdatingItems(&updating_items, heap_->code_space());
    if (heap_->shared_space()) {
      CollectRememberedSetUpdatingItems(&updating_items, heap_->shared_space());
    }
    CollectRememberedSetUpdatingItems(&updating_items, heap_->lo_space());
    CollectRememberedSetUpdatingItems(&updating_items, heap_->code_lo_space());
    if (heap_->shared_lo_space()) {
      CollectRememberedSetUpdatingItems(&updating_items,
                                        heap_->shared_lo_space());
    }
    CollectRememberedSetUpdatingItems(&updating_items, heap_->trusted_space());
    CollectRememberedSetUpdatingItems(&updating_items,
                                      heap_->trusted_lo_space());
    if (heap_->shared_trusted_space()) {
      CollectRememberedSetUpdatingItems(&updating_items,
                                        heap_->shared_trusted_space());
    }
    if (heap_->shared_trusted_lo_space()) {
      CollectRememberedSetUpdatingItems(&updating_items,
                                        heap_->shared_trusted_lo_space());
    }

    // Iterating to space may require a valid body descriptor for e.g.
    // WasmStruct which races with updating a slot in Map. Since to space is
    // empty after a full GC, such races can't happen.
    DCHECK_IMPLIES(heap_->new_space(), heap_->new_space()->Size() == 0);

    updating_items.push_back(
        std::make_unique<EphemeronTableUpdatingItem>(heap_));

    auto pointers_updating_job = std::make_unique<PointersUpdatingJob>(
        heap_->isolate(), this, std::move(updating_items));
    TRACE_GC_NOTE_WITH_FLOW("PointersUpdatingJob started",
                            pointers_updating_job->trace_id(),
                            TRACE_EVENT_FLAG_FLOW_OUT);
    V8::GetCurrentPlatform()
        ->CreateJob(v8::TaskPriority::kUserBlocking,
                    std::move(pointers_updating_job))
        ->Join();
  }

  {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MC_EVACUATE_UPDATE_POINTERS_WEAK);
    // Update pointers from external string table.
    heap_->UpdateReferencesInExternalStringTable(
        &UpdateReferenceInExternalStringTableEntry);

    // Update pointers in string forwarding table.
    // When GC was performed without a stack, the table was cleared and this
    // does nothing. In the case this was a GC with stack, we need to update
    // the entries for evacuated objects.
    // All entries are objects in shared space (unless
    // --always-use-forwarding-table), so we only need to update pointers during
    // a shared GC.
    if (heap_->isolate()->OwnsStringTables() ||
        V8_UNLIKELY(v8_flags.always_use_string_forwarding_table)) {
      heap_->isolate()->string_forwarding_table()->UpdateAfterFullEvacuation();
    }

    EvacuationWeakObjectRetainer evacuation_object_retainer;
    heap_->ProcessWeakListRoots(&evacuation_object_retainer);
  }

  {
    TRACE_GC(heap_->tracer(),
             GCTracer::Scope::MC_EVACUATE_UPDATE_POINTERS_POINTER_TABLES);
    UpdatePointersInPointerTables();
  }

  // Flush the inner_pointer_to_code_cache which may now have stale contents.
  heap_->isolate()->inner_pointer_to_code_cache()->Flush();
}

void MarkCompactCollector::UpdatePointersInClientHeaps() {
  Isolate* const isolate = heap_->isolate();
  if (!isolate->is_shared_space_isolate()) return;

  isolate->global_safepoint()->IterateClientIsolates(
      [this](Isolate* client) { UpdatePointersInClientHeap(client); });
}

void MarkCompactCollector::UpdatePointersInClientHeap(Isolate* client) {
  PtrComprCageBase cage_base(client);
  MemoryChunkIterator chunk_iterator(client->heap());

  while (chunk_iterator.HasNext()) {
    MutablePageMetadata* page = chunk_iterator.Next();
    MemoryChunk* chunk = page->Chunk();

    const auto slot_count = RememberedSet<OLD_TO_SHARED>::Iterate(
        page,
        [cage_base](MaybeObjectSlot slot) {
          return UpdateOldToSharedSlot(cage_base, slot);
        },
        SlotSet::FREE_EMPTY_BUCKETS);

    if (slot_count == 0 || chunk->InYoungGeneration()) {
      page->ReleaseSlotSet(OLD_TO_SHARED);
    }

    const PtrComprCageBase unused_cage_base(kNullAddress);

    const auto protected_slot_count =
        RememberedSet<TRUSTED_TO_SHARED_TRUSTED>::Iterate(
            page,
            [unused_cage_base](MaybeObjectSlot slot) {
              ProtectedPointerSlot protected_slot(slot.address());
              return UpdateOldToSharedSlot(unused_cage_base, protected_slot);
            },
            SlotSet::FREE_EMPTY_BUCKETS);
    if (protected_slot_count == 0) {
      page->ReleaseSlotSet(TRUSTED_TO_SHARED_TRUSTED);
    }

    if (!chunk->executable()) {
      DCHECK_NULL(page->typed_slot_set<OLD_TO_SHARED>());
      continue;
    }

    WritableJitPage jit_page = ThreadIsolation::LookupWritableJitPage(
        page->area_start(), page->area_size());
    const auto typed_slot_count = RememberedSet<OLD_TO_SHARED>::IterateTyped(
        page, [this, &jit_page](SlotType slot_type, Address slot) {
          // Using UpdateStrongSlot is OK here, because there are no weak
          // typed slots.
          PtrComprCageBase cage_base = heap_->isolate();
          WritableJitAllocation jit_allocation =
              jit_page.LookupAllocationContaining(slot);
          return UpdateTypedSlotHelper::UpdateTypedSlot(
              jit_allocation, heap_, slot_type, slot,
              [cage_base](FullMaybeObjectSlot slot) {
                return UpdateStrongOldToSharedSlot(cage_base, slot);
              });
        });
    if (typed_slot_count == 0 || chunk->InYoungGeneration())
      page->ReleaseTypedSlotSet(OLD_TO_SHARED);
  }

#ifdef V8_ENABLE_SANDBOX
  TrustedPointerTable* const tpt = &client->trusted_pointer_table();
  tpt->IterateActiveEntriesIn(
      client->heap()->trusted_pointer_space(),
      [tpt](TrustedPointerHandle handle, Address content) {
        Tagged<HeapObject> heap_obj = Cast<HeapObject>(Tagged<Object>(content));
        MapWord map_word = heap_obj->map_word(kRelaxedLoad);
        if (!map_word.IsForwardingAddress()) return;
        Tagged<HeapObject> relocated_object =
            map_word.ToForwardingAddress(heap_obj);
        DCHECK(IsExposedTrustedObject(relocated_object));
        DCHECK(InWritableSharedSpace(relocated_object));
        auto instance_type = relocated_object->map()->instance_type();
        auto tag = IndirectPointerTagFromInstanceType(instance_type);
        tpt->Set(handle, relocated_object.ptr(), tag);
      });
#endif  // V8_ENABLE_SANDBOX
}

void MarkCompactCollector::UpdatePointersInPointerTables() {
#ifdef V8_ENABLE_SANDBOX
  // Process an entry of a pointer table, returning either the relocated object
  // or a null pointer if the object wasn't relocated.
  auto process_entry = [&](Address content) -> Tagged<ExposedTrustedObject> {
    Tagged<HeapObject> heap_obj = Cast<HeapObject>(Tagged<Object>(content));
    MapWord map_word = heap_obj->map_word(kRelaxedLoad);
    if (!map_word.IsForwardingAddress()) return {};
    Tagged<HeapObject> relocated_object =
        map_word.ToForwardingAddress(heap_obj);
    DCHECK(IsExposedTrustedObject(relocated_object));
    return Cast<ExposedTrustedObject>(relocated_object);
  };

  TrustedPointerTable* const tpt = &heap_->isolate()->trusted_pointer_table();
  tpt->IterateActiveEntriesIn(
      heap_->trusted_pointer_space(),
      [&](TrustedPointerHandle handle, Address content) {
        Tagged<ExposedTrustedObject> relocated_object = process_entry(content);
        if (!relocated_object.is_null()) {
          DCHECK_EQ(handle, relocated_object->self_indirect_pointer_handle());
          auto instance_type = relocated_object->map()->instance_type();
          auto tag = IndirectPointerTagFromInstanceType(instance_type);
          tpt->Set(handle, relocated_object.ptr(), tag);
        }
      });

  CodePointerTable* const cpt = GetProcessWideCodePointerTable();
  cpt->IterateActiveEntriesIn(
      heap_->code_pointer_space(),
      [&](CodePointerHandle handle, Address content) {
        Tagged<ExposedTrustedObject> relocated_object = process_entry(content);
        if (!relocated_object.is_null()) {
          DCHECK_EQ(handle, relocated_object->self_indirect_pointer_handle());
          cpt->SetCodeObject(handle, relocated_object.address());
        }
      });
#endif  // V8_ENABLE_SANDBOX
}

void MarkCompactCollector::ReportAbortedEvacuationCandidateDueToOOM(
    Address failed_start, PageMetadata* page) {
  base::MutexGuard guard(&mutex_);
  aborted_evacuation_candidates_due_to_oom_.push_back(
      std::make_pair(failed_start, page));
}

void MarkCompactCollector::ReportAbortedEvacuationCandidateDueToFlags(
    Address failed_start, PageMetadata* page) {
  MemoryChunk* chunk = page->Chunk();
  DCHECK(!chunk->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED));
  chunk->SetFlagSlow(MemoryChunk::COMPACTION_WAS_ABORTED);
  base::MutexGuard guard(&mutex_);
  aborted_evacuation_candidates_due_to_flags_.push_back(
      std::make_pair(failed_start, page));
}

namespace {

void ReRecordPage(Heap* heap, Address failed_start, PageMetadata* page) {
  DCHECK(page->Chunk()->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED));

  // Aborted compaction page. We have to record slots here, since we
  // might not have recorded them in first place.

  // Remove mark bits in evacuated area.
  page->marking_bitmap()->ClearRange<AccessMode::NON_ATOMIC>(
      MarkingBitmap::AddressToIndex(page->area_start()),
      MarkingBitmap::LimitAddressToIndex(failed_start));

  // Remove outdated slots.
  RememberedSet<OLD_TO_NEW>::RemoveRange(page, page->area_start(), failed_start,
                                         SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<OLD_TO_NEW>::RemoveRangeTyped(page, page->area_start(),
                                              failed_start);

  RememberedSet<OLD_TO_NEW_BACKGROUND>::RemoveRange(
      page, page->area_start(), failed_start, SlotSet::FREE_EMPTY_BUCKETS);
  DCHECK_NULL(page->typed_slot_set<OLD_TO_NEW_BACKGROUND>());

  RememberedSet<OLD_TO_SHARED>::RemoveRange(
      page, page->area_start(), failed_start, SlotSet::FREE_EMPTY_BUCKETS);
  RememberedSet<OLD_TO_SHARED>::RemoveRangeTyped(page, page->area_start(),
                                                 failed_start);

  // Re-record slots and recompute live bytes.
  EvacuateRecordOnlyVisitor visitor(heap);
  LiveObjectVisitor::VisitMarkedObjectsNoFail(page, &visitor);
  page->SetLiveBytes(visitor.live_object_size());
  // Array buffers will be processed during pointer updating.
}

}  // namespace

size_t MarkCompactCollector::PostProcessAbortedEvacuationCandidates() {
  for (auto start_and_page : aborted_evacuation_candidates_due_to_oom_) {
    PageMetadata* page = start_and_page.second;
    MemoryChunk* chunk = page->Chunk();
    DCHECK(!chunk->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED));
    chunk->SetFlagSlow(MemoryChunk::COMPACTION_WAS_ABORTED);
  }
  for (auto start_and_page : aborted_evacuation_candidates_due_to_oom_) {
    ReRecordPage(heap_, start_and_page.first, start_and_page.second);
  }
  for (auto start_and_page : aborted_evacuation_candidates_due_to_flags_) {
    ReRecordPage(heap_, start_and_page.first, start_and_page.second);
  }
  const size_t aborted_pages =
      aborted_evacuation_candidates_due_to_oom_.size() +
      aborted_evacuation_candidates_due_to_flags_.size();
  size_t aborted_pages_verified = 0;
  for (PageMetadata* p : old_space_evacuation_pages_) {
    MemoryChunk* chunk = p->Chunk();
    if (chunk->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED)) {
      // Only clear EVACUATION_CANDIDATE flag after all slots were re-recorded
      // on all aborted pages. Necessary since repopulating
      // OLD_TO_OLD still requires the EVACUATION_CANDIDATE flag. After clearing
      // the evacuation candidate flag the page is again in a regular state.
      p->ClearEvacuationCandidate();
      aborted_pages_verified++;
    } else {
      DCHECK(chunk->IsEvacuationCandidate());
      DCHECK(p->SweepingDone());
    }
  }
  DCHECK_EQ(aborted_pages_verified, aborted_pages);
  USE(aborted_pages_verified);
  return aborted_pages;
}

void MarkCompactCollector::ReleaseEvacuationCandidates() {
  for (PageMetadata* p : old_space_evacuation_pages_) {
    if (!p->Chunk()->IsEvacuationCandidate()) continue;
    PagedSpace* space = static_cast<PagedSpace*>(p->owner());
    p->SetLiveBytes(0);
    CHECK(p->SweepingDone());
    space->ReleasePage(p);
  }
  old_space_evacuation_pages_.clear();
  compacting_ = false;
}

void MarkCompactCollector::StartSweepNewSpace() {
  PagedSpaceForNewSpace* paged_space = heap_->paged_new_space()->paged_space();
  paged_space->ClearAllocatorState();

  int will_be_swept = 0;

  DCHECK_EQ(Heap::ResizeNewSpaceMode::kNone, resize_new_space_);
  resize_new_space_ = heap_->ShouldResizeNewSpace();
  if (resize_new_space_ == Heap::ResizeNewSpaceMode::kShrink) {
    paged_space->StartShrinking();
  }

  DCHECK(empty_new_space_pages_to_be_swept_.empty());
  for (auto it = paged_space->begin(); it != paged_space->end();) {
    PageMetadata* p = *(it++);
    DCHECK(p->SweepingDone());

    if (p->live_bytes() > 0) {
      // Non-empty pages will be evacuated/promoted.
      continue;
    }

    if (paged_space->ShouldReleaseEmptyPage()) {
      paged_space->ReleasePage(p);
    } else {
      empty_new_space_pages_to_be_swept_.push_back(p);
    }
    will_be_swept++;
  }

  if (v8_flags.gc_verbose) {
    PrintIsolate(heap_->isolate(),
                 "sweeping: space=%s initialized_for_sweeping=%d",
                 ToString(paged_space->identity()), will_be_swept);
  }
}

void MarkCompactCollector::StartSweepSpace(PagedSpace* space) {
  DCHECK_NE(NEW_SPACE, space->identity());
  space->ClearAllocatorState();

  int will_be_swept = 0;
  bool unused_page_present = false;

  Sweeper* sweeper = heap_->sweeper();

  // Loop needs to support deletion if live bytes == 0 for a page.
  for (auto it = space->begin(); it != space->end();) {
    PageMetadata* p = *(it++);
    DCHECK(p->SweepingDone());

    if (p->Chunk()->IsEvacuationCandidate()) {
      DCHECK_NE(NEW_SPACE, space->identity());
      // Will be processed in Evacuate.
      continue;
    }

    // One unused page is kept, all further are released before sweeping them.
    if (p->live_bytes() == 0) {
      if (unused_page_present) {
        if (v8_flags.gc_verbose) {
          PrintIsolate(heap_->isolate(), "sweeping: released page: %p",
                       static_cast<void*>(p));
        }
        space->ReleasePage(p);
        continue;
      }
      unused_page_present = true;
    }

    sweeper->AddPage(space->identity(), p);
    will_be_swept++;
  }

  if (v8_flags.sticky_mark_bits && space->identity() == OLD_SPACE) {
    static_cast<StickySpace*>(space)->set_old_objects_size(space->Size());
  }

  if (v8_flags.gc_verbose) {
    PrintIsolate(heap_->isolate(),
                 "sweeping: space=%s initialized_for_sweeping=%d",
                 ToString(space->identity()), will_be_swept);
  }
}

namespace {
bool ShouldPostponeFreeingEmptyPages(LargeObjectSpace* space) {
  // Delay releasing dead old large object pages until after pointer updating is
  // done because dead old space objects may have old-to-new slots (which
  // were possibly later overriden with old-to-old references) that are
  // pointing to these pages and will need to be updated.
  if (space->identity() == LO_SPACE) return true;
  // Old-to-new slots may also point to shared spaces. Delay releasing so that
  // updating slots in dead old objects can access the dead shared objects.
  if (space->identity() == SHARED_LO_SPACE) return true;
  return false;
}
}  // namespace

void MarkCompactCollector::SweepLargeSpace(LargeObjectSpace* space) {
  PtrComprCageBase cage_base(heap_->isolate());
  size_t surviving_object_size = 0;
  const MemoryAllocator::FreeMode free_mode =
      ShouldPostponeFreeingEmptyPages(space)
          ? MemoryAllocator::FreeMode::kPostpone
          : MemoryAllocator::FreeMode::kImmediately;
  for (auto it = space->begin(); it != space->end();) {
    LargePageMetadata* current = *(it++);
    Tagged<HeapObject> object = current->GetObject();
    if (!marking_state_->IsMarked(object)) {
      // Object is dead and page can be released.
      space->RemovePage(current);
      heap_->memory_allocator()->Free(free_mode, current);

      continue;
    }
    if (!v8_flags.sticky_mark_bits) {
      MarkBit::From(object).Clear();
      current->SetLiveBytes(0);
    }
    current->ProgressBar().ResetIfEnabled();
    surviving_object_size += static_cast<size_t>(object->Size(cage_base));
  }
  space->set_objects_size(surviving_object_size);
}

void MarkCompactCollector::Sweep() {
  DCHECK(!sweeper_->sweeping_in_progress());
  sweeper_->InitializeMajorSweeping();

  TRACE_GC_EPOCH_WITH_FLOW(
      heap_->tracer(), GCTracer::Scope::MC_SWEEP, ThreadKind::kMain,
      sweeper_->GetTraceIdForFlowEvent(GCTracer::Scope::MC_SWEEP),
      TRACE_EVENT_FLAG_FLOW_OUT);
#ifdef DEBUG
  state_ = SWEEP_SPACES;
#endif

  {
    GCTracer::Scope sweep_scope(heap_->tracer(), GCTracer::Scope::MC_SWEEP_LO,
                                ThreadKind::kMain);
    SweepLargeSpace(heap_->lo_space());
  }
  {
    GCTracer::Scope sweep_scope(
        heap_->tracer(), GCTracer::Scope::MC_SWEEP_CODE_LO, ThreadKind::kMain);
    SweepLargeSpace(heap_->code_lo_space());
  }
  if (heap_->shared_space()) {
    GCTracer::Scope sweep_scope(heap_->tracer(),
                                GCTracer::Scope::MC_SWEEP_SHARED_LO,
                                ThreadKind::kMain);
    SweepLargeSpace(heap_->shared_lo_space());
  }
  {
    GCTracer::Scope sweep_scope(heap_->tracer(), GCTracer::Scope::MC_SWEEP_OLD,
                                ThreadKind::kMain);
    StartSweepSpace(heap_->old_space());
  }
  {
    GCTracer::Scope sweep_scope(heap_->tracer(), GCTracer::Scope::MC_SWEEP_CODE,
                                ThreadKind::kMain);
    StartSweepSpace(heap_->code_space());
  }
  if (heap_->shared_space()) {
    GCTracer::Scope sweep_scope(
        heap_->tracer(), GCTracer::Scope::MC_SWEEP_SHARED, ThreadKind::kMain);
    StartSweepSpace(heap_->shared_space());
  }
  {
    GCTracer::Scope sweep_scope(
        heap_->tracer(), GCTracer::Scope::MC_SWEEP_TRUSTED, ThreadKind::kMain);
    StartSweepSpace(heap_->trusted_space());
  }
  if (heap_->shared_trusted_space()) {
    GCTracer::Scope sweep_scope(
        heap_->tracer(), GCTracer::Scope::MC_SWEEP_SHARED, ThreadKind::kMain);
    StartSweepSpace(heap_->shared_trusted_space());
  }
  {
    GCTracer::Scope sweep_scope(heap_->tracer(),
                                GCTracer::Scope::MC_SWEEP_TRUSTED_LO,
                                ThreadKind::kMain);
    SweepLargeSpace(heap_->trusted_lo_space());
  }
  if (v8_flags.minor_ms && heap_->new_space()) {
    GCTracer::Scope sweep_scope(heap_->tracer(), GCTracer::Scope::MC_SWEEP_NEW,
                                ThreadKind::kMain);
    StartSweepNewSpace();
  }

  sweeper_->StartMajorSweeping();
}

RootMarkingVisitor::RootMarkingVisitor(MarkCompactCollector* collector)
    : collector_(collector) {}

RootMarkingVisitor::~RootMarkingVisitor() = default;

void RootMarkingVisitor::VisitRunningCode(
    FullObjectSlot code_slot, FullObjectSlot istream_or_smi_zero_slot) {
  Tagged<Object> istream_or_smi_zero = *istream_or_smi_zero_slot;
  DCHECK(istream_or_smi_zero == Smi::zero() ||
         IsInstructionStream(istream_or_smi_zero));
  Tagged<Code> code = Cast<Code>(*code_slot);
  DCHECK_EQ(code->raw_instruction_stream(PtrComprCageBase{
                collector_->heap()->isolate()->code_cage_base()}),
            istream_or_smi_zero);

  // We must not remove deoptimization literals which may be needed in
  // order to successfully deoptimize.
  code->IterateDeoptimizationLiterals(this);

  if (istream_or_smi_zero != Smi::zero()) {
    VisitRootPointer(Root::kStackRoots, nullptr, istream_or_smi_zero_slot);
  }

  VisitRootPointer(Root::kStackRoots, nullptr, code_slot);
}

}  // namespace internal
}  // namespace v8
