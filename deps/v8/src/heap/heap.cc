// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap.h"

#include <atomic>
#include <cinttypes>
#include <iomanip>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "include/v8-locker.h"
#include "src/api/api-inl.h"
#include "src/base/bits.h"
#include "src/base/flags.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/once.h"
#include "src/base/platform/memory.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/base/utils/random-number-generator.h"
#include "src/builtins/accessors.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/compilation-cache.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/compiler-dispatcher/optimizing-compile-dispatcher.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/embedder-state.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/execution/microtask-queue.h"
#include "src/execution/v8threads.h"
#include "src/execution/vm-state-inl.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles-inl.h"
#include "src/handles/traced-handles.h"
#include "src/heap/allocation-observer.h"
#include "src/heap/array-buffer-sweeper.h"
#include "src/heap/base/stack.h"
#include "src/heap/base/worklist.h"
#include "src/heap/code-range.h"
#include "src/heap/code-stats.h"
#include "src/heap/collection-barrier.h"
#include "src/heap/combined-heap.h"
#include "src/heap/concurrent-marking.h"
#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/evacuation-verifier-inl.h"
#include "src/heap/finalization-registry-cleanup-task.h"
#include "src/heap/gc-callbacks.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-allocator.h"
#include "src/heap/heap-controller.h"
#include "src/heap/heap-layout-tracer.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/incremental-marking-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/large-spaces.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-barrier-inl.h"
#include "src/heap/marking-barrier.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/marking-state.h"
#include "src/heap/memory-balancer.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/memory-measurement.h"
#include "src/heap/memory-reducer.h"
#include "src/heap/minor-gc-job.h"
#include "src/heap/minor-mark-sweep.h"
#include "src/heap/mutable-page-metadata-inl.h"
#include "src/heap/new-spaces.h"
#include "src/heap/object-lock.h"
#include "src/heap/object-stats.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/objects-visiting.h"
#include "src/heap/paged-spaces-inl.h"
#include "src/heap/parked-scope.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/remembered-set.h"
#include "src/heap/safepoint.h"
#include "src/heap/scavenger-inl.h"
#include "src/heap/stress-scavenge-observer.h"
#include "src/heap/sweeper.h"
#include "src/heap/trusted-range.h"
#include "src/heap/zapping.h"
#include "src/init/bootstrapper.h"
#include "src/init/v8.h"
#include "src/interpreter/interpreter.h"
#include "src/logging/log.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/numbers/conversions.h"
#include "src/objects/data-handler.h"
#include "src/objects/free-space-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/hash-table.h"
#include "src/objects/instance-type.h"
#include "src/objects/maybe-object.h"
#include "src/objects/objects.h"
#include "src/objects/slots-atomic-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/visitors.h"
#include "src/profiler/heap-profiler.h"
#include "src/regexp/regexp.h"
#include "src/snapshot/embedded/embedded-data.h"
#include "src/snapshot/serializer-deserializer.h"
#include "src/snapshot/snapshot.h"
#include "src/strings/string-stream.h"
#include "src/strings/unicode-inl.h"
#include "src/tasks/cancelable-task.h"
#include "src/tracing/trace-event.h"
#include "src/utils/utils-inl.h"
#include "src/utils/utils.h"

#ifdef V8_ENABLE_CONSERVATIVE_STACK_SCANNING
#include "src/heap/conservative-stack-visitor.h"
#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-engine.h"
#endif  // V8_ENABLE_WEBASSEMBLY

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_THIRD_PARTY_HEAP
Isolate* Heap::GetIsolateFromWritableObject(Tagged<HeapObject> object) {
  return reinterpret_cast<Isolate*>(
      third_party_heap::Heap::GetIsolate(object.address()));
}
#endif

// These are outside the Heap class so they can be forward-declared
// in heap-write-barrier-inl.h.
bool Heap_PageFlagsAreConsistent(Tagged<HeapObject> object) {
  return Heap::PageFlagsAreConsistent(object);
}

void Heap_CombinedGenerationalAndSharedBarrierSlow(Tagged<HeapObject> object,
                                                   Address slot,
                                                   Tagged<HeapObject> value) {
  Heap::CombinedGenerationalAndSharedBarrierSlow(object, slot, value);
}

void Heap_CombinedGenerationalAndSharedEphemeronBarrierSlow(
    Tagged<EphemeronHashTable> table, Address slot, Tagged<HeapObject> value) {
  Heap::CombinedGenerationalAndSharedEphemeronBarrierSlow(table, slot, value);
}

void Heap_GenerationalBarrierForCodeSlow(Tagged<InstructionStream> host,
                                         RelocInfo* rinfo,
                                         Tagged<HeapObject> object) {
  Heap::GenerationalBarrierForCodeSlow(host, rinfo, object);
}

void Heap::SetConstructStubCreateDeoptPCOffset(int pc_offset) {
  DCHECK_EQ(Smi::zero(), construct_stub_create_deopt_pc_offset());
  set_construct_stub_create_deopt_pc_offset(Smi::FromInt(pc_offset));
}

void Heap::SetConstructStubInvokeDeoptPCOffset(int pc_offset) {
  DCHECK_EQ(Smi::zero(), construct_stub_invoke_deopt_pc_offset());
  set_construct_stub_invoke_deopt_pc_offset(Smi::FromInt(pc_offset));
}

void Heap::SetInterpreterEntryReturnPCOffset(int pc_offset) {
  DCHECK_EQ(Smi::zero(), interpreter_entry_return_pc_offset());
  set_interpreter_entry_return_pc_offset(Smi::FromInt(pc_offset));
}

void Heap::SetSerializedObjects(Tagged<HeapObject> objects) {
  DCHECK(isolate()->serializer_enabled());
  set_serialized_objects(objects);
}

void Heap::SetSerializedGlobalProxySizes(Tagged<FixedArray> sizes) {
  DCHECK(isolate()->serializer_enabled());
  set_serialized_global_proxy_sizes(sizes);
}

void Heap::SetBasicBlockProfilingData(DirectHandle<ArrayList> list) {
  set_basic_block_profiling_data(*list);
}

class ScheduleMinorGCTaskObserver final : public AllocationObserver {
 public:
  explicit ScheduleMinorGCTaskObserver(Heap* heap)
      : AllocationObserver(kNotUsingFixedStepSize), heap_(heap) {
    // Register GC callback for all atomic pause types.
    heap_->main_thread_local_heap()->AddGCEpilogueCallback(
        &GCEpilogueCallback, this, GCCallbacksInSafepoint::GCType::kLocal);
    AddToNewSpace();
  }
  ~ScheduleMinorGCTaskObserver() final {
    RemoveFromNewSpace();
    heap_->main_thread_local_heap()->RemoveGCEpilogueCallback(
        &GCEpilogueCallback, this);
  }

  intptr_t GetNextStepSize() final {
    size_t new_space_threshold =
        MinorGCJob::YoungGenerationTaskTriggerSize(heap_);
    size_t new_space_size = v8_flags.sticky_mark_bits
                                ? heap_->sticky_space()->young_objects_size()
                                : heap_->new_space()->Size();
    if (new_space_size < new_space_threshold) {
      return new_space_threshold - new_space_size;
    }
    // Force a step on next allocation.
    return 1;
  }

  void Step(int, Address, size_t) final {
    heap_->ScheduleMinorGCTaskIfNeeded();
    // Remove this observer. It will be re-added after a GC.
    DCHECK(was_added_to_space_);
    heap_->allocator()->new_space_allocator()->RemoveAllocationObserver(this);
    was_added_to_space_ = false;
  }

 protected:
  static void GCEpilogueCallback(void* observer) {
    reinterpret_cast<ScheduleMinorGCTaskObserver*>(observer)
        ->RemoveFromNewSpace();
    reinterpret_cast<ScheduleMinorGCTaskObserver*>(observer)->AddToNewSpace();
  }

  void AddToNewSpace() {
    DCHECK(!was_added_to_space_);
    DCHECK_IMPLIES(v8_flags.minor_ms,
                   !heap_->allocator()->new_space_allocator()->IsLabValid());
    heap_->allocator()->new_space_allocator()->AddAllocationObserver(this);
    was_added_to_space_ = true;
  }

  void RemoveFromNewSpace() {
    if (!was_added_to_space_) return;
    heap_->allocator()->new_space_allocator()->RemoveAllocationObserver(this);
    was_added_to_space_ = false;
  }

  Heap* heap_;
  bool was_added_to_space_ = false;
};

Heap::Heap()
    : isolate_(isolate()),
      memory_pressure_level_(MemoryPressureLevel::kNone),
      safepoint_(std::make_unique<IsolateSafepoint>(this)),
      external_string_table_(this),
      allocation_type_for_in_place_internalizable_strings_(
          isolate()->OwnsStringTables() ? AllocationType::kOld
                                        : AllocationType::kSharedOld),
      marking_state_(isolate_),
      non_atomic_marking_state_(isolate_),
      pretenuring_handler_(this) {
  // Ensure old_generation_size_ is a multiple of kPageSize.
  DCHECK_EQ(0, max_old_generation_size() & (PageMetadata::kPageSize - 1));

  max_regular_code_object_size_ = MemoryChunkLayout::MaxRegularCodeObjectSize();

  set_native_contexts_list(Smi::zero());

  // Put a dummy entry in the remembered pages so we can find the list the
  // minidump even if there are no real unmapped pages.
  RememberUnmappedPage(kNullAddress, false);
}

Heap::~Heap() = default;

size_t Heap::MaxReserved() const {
  const size_t kMaxNewLargeObjectSpaceSize = max_semi_space_size_;
  return static_cast<size_t>(
      (v8_flags.minor_ms ? 1 : 2) * max_semi_space_size_ +
      kMaxNewLargeObjectSpaceSize + max_old_generation_size());
}

size_t Heap::YoungGenerationSizeFromOldGenerationSize(size_t old_generation) {
  // Compute the semi space size and cap it.
  bool is_low_memory = old_generation <= kOldGenerationLowMemory;
  size_t semi_space;
  if (v8_flags.minor_ms && !is_low_memory) {
    semi_space = DefaultMaxSemiSpaceSize();
  } else {
    size_t ratio = is_low_memory ? OldGenerationToSemiSpaceRatioLowMemory()
                                 : OldGenerationToSemiSpaceRatio();
    semi_space = old_generation / ratio;
    semi_space = std::min({semi_space, DefaultMaxSemiSpaceSize()});
    semi_space = std::max({semi_space, DefaultMinSemiSpaceSize()});
    semi_space = RoundUp(semi_space, PageMetadata::kPageSize);
  }
  return YoungGenerationSizeFromSemiSpaceSize(semi_space);
}

size_t Heap::HeapSizeFromPhysicalMemory(uint64_t physical_memory) {
  // Compute the old generation size and cap it.
  uint64_t old_generation = physical_memory /
                            kPhysicalMemoryToOldGenerationRatio *
                            kHeapLimitMultiplier;
  old_generation =
      std::min(old_generation,
               static_cast<uint64_t>(MaxOldGenerationSize(physical_memory)));
  old_generation =
      std::max({old_generation, static_cast<uint64_t>(V8HeapTrait::kMinSize)});
  old_generation = RoundUp(old_generation, PageMetadata::kPageSize);

  size_t young_generation = YoungGenerationSizeFromOldGenerationSize(
      static_cast<size_t>(old_generation));
  return static_cast<size_t>(old_generation) + young_generation;
}

void Heap::GenerationSizesFromHeapSize(size_t heap_size,
                                       size_t* young_generation_size,
                                       size_t* old_generation_size) {
  // Initialize values for the case when the given heap size is too small.
  *young_generation_size = 0;
  *old_generation_size = 0;
  // Binary search for the largest old generation size that fits to the given
  // heap limit considering the correspondingly sized young generation.
  size_t lower = 0, upper = heap_size;
  while (lower + 1 < upper) {
    size_t old_generation = lower + (upper - lower) / 2;
    size_t young_generation =
        YoungGenerationSizeFromOldGenerationSize(old_generation);
    if (old_generation + young_generation <= heap_size) {
      // This size configuration fits into the given heap limit.
      *young_generation_size = young_generation;
      *old_generation_size = old_generation;
      lower = old_generation;
    } else {
      upper = old_generation;
    }
  }
}

size_t Heap::MinYoungGenerationSize() {
  return YoungGenerationSizeFromSemiSpaceSize(DefaultMinSemiSpaceSize());
}

size_t Heap::MinOldGenerationSize() {
  size_t paged_space_count =
      LAST_GROWABLE_PAGED_SPACE - FIRST_GROWABLE_PAGED_SPACE + 1;
  return paged_space_count * PageMetadata::kPageSize;
}

size_t Heap::AllocatorLimitOnMaxOldGenerationSize() {
#ifdef V8_COMPRESS_POINTERS
  // Isolate and the young generation are also allocated on the heap.
  return kPtrComprCageReservationSize -
         YoungGenerationSizeFromSemiSpaceSize(DefaultMaxSemiSpaceSize()) -
         RoundUp(sizeof(Isolate), size_t{1} << kPageSizeBits);
#else
  return std::numeric_limits<size_t>::max();
#endif
}

size_t Heap::MaxOldGenerationSize(uint64_t physical_memory) {
  size_t max_size = V8HeapTrait::kMaxSize;
  // Increase the heap size from 2GB to 4GB for 64-bit systems with physical
  // memory at least 16GB. The theshold is set to 15GB to accomodate for some
  // memory being reserved by the hardware.
  constexpr bool x64_bit = Heap::kHeapLimitMultiplier >= 2;
  if (v8_flags.huge_max_old_generation_size && x64_bit &&
      (physical_memory / GB) >= 15) {
    DCHECK_EQ(max_size / GB, 2u);
    max_size *= 2;
  }
  return std::min(max_size, AllocatorLimitOnMaxOldGenerationSize());
}

namespace {
int NumberOfSemiSpaces() { return v8_flags.minor_ms ? 1 : 2; }
}  // namespace

size_t Heap::YoungGenerationSizeFromSemiSpaceSize(size_t semi_space_size) {
  return semi_space_size *
         (NumberOfSemiSpaces() + kNewLargeObjectSpaceToSemiSpaceRatio);
}

size_t Heap::SemiSpaceSizeFromYoungGenerationSize(
    size_t young_generation_size) {
  return young_generation_size /
         (NumberOfSemiSpaces() + kNewLargeObjectSpaceToSemiSpaceRatio);
}

size_t Heap::Capacity() {
  if (!HasBeenSetUp()) return 0;

  if (v8_flags.enable_third_party_heap) return tp_heap_->Capacity();

  return NewSpaceCapacity() + OldGenerationCapacity();
}

size_t Heap::OldGenerationCapacity() const {
  if (!HasBeenSetUp()) return 0;
  PagedSpaceIterator spaces(this);
  size_t total = 0;
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    total += space->Capacity();
  }
  if (shared_lo_space_) {
    total += shared_lo_space_->SizeOfObjects();
  }
  return total + lo_space_->SizeOfObjects() + code_lo_space_->SizeOfObjects() +
         trusted_lo_space_->SizeOfObjects();
}

size_t Heap::CommittedOldGenerationMemory() {
  if (!HasBeenSetUp()) return 0;

  PagedSpaceIterator spaces(this);
  size_t total = 0;
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    total += space->CommittedMemory();
  }
  if (shared_lo_space_) {
    total += shared_lo_space_->Size();
  }
  return total + lo_space_->Size() + code_lo_space_->Size() +
         trusted_lo_space_->Size();
}

size_t Heap::CommittedMemoryOfPool() {
  if (!HasBeenSetUp()) return 0;

  return memory_allocator()->pool()->CommittedBufferedMemory();
}

size_t Heap::CommittedMemory() {
  if (!HasBeenSetUp()) return 0;

  size_t new_space_committed = new_space_ ? new_space_->CommittedMemory() : 0;
  size_t new_lo_space_committed = new_lo_space_ ? new_lo_space_->Size() : 0;

  return new_space_committed + new_lo_space_committed +
         CommittedOldGenerationMemory();
}

size_t Heap::CommittedPhysicalMemory() {
  if (!HasBeenSetUp()) return 0;

  size_t total = 0;
  for (SpaceIterator it(this); it.HasNext();) {
    total += it.Next()->CommittedPhysicalMemory();
  }

  return total;
}

size_t Heap::CommittedMemoryExecutable() {
  if (!HasBeenSetUp()) return 0;

  return static_cast<size_t>(memory_allocator()->SizeExecutable());
}

void Heap::UpdateMaximumCommitted() {
  if (!HasBeenSetUp()) return;

  const size_t current_committed_memory = CommittedMemory();
  if (current_committed_memory > maximum_committed_) {
    maximum_committed_ = current_committed_memory;
  }
}

size_t Heap::Available() {
  if (!HasBeenSetUp()) return 0;

  size_t total = 0;

  for (SpaceIterator it(this); it.HasNext();) {
    total += it.Next()->Available();
  }

  total += memory_allocator()->Available();
  return total;
}

bool Heap::CanExpandOldGeneration(size_t size) const {
  if (force_oom_ || force_gc_on_next_allocation_) return false;
  if (OldGenerationCapacity() + size > max_old_generation_size()) return false;
  // Stay below `MaxReserved()` such that it is more likely that committing the
  // second semi space at the beginning of a GC succeeds.
  return memory_allocator()->Size() + size <= MaxReserved();
}

bool Heap::IsOldGenerationExpansionAllowed(
    size_t size, const base::MutexGuard& expansion_mutex_guard) const {
  return OldGenerationCapacity() + size <= max_old_generation_size();
}

bool Heap::CanPromoteYoungAndExpandOldGeneration(size_t size) const {
  size_t new_space_capacity = NewSpaceTargetCapacity();
  size_t new_lo_space_capacity = new_lo_space_ ? new_lo_space_->Size() : 0;

  // Over-estimate the new space size using capacity to allow some slack.
  return CanExpandOldGeneration(size + new_space_capacity +
                                new_lo_space_capacity);
}

bool Heap::HasBeenSetUp() const {
  // We will always have an old space when the heap is set up.
  return old_space_ != nullptr;
}

bool Heap::ShouldUseBackgroundThreads() const {
  return !v8_flags.single_threaded_gc_in_background ||
         !isolate()->EfficiencyModeEnabled();
}

bool Heap::ShouldUseIncrementalMarking() const {
  if (v8_flags.single_threaded_gc_in_background &&
      isolate()->EfficiencyModeEnabled()) {
    return v8_flags.incremental_marking_for_gc_in_background;
  } else {
    return true;
  }
}

bool Heap::ShouldOptimizeForBattery() const {
  return v8_flags.optimize_gc_for_battery ||
         isolate()->BatterySaverModeEnabled();
}

GarbageCollector Heap::SelectGarbageCollector(AllocationSpace space,
                                              GarbageCollectionReason gc_reason,
                                              const char** reason) const {
  if (gc_reason == GarbageCollectionReason::kFinalizeConcurrentMinorMS) {
    DCHECK_NE(static_cast<bool>(new_space()),
              v8_flags.sticky_mark_bits.value());
    DCHECK(!ShouldReduceMemory());
    *reason = "Concurrent MinorMS needs finalization";
    return GarbageCollector::MINOR_MARK_SWEEPER;
  }

  // Is global GC requested?
  if (space != NEW_SPACE && space != NEW_LO_SPACE) {
    isolate_->counters()->gc_compactor_caused_by_request()->Increment();
    *reason = "GC in old space requested";
    return GarbageCollector::MARK_COMPACTOR;
  }

  if (v8_flags.gc_global || ShouldStressCompaction() || !use_new_space()) {
    *reason = "GC in old space forced by flags";
    return GarbageCollector::MARK_COMPACTOR;
  }

  if (v8_flags.separate_gc_phases && incremental_marking()->IsMajorMarking()) {
    // TODO(v8:12503): Remove next condition (allocation limit overshot) when
    // separate_gc_phases flag is enabled and removed.
    *reason = "Incremental marking forced finalization";
    return GarbageCollector::MARK_COMPACTOR;
  }

  if (incremental_marking()->IsMajorMarking() &&
      incremental_marking()->IsMajorMarkingComplete() &&
      AllocationLimitOvershotByLargeMargin()) {
    DCHECK(!v8_flags.minor_ms);
    *reason = "Incremental marking needs finalization";
    return GarbageCollector::MARK_COMPACTOR;
  }

  if (!CanPromoteYoungAndExpandOldGeneration(0)) {
    isolate_->counters()
        ->gc_compactor_caused_by_oldspace_exhaustion()
        ->Increment();
    *reason = "scavenge might not succeed";
    return GarbageCollector::MARK_COMPACTOR;
  }

  DCHECK(!v8_flags.single_generation);
  DCHECK(!v8_flags.gc_global);
  // Default
  *reason = nullptr;
  return YoungGenerationCollector();
}

void Heap::SetGCState(HeapState state) {
  gc_state_.store(state, std::memory_order_relaxed);
}

bool Heap::IsGCWithMainThreadStack() const {
  return embedder_stack_state_ == StackState::kMayContainHeapPointers;
}

bool Heap::IsGCWithStack() const {
  return IsGCWithMainThreadStack() || stack().HasBackgroundStacks();
}

bool Heap::CanShortcutStringsDuringGC(GarbageCollector collector) const {
  if (!v8_flags.shortcut_strings_with_stack && IsGCWithStack()) return false;

  switch (collector) {
    case GarbageCollector::MINOR_MARK_SWEEPER:
      if (!v8_flags.minor_ms_shortcut_strings) return false;

      DCHECK(!incremental_marking()->IsMajorMarking());

      // Minor MS cannot short cut strings during concurrent marking.
      if (incremental_marking()->IsMinorMarking()) return false;

      // Minor MS uses static roots to check for strings to shortcut.
      if (!V8_STATIC_ROOTS_BOOL) return false;

      break;
    case GarbageCollector::SCAVENGER:
      // Scavenger cannot short cut strings during incremental marking.
      if (incremental_marking()->IsMajorMarking()) return false;

      if (isolate()->has_shared_space() &&
          !isolate()->is_shared_space_isolate() &&
          isolate()
              ->shared_space_isolate()
              ->heap()
              ->incremental_marking()
              ->IsMarking()) {
        DCHECK(isolate()
                   ->shared_space_isolate()
                   ->heap()
                   ->incremental_marking()
                   ->IsMajorMarking());
        return false;
      }
      break;
    default:
      UNREACHABLE();
  }

  return true;
}

void Heap::PrintShortHeapStatistics() {
  if (!v8_flags.trace_gc_verbose) return;
  PrintIsolate(isolate_,
               "Memory allocator,       used: %6zu KB,"
               " available: %6zu KB\n",
               memory_allocator()->Size() / KB,
               memory_allocator()->Available() / KB);
  PrintIsolate(isolate_,
               "Read-only space,        used: %6zu KB"
               ", available: %6zu KB"
               ", committed: %6zu KB\n",
               read_only_space_->Size() / KB, size_t{0},
               read_only_space_->CommittedMemory() / KB);
  PrintIsolate(isolate_,
               "New space,              used: %6zu KB"
               ", available: %6zu KB%s"
               ", committed: %6zu KB\n",
               NewSpaceSize() / KB, new_space_->Available() / KB,
               (v8_flags.minor_ms && minor_sweeping_in_progress()) ? "*" : "",
               new_space_->CommittedMemory() / KB);
  PrintIsolate(isolate_,
               "New large object space, used: %6zu KB"
               ", available: %6zu KB"
               ", committed: %6zu KB\n",
               new_lo_space_->SizeOfObjects() / KB,
               new_lo_space_->Available() / KB,
               new_lo_space_->CommittedMemory() / KB);
  PrintIsolate(isolate_,
               "Old space,              used: %6zu KB"
               ", available: %6zu KB%s"
               ", committed: %6zu KB\n",
               old_space_->SizeOfObjects() / KB, old_space_->Available() / KB,
               major_sweeping_in_progress() ? "*" : "",
               old_space_->CommittedMemory() / KB);
  PrintIsolate(isolate_,
               "Code space,             used: %6zu KB"
               ", available: %6zu KB%s"
               ", committed: %6zu KB\n",
               code_space_->SizeOfObjects() / KB, code_space_->Available() / KB,
               major_sweeping_in_progress() ? "*" : "",
               code_space_->CommittedMemory() / KB);
  PrintIsolate(isolate_,
               "Large object space,     used: %6zu KB"
               ", available: %6zu KB"
               ", committed: %6zu KB\n",
               lo_space_->SizeOfObjects() / KB, lo_space_->Available() / KB,
               lo_space_->CommittedMemory() / KB);
  PrintIsolate(isolate_,
               "Code large object space,     used: %6zu KB"
               ", available: %6zu KB"
               ", committed: %6zu KB\n",
               code_lo_space_->SizeOfObjects() / KB,
               code_lo_space_->Available() / KB,
               code_lo_space_->CommittedMemory() / KB);
  PrintIsolate(isolate_,
               "Trusted space,              used: %6zu KB"
               ", available: %6zu KB%s"
               ", committed: %6zu KB\n",
               trusted_space_->SizeOfObjects() / KB,
               trusted_space_->Available() / KB,
               major_sweeping_in_progress() ? "*" : "",
               trusted_space_->CommittedMemory() / KB);
  PrintIsolate(isolate_,
               "Trusted large object space,     used: %6zu KB"
               ", available: %6zu KB"
               ", committed: %6zu KB\n",
               trusted_lo_space_->SizeOfObjects() / KB,
               trusted_lo_space_->Available() / KB,
               trusted_lo_space_->CommittedMemory() / KB);
  ReadOnlySpace* const ro_space = read_only_space_;
  PrintIsolate(isolate_,
               "All spaces,             used: %6zu KB"
               ", available: %6zu KB%s"
               ", committed: %6zu KB\n",
               (this->SizeOfObjects() + ro_space->Size()) / KB,
               (this->Available()) / KB, sweeping_in_progress() ? "*" : "",
               (this->CommittedMemory() + ro_space->CommittedMemory()) / KB);
  PrintIsolate(isolate_, "Pool buffering %zu chunks of committed: %6zu KB\n",
               memory_allocator()->pool()->NumberOfCommittedChunks(),
               CommittedMemoryOfPool() / KB);
  PrintIsolate(isolate_, "External memory reported: %6" PRId64 " KB\n",
               external_memory_.total() / KB);
  PrintIsolate(isolate_, "Backing store memory: %6" PRIu64 " KB\n",
               backing_store_bytes() / KB);
  PrintIsolate(isolate_, "External memory global %zu KB\n",
               external_memory_callback_() / KB);
  PrintIsolate(isolate_, "Total time spent in GC  : %.1f ms\n",
               total_gc_time_ms_.InMillisecondsF());
  if (sweeping_in_progress()) {
    PrintIsolate(isolate_,
                 "(*) Sweeping is still in progress, making available sizes "
                 "inaccurate.\n");
  }
}

void Heap::PrintFreeListsStats() {
  DCHECK(v8_flags.trace_gc_freelists);

  if (v8_flags.trace_gc_freelists_verbose) {
    PrintIsolate(isolate_,
                 "Freelists statistics per Page: "
                 "[category: length || total free bytes]\n");
  }

  std::vector<int> categories_lengths(
      old_space()->free_list()->number_of_categories(), 0);
  std::vector<size_t> categories_sums(
      old_space()->free_list()->number_of_categories(), 0);
  unsigned int pageCnt = 0;

  // This loops computes freelists lengths and sum.
  // If v8_flags.trace_gc_freelists_verbose is enabled, it also prints
  // the stats of each FreeListCategory of each Page.
  for (PageMetadata* page : *old_space()) {
    std::ostringstream out_str;

    if (v8_flags.trace_gc_freelists_verbose) {
      out_str << "Page " << std::setw(4) << pageCnt;
    }

    for (int cat = kFirstCategory;
         cat <= old_space()->free_list()->last_category(); cat++) {
      FreeListCategory* free_list =
          page->free_list_category(static_cast<FreeListCategoryType>(cat));
      int length = free_list->FreeListLength();
      size_t sum = free_list->SumFreeList();

      if (v8_flags.trace_gc_freelists_verbose) {
        out_str << "[" << cat << ": " << std::setw(4) << length << " || "
                << std::setw(6) << sum << " ]"
                << (cat == old_space()->free_list()->last_category() ? "\n"
                                                                     : ", ");
      }
      categories_lengths[cat] += length;
      categories_sums[cat] += sum;
    }

    if (v8_flags.trace_gc_freelists_verbose) {
      PrintIsolate(isolate_, "%s", out_str.str().c_str());
    }

    pageCnt++;
  }

  // Print statistics about old_space (pages, free/wasted/used memory...).
  PrintIsolate(
      isolate_,
      "%d pages. Free space: %.1f MB (waste: %.2f). "
      "Usage: %.1f/%.1f (MB) -> %.2f%%.\n",
      pageCnt, static_cast<double>(old_space_->Available()) / MB,
      static_cast<double>(old_space_->Waste()) / MB,
      static_cast<double>(old_space_->Size()) / MB,
      static_cast<double>(old_space_->Capacity()) / MB,
      static_cast<double>(old_space_->Size()) / old_space_->Capacity() * 100);

  // Print global statistics of each FreeListCategory (length & sum).
  PrintIsolate(isolate_,
               "FreeLists global statistics: "
               "[category: length || total free KB]\n");
  std::ostringstream out_str;
  for (int cat = kFirstCategory;
       cat <= old_space()->free_list()->last_category(); cat++) {
    out_str << "[" << cat << ": " << categories_lengths[cat] << " || "
            << std::fixed << std::setprecision(2)
            << static_cast<double>(categories_sums[cat]) / KB << " KB]"
            << (cat == old_space()->free_list()->last_category() ? "\n" : ", ");
  }
  PrintIsolate(isolate_, "%s", out_str.str().c_str());
}

void Heap::DumpJSONHeapStatistics(std::stringstream& stream) {
  HeapStatistics stats;
  reinterpret_cast<v8::Isolate*>(isolate())->GetHeapStatistics(&stats);

// clang-format off
#define DICT(s) "{" << s << "}"
#define LIST(s) "[" << s << "]"
#define QUOTE(s) "\"" << s << "\""
#define MEMBER(s) QUOTE(s) << ":"

  auto SpaceStatistics = [this](int space_index) {
    HeapSpaceStatistics space_stats;
    reinterpret_cast<v8::Isolate*>(isolate())->GetHeapSpaceStatistics(
        &space_stats, space_index);
    std::stringstream stream;
    stream << DICT(
      MEMBER("name")
        << QUOTE(ToString(
              static_cast<AllocationSpace>(space_index)))
        << ","
      MEMBER("size") << space_stats.space_size() << ","
      MEMBER("used_size") << space_stats.space_used_size() << ","
      MEMBER("available_size") << space_stats.space_available_size() << ","
      MEMBER("physical_size") << space_stats.physical_space_size());
    return stream.str();
  };

  stream << DICT(
    MEMBER("isolate") << QUOTE(reinterpret_cast<void*>(isolate())) << ","
    MEMBER("id") << gc_count() << ","
    MEMBER("time_ms") << isolate()->time_millis_since_init() << ","
    MEMBER("total_heap_size") << stats.total_heap_size() << ","
    MEMBER("total_heap_size_executable")
      << stats.total_heap_size_executable() << ","
    MEMBER("total_physical_size") << stats.total_physical_size() << ","
    MEMBER("total_available_size") << stats.total_available_size() << ","
    MEMBER("used_heap_size") << stats.used_heap_size() << ","
    MEMBER("heap_size_limit") << stats.heap_size_limit() << ","
    MEMBER("malloced_memory") << stats.malloced_memory() << ","
    MEMBER("external_memory") << stats.external_memory() << ","
    MEMBER("peak_malloced_memory") << stats.peak_malloced_memory() << ","
    MEMBER("spaces") << LIST(
      SpaceStatistics(RO_SPACE)      << "," <<
      SpaceStatistics(NEW_SPACE)     << "," <<
      SpaceStatistics(OLD_SPACE)     << "," <<
      SpaceStatistics(CODE_SPACE)    << "," <<
      SpaceStatistics(LO_SPACE)      << "," <<
      SpaceStatistics(CODE_LO_SPACE) << "," <<
      SpaceStatistics(NEW_LO_SPACE)  << "," <<
      SpaceStatistics(TRUSTED_SPACE) << "," <<
      SpaceStatistics(TRUSTED_LO_SPACE)));

#undef DICT
#undef LIST
#undef QUOTE
#undef MEMBER
  // clang-format on
}

void Heap::ReportStatisticsAfterGC() {
  if (deferred_counters_.empty()) return;
  // Move the contents into a new SmallVector first, in case
  // {Isolate::CountUsage} puts the counters into {deferred_counters_} again.
  decltype(deferred_counters_) to_report = std::move(deferred_counters_);
  DCHECK(deferred_counters_.empty());
  isolate()->CountUsage(base::VectorOf(to_report));
}

class Heap::AllocationTrackerForDebugging final
    : public HeapObjectAllocationTracker {
 public:
  static bool IsNeeded() {
    return v8_flags.verify_predictable || v8_flags.fuzzer_gc_analysis ||
           (v8_flags.trace_allocation_stack_interval > 0);
  }

  explicit AllocationTrackerForDebugging(Heap* heap) : heap_(heap) {
    CHECK(IsNeeded());
    heap_->AddHeapObjectAllocationTracker(this);
  }

  ~AllocationTrackerForDebugging() final {
    heap_->RemoveHeapObjectAllocationTracker(this);
    if (v8_flags.verify_predictable || v8_flags.fuzzer_gc_analysis) {
      PrintAllocationsHash();
    }
  }

  void AllocationEvent(Address addr, int size) final {
    if (v8_flags.verify_predictable) {
      allocations_count_.fetch_add(1, std::memory_order_relaxed);
      // Advance synthetic time by making a time request.
      heap_->MonotonicallyIncreasingTimeInMs();

      UpdateAllocationsHash(HeapObject::FromAddress(addr));
      UpdateAllocationsHash(size);

      if (allocations_count_ % v8_flags.dump_allocations_digest_at_alloc == 0) {
        PrintAllocationsHash();
      }
    } else if (v8_flags.fuzzer_gc_analysis) {
      allocations_count_.fetch_add(1, std::memory_order_relaxed);
    } else if (v8_flags.trace_allocation_stack_interval > 0) {
      allocations_count_.fetch_add(1, std::memory_order_relaxed);
      if (allocations_count_ % v8_flags.trace_allocation_stack_interval == 0) {
        heap_->isolate()->PrintStack(stdout, Isolate::kPrintStackConcise);
      }
    }
  }

  void MoveEvent(Address source, Address target, int size) final {
    if (v8_flags.verify_predictable) {
      allocations_count_.fetch_add(1, std::memory_order_relaxed);
      // Advance synthetic time by making a time request.
      heap_->MonotonicallyIncreasingTimeInMs();

      UpdateAllocationsHash(HeapObject::FromAddress(source));
      UpdateAllocationsHash(HeapObject::FromAddress(target));
      UpdateAllocationsHash(size);

      if (allocations_count_ % v8_flags.dump_allocations_digest_at_alloc == 0) {
        PrintAllocationsHash();
      }
    } else if (v8_flags.fuzzer_gc_analysis) {
      allocations_count_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void UpdateObjectSizeEvent(Address, int) final {}

 private:
  void UpdateAllocationsHash(Tagged<HeapObject> object) {
    Address object_address = object.address();
    MemoryChunk* memory_chunk = MemoryChunk::FromAddress(object_address);
    AllocationSpace allocation_space =
        MutablePageMetadata::cast(memory_chunk->Metadata())->owner_identity();

    static_assert(kSpaceTagSize + kPageSizeBits <= 32);
    uint32_t value =
        static_cast<uint32_t>(memory_chunk->Offset(object_address)) |
        (static_cast<uint32_t>(allocation_space) << kPageSizeBits);

    UpdateAllocationsHash(value);
  }

  void UpdateAllocationsHash(uint32_t value) {
    const uint16_t c1 = static_cast<uint16_t>(value);
    const uint16_t c2 = static_cast<uint16_t>(value >> 16);
    raw_allocations_hash_ =
        StringHasher::AddCharacterCore(raw_allocations_hash_, c1);
    raw_allocations_hash_ =
        StringHasher::AddCharacterCore(raw_allocations_hash_, c2);
  }

  void PrintAllocationsHash() {
    uint32_t hash = StringHasher::GetHashCore(raw_allocations_hash_);
    PrintF("\n### Allocations = %zu, hash = 0x%08x\n",
           allocations_count_.load(std::memory_order_relaxed), hash);
  }

  Heap* const heap_;
  // Count of all allocations performed through C++ bottlenecks. This needs to
  // be atomic as objects are moved in parallel in the GC which counts as
  // allocations.
  std::atomic<size_t> allocations_count_{0};
  // Running hash over allocations performed.
  uint32_t raw_allocations_hash_ = 0;
};

void Heap::AddHeapObjectAllocationTracker(
    HeapObjectAllocationTracker* tracker) {
  if (allocation_trackers_.empty() && v8_flags.inline_new) {
    DisableInlineAllocation();
  }
  allocation_trackers_.push_back(tracker);
  if (allocation_trackers_.size() == 1) {
    isolate_->UpdateLogObjectRelocation();
  }
}

void Heap::RemoveHeapObjectAllocationTracker(
    HeapObjectAllocationTracker* tracker) {
  allocation_trackers_.erase(std::remove(allocation_trackers_.begin(),
                                         allocation_trackers_.end(), tracker),
                             allocation_trackers_.end());
  if (allocation_trackers_.empty()) {
    isolate_->UpdateLogObjectRelocation();
  }
  if (allocation_trackers_.empty() && v8_flags.inline_new) {
    EnableInlineAllocation();
  }
}

void UpdateRetainersMapAfterScavenge(
    UnorderedHeapObjectMap<Tagged<HeapObject>>* map) {
  // This is only used for Scavenger.
  DCHECK(!v8_flags.minor_ms);

  UnorderedHeapObjectMap<Tagged<HeapObject>> updated_map;

  for (auto pair : *map) {
    Tagged<HeapObject> object = pair.first;
    Tagged<HeapObject> retainer = pair.second;

    if (Heap::InFromPage(object)) {
      MapWord map_word = object->map_word(kRelaxedLoad);
      if (!map_word.IsForwardingAddress()) continue;
      object = map_word.ToForwardingAddress(object);
    }

    if (Heap::InFromPage(retainer)) {
      MapWord map_word = retainer->map_word(kRelaxedLoad);
      if (!map_word.IsForwardingAddress()) continue;
      retainer = map_word.ToForwardingAddress(retainer);
    }

    updated_map[object] = retainer;
  }

  *map = std::move(updated_map);
}

void Heap::IncrementDeferredCounts(
    base::Vector<const v8::Isolate::UseCounterFeature> features) {
  deferred_counters_.insert(deferred_counters_.end(), features.begin(),
                            features.end());
}

void Heap::GarbageCollectionPrologue(
    GarbageCollectionReason gc_reason,
    const v8::GCCallbackFlags gc_callback_flags) {
  TRACE_GC(tracer(), GCTracer::Scope::HEAP_PROLOGUE);

  is_current_gc_forced_ = gc_callback_flags & v8::kGCCallbackFlagForced ||
                          current_gc_flags_ & GCFlag::kForced ||
                          force_gc_on_next_allocation_;
  is_current_gc_for_heap_profiler_ =
      gc_reason == GarbageCollectionReason::kHeapProfiler;
  if (force_gc_on_next_allocation_) force_gc_on_next_allocation_ = false;

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  heap_allocator_->UpdateAllocationTimeout();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

  // There may be an allocation memento behind objects in new space. Upon
  // evacuation of a non-full new space (or if we are on the last page) there
  // may be uninitialized memory behind top. We fill the remainder of the page
  // with a filler.
  if (use_new_space()) {
    DCHECK_NOT_NULL(minor_gc_job());
    minor_gc_job()->CancelTaskIfScheduled();
  }

  // Reset GC statistics.
  promoted_objects_size_ = 0;
  previous_new_space_surviving_object_size_ = new_space_surviving_object_size_;
  new_space_surviving_object_size_ = 0;
  nodes_died_in_new_space_ = 0;
  nodes_copied_in_new_space_ = 0;
  nodes_promoted_ = 0;

  UpdateMaximumCommitted();

#ifdef DEBUG
  DCHECK(!AllowGarbageCollection::IsAllowed());
  DCHECK_EQ(gc_state(), NOT_IN_GC);

  if (v8_flags.gc_verbose) Print();
#endif  // DEBUG
}

void Heap::GarbageCollectionPrologueInSafepoint() {
  TRACE_GC(tracer(), GCTracer::Scope::HEAP_PROLOGUE_SAFEPOINT);
  gc_count_++;

  DCHECK_EQ(ResizeNewSpaceMode::kNone, resize_new_space_mode_);
  if (new_space_) {
    UpdateNewSpaceAllocationCounter();
    if (!v8_flags.minor_ms) {
      resize_new_space_mode_ = ShouldResizeNewSpace();
      // Pretenuring heuristics require that new space grows before pretenuring
      // feedback is processed.
      if (resize_new_space_mode_ == ResizeNewSpaceMode::kGrow) {
        ExpandNewSpaceSize();
      }
      SemiSpaceNewSpace::From(new_space_)->ResetParkedAllocationBuffers();
    }
  }
}

void Heap::UpdateNewSpaceAllocationCounter() {
  new_space_allocation_counter_ = NewSpaceAllocationCounter();
}

size_t Heap::NewSpaceAllocationCounter() {
  size_t counter = new_space_allocation_counter_;
  if (new_space_) {
    DCHECK(!allocator()->new_space_allocator()->IsLabValid());
    counter += new_space()->AllocatedSinceLastGC();
  }
  return counter;
}

size_t Heap::SizeOfObjects() {
  size_t total = 0;

  for (SpaceIterator it(this); it.HasNext();) {
    total += it.Next()->SizeOfObjects();
  }
  return total;
}

size_t Heap::TotalGlobalHandlesSize() {
  return isolate_->global_handles()->TotalSize() +
         isolate_->traced_handles()->total_size_bytes();
}

size_t Heap::UsedGlobalHandlesSize() {
  return isolate_->global_handles()->UsedSize() +
         isolate_->traced_handles()->used_size_bytes();
}

void Heap::AddAllocationObserversToAllSpaces(
    AllocationObserver* observer, AllocationObserver* new_space_observer) {
  DCHECK(observer && new_space_observer);
  FreeMainThreadLinearAllocationAreas();
  allocator()->AddAllocationObserver(observer, new_space_observer);
}

void Heap::RemoveAllocationObserversFromAllSpaces(
    AllocationObserver* observer, AllocationObserver* new_space_observer) {
  DCHECK(observer && new_space_observer);
  allocator()->RemoveAllocationObserver(observer, new_space_observer);
}

void Heap::PublishMainThreadPendingAllocations() {
  if (v8_flags.enable_third_party_heap) return;
  allocator()->PublishPendingAllocations();
}

void Heap::DeoptMarkedAllocationSites() {
  // TODO(hpayer): If iterating over the allocation sites list becomes a
  // performance issue, use a cache data structure in heap instead.

  ForeachAllocationSite(
      allocation_sites_list(), [this](Tagged<AllocationSite> site) {
        if (site->deopt_dependent_code()) {
          DependentCode::MarkCodeForDeoptimization(
              isolate_, site,
              DependentCode::kAllocationSiteTenuringChangedGroup);
          site->set_deopt_dependent_code(false);
        }
      });

  Deoptimizer::DeoptimizeMarkedCode(isolate_);
}

static GCType GetGCTypeFromGarbageCollector(GarbageCollector collector) {
  switch (collector) {
    case GarbageCollector::MARK_COMPACTOR:
      return kGCTypeMarkSweepCompact;
    case GarbageCollector::SCAVENGER:
      return kGCTypeScavenge;
    case GarbageCollector::MINOR_MARK_SWEEPER:
      return kGCTypeMinorMarkSweep;
    default:
      UNREACHABLE();
  }
}

void Heap::GarbageCollectionEpilogueInSafepoint(GarbageCollector collector) {
  if (collector == GarbageCollector::MARK_COMPACTOR) {
    memory_pressure_level_.store(MemoryPressureLevel::kNone,
                                 std::memory_order_relaxed);

    if (v8_flags.stress_marking > 0) {
      stress_marking_percentage_ = NextStressMarkingLimit();
    }
  }

  TRACE_GC(tracer(), GCTracer::Scope::HEAP_EPILOGUE_SAFEPOINT);

  {
    // Allows handle derefs for all threads/isolates from this thread.
    AllowHandleDereferenceAllThreads allow_all_handle_derefs;
    safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
      local_heap->InvokeGCEpilogueCallbacksInSafepoint(
          GCCallbacksInSafepoint::GCType::kLocal);
    });

    if (collector == GarbageCollector::MARK_COMPACTOR &&
        isolate()->is_shared_space_isolate()) {
      isolate()->global_safepoint()->IterateClientIsolates([](Isolate* client) {
        client->heap()->safepoint()->IterateLocalHeaps(
            [](LocalHeap* local_heap) {
              local_heap->InvokeGCEpilogueCallbacksInSafepoint(
                  GCCallbacksInSafepoint::GCType::kShared);
            });
      });
    }
  }

#define UPDATE_COUNTERS_FOR_SPACE(space)                \
  isolate_->counters()->space##_bytes_available()->Set( \
      static_cast<int>(space()->Available()));          \
  isolate_->counters()->space##_bytes_committed()->Set( \
      static_cast<int>(space()->CommittedMemory()));    \
  isolate_->counters()->space##_bytes_used()->Set(      \
      static_cast<int>(space()->SizeOfObjects()));
#define UPDATE_FRAGMENTATION_FOR_SPACE(space)                          \
  if (space()->CommittedMemory() > 0) {                                \
    isolate_->counters()->external_fragmentation_##space()->AddSample( \
        static_cast<int>(100 - (space()->SizeOfObjects() * 100.0) /    \
                                   space()->CommittedMemory()));       \
  }
#define UPDATE_COUNTERS_AND_FRAGMENTATION_FOR_SPACE(space) \
  UPDATE_COUNTERS_FOR_SPACE(space)                         \
  UPDATE_FRAGMENTATION_FOR_SPACE(space)

  if (new_space()) {
    UPDATE_COUNTERS_FOR_SPACE(new_space)
  }

  UPDATE_COUNTERS_AND_FRAGMENTATION_FOR_SPACE(old_space)
  UPDATE_COUNTERS_AND_FRAGMENTATION_FOR_SPACE(code_space)

  UPDATE_COUNTERS_AND_FRAGMENTATION_FOR_SPACE(lo_space)
#undef UPDATE_COUNTERS_FOR_SPACE
#undef UPDATE_FRAGMENTATION_FOR_SPACE
#undef UPDATE_COUNTERS_AND_FRAGMENTATION_FOR_SPACE

#ifdef DEBUG
  if (v8_flags.print_global_handles) isolate_->global_handles()->Print();
  if (v8_flags.print_handles) PrintHandles();
  if (v8_flags.check_handle_count) CheckHandleCount();
#endif

  if (new_space() && !v8_flags.minor_ms) {
    SemiSpaceNewSpace* semi_space_new_space =
        SemiSpaceNewSpace::From(new_space());
    if (heap::ShouldZapGarbage() || v8_flags.clear_free_memory) {
      semi_space_new_space->ZapUnusedMemory();
    }

    DCHECK(!allocator()->new_space_allocator()->IsLabValid());

    {
      TRACE_GC(tracer(), GCTracer::Scope::HEAP_EPILOGUE_REDUCE_NEW_SPACE);
      if (resize_new_space_mode_ == ResizeNewSpaceMode::kShrink) {
        ReduceNewSpaceSize();
      }
    }
    resize_new_space_mode_ = ResizeNewSpaceMode::kNone;

    semi_space_new_space->MakeAllPagesInFromSpaceIterable();

    // Discard pooled pages for scavenger if needed.
    if (ShouldReduceMemory()) {
      memory_allocator_->pool()->ReleasePooledChunks();
#if V8_ENABLE_WEBASSEMBLY
      isolate_->stack_pool().ReleaseFinishedStacks();
#endif
    }
  }

  // Remove CollectionRequested flag from main thread state, as the collection
  // was just performed.
  safepoint()->AssertActive();
  LocalHeap::ThreadState old_state =
      main_thread_local_heap()->state_.ClearCollectionRequested();

  CHECK(old_state.IsRunning());

  // Resume all threads waiting for the GC.
  collection_barrier_->ResumeThreadsAwaitingCollection();
}

void Heap::GarbageCollectionEpilogue(GarbageCollector collector) {
  TRACE_GC(tracer(), GCTracer::Scope::HEAP_EPILOGUE);
  AllowGarbageCollection for_the_rest_of_the_epilogue;

  UpdateMaximumCommitted();

  isolate_->counters()->alive_after_last_gc()->Set(
      static_cast<int>(SizeOfObjects()));

  if (CommittedMemory() > 0) {
    isolate_->counters()->external_fragmentation_total()->AddSample(
        static_cast<int>(100 - (SizeOfObjects() * 100.0) / CommittedMemory()));

    isolate_->counters()->heap_sample_total_committed()->AddSample(
        static_cast<int>(CommittedMemory() / KB));
    isolate_->counters()->heap_sample_total_used()->AddSample(
        static_cast<int>(SizeOfObjects() / KB));
    isolate_->counters()->heap_sample_code_space_committed()->AddSample(
        static_cast<int>(code_space()->CommittedMemory() / KB));

    isolate_->counters()->heap_sample_maximum_committed()->AddSample(
        static_cast<int>(MaximumCommittedMemory() / KB));
  }

#ifdef DEBUG
  ReportStatisticsAfterGC();
  if (v8_flags.code_stats) ReportCodeStatistics("After GC");
#endif  // DEBUG

  last_gc_time_ = MonotonicallyIncreasingTimeInMs();
}

GCCallbacksScope::GCCallbacksScope(Heap* heap) : heap_(heap) {
  heap_->gc_callbacks_depth_++;
}

GCCallbacksScope::~GCCallbacksScope() { heap_->gc_callbacks_depth_--; }

bool GCCallbacksScope::CheckReenter() const {
  return heap_->gc_callbacks_depth_ == 1;
}

void Heap::HandleGCRequest() {
  if (IsStressingScavenge() && stress_scavenge_observer_->HasRequestedGC()) {
    CollectGarbage(NEW_SPACE, GarbageCollectionReason::kTesting);
    stress_scavenge_observer_->RequestedGCDone();
  } else if (HighMemoryPressure()) {
    CheckMemoryPressure();
  } else if (CollectionRequested()) {
    CheckCollectionRequested();
  } else if (incremental_marking()->MajorCollectionRequested()) {
    CollectAllGarbage(current_gc_flags_,
                      GarbageCollectionReason::kFinalizeMarkingViaStackGuard,
                      current_gc_callback_flags_);
  } else if (minor_mark_sweep_collector()->gc_finalization_requsted()) {
    CollectGarbage(NEW_SPACE,
                   GarbageCollectionReason::kFinalizeConcurrentMinorMS);
  }
}

void Heap::ScheduleMinorGCTaskIfNeeded() {
  DCHECK_NOT_NULL(minor_gc_job_);
  minor_gc_job_->ScheduleTask();
}

namespace {
size_t MinorMSConcurrentMarkingTrigger(Heap* heap) {
  size_t young_capacity = 0;
  if (v8_flags.sticky_mark_bits) {
    // TODO(333906585): Adjust parameters.
    young_capacity = heap->sticky_space()->Capacity() -
                     heap->sticky_space()->old_objects_size();
  } else {
    young_capacity = heap->new_space()->TotalCapacity();
  }
  return young_capacity * v8_flags.minor_ms_concurrent_marking_trigger / 100;
}
}  // namespace

void Heap::StartMinorMSIncrementalMarkingIfNeeded() {
  if (incremental_marking()->IsMarking()) return;
  if (v8_flags.concurrent_minor_ms_marking && !IsTearingDown() &&
      incremental_marking()->CanAndShouldBeStarted() &&
      V8_LIKELY(!v8_flags.gc_global)) {
    size_t usable_capacity = 0;
    size_t new_space_size = 0;
    if (v8_flags.sticky_mark_bits) {
      // TODO(333906585): Adjust parameters.
      usable_capacity =
          sticky_space()->Capacity() - sticky_space()->old_objects_size();
      new_space_size = sticky_space()->young_objects_size();
    } else {
      usable_capacity = paged_new_space()->paged_space()->UsableCapacity();
      new_space_size = new_space()->Size();
    }
    if ((usable_capacity >=
         v8_flags.minor_ms_min_new_space_capacity_for_concurrent_marking_mb *
             MB) &&
        (new_space_size >= MinorMSConcurrentMarkingTrigger(this)) &&
        ShouldUseBackgroundThreads()) {
      StartIncrementalMarking(GCFlag::kNoFlags, GarbageCollectionReason::kTask,
                              kNoGCCallbackFlags,
                              GarbageCollector::MINOR_MARK_SWEEPER);
      // Schedule a task for finalizing the GC if needed.
      ScheduleMinorGCTaskIfNeeded();
    }
  }
}

void Heap::CollectAllGarbage(GCFlags gc_flags,
                             GarbageCollectionReason gc_reason,
                             const v8::GCCallbackFlags gc_callback_flags) {
  current_gc_flags_ = gc_flags;
  CollectGarbage(OLD_SPACE, gc_reason, gc_callback_flags);
  DCHECK_EQ(GCFlags(GCFlag::kNoFlags), current_gc_flags_);
}

namespace {

intptr_t CompareWords(int size, Tagged<HeapObject> a, Tagged<HeapObject> b) {
  int slots = size / kTaggedSize;
  DCHECK_EQ(a->Size(), size);
  DCHECK_EQ(b->Size(), size);
  Tagged_t* slot_a = reinterpret_cast<Tagged_t*>(a.address());
  Tagged_t* slot_b = reinterpret_cast<Tagged_t*>(b.address());
  for (int i = 0; i < slots; i++) {
    if (*slot_a != *slot_b) {
      return *slot_a - *slot_b;
    }
    slot_a++;
    slot_b++;
  }
  return 0;
}

void ReportDuplicates(int size, std::vector<Tagged<HeapObject>>* objects) {
  if (objects->empty()) return;

  sort(objects->begin(), objects->end(),
       [size](Tagged<HeapObject> a, Tagged<HeapObject> b) {
         intptr_t c = CompareWords(size, a, b);
         if (c != 0) return c < 0;
         return a < b;
       });

  std::vector<std::pair<int, Tagged<HeapObject>>> duplicates;
  Tagged<HeapObject> current = (*objects)[0];
  int count = 1;
  for (size_t i = 1; i < objects->size(); i++) {
    if (CompareWords(size, current, (*objects)[i]) == 0) {
      count++;
    } else {
      if (count > 1) {
        duplicates.push_back(std::make_pair(count - 1, current));
      }
      count = 1;
      current = (*objects)[i];
    }
  }
  if (count > 1) {
    duplicates.push_back(std::make_pair(count - 1, current));
  }

  int threshold = v8_flags.trace_duplicate_threshold_kb * KB;

  sort(duplicates.begin(), duplicates.end());
  for (auto it = duplicates.rbegin(); it != duplicates.rend(); ++it) {
    int duplicate_bytes = it->first * size;
    if (duplicate_bytes < threshold) break;
    PrintF("%d duplicates of size %d each (%dKB)\n", it->first, size,
           duplicate_bytes / KB);
    PrintF("Sample object: ");
    Print(it->second);
    PrintF("============================\n");
  }
}
}  // anonymous namespace

void Heap::CollectAllAvailableGarbage(GarbageCollectionReason gc_reason) {
  // Min and max number of attempts for GC. The method will continue with more
  // GCs until the root set is stable.
  static constexpr int kMaxNumberOfAttempts = 7;
  static constexpr int kMinNumberOfAttempts = 2;

  // Returns the number of roots. We assume stack layout is stable but global
  // roots could change between GCs due to finalizers and weak callbacks.
  const auto num_roots = [this]() {
    size_t js_roots = 0;
    js_roots += isolate()->global_handles()->handles_count();
    js_roots += isolate()->eternal_handles()->handles_count();
    size_t cpp_roots = 0;
    if (auto* cpp_heap = CppHeap::From(cpp_heap_)) {
      cpp_roots += cpp_heap->GetStrongPersistentRegion().NodesInUse();
      cpp_roots +=
          cpp_heap->GetStrongCrossThreadPersistentRegion().NodesInUse();
    }
    return js_roots + cpp_roots;
  };

  if (gc_reason == GarbageCollectionReason::kLastResort) {
    InvokeNearHeapLimitCallback();
  }
  RCS_SCOPE(isolate(), RuntimeCallCounterId::kGC_Custom_AllAvailableGarbage);

  // The optimizing compiler may be unnecessarily holding on to memory.
  isolate()->AbortConcurrentOptimization(BlockingBehavior::kDontBlock);
  isolate()->ClearSerializerData();
  isolate()->compilation_cache()->Clear();

  const GCFlags gc_flags =
      GCFlag::kReduceMemoryFootprint |
      (gc_reason == GarbageCollectionReason::kLowMemoryNotification
           ? GCFlag::kForced
           : GCFlag::kNoFlags);
  for (int attempt = 0; attempt < kMaxNumberOfAttempts; attempt++) {
    const size_t roots_before = num_roots();
    current_gc_flags_ = gc_flags;
    CollectGarbage(OLD_SPACE, gc_reason, kNoGCCallbackFlags);
    DCHECK_EQ(GCFlags(GCFlag::kNoFlags), current_gc_flags_);
    if ((roots_before == num_roots()) &&
        ((attempt + 1) >= kMinNumberOfAttempts)) {
      break;
    }
  }

  EagerlyFreeExternalMemoryAndWasmCode();

  if (v8_flags.trace_duplicate_threshold_kb) {
    std::map<int, std::vector<Tagged<HeapObject>>> objects_by_size;
    PagedSpaceIterator spaces(this);
    for (PagedSpace* space = spaces.Next(); space != nullptr;
         space = spaces.Next()) {
      PagedSpaceObjectIterator it(this, space);
      for (Tagged<HeapObject> obj = it.Next(); !obj.is_null();
           obj = it.Next()) {
        objects_by_size[obj->Size()].push_back(obj);
      }
    }
    {
      LargeObjectSpaceObjectIterator it(lo_space());
      for (Tagged<HeapObject> obj = it.Next(); !obj.is_null();
           obj = it.Next()) {
        objects_by_size[obj->Size()].push_back(obj);
      }
    }
    for (auto it = objects_by_size.rbegin(); it != objects_by_size.rend();
         ++it) {
      ReportDuplicates(it->first, &it->second);
    }
  }

  if (gc_reason == GarbageCollectionReason::kLastResort &&
      v8_flags.heap_snapshot_on_oom) {
    isolate()->heap_profiler()->WriteSnapshotToDiskAfterGC();
  }
}

void Heap::PreciseCollectAllGarbage(GCFlags gc_flags,
                                    GarbageCollectionReason gc_reason,
                                    const GCCallbackFlags gc_callback_flags) {
  if (!incremental_marking()->IsStopped()) {
    FinalizeIncrementalMarkingAtomically(gc_reason);
  }
  CollectAllGarbage(gc_flags, gc_reason, gc_callback_flags);
}

void Heap::ReportExternalMemoryPressure() {
  const GCCallbackFlags kGCCallbackFlagsForExternalMemory =
      static_cast<GCCallbackFlags>(
          kGCCallbackFlagSynchronousPhantomCallbackProcessing |
          kGCCallbackFlagCollectAllExternalMemory);
  int64_t current = external_memory_.total();
  int64_t baseline = external_memory_.low_since_mark_compact();
  int64_t limit = external_memory_.limit();
  TRACE_EVENT2(
      "devtools.timeline,v8", "V8.ExternalMemoryPressure", "external_memory_mb",
      static_cast<int>((current - baseline) / MB), "external_memory_limit_mb",
      static_cast<int>((limit - baseline) / MB));
  if (current > baseline + external_memory_hard_limit()) {
    CollectAllGarbage(
        GCFlag::kReduceMemoryFootprint,
        GarbageCollectionReason::kExternalMemoryPressure,
        static_cast<GCCallbackFlags>(kGCCallbackFlagCollectAllAvailableGarbage |
                                     kGCCallbackFlagsForExternalMemory));
    return;
  }
  if (incremental_marking()->IsStopped()) {
    if (incremental_marking()->CanAndShouldBeStarted()) {
      StartIncrementalMarking(GCFlagsForIncrementalMarking(),
                              GarbageCollectionReason::kExternalMemoryPressure,
                              kGCCallbackFlagsForExternalMemory);
    } else {
      CollectAllGarbage(i::GCFlag::kNoFlags,
                        GarbageCollectionReason::kExternalMemoryPressure,
                        kGCCallbackFlagsForExternalMemory);
    }
  } else {
    // Incremental marking is turned on and has already been started.
    current_gc_callback_flags_ = static_cast<GCCallbackFlags>(
        current_gc_callback_flags_ | kGCCallbackFlagsForExternalMemory);
    incremental_marking()->AdvanceAndFinalizeIfNecessary();
  }
}

int64_t Heap::external_memory_limit() { return external_memory_.limit(); }

Heap::DevToolsTraceEventScope::DevToolsTraceEventScope(Heap* heap,
                                                       const char* event_name,
                                                       const char* event_type)
    : heap_(heap), event_name_(event_name) {
  TRACE_EVENT_BEGIN2("devtools.timeline,v8", event_name_, "usedHeapSizeBefore",
                     heap_->SizeOfObjects(), "type", event_type);
}

Heap::DevToolsTraceEventScope::~DevToolsTraceEventScope() {
  TRACE_EVENT_END1("devtools.timeline,v8", event_name_, "usedHeapSizeAfter",
                   heap_->SizeOfObjects());
}

namespace {

template <typename Callback>
void InvokeExternalCallbacks(Isolate* isolate, Callback callback) {
  DCHECK(!AllowJavascriptExecution::IsAllowed(isolate));
  AllowGarbageCollection allow_gc;
  // Temporary override any embedder stack state as callbacks may create
  // their own state on the stack and recursively trigger GC.
  EmbedderStackStateScope embedder_scope(
      isolate->heap(), EmbedderStackStateOrigin::kExplicitInvocation,
      StackState::kMayContainHeapPointers);
  VMState<EXTERNAL> callback_state(isolate);

  callback();
}

size_t GlobalMemorySizeFromV8Size(size_t v8_size) {
  const size_t kGlobalMemoryToV8Ratio = 2;
  return std::min(static_cast<uint64_t>(std::numeric_limits<size_t>::max()),
                  static_cast<uint64_t>(v8_size) * kGlobalMemoryToV8Ratio);
}

}  // anonymous namespace

void Heap::SetOldGenerationAndGlobalMaximumSize(
    size_t max_old_generation_size) {
  max_old_generation_size_.store(max_old_generation_size,
                                 std::memory_order_relaxed);
  max_global_memory_size_ = GlobalMemorySizeFromV8Size(max_old_generation_size);
}

void Heap::SetOldGenerationAndGlobalAllocationLimit(
    size_t new_old_generation_allocation_limit,
    size_t new_global_allocation_limit) {
  CHECK_GE(new_global_allocation_limit, new_old_generation_allocation_limit);
  old_generation_allocation_limit_.store(new_old_generation_allocation_limit,
                                         std::memory_order_relaxed);
  global_allocation_limit_.store(new_global_allocation_limit,
                                 std::memory_order_relaxed);
  set_old_generation_allocation_limit_configured(true);
}

void Heap::ResetOldGenerationAndGlobalAllocationLimit() {
  SetOldGenerationAndGlobalAllocationLimit(
      initial_old_generation_size_,
      GlobalMemorySizeFromV8Size(initial_old_generation_size_));
  set_old_generation_allocation_limit_configured(false);
}

void Heap::CollectGarbage(AllocationSpace space,
                          GarbageCollectionReason gc_reason,
                          const v8::GCCallbackFlags gc_callback_flags) {
  if (V8_UNLIKELY(!deserialization_complete_)) {
    // During isolate initialization heap always grows. GC is only requested
    // if a new page allocation fails. In such a case we should crash with
    // an out-of-memory instead of performing GC because the prologue/epilogue
    // callbacks may see objects that are not yet deserialized.
    CHECK(always_allocate());
    FatalProcessOutOfMemory("GC during deserialization");
  }

  // CollectGarbage consists of three parts:
  // 1. The prologue part which may execute callbacks. These callbacks may
  // allocate and trigger another garbage collection.
  // 2. The main garbage collection phase.
  // 3. The epilogue part which may execute callbacks. These callbacks may
  // allocate and trigger another garbage collection

  // Part 1: Invoke all callbacks which should happen before the actual garbage
  // collection is triggered. Note that these callbacks may trigger another
  // garbage collection since they may allocate.

  // JS execution is not allowed in any of the callbacks.
  DisallowJavascriptExecution no_js(isolate());

  DCHECK(AllowGarbageCollection::IsAllowed());
  // TODO(chromium:1523607): Ensure this for standalone cppgc as well.
  CHECK_IMPLIES(!v8_flags.allow_allocation_in_fast_api_call,
                !isolate()->InFastCCall());

  const char* collector_reason = nullptr;
  const GarbageCollector collector =
      SelectGarbageCollector(space, gc_reason, &collector_reason);
  current_or_last_garbage_collector_ = collector;
  DCHECK_IMPLIES(v8_flags.minor_ms && IsYoungGenerationCollector(collector),
                 !ShouldReduceMemory());

  if (collector == GarbageCollector::MARK_COMPACTOR &&
      incremental_marking()->IsMinorMarking()) {
    const GCFlags gc_flags = current_gc_flags_;
    // Minor GCs should not be memory reducing.
    current_gc_flags_ &= ~GCFlag::kReduceMemoryFootprint;
    CollectGarbage(NEW_SPACE,
                   GarbageCollectionReason::kFinalizeConcurrentMinorMS);
    current_gc_flags_ = gc_flags;
  }

  const GCType gc_type = GetGCTypeFromGarbageCollector(collector);

  // Prologue callbacks. These callbacks may trigger GC themselves and thus
  // cannot be related exactly to garbage collection cycles.
  //
  // GCTracer scopes are managed by callees.
  InvokeExternalCallbacks(isolate(), [this, gc_callback_flags, gc_type]() {
    // Ensure that all pending phantom callbacks are invoked.
    isolate()->global_handles()->InvokeSecondPassPhantomCallbacks();

    // Prologue callbacks registered with Heap.
    CallGCPrologueCallbacks(gc_type, gc_callback_flags,
                            GCTracer::Scope::HEAP_EXTERNAL_PROLOGUE);
  });

  // The main garbage collection phase.
  //
  // We need a stack marker at the top of all entry points to allow
  // deterministic passes over the stack. E.g., a verifier that should only
  // find a subset of references of the marker.
  //
  // TODO(chromium:1056170): Consider adding a component that keeps track
  // of relevant GC stack regions where interesting pointers can be found.
  stack().SetMarkerIfNeededAndCallback([this, collector, gc_reason,
                                        collector_reason, gc_callback_flags]() {
    DisallowGarbageCollection no_gc_during_gc;

    size_t committed_memory_before =
        collector == GarbageCollector::MARK_COMPACTOR
            ? CommittedOldGenerationMemory()
            : 0;

    tracer()->StartObservablePause(base::TimeTicks::Now());
    VMState<GC> state(isolate());
    DevToolsTraceEventScope devtools_trace_event_scope(
        this, IsYoungGenerationCollector(collector) ? "MinorGC" : "MajorGC",
        ToString(gc_reason));

    GarbageCollectionPrologue(gc_reason, gc_callback_flags);
    {
      GCTracer::RecordGCPhasesInfo record_gc_phases_info(this, collector,
                                                         gc_reason);
      std::optional<TimedHistogramScope> histogram_timer_scope;
      std::optional<OptionalTimedHistogramScope> histogram_timer_priority_scope;
      TRACE_EVENT0("v8", record_gc_phases_info.trace_event_name());
      if (record_gc_phases_info.type_timer()) {
        histogram_timer_scope.emplace(record_gc_phases_info.type_timer(),
                                      isolate_);
      }
      if (record_gc_phases_info.type_priority_timer()) {
        histogram_timer_priority_scope.emplace(
            record_gc_phases_info.type_priority_timer(), isolate_,
            OptionalTimedHistogramScopeMode::TAKE_TIME);
      }

      if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
        tp_heap_->CollectGarbage();
      } else {
        PerformGarbageCollection(collector, gc_reason, collector_reason);
      }
      // Clear flags describing the current GC now that the current GC is
      // complete. Do this before GarbageCollectionEpilogue() since that could
      // trigger another unforced GC.
      is_current_gc_forced_ = false;
      is_current_gc_for_heap_profiler_ = false;

      if (collector == GarbageCollector::MARK_COMPACTOR ||
          collector == GarbageCollector::SCAVENGER) {
        tracer()->RecordGCPhasesHistograms(record_gc_phases_info.mode());
      }
      if ((collector == GarbageCollector::MARK_COMPACTOR ||
           collector == GarbageCollector::MINOR_MARK_SWEEPER) &&
          cpp_heap()) {
        CppHeap::From(cpp_heap())->FinishAtomicSweepingIfRunning();
      }
    }

    GarbageCollectionEpilogue(collector);
    if (collector == GarbageCollector::MARK_COMPACTOR &&
        v8_flags.track_detached_contexts) {
      isolate()->CheckDetachedContextsAfterGC();
    }

    if (collector == GarbageCollector::MARK_COMPACTOR) {
      if (memory_reducer_ != nullptr) {
        memory_reducer_->NotifyMarkCompact(committed_memory_before);
      }
      if (initial_max_old_generation_size_ < max_old_generation_size() &&
          OldGenerationSizeOfObjects() <
              initial_max_old_generation_size_threshold_) {
        SetOldGenerationAndGlobalMaximumSize(initial_max_old_generation_size_);
      }
    }

    tracer()->StopAtomicPause();
    tracer()->StopObservablePause(collector, base::TimeTicks::Now());
    // Young generation cycles finish atomically. It is important that
    // StopObservablePause, and StopCycle are called in this
    // order; the latter may replace the current event with that of an
    // interrupted full cycle.
    if (IsYoungGenerationCollector(collector)) {
      tracer()->StopYoungCycleIfNeeded();
    } else {
      tracer()->StopFullCycleIfNeeded();
      ReportIneffectiveMarkCompactIfNeeded();
    }
  });

  // Epilogue callbacks. These callbacks may trigger GC themselves and thus
  // cannot be related exactly to garbage collection cycles.
  //
  // GCTracer scopes are managed by callees.
  InvokeExternalCallbacks(isolate(), [this, gc_callback_flags, gc_type]() {
    // Epilogue callbacks registered with Heap.
    CallGCEpilogueCallbacks(gc_type, gc_callback_flags,
                            GCTracer::Scope::HEAP_EXTERNAL_EPILOGUE);

    isolate()->global_handles()->PostGarbageCollectionProcessing(
        gc_callback_flags);
  });

  if (collector == GarbageCollector::MARK_COMPACTOR) {
    if ((gc_callback_flags &
         (kGCCallbackFlagForced | kGCCallbackFlagCollectAllAvailableGarbage))) {
      isolate()->CountUsage(v8::Isolate::kForcedGC);
    }
    if (v8_flags.heap_snapshot_on_gc > 0 &&
        static_cast<size_t>(v8_flags.heap_snapshot_on_gc) == ms_count_) {
      isolate()->heap_profiler()->WriteSnapshotToDiskAfterGC();
    }
  } else {
    // Start incremental marking for the next cycle. We do this only for
    // minor GCs to avoid a loop where mark-compact causes another mark-compact.
    StartIncrementalMarkingIfAllocationLimitIsReached(
        main_thread_local_heap(), GCFlagsForIncrementalMarking(),
        kGCCallbackScheduleIdleGarbageCollection);
  }

  if (!CanExpandOldGeneration(0)) {
    InvokeNearHeapLimitCallback();
    if (!CanExpandOldGeneration(0)) {
      if (v8_flags.heap_snapshot_on_oom) {
        isolate()->heap_profiler()->WriteSnapshotToDiskAfterGC();
      }
      FatalProcessOutOfMemory("Reached heap limit");
    }
  }

  if (collector == GarbageCollector::MARK_COMPACTOR) {
    current_gc_flags_ = GCFlag::kNoFlags;
  }
}

class IdleTaskOnContextDispose : public CancelableIdleTask {
 public:
  static void TryPostJob(Heap* heap) {
    const auto runner = heap->GetForegroundTaskRunner();
    if (runner->IdleTasksEnabled()) {
      runner->PostIdleTask(
          std::make_unique<IdleTaskOnContextDispose>(heap->isolate()));
    }
  }

  explicit IdleTaskOnContextDispose(Isolate* isolate)
      : CancelableIdleTask(isolate), isolate_(isolate) {}

  void RunInternal(double deadline_in_seconds) override {
    auto* heap = isolate_->heap();
    const base::TimeDelta time_to_run = base::TimeTicks::Now() - creation_time_;
    // The provided delta uses embedder timestamps.
    const base::TimeDelta idle_time = base::TimeDelta::FromMillisecondsD(
        (deadline_in_seconds * 1000) - heap->MonotonicallyIncreasingTimeInMs());
    const bool time_to_run_exceeded = time_to_run > kMaxTimeToRun;
    if (V8_UNLIKELY(v8_flags.trace_context_disposal)) {
      isolate_->PrintWithTimestamp(
          "[context-disposal/idle task] time-to-run: %fms (max delay: %fms), "
          "idle time: %fms%s\n",
          time_to_run.InMillisecondsF(), kMaxTimeToRun.InMillisecondsF(),
          idle_time.InMillisecondsF(),
          time_to_run_exceeded ? ", not starting any action" : "");
    }
    if (time_to_run_exceeded) {
      return;
    }
    TryRunMinorGC(idle_time);
  }

 private:
  static constexpr base::TimeDelta kFrameTime =
      base::TimeDelta::FromMillisecondsD(16);

  // We limit any idle time actions here by a maximum time to run of a single
  // frame. This avoids that these tasks are executed too late and causes
  // (unpredictable) side effects with e.g. promotion of newly allocated
  // objects.
  static constexpr base::TimeDelta kMaxTimeToRun = kFrameTime + kFrameTime;

  void TryRunMinorGC(const base::TimeDelta idle_time) {
    // The following logic estimates whether a young generation GC would fit in
    // `idle_time.` We bail out for a young gen below 1MB to avoid executing GC
    // when the mutator is not actually active.
    static constexpr size_t kMinYounGenSize = 1 * MB;

    auto* heap = isolate_->heap();
    const double young_gen_gc_speed =
        heap->tracer()->YoungGenerationSpeedInBytesPerMillisecond(
            YoungGenerationSpeedMode::kUpToAndIncludingAtomicPause);
    const size_t young_gen_bytes = heap->YoungGenerationSizeOfObjects();
    const base::TimeDelta young_gen_estimate =
        base::TimeDelta::FromMillisecondsD(young_gen_bytes /
                                           young_gen_gc_speed);
    const bool run_young_gen_gc =
        young_gen_estimate < idle_time && young_gen_bytes > kMinYounGenSize;
    if (V8_UNLIKELY(v8_flags.trace_context_disposal)) {
      isolate_->PrintWithTimestamp(
          "[context-disposal/idle task] young generation size: %zuKB (min: "
          "%zuKB), GC speed: %fKB/ms, estimated time: %fms%s\n",
          young_gen_bytes / KB, kMinYounGenSize / KB, young_gen_gc_speed / KB,
          young_gen_estimate.InMillisecondsF(),
          run_young_gen_gc ? ", performing young gen GC"
                           : ", not starting young gen GC");
    }
    if (run_young_gen_gc) {
      heap->CollectGarbage(NEW_SPACE, GarbageCollectionReason::kTesting);
    }
  }

  Isolate* isolate_;
  const base::TimeTicks creation_time_ = base::TimeTicks::Now();
};

int Heap::NotifyContextDisposed(bool has_dependent_context) {
  if (V8_UNLIKELY(v8_flags.trace_context_disposal)) {
    isolate()->PrintWithTimestamp(
        "[context-disposal] Disposing %s context\n",
        has_dependent_context ? "nested" : "top-level");
  }
  if (!has_dependent_context) {
    tracer()->ResetSurvivalEvents();
    ResetOldGenerationAndGlobalAllocationLimit();
    if (memory_reducer_) {
      memory_reducer_->NotifyPossibleGarbage();
    }
  } else if (v8_flags.idle_gc_on_context_disposal) {
    IdleTaskOnContextDispose::TryPostJob(this);
  }
  isolate()->AbortConcurrentOptimization(BlockingBehavior::kDontBlock);
  if (!isolate()->context().is_null()) {
    RemoveDirtyFinalizationRegistriesOnContext(isolate()->raw_native_context());
    isolate()->raw_native_context()->set_retained_maps(
        ReadOnlyRoots(this).empty_weak_array_list());
  }

  return ++contexts_disposed_;
}

void Heap::StartIncrementalMarking(GCFlags gc_flags,
                                   GarbageCollectionReason gc_reason,
                                   GCCallbackFlags gc_callback_flags,
                                   GarbageCollector collector) {
  DCHECK(incremental_marking()->IsStopped());
  CHECK_IMPLIES(!v8_flags.allow_allocation_in_fast_api_call,
                !isolate()->InFastCCall());

  if (v8_flags.separate_gc_phases && gc_callbacks_depth_ > 0) {
    // Do not start incremental marking while invoking GC callbacks.
    // Heap::CollectGarbage already decided which GC is going to be
    // invoked. In case it chose a young-gen GC, starting an incremental
    // full GC during callbacks would break the separate GC phases
    // guarantee.
    return;
  }

  if (IsYoungGenerationCollector(collector)) {
    CompleteSweepingYoung();
  } else {
    // Sweeping needs to be completed such that markbits are all cleared before
    // starting marking again.
    CompleteSweepingFull();
  }

  std::optional<SafepointScope> safepoint_scope;

  {
    AllowGarbageCollection allow_shared_gc;
    SafepointKind safepoint_kind = isolate()->is_shared_space_isolate()
                                       ? SafepointKind::kGlobal
                                       : SafepointKind::kIsolate;
    safepoint_scope.emplace(isolate(), safepoint_kind);
  }

#ifdef DEBUG
  VerifyCountersAfterSweeping();
#endif

  std::vector<Isolate*> paused_clients =
      PauseConcurrentThreadsInClients(collector);

  // Now that sweeping is completed, we can start the next full GC cycle.
  tracer()->StartCycle(collector, gc_reason, nullptr,
                       GCTracer::MarkingType::kIncremental);

  current_gc_flags_ = gc_flags;
  current_gc_callback_flags_ = gc_callback_flags;

  incremental_marking()->Start(collector, gc_reason);

  if (collector == GarbageCollector::MARK_COMPACTOR) {
    is_full_gc_during_loading_ = update_allocation_limits_after_loading_;
    RecomputeLimitsAfterLoadingIfNeeded();
    DCHECK(!update_allocation_limits_after_loading_);
  }

  if (isolate()->is_shared_space_isolate()) {
    for (Isolate* client : paused_clients) {
      client->heap()->concurrent_marking()->Resume();
    }
  } else {
    DCHECK(paused_clients.empty());
  }
}

namespace {
void CompleteArrayBufferSweeping(Heap* heap) {
  auto* array_buffer_sweeper = heap->array_buffer_sweeper();
  if (array_buffer_sweeper->sweeping_in_progress()) {
    auto* tracer = heap->tracer();
    GCTracer::Scope::ScopeId scope_id;

    switch (tracer->GetCurrentCollector()) {
      case GarbageCollector::MINOR_MARK_SWEEPER:
        scope_id = GCTracer::Scope::MINOR_MS_COMPLETE_SWEEP_ARRAY_BUFFERS;
        break;
      case GarbageCollector::SCAVENGER:
        scope_id = GCTracer::Scope::SCAVENGER_COMPLETE_SWEEP_ARRAY_BUFFERS;
        break;
      case GarbageCollector::MARK_COMPACTOR:
        scope_id = GCTracer::Scope::MC_COMPLETE_SWEEP_ARRAY_BUFFERS;
    }

    TRACE_GC_EPOCH_WITH_FLOW(
        tracer, scope_id, ThreadKind::kMain,
        array_buffer_sweeper->GetTraceIdForFlowEvent(scope_id),
        TRACE_EVENT_FLAG_FLOW_IN);
    array_buffer_sweeper->EnsureFinished();
  }
}
}  // namespace

void Heap::CompleteSweepingFull() {
  EnsureSweepingCompleted(SweepingForcedFinalizationMode::kUnifiedHeap);

  DCHECK(!sweeping_in_progress());
  DCHECK_IMPLIES(cpp_heap(),
                 !CppHeap::From(cpp_heap())->sweeper().IsSweepingInProgress());
  DCHECK(!tracer()->IsSweepingInProgress());
}

void Heap::StartIncrementalMarkingOnInterrupt() {
  StartIncrementalMarkingIfAllocationLimitIsReached(
      main_thread_local_heap(), GCFlagsForIncrementalMarking(),
      kGCCallbackScheduleIdleGarbageCollection);
}

void Heap::StartIncrementalMarkingIfAllocationLimitIsReached(
    LocalHeap* local_heap, GCFlags gc_flags,
    const GCCallbackFlags gc_callback_flags) {
  if (incremental_marking()->IsStopped() &&
      incremental_marking()->CanAndShouldBeStarted()) {
    switch (IncrementalMarkingLimitReached()) {
      case IncrementalMarkingLimit::kHardLimit:
        if (local_heap->is_main_thread_for(this)) {
          StartIncrementalMarking(
              gc_flags,
              OldGenerationSpaceAvailable() <= NewSpaceTargetCapacity()
                  ? GarbageCollectionReason::kAllocationLimit
                  : GarbageCollectionReason::kGlobalAllocationLimit,
              gc_callback_flags);
        } else {
          ExecutionAccess access(isolate());
          isolate()->stack_guard()->RequestStartIncrementalMarking();
          if (auto* job = incremental_marking()->incremental_marking_job()) {
            job->ScheduleTask();
          }
        }
        break;
      case IncrementalMarkingLimit::kSoftLimit:
        if (auto* job = incremental_marking()->incremental_marking_job()) {
          job->ScheduleTask();
        }
        break;
      case IncrementalMarkingLimit::kFallbackForEmbedderLimit:
        // This is a fallback case where no appropriate limits have been
        // configured yet.
        if (local_heap->is_main_thread_for(this) &&
            memory_reducer() != nullptr) {
          memory_reducer()->NotifyPossibleGarbage();
        }
        break;
      case IncrementalMarkingLimit::kNoLimit:
        break;
    }
  }
}

void Heap::MoveRange(Tagged<HeapObject> dst_object, const ObjectSlot dst_slot,
                     const ObjectSlot src_slot, int len,
                     WriteBarrierMode mode) {
  DCHECK_NE(len, 0);
  DCHECK_NE(dst_object->map(), ReadOnlyRoots(this).fixed_cow_array_map());
  const ObjectSlot dst_end(dst_slot + len);
  // Ensure no range overflow.
  DCHECK(dst_slot < dst_end);
  DCHECK(src_slot < src_slot + len);

  if ((v8_flags.concurrent_marking && incremental_marking()->IsMarking()) ||
      (v8_flags.minor_ms && sweeper()->IsIteratingPromotedPages())) {
    if (dst_slot < src_slot) {
      // Copy tagged values forward using relaxed load/stores that do not
      // involve value decompression.
      const AtomicSlot atomic_dst_end(dst_end);
      AtomicSlot dst(dst_slot);
      AtomicSlot src(src_slot);
      while (dst < atomic_dst_end) {
        *dst = *src;
        ++dst;
        ++src;
      }
    } else {
      // Copy tagged values backwards using relaxed load/stores that do not
      // involve value decompression.
      const AtomicSlot atomic_dst_begin(dst_slot);
      AtomicSlot dst(dst_slot + len - 1);
      AtomicSlot src(src_slot + len - 1);
      while (dst >= atomic_dst_begin) {
        *dst = *src;
        --dst;
        --src;
      }
    }
  } else {
    MemMove(dst_slot.ToVoidPtr(), src_slot.ToVoidPtr(), len * kTaggedSize);
  }
  if (mode == SKIP_WRITE_BARRIER) return;
  WriteBarrierForRange(dst_object, dst_slot, dst_end);
}

// Instantiate Heap::CopyRange().
template V8_EXPORT_PRIVATE void Heap::CopyRange<ObjectSlot>(
    Tagged<HeapObject> dst_object, ObjectSlot dst_slot, ObjectSlot src_slot,
    int len, WriteBarrierMode mode);
template V8_EXPORT_PRIVATE void Heap::CopyRange<MaybeObjectSlot>(
    Tagged<HeapObject> dst_object, MaybeObjectSlot dst_slot,
    MaybeObjectSlot src_slot, int len, WriteBarrierMode mode);

template <typename TSlot>
void Heap::CopyRange(Tagged<HeapObject> dst_object, const TSlot dst_slot,
                     const TSlot src_slot, int len, WriteBarrierMode mode) {
  DCHECK_NE(len, 0);

  DCHECK_NE(dst_object->map(), ReadOnlyRoots(this).fixed_cow_array_map());
  const TSlot dst_end(dst_slot + len);
  // Ensure ranges do not overlap.
  DCHECK(dst_end <= src_slot || (src_slot + len) <= dst_slot);

  if ((v8_flags.concurrent_marking && incremental_marking()->IsMarking()) ||
      (v8_flags.minor_ms && sweeper()->IsIteratingPromotedPages())) {
    // Copy tagged values using relaxed load/stores that do not involve value
    // decompression.
    const AtomicSlot atomic_dst_end(dst_end);
    AtomicSlot dst(dst_slot);
    AtomicSlot src(src_slot);
    while (dst < atomic_dst_end) {
      *dst = *src;
      ++dst;
      ++src;
    }
  } else {
    MemCopy(dst_slot.ToVoidPtr(), src_slot.ToVoidPtr(), len * kTaggedSize);
  }
  if (mode == SKIP_WRITE_BARRIER) return;
  WriteBarrierForRange(dst_object, dst_slot, dst_end);
}

bool Heap::CollectionRequested() {
  return collection_barrier_->WasGCRequested();
}

void Heap::CollectGarbageForBackground(LocalHeap* local_heap) {
  CHECK(local_heap->is_main_thread());
  CollectAllGarbage(current_gc_flags_,
                    GarbageCollectionReason::kBackgroundAllocationFailure,
                    current_gc_callback_flags_);
}

void Heap::CheckCollectionRequested() {
  if (!CollectionRequested()) return;

  CollectAllGarbage(current_gc_flags_,
                    GarbageCollectionReason::kBackgroundAllocationFailure,
                    current_gc_callback_flags_);
}

#if V8_ENABLE_WEBASSEMBLY
void Heap::EnsureWasmCanonicalRttsSize(int length) {
  HandleScope scope(isolate());

  Handle<WeakArrayList> current_rtts = handle(wasm_canonical_rtts(), isolate_);
  if (length <= current_rtts->length()) return;
  DirectHandle<WeakArrayList> new_rtts = WeakArrayList::EnsureSpace(
      isolate(), current_rtts, length, AllocationType::kOld);
  new_rtts->set_length(length);
  set_wasm_canonical_rtts(*new_rtts);

  // Wrappers are indexed by canonical rtt length, and an additional boolean
  // storing whether the corresponding function is imported or not.
  int required_wrapper_length = 2 * length;
  Handle<WeakArrayList> current_wrappers =
      handle(js_to_wasm_wrappers(), isolate_);
  if (required_wrapper_length <= current_wrappers->length()) return;
  DirectHandle<WeakArrayList> new_wrappers =
      WeakArrayList::EnsureSpace(isolate(), current_wrappers,
                                 required_wrapper_length, AllocationType::kOld);
  new_wrappers->set_length(required_wrapper_length);
  set_js_to_wasm_wrappers(*new_wrappers);
}

void Heap::ClearWasmCanonicalRttsForTesting() {
  ReadOnlyRoots roots(this);
  set_wasm_canonical_rtts(roots.empty_weak_array_list());
}
#endif

void Heap::UpdateSurvivalStatistics(int start_new_space_size) {
  if (start_new_space_size == 0) return;

  promotion_ratio_ = (static_cast<double>(promoted_objects_size_) /
                      static_cast<double>(start_new_space_size) * 100);

  if (previous_new_space_surviving_object_size_ > 0) {
    promotion_rate_ =
        (static_cast<double>(promoted_objects_size_) /
         static_cast<double>(previous_new_space_surviving_object_size_) * 100);
  } else {
    promotion_rate_ = 0;
  }

  new_space_surviving_rate_ =
      (static_cast<double>(new_space_surviving_object_size_) /
       static_cast<double>(start_new_space_size) * 100);

  double survival_rate = promotion_ratio_ + new_space_surviving_rate_;
  tracer()->AddSurvivalRatio(survival_rate);
}

namespace {

GCTracer::Scope::ScopeId CollectorScopeId(GarbageCollector collector) {
  switch (collector) {
    case GarbageCollector::MARK_COMPACTOR:
      return GCTracer::Scope::ScopeId::MARK_COMPACTOR;
    case GarbageCollector::MINOR_MARK_SWEEPER:
      return GCTracer::Scope::ScopeId::MINOR_MARK_SWEEPER;
    case GarbageCollector::SCAVENGER:
      return GCTracer::Scope::ScopeId::SCAVENGER;
  }
  UNREACHABLE();
}

void ClearStubCaches(Isolate* isolate) {
  isolate->load_stub_cache()->Clear();
  isolate->store_stub_cache()->Clear();
  isolate->define_own_stub_cache()->Clear();

  if (isolate->is_shared_space_isolate()) {
    isolate->global_safepoint()->IterateClientIsolates([](Isolate* client) {
      client->load_stub_cache()->Clear();
      client->store_stub_cache()->Clear();
      client->define_own_stub_cache()->Clear();
    });
  }
}

}  // namespace

void Heap::PerformGarbageCollection(GarbageCollector collector,
                                    GarbageCollectionReason gc_reason,
                                    const char* collector_reason) {
  if (IsYoungGenerationCollector(collector)) {
    if (v8_flags.sticky_mark_bits) {
      DCHECK_EQ(GarbageCollector::MINOR_MARK_SWEEPER, collector);
      // TODO(333906585): It's not necessary to complete full sweeping here.
      // Make sure that only the OLD_SPACE is swept.
      CompleteSweepingFull();
    } else {
      CompleteSweepingYoung();
      if (v8_flags.verify_heap) {
        // If heap verification is enabled, we want to ensure that sweeping is
        // completed here, as it will be triggered from Heap::Verify anyway.
        // In this way, sweeping finalization is accounted to the corresponding
        // full GC cycle.
        CompleteSweepingFull();
      }
    }
  } else {
    DCHECK_EQ(GarbageCollector::MARK_COMPACTOR, collector);
    CompleteSweepingFull();
  }

  const base::TimeTicks atomic_pause_start_time = base::TimeTicks::Now();

  std::optional<SafepointScope> safepoint_scope;
  {
    AllowGarbageCollection allow_shared_gc;

    SafepointKind safepoint_kind = isolate()->is_shared_space_isolate()
                                       ? SafepointKind::kGlobal
                                       : SafepointKind::kIsolate;
    safepoint_scope.emplace(isolate(), safepoint_kind);
  }

  if (!incremental_marking_->IsMarking() ||
      (collector == GarbageCollector::SCAVENGER)) {
    tracer()->StartCycle(collector, gc_reason, collector_reason,
                         GCTracer::MarkingType::kAtomic);
  }

  tracer()->StartAtomicPause();
  if ((!Heap::IsYoungGenerationCollector(collector) || v8_flags.minor_ms) &&
      incremental_marking_->IsMarking()) {
    DCHECK_IMPLIES(Heap::IsYoungGenerationCollector(collector),
                   incremental_marking_->IsMinorMarking());
    tracer()->UpdateCurrentEvent(gc_reason, collector_reason);
  }

  DCHECK(tracer()->IsConsistentWithCollector(collector));
  TRACE_GC_EPOCH(tracer(), CollectorScopeId(collector), ThreadKind::kMain);

  collection_barrier_->StopTimeToCollectionTimer();

  std::vector<Isolate*> paused_clients =
      PauseConcurrentThreadsInClients(collector);

  FreeLinearAllocationAreas();

  tracer()->StartInSafepoint(atomic_pause_start_time);

  GarbageCollectionPrologueInSafepoint();

  PerformHeapVerification();

  const size_t start_young_generation_size =
      NewSpaceSize() + (new_lo_space() ? new_lo_space()->SizeOfObjects() : 0);

  // Make sure allocation observers are disabled until the new new space
  // capacity is set in the epilogue.
  PauseAllocationObserversScope pause_observers(this);

  size_t new_space_capacity_before_gc = NewSpaceTargetCapacity();

  if (collector == GarbageCollector::MARK_COMPACTOR) {
    MarkCompact();
  } else if (collector == GarbageCollector::MINOR_MARK_SWEEPER) {
    MinorMarkSweep();
  } else {
    DCHECK_EQ(GarbageCollector::SCAVENGER, collector);
    Scavenge();
  }

  pretenuring_handler_.ProcessPretenuringFeedback(new_space_capacity_before_gc);

  UpdateSurvivalStatistics(static_cast<int>(start_young_generation_size));
  ShrinkOldGenerationAllocationLimitIfNotConfigured();

  if (collector == GarbageCollector::SCAVENGER) {
    // Objects that died in the new space might have been accounted
    // as bytes marked ahead of schedule by the incremental marker.
    incremental_marking()->UpdateMarkedBytesAfterScavenge(
        start_young_generation_size - SurvivedYoungObjectSize());
  }

  isolate_->counters()->objs_since_last_young()->Set(0);

  isolate_->eternal_handles()->PostGarbageCollectionProcessing();

  // Update relocatables.
  Relocatable::PostGarbageCollectionProcessing(isolate_);

  if (isolate_->is_shared_space_isolate()) {
    // Allows handle derefs for all threads/isolates from this thread.
    AllowHandleDereferenceAllThreads allow_all_handle_derefs;
    isolate()->global_safepoint()->IterateClientIsolates([](Isolate* client) {
      Relocatable::PostGarbageCollectionProcessing(client);
    });
  }

  // First round weak callbacks are not supposed to allocate and trigger
  // nested GCs.
  isolate_->global_handles()->InvokeFirstPassWeakCallbacks();

  if (cpp_heap() && (collector == GarbageCollector::MARK_COMPACTOR ||
                     collector == GarbageCollector::MINOR_MARK_SWEEPER)) {
    // TraceEpilogue may trigger operations that invalidate global handles. It
    // has to be called *after* all other operations that potentially touch
    // and reset global handles. It is also still part of the main garbage
    // collection pause and thus needs to be called *before* any operation
    // that can potentially trigger recursive garbage collections.
    TRACE_GC(tracer(), GCTracer::Scope::HEAP_EMBEDDER_TRACING_EPILOGUE);
    CppHeap::From(cpp_heap())->CompactAndSweep();
  }

  if (collector == GarbageCollector::MARK_COMPACTOR) {
    ClearStubCaches(isolate());
  }

  PerformHeapVerification();

  GarbageCollectionEpilogueInSafepoint(collector);

  const base::TimeTicks atomic_pause_end_time = base::TimeTicks::Now();
  tracer()->StopInSafepoint(atomic_pause_end_time);

  ResumeConcurrentThreadsInClients(std::move(paused_clients));

  RecomputeLimits(collector, atomic_pause_end_time);
  if ((collector == GarbageCollector::MARK_COMPACTOR) &&
      is_full_gc_during_loading_) {
    if (ShouldOptimizeForLoadTime()) {
      update_allocation_limits_after_loading_ = true;
    }
    is_full_gc_during_loading_ = false;
  }

  // After every full GC the old generation allocation limit should be
  // configured.
  DCHECK_IMPLIES(collector == GarbageCollector::MARK_COMPACTOR,
                 old_generation_allocation_limit_configured());
}

void Heap::PerformHeapVerification() {
  HeapVerifier::VerifyHeapIfEnabled(this);

  if (isolate()->is_shared_space_isolate()) {
    isolate()->global_safepoint()->IterateClientIsolates([](Isolate* client) {
      HeapVerifier::VerifyHeapIfEnabled(client->heap());
    });
  }
}

std::vector<Isolate*> Heap::PauseConcurrentThreadsInClients(
    GarbageCollector collector) {
  std::vector<Isolate*> paused_clients;

  if (isolate()->is_shared_space_isolate()) {
    isolate()->global_safepoint()->IterateClientIsolates(
        [collector, &paused_clients](Isolate* client) {
          CHECK(client->heap()->deserialization_complete());

          if (v8_flags.concurrent_marking &&
              client->heap()->concurrent_marking()->Pause()) {
            paused_clients.push_back(client);
          }

          if (collector == GarbageCollector::MARK_COMPACTOR) {
            Sweeper* const client_sweeper = client->heap()->sweeper();
            client_sweeper->ContributeAndWaitForPromotedPagesIteration();
          }
        });
  }

  return paused_clients;
}

void Heap::ResumeConcurrentThreadsInClients(
    std::vector<Isolate*> paused_clients) {
  if (isolate()->is_shared_space_isolate()) {
    for (Isolate* client : paused_clients) {
      client->heap()->concurrent_marking()->Resume();
    }
  } else {
    DCHECK(paused_clients.empty());
  }
}

bool Heap::CollectGarbageShared(LocalHeap* local_heap,
                                GarbageCollectionReason gc_reason) {
  CHECK(deserialization_complete());
  DCHECK(isolate()->has_shared_space());

  Isolate* shared_space_isolate = isolate()->shared_space_isolate();
  return shared_space_isolate->heap()->CollectGarbageFromAnyThread(local_heap,
                                                                   gc_reason);
}

bool Heap::CollectGarbageFromAnyThread(LocalHeap* local_heap,
                                       GarbageCollectionReason gc_reason) {
  DCHECK(local_heap->IsRunning());

  if (isolate() == local_heap->heap()->isolate() &&
      local_heap->is_main_thread()) {
    CollectAllGarbage(current_gc_flags_, gc_reason, current_gc_callback_flags_);
    return true;
  } else {
    if (!collection_barrier_->TryRequestGC()) return false;

    const LocalHeap::ThreadState old_state =
        main_thread_local_heap()->state_.SetCollectionRequested();

    if (old_state.IsRunning()) {
      const bool performed_gc =
          collection_barrier_->AwaitCollectionBackground(local_heap);
      return performed_gc;
    } else {
      DCHECK(old_state.IsParked());
      return false;
    }
  }
}

void Heap::CompleteSweepingYoung() {
  DCHECK(!v8_flags.sticky_mark_bits);
  CompleteArrayBufferSweeping(this);

  // If sweeping is in progress and there are no sweeper tasks running, finish
  // the sweeping here, to avoid having to pause and resume during the young
  // generation GC.
  FinishSweepingIfOutOfWork();

  if (v8_flags.minor_ms) {
    EnsureYoungSweepingCompleted();
  }

#if defined(CPPGC_YOUNG_GENERATION)
  // Always complete sweeping if young generation is enabled.
  if (cpp_heap()) {
    if (auto* iheap = CppHeap::From(cpp_heap());
        iheap->generational_gc_supported())
      iheap->FinishSweepingIfRunning();
  }
#endif  // defined(CPPGC_YOUNG_GENERATION)
}

void Heap::EnsureSweepingCompletedForObject(Tagged<HeapObject> object) {
  if (!sweeping_in_progress()) return;

  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  if (chunk->InReadOnlySpace()) return;

  MutablePageMetadata* mutable_page =
      MutablePageMetadata::cast(chunk->Metadata());
  if (mutable_page->SweepingDone()) return;

  // SweepingDone() is always true for large pages.
  DCHECK(!chunk->IsLargePage());

  PageMetadata* page = PageMetadata::cast(mutable_page);
  sweeper()->EnsurePageIsSwept(page);
}

// static
Heap::LimitsCompuatationResult Heap::ComputeNewAllocationLimits(Heap* heap) {
  double v8_gc_speed =
      heap->tracer()->CombinedMarkCompactSpeedInBytesPerMillisecond();
  double v8_mutator_speed =
      heap->tracer()
          ->CurrentOldGenerationAllocationThroughputInBytesPerMillisecond();
  double v8_growing_factor = MemoryController<V8HeapTrait>::GrowingFactor(
      heap, heap->max_old_generation_size(), v8_gc_speed, v8_mutator_speed);
  double embedder_gc_speed =
      heap->tracer()->EmbedderSpeedInBytesPerMillisecond();
  double embedder_speed =
      heap->tracer()
          ->CurrentEmbedderAllocationThroughputInBytesPerMillisecond();
  double embedder_growing_factor =
      (embedder_gc_speed > 0 && embedder_speed > 0)
          ? MemoryController<GlobalMemoryTrait>::GrowingFactor(
                heap, heap->max_global_memory_size_, embedder_gc_speed,
                embedder_speed)
          : 0;
  double global_growing_factor =
      std::max(v8_growing_factor, embedder_growing_factor);

  size_t old_gen_size = heap->OldGenerationConsumedBytes();
  size_t global_size = heap->GlobalConsumedBytes();
  size_t new_space_capacity = heap->NewSpaceTargetCapacity();
  HeapGrowingMode mode = heap->CurrentHeapGrowingMode();

  size_t new_old_generation_allocation_limit =
      MemoryController<V8HeapTrait>::CalculateAllocationLimit(
          heap, old_gen_size, heap->min_old_generation_size_,
          heap->max_old_generation_size(), new_space_capacity,
          v8_growing_factor, mode);

  DCHECK_GT(global_growing_factor, 0);
  size_t new_global_allocation_limit =
      MemoryController<GlobalMemoryTrait>::CalculateAllocationLimit(
          heap, global_size, heap->min_global_memory_size_,
          heap->max_global_memory_size_, new_space_capacity,
          global_growing_factor, mode);

  return {new_old_generation_allocation_limit, new_global_allocation_limit};
}

void Heap::RecomputeLimits(GarbageCollector collector, base::TimeTicks time) {
  if (!((collector == GarbageCollector::MARK_COMPACTOR) ||
        (HasLowYoungGenerationAllocationRate() &&
         old_generation_allocation_limit_configured()))) {
    return;
  }

  auto new_limits = ComputeNewAllocationLimits(this);
  size_t new_old_generation_allocation_limit =
      new_limits.old_generation_allocation_limit;
  size_t new_global_allocation_limit = new_limits.global_allocation_limit;

  if (collector == GarbageCollector::MARK_COMPACTOR) {
    external_memory_.ResetAfterGC();

    if (v8_flags.memory_balancer) {
      // Now recompute the new allocation limit.
      mb_->RecomputeLimits(new_limits.global_allocation_limit -
                               new_limits.old_generation_allocation_limit,
                           time);
    } else {
      SetOldGenerationAndGlobalAllocationLimit(
          new_limits.old_generation_allocation_limit,
          new_limits.global_allocation_limit);
    }

    CheckIneffectiveMarkCompact(
        OldGenerationConsumedBytes(),
        tracer()->AverageMarkCompactMutatorUtilization());
  } else {
    DCHECK(HasLowYoungGenerationAllocationRate() &&
           old_generation_allocation_limit_configured());
    new_old_generation_allocation_limit = std::min(
        new_old_generation_allocation_limit, old_generation_allocation_limit());
    new_global_allocation_limit =
        std::min(new_global_allocation_limit, global_allocation_limit());
    SetOldGenerationAndGlobalAllocationLimit(
        new_old_generation_allocation_limit, new_global_allocation_limit);
  }

  CHECK_EQ(max_global_memory_size_,
           GlobalMemorySizeFromV8Size(max_old_generation_size_));
  CHECK_GE(global_allocation_limit(), old_generation_allocation_limit_);
}

void Heap::RecomputeLimitsAfterLoadingIfNeeded() {
  if (!update_allocation_limits_after_loading_) return;
  update_allocation_limits_after_loading_ = false;

  if (!v8_flags.update_allocation_limits_after_loading) return;

  if ((OldGenerationSpaceAvailable() > 0) && (GlobalMemoryAvailable() > 0)) {
    // Only recompute limits if memory accumulated during loading may lead to
    // atomic GC. If there is still room to allocate, keep the current limits.
    // TODO(346498599): Consider removing this bailout.
    DCHECK(!AllocationLimitOvershotByLargeMargin());
    return;
  }

  auto new_limits = ComputeNewAllocationLimits(this);
  size_t new_old_generation_allocation_limit =
      new_limits.old_generation_allocation_limit;
  size_t new_global_allocation_limit = new_limits.global_allocation_limit;

  new_old_generation_allocation_limit = std::max(
      new_old_generation_allocation_limit, old_generation_allocation_limit());
  new_global_allocation_limit =
      std::max(new_global_allocation_limit, global_allocation_limit());
  SetOldGenerationAndGlobalAllocationLimit(new_old_generation_allocation_limit,
                                           new_global_allocation_limit);

  CHECK_EQ(max_global_memory_size_,
           GlobalMemorySizeFromV8Size(max_old_generation_size_));
  CHECK_GE(global_allocation_limit(), old_generation_allocation_limit_);
}

void Heap::CallGCPrologueCallbacks(GCType gc_type, GCCallbackFlags flags,
                                   GCTracer::Scope::ScopeId scope_id) {
  if (gc_prologue_callbacks_.IsEmpty()) return;

  GCCallbacksScope scope(this);
  if (scope.CheckReenter()) {
    RCS_SCOPE(isolate(), RuntimeCallCounterId::kGCPrologueCallback);
    TRACE_GC(tracer(), scope_id);
    HandleScope handle_scope(isolate());
    gc_prologue_callbacks_.Invoke(gc_type, flags);
  }
}

void Heap::CallGCEpilogueCallbacks(GCType gc_type, GCCallbackFlags flags,
                                   GCTracer::Scope::ScopeId scope_id) {
  if (gc_epilogue_callbacks_.IsEmpty()) return;

  GCCallbacksScope scope(this);
  if (scope.CheckReenter()) {
    RCS_SCOPE(isolate(), RuntimeCallCounterId::kGCEpilogueCallback);
    TRACE_GC(tracer(), scope_id);
    HandleScope handle_scope(isolate());
    gc_epilogue_callbacks_.Invoke(gc_type, flags);
  }
}

void Heap::MarkCompact() {
  SetGCState(MARK_COMPACT);

  PROFILE(isolate_, CodeMovingGCEvent());

  UpdateOldGenerationAllocationCounter();
  uint64_t size_of_objects_before_gc = SizeOfObjects();

  mark_compact_collector()->Prepare();

  ms_count_++;
  contexts_disposed_ = 0;

  MarkCompactPrologue();

  mark_compact_collector()->CollectGarbage();

  MarkCompactEpilogue();

  if (v8_flags.allocation_site_pretenuring) {
    EvaluateOldSpaceLocalPretenuring(size_of_objects_before_gc);
  }
  // This should be updated before PostGarbageCollectionProcessing, which
  // can cause another GC. Take into account the objects promoted during
  // GC.
  old_generation_allocation_counter_at_last_gc_ +=
      static_cast<size_t>(promoted_objects_size_);
  old_generation_size_at_last_gc_ = OldGenerationSizeOfObjects();
  old_generation_wasted_at_last_gc_ = OldGenerationWastedBytes();
  global_consumed_memory_at_last_gc_ = GlobalConsumedBytes();
}

void Heap::MinorMarkSweep() {
  DCHECK(v8_flags.minor_ms);
  CHECK_EQ(NOT_IN_GC, gc_state());
  DCHECK(use_new_space());
  DCHECK(!incremental_marking()->IsMajorMarking());

  TRACE_GC(tracer(), GCTracer::Scope::MINOR_MS);

  SetGCState(MINOR_MARK_SWEEP);
  minor_mark_sweep_collector_->CollectGarbage();
  SetGCState(NOT_IN_GC);
}

void Heap::MarkCompactEpilogue() {
  TRACE_GC(tracer(), GCTracer::Scope::MC_EPILOGUE);
  SetGCState(NOT_IN_GC);

  isolate_->counters()->objs_since_last_full()->Set(0);
}

void Heap::MarkCompactPrologue() {
  TRACE_GC(tracer(), GCTracer::Scope::MC_PROLOGUE);
  isolate_->descriptor_lookup_cache()->Clear();
  RegExpResultsCache::Clear(string_split_cache());
  RegExpResultsCache::Clear(regexp_multiple_cache());

  FlushNumberStringCache();
}

void Heap::Scavenge() {
  DCHECK_NOT_NULL(new_space());
  DCHECK_IMPLIES(v8_flags.separate_gc_phases,
                 !incremental_marking()->IsMarking());

  if (v8_flags.trace_incremental_marking &&
      !incremental_marking()->IsStopped()) {
    isolate()->PrintWithTimestamp(
        "[IncrementalMarking] Scavenge during marking.\n");
  }

  TRACE_GC(tracer(), GCTracer::Scope::SCAVENGER_SCAVENGE);
  base::MutexGuard guard(relocation_mutex());
  // Young generation garbage collection is orthogonal from full GC marking. It
  // is possible that objects that are currently being processed for marking are
  // reclaimed in the young generation GC that interleaves concurrent marking.
  // Pause concurrent markers to allow processing them using
  // `UpdateMarkingWorklistAfterYoungGenGC()`.
  ConcurrentMarking::PauseScope pause_js_marking(concurrent_marking());
  CppHeap::PauseConcurrentMarkingScope pause_cpp_marking(
      CppHeap::From(cpp_heap_));

  // Bump-pointer allocations done during scavenge are not real allocations.
  // Pause the inline allocation steps.
  IncrementalMarking::PauseBlackAllocationScope pause_black_allocation(
      incremental_marking());

  SetGCState(SCAVENGE);

  // Implements Cheney's copying algorithm
  scavenger_collector_->CollectGarbage();

  SetGCState(NOT_IN_GC);
}

bool Heap::ExternalStringTable::Contains(Tagged<String> string) {
  for (size_t i = 0; i < young_strings_.size(); ++i) {
    if (young_strings_[i] == string) return true;
  }
  for (size_t i = 0; i < old_strings_.size(); ++i) {
    if (old_strings_[i] == string) return true;
  }
  return false;
}

void Heap::UpdateExternalString(Tagged<String> string, size_t old_payload,
                                size_t new_payload) {
  DCHECK(IsExternalString(string));
  if (v8_flags.enable_third_party_heap) return;

  PageMetadata* page = PageMetadata::FromHeapObject(string);

  if (old_payload > new_payload) {
    page->DecrementExternalBackingStoreBytes(
        ExternalBackingStoreType::kExternalString, old_payload - new_payload);
  } else {
    page->IncrementExternalBackingStoreBytes(
        ExternalBackingStoreType::kExternalString, new_payload - old_payload);
  }
}

Tagged<String> Heap::UpdateYoungReferenceInExternalStringTableEntry(
    Heap* heap, FullObjectSlot p) {
  // This is only used for Scavenger.
  DCHECK(!v8_flags.minor_ms);

  PtrComprCageBase cage_base(heap->isolate());
  Tagged<HeapObject> obj = Cast<HeapObject>(*p);
  MapWord first_word = obj->map_word(cage_base, kRelaxedLoad);

  Tagged<String> new_string;

  if (InFromPage(obj)) {
    if (!first_word.IsForwardingAddress()) {
      // Unreachable external string can be finalized.
      Tagged<String> string = Cast<String>(obj);
      if (!IsExternalString(string, cage_base)) {
        // Original external string has been internalized.
        DCHECK(IsThinString(string, cage_base));
        return Tagged<String>();
      }
      heap->FinalizeExternalString(string);
      return Tagged<String>();
    }
    new_string = Cast<String>(first_word.ToForwardingAddress(obj));
  } else {
    new_string = Cast<String>(obj);
  }

  // String is still reachable.
  if (IsThinString(new_string, cage_base)) {
    // Filtering Thin strings out of the external string table.
    return Tagged<String>();
  } else if (IsExternalString(new_string, cage_base)) {
    MutablePageMetadata::MoveExternalBackingStoreBytes(
        ExternalBackingStoreType::kExternalString,
        PageMetadata::FromAddress((*p).ptr()),
        PageMetadata::FromHeapObject(new_string),
        Cast<ExternalString>(new_string)->ExternalPayloadSize());
    return new_string;
  }

  // Internalization can replace external strings with non-external strings.
  return IsExternalString(new_string, cage_base) ? new_string
                                                 : Tagged<String>();
}

void Heap::ExternalStringTable::VerifyYoung() {
#ifdef DEBUG
  std::set<Tagged<String>> visited_map;
  std::map<MutablePageMetadata*, size_t> size_map;
  ExternalBackingStoreType type = ExternalBackingStoreType::kExternalString;
  for (size_t i = 0; i < young_strings_.size(); ++i) {
    Tagged<String> obj = Cast<String>(Tagged<Object>(young_strings_[i]));
    MutablePageMetadata* mc = MutablePageMetadata::FromHeapObject(obj);
    DCHECK_IMPLIES(!v8_flags.sticky_mark_bits,
                   mc->Chunk()->InYoungGeneration());
    DCHECK(heap_->InYoungGeneration(obj));
    DCHECK(!IsTheHole(obj, heap_->isolate()));
    DCHECK(IsExternalString(obj));
    // Note: we can have repeated elements in the table.
    DCHECK_EQ(0, visited_map.count(obj));
    visited_map.insert(obj);
    size_map[mc] += Cast<ExternalString>(obj)->ExternalPayloadSize();
  }
  for (std::map<MutablePageMetadata*, size_t>::iterator it = size_map.begin();
       it != size_map.end(); it++)
    DCHECK_EQ(it->first->ExternalBackingStoreBytes(type), it->second);
#endif
}

void Heap::ExternalStringTable::Verify() {
#ifdef DEBUG
  std::set<Tagged<String>> visited_map;
  std::map<MutablePageMetadata*, size_t> size_map;
  ExternalBackingStoreType type = ExternalBackingStoreType::kExternalString;
  VerifyYoung();
  for (size_t i = 0; i < old_strings_.size(); ++i) {
    Tagged<String> obj = Cast<String>(Tagged<Object>(old_strings_[i]));
    MutablePageMetadata* mc = MutablePageMetadata::FromHeapObject(obj);
    DCHECK_IMPLIES(!v8_flags.sticky_mark_bits,
                   !mc->Chunk()->InYoungGeneration());
    DCHECK(!heap_->InYoungGeneration(obj));
    DCHECK(!IsTheHole(obj, heap_->isolate()));
    DCHECK(IsExternalString(obj));
    // Note: we can have repeated elements in the table.
    DCHECK_EQ(0, visited_map.count(obj));
    visited_map.insert(obj);
    size_map[mc] += Cast<ExternalString>(obj)->ExternalPayloadSize();
  }
  for (std::map<MutablePageMetadata*, size_t>::iterator it = size_map.begin();
       it != size_map.end(); it++)
    DCHECK_EQ(it->first->ExternalBackingStoreBytes(type), it->second);
#endif
}

void Heap::ExternalStringTable::UpdateYoungReferences(
    Heap::ExternalStringTableUpdaterCallback updater_func) {
  if (young_strings_.empty()) return;

  FullObjectSlot start(young_strings_.data());
  FullObjectSlot end(young_strings_.data() + young_strings_.size());
  FullObjectSlot last = start;

  for (FullObjectSlot p = start; p < end; ++p) {
    Tagged<String> target = updater_func(heap_, p);

    if (target.is_null()) continue;

    DCHECK(IsExternalString(target));

    if (InYoungGeneration(target)) {
      // String is still in new space. Update the table entry.
      last.store(target);
      ++last;
    } else {
      // String got promoted. Move it to the old string list.
      old_strings_.push_back(target);
    }
  }

  DCHECK(last <= end);
  young_strings_.resize(last - start);
  if (v8_flags.verify_heap) {
    VerifyYoung();
  }
}

void Heap::ExternalStringTable::PromoteYoung() {
  old_strings_.reserve(old_strings_.size() + young_strings_.size());
  std::move(std::begin(young_strings_), std::end(young_strings_),
            std::back_inserter(old_strings_));
  young_strings_.clear();
}

void Heap::ExternalStringTable::IterateYoung(RootVisitor* v) {
  if (!young_strings_.empty()) {
    v->VisitRootPointers(
        Root::kExternalStringsTable, nullptr,
        FullObjectSlot(young_strings_.data()),
        FullObjectSlot(young_strings_.data() + young_strings_.size()));
  }
}

void Heap::ExternalStringTable::IterateAll(RootVisitor* v) {
  IterateYoung(v);
  if (!old_strings_.empty()) {
    v->VisitRootPointers(
        Root::kExternalStringsTable, nullptr,
        FullObjectSlot(old_strings_.data()),
        FullObjectSlot(old_strings_.data() + old_strings_.size()));
  }
}

void Heap::UpdateYoungReferencesInExternalStringTable(
    ExternalStringTableUpdaterCallback updater_func) {
  external_string_table_.UpdateYoungReferences(updater_func);
}

void Heap::ExternalStringTable::UpdateReferences(
    Heap::ExternalStringTableUpdaterCallback updater_func) {
  if (!old_strings_.empty()) {
    FullObjectSlot start(old_strings_.data());
    FullObjectSlot end(old_strings_.data() + old_strings_.size());
    for (FullObjectSlot p = start; p < end; ++p)
      p.store(updater_func(heap_, p));
  }

  UpdateYoungReferences(updater_func);
}

void Heap::UpdateReferencesInExternalStringTable(
    ExternalStringTableUpdaterCallback updater_func) {
  external_string_table_.UpdateReferences(updater_func);
}

void Heap::ProcessAllWeakReferences(WeakObjectRetainer* retainer) {
  ProcessNativeContexts(retainer);
  ProcessAllocationSites(retainer);
  ProcessDirtyJSFinalizationRegistries(retainer);
}

void Heap::ProcessNativeContexts(WeakObjectRetainer* retainer) {
  Tagged<Object> head =
      VisitWeakList<Context>(this, native_contexts_list(), retainer);
  // Update the head of the list of contexts.
  set_native_contexts_list(head);
}

void Heap::ProcessAllocationSites(WeakObjectRetainer* retainer) {
  Tagged<Object> allocation_site_obj =
      VisitWeakList<AllocationSite>(this, allocation_sites_list(), retainer);
  set_allocation_sites_list(allocation_site_obj);
}

void Heap::ProcessDirtyJSFinalizationRegistries(WeakObjectRetainer* retainer) {
  Tagged<Object> head = VisitWeakList<JSFinalizationRegistry>(
      this, dirty_js_finalization_registries_list(), retainer);
  set_dirty_js_finalization_registries_list(head);
  // If the list is empty, set the tail to undefined. Otherwise the tail is set
  // by WeakListVisitor<JSFinalizationRegistry>::VisitLiveObject.
  if (IsUndefined(head, isolate())) {
    set_dirty_js_finalization_registries_list_tail(head);
  }
}

void Heap::ProcessWeakListRoots(WeakObjectRetainer* retainer) {
  set_native_contexts_list(retainer->RetainAs(native_contexts_list()));
  set_allocation_sites_list(retainer->RetainAs(allocation_sites_list()));
  set_dirty_js_finalization_registries_list(
      retainer->RetainAs(dirty_js_finalization_registries_list()));
  set_dirty_js_finalization_registries_list_tail(
      retainer->RetainAs(dirty_js_finalization_registries_list_tail()));
}

void Heap::ForeachAllocationSite(
    Tagged<Object> list,
    const std::function<void(Tagged<AllocationSite>)>& visitor) {
  DisallowGarbageCollection no_gc;
  Tagged<Object> current = list;
  while (IsAllocationSite(current)) {
    Tagged<AllocationSite> site = Cast<AllocationSite>(current);
    visitor(site);
    Tagged<Object> current_nested = site->nested_site();
    while (IsAllocationSite(current_nested)) {
      Tagged<AllocationSite> nested_site = Cast<AllocationSite>(current_nested);
      visitor(nested_site);
      current_nested = nested_site->nested_site();
    }
    current = site->weak_next();
  }
}

void Heap::ResetAllAllocationSitesDependentCode(AllocationType allocation) {
  DisallowGarbageCollection no_gc_scope;
  bool marked = false;

  ForeachAllocationSite(
      allocation_sites_list(),
      [&marked, allocation, this](Tagged<AllocationSite> site) {
        if (site->GetAllocationType() == allocation) {
          site->ResetPretenureDecision();
          site->set_deopt_dependent_code(true);
          marked = true;
          pretenuring_handler_.RemoveAllocationSitePretenuringFeedback(site);
          return;
        }
      });
  if (marked) isolate_->stack_guard()->RequestDeoptMarkedAllocationSites();
}

void Heap::EvaluateOldSpaceLocalPretenuring(
    uint64_t size_of_objects_before_gc) {
  uint64_t size_of_objects_after_gc = SizeOfObjects();
  double old_generation_survival_rate =
      (static_cast<double>(size_of_objects_after_gc) * 100) /
      static_cast<double>(size_of_objects_before_gc);

  if (old_generation_survival_rate < kOldSurvivalRateLowThreshold) {
    // Too many objects died in the old generation, pretenuring of wrong
    // allocation sites may be the cause for that. We have to deopt all
    // dependent code registered in the allocation sites to re-evaluate
    // our pretenuring decisions.
    ResetAllAllocationSitesDependentCode(AllocationType::kOld);
    if (v8_flags.trace_pretenuring) {
      PrintF(
          "Deopt all allocation sites dependent code due to low survival "
          "rate in the old generation %f\n",
          old_generation_survival_rate);
    }
  }
}

void Heap::VisitExternalResources(v8::ExternalResourceVisitor* visitor) {
  DisallowGarbageCollection no_gc;
  // All external strings are listed in the external string table.

  class ExternalStringTableVisitorAdapter : public RootVisitor {
   public:
    explicit ExternalStringTableVisitorAdapter(
        Isolate* isolate, v8::ExternalResourceVisitor* visitor)
        : isolate_(isolate), visitor_(visitor) {}
    void VisitRootPointers(Root root, const char* description,
                           FullObjectSlot start, FullObjectSlot end) override {
      for (FullObjectSlot p = start; p < end; ++p) {
        DCHECK(IsExternalString(*p));
        visitor_->VisitExternalString(
            Utils::ToLocal(Handle<String>(Cast<String>(*p), isolate_)));
      }
    }

   private:
    Isolate* isolate_;
    v8::ExternalResourceVisitor* visitor_;
  } external_string_table_visitor(isolate(), visitor);

  external_string_table_.IterateAll(&external_string_table_visitor);
}

static_assert(IsAligned(FixedDoubleArray::kHeaderSize, kDoubleAlignment));

#ifdef V8_COMPRESS_POINTERS
// TODO(ishell, v8:8875): When pointer compression is enabled the kHeaderSize
// is only kTaggedSize aligned but we can keep using unaligned access since
// both x64 and arm64 architectures (where pointer compression supported)
// allow unaligned access to doubles.
static_assert(IsAligned(ByteArray::kHeaderSize, kTaggedSize));
#else
static_assert(IsAligned(ByteArray::kHeaderSize, kDoubleAlignment));
#endif

int Heap::GetMaximumFillToAlign(AllocationAlignment alignment) {
  if (V8_COMPRESS_POINTERS_8GB_BOOL) return 0;
  switch (alignment) {
    case kTaggedAligned:
      return 0;
    case kDoubleAligned:
    case kDoubleUnaligned:
      return kDoubleSize - kTaggedSize;
    default:
      UNREACHABLE();
  }
}

// static
int Heap::GetFillToAlign(Address address, AllocationAlignment alignment) {
  if (V8_COMPRESS_POINTERS_8GB_BOOL) return 0;
  if (alignment == kDoubleAligned && (address & kDoubleAlignmentMask) != 0)
    return kTaggedSize;
  if (alignment == kDoubleUnaligned && (address & kDoubleAlignmentMask) == 0) {
    return kDoubleSize - kTaggedSize;  // No fill if double is always aligned.
  }
  return 0;
}

size_t Heap::GetCodeRangeReservedAreaSize() {
  return CodeRange::GetWritableReservedAreaSize();
}

Tagged<HeapObject> Heap::PrecedeWithFiller(Tagged<HeapObject> object,
                                           int filler_size) {
  CreateFillerObjectAt(object.address(), filler_size);
  return HeapObject::FromAddress(object.address() + filler_size);
}

Tagged<HeapObject> Heap::PrecedeWithFillerBackground(Tagged<HeapObject> object,
                                                     int filler_size) {
  CreateFillerObjectAtBackground(
      WritableFreeSpace::ForNonExecutableMemory(object.address(), filler_size));
  return HeapObject::FromAddress(object.address() + filler_size);
}

Tagged<HeapObject> Heap::AlignWithFillerBackground(
    Tagged<HeapObject> object, int object_size, int allocation_size,
    AllocationAlignment alignment) {
  const int filler_size = allocation_size - object_size;
  DCHECK_LT(0, filler_size);
  const int pre_filler = GetFillToAlign(object.address(), alignment);
  if (pre_filler) {
    object = PrecedeWithFillerBackground(object, pre_filler);
  }
  DCHECK_LE(0, filler_size - pre_filler);
  const int post_filler = filler_size - pre_filler;
  if (post_filler) {
    CreateFillerObjectAtBackground(WritableFreeSpace::ForNonExecutableMemory(
        object.address() + object_size, post_filler));
  }
  return object;
}

void* Heap::AllocateExternalBackingStore(
    const std::function<void*(size_t)>& allocate, size_t byte_length) {
  if (!always_allocate() && new_space()) {
    size_t new_space_backing_store_bytes =
        new_space()->ExternalBackingStoreOverallBytes();
    if (new_space_backing_store_bytes >= 2 * DefaultMaxSemiSpaceSize() &&
        new_space_backing_store_bytes >= byte_length) {
      // Performing a young generation GC amortizes over the allocated backing
      // store bytes and may free enough external bytes for this allocation.
      CollectGarbage(NEW_SPACE,
                     GarbageCollectionReason::kExternalMemoryPressure);
    }
  }
  void* result = allocate(byte_length);
  if (result) return result;
  if (!always_allocate()) {
    for (int i = 0; i < 2; i++) {
      CollectGarbage(OLD_SPACE,
                     GarbageCollectionReason::kExternalMemoryPressure);
      result = allocate(byte_length);
      if (result) return result;
    }
    CollectAllAvailableGarbage(
        GarbageCollectionReason::kExternalMemoryPressure);
  }
  return allocate(byte_length);
}

// When old generation allocation limit is not configured (before the first full
// GC), this method shrinks the initial very large old generation size. This
// method can only shrink allocation limits but not increase it again.
void Heap::ShrinkOldGenerationAllocationLimitIfNotConfigured() {
  if (!old_generation_allocation_limit_configured() &&
      tracer()->SurvivalEventsRecorded()) {
    const size_t minimum_growing_step =
        MemoryController<V8HeapTrait>::MinimumAllocationLimitGrowingStep(
            CurrentHeapGrowingMode());
    size_t new_old_generation_allocation_limit =
        std::max(OldGenerationConsumedBytes() + minimum_growing_step,
                 static_cast<size_t>(
                     static_cast<double>(old_generation_allocation_limit()) *
                     (tracer()->AverageSurvivalRatio() / 100)));
    new_old_generation_allocation_limit = std::min(
        new_old_generation_allocation_limit, old_generation_allocation_limit());
    size_t new_global_allocation_limit = std::max(
        GlobalConsumedBytes() + minimum_growing_step,
        static_cast<size_t>(static_cast<double>(global_allocation_limit()) *
                            (tracer()->AverageSurvivalRatio() / 100)));
    new_global_allocation_limit =
        std::min(new_global_allocation_limit, global_allocation_limit());
    SetOldGenerationAndGlobalAllocationLimit(
        new_old_generation_allocation_limit, new_global_allocation_limit);
    // We need to update limits but still remain in the "not configured" state.
    // The first full GC will configure the heap.
    set_old_generation_allocation_limit_configured(false);
  }
}

void Heap::FlushNumberStringCache() {
  // Flush the number to string cache.
  int len = number_string_cache()->length();
  ReadOnlyRoots roots{isolate()};
  for (int i = 0; i < len; i++) {
    number_string_cache()->set(i, roots.undefined_value(), SKIP_WRITE_BARRIER);
  }
}

namespace {

void CreateFillerObjectAtImpl(const WritableFreeSpace& free_space, Heap* heap,
                              ClearFreedMemoryMode clear_memory_mode) {
  int size = free_space.Size();
  if (size == 0) return;
  DCHECK_IMPLIES(V8_COMPRESS_POINTERS_8GB_BOOL,
                 IsAligned(free_space.Address(), kObjectAlignment8GbHeap));
  DCHECK_IMPLIES(V8_COMPRESS_POINTERS_8GB_BOOL,
                 IsAligned(size, kObjectAlignment8GbHeap));

  // TODO(v8:13070): Filler sizes are irrelevant for 8GB+ heaps. Adding them
  // should be avoided in this mode.
  ReadOnlyRoots roots(heap);
  if (size == kTaggedSize) {
    HeapObject::SetFillerMap(free_space,
                             roots.unchecked_one_pointer_filler_map());
    // Ensure the filler map is properly initialized.
    DCHECK(IsMap(
        HeapObject::FromAddress(free_space.Address())->map(heap->isolate())));
  } else if (size == 2 * kTaggedSize) {
    HeapObject::SetFillerMap(free_space,
                             roots.unchecked_two_pointer_filler_map());
    if (clear_memory_mode == ClearFreedMemoryMode::kClearFreedMemory) {
      free_space.ClearTagged<kTaggedSize>((size / kTaggedSize) - 1);
    }
    // Ensure the filler map is properly initialized.
    DCHECK(IsMap(
        HeapObject::FromAddress(free_space.Address())->map(heap->isolate())));
  } else {
    DCHECK_GT(size, 2 * kTaggedSize);
    HeapObject::SetFillerMap(free_space, roots.unchecked_free_space_map());
    FreeSpace::SetSize(free_space, size, kRelaxedStore);
    if (clear_memory_mode == ClearFreedMemoryMode::kClearFreedMemory) {
      free_space.ClearTagged<2 * kTaggedSize>((size / kTaggedSize) - 2);
    }

    // During bootstrapping we need to create a free space object before its
    // map is initialized. In this case we cannot access the map yet, as it
    // might be null, or not set up properly yet.
    DCHECK_IMPLIES(roots.is_initialized(RootIndex::kFreeSpaceMap),
                   IsMap(HeapObject::FromAddress(free_space.Address())
                             ->map(heap->isolate())));
  }
}

#ifdef DEBUG
void VerifyNoNeedToClearSlots(Address start, Address end) {
  MemoryChunk* chunk = MemoryChunk::FromAddress(start);
  if (chunk->InReadOnlySpace()) return;
  if (!v8_flags.sticky_mark_bits && chunk->InYoungGeneration()) return;
  MutablePageMetadata* mutable_page =
      MutablePageMetadata::cast(chunk->Metadata());
  BaseSpace* space = mutable_page->owner();
  space->heap()->VerifySlotRangeHasNoRecordedSlots(start, end);
}
#else
void VerifyNoNeedToClearSlots(Address start, Address end) {}
#endif  // DEBUG

}  // namespace

void Heap::CreateFillerObjectAtBackground(const WritableFreeSpace& free_space) {
  // TODO(leszeks): Verify that no slots need to be recorded.
  // Do not verify whether slots are cleared here: the concurrent thread is not
  // allowed to access the main thread's remembered set.
  CreateFillerObjectAtRaw(free_space,
                          ClearFreedMemoryMode::kDontClearFreedMemory,
                          ClearRecordedSlots::kNo, VerifyNoSlotsRecorded::kNo);
}

void Heap::CreateFillerObjectAt(Address addr, int size,
                                ClearFreedMemoryMode clear_memory_mode) {
  if (size == 0) return;
  if (MemoryChunk::FromAddress(addr)->executable()) {
    WritableJitPage jit_page(addr, size);
    WritableFreeSpace free_space = jit_page.FreeRange(addr, size);
    CreateFillerObjectAtRaw(free_space, clear_memory_mode,
                            ClearRecordedSlots::kNo,
                            VerifyNoSlotsRecorded::kYes);
  } else {
    WritableFreeSpace free_space =
        WritableFreeSpace::ForNonExecutableMemory(addr, size);
    CreateFillerObjectAtRaw(free_space, clear_memory_mode,
                            ClearRecordedSlots::kNo,
                            VerifyNoSlotsRecorded::kYes);
  }
}

void Heap::CreateFillerObjectAtRaw(
    const WritableFreeSpace& free_space, ClearFreedMemoryMode clear_memory_mode,
    ClearRecordedSlots clear_slots_mode,
    VerifyNoSlotsRecorded verify_no_slots_recorded) {
  // TODO(mlippautz): It would be nice to DCHECK that we never call this
  // with {addr} pointing into large object space; however we currently do,
  // see, e.g., Factory::NewFillerObject and in many tests.
  size_t size = free_space.Size();
  if (size == 0) return;
  CreateFillerObjectAtImpl(free_space, this, clear_memory_mode);
  if (!V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    Address addr = free_space.Address();
    if (clear_slots_mode == ClearRecordedSlots::kYes) {
      ClearRecordedSlotRange(addr, addr + size);
    } else if (verify_no_slots_recorded == VerifyNoSlotsRecorded::kYes) {
      VerifyNoNeedToClearSlots(addr, addr + size);
    }
  }
}

bool Heap::CanMoveObjectStart(Tagged<HeapObject> object) {
  if (!v8_flags.move_object_start) return false;

  // Sampling heap profiler may have a reference to the object.
  if (isolate()->heap_profiler()->is_sampling_allocations()) return false;

  if (IsLargeObject(object)) return false;

  // Compilation jobs may have references to the object.
  if (isolate()->concurrent_recompilation_enabled() &&
      isolate()->optimizing_compile_dispatcher()->HasJobs()) {
    return false;
  }

  // Concurrent marking does not support moving object starts without snapshot
  // protocol.
  //
  // TODO(v8:13726): This can be improved via concurrently reading the contents
  // in the marker at the cost of some complexity.
  if (incremental_marking()->IsMarking()) return false;

  // Concurrent sweeper does not support moving object starts. It assumes that
  // markbits (black regions) and object starts are matching up.
  if (!PageMetadata::FromHeapObject(object)->SweepingDone()) return false;

  return true;
}

bool Heap::IsImmovable(Tagged<HeapObject> object) {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL)
    return third_party_heap::Heap::IsImmovable(object);

  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  return chunk->NeverEvacuate() || chunk->IsLargePage();
}

bool Heap::IsLargeObject(Tagged<HeapObject> object) {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL)
    return third_party_heap::Heap::InLargeObjectSpace(object.address()) ||
           third_party_heap::Heap::InSpace(object.address(), CODE_LO_SPACE);
  return MemoryChunk::FromHeapObject(object)->IsLargePage();
}

#ifdef ENABLE_SLOW_DCHECKS
namespace {

class LeftTrimmerVerifierRootVisitor : public RootVisitor {
 public:
  explicit LeftTrimmerVerifierRootVisitor(Tagged<FixedArrayBase> to_check)
      : to_check_(to_check) {}

  LeftTrimmerVerifierRootVisitor(const LeftTrimmerVerifierRootVisitor&) =
      delete;
  LeftTrimmerVerifierRootVisitor& operator=(
      const LeftTrimmerVerifierRootVisitor&) = delete;

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot p = start; p < end; ++p) {
      // V8_EXTERNAL_CODE_SPACE specific: we might be comparing
      // InstructionStream object with non-InstructionStream object here and it
      // might produce false positives because operator== for tagged values
      // compares only lower 32 bits when pointer compression is enabled.
      DCHECK_NE((*p).ptr(), to_check_.ptr());
    }
  }

  void VisitRootPointers(Root root, const char* description,
                         OffHeapObjectSlot start,
                         OffHeapObjectSlot end) override {
    DCHECK(root == Root::kStringTable ||
           root == Root::kSharedStructTypeRegistry);
    // We can skip iterating the string table and shared struct type registry,
    // they don't point to any fixed arrays.
  }

 private:
  Tagged<FixedArrayBase> to_check_;
};
}  // namespace
#endif  // ENABLE_SLOW_DCHECKS

namespace {
bool MayContainRecordedSlots(Tagged<HeapObject> object) {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) return false;
  // New space object do not have recorded slots.
  if (Heap::InYoungGeneration(object)) {
    return false;
  }
  // Allowlist objects that definitely do not have pointers.
  if (IsByteArray(object) || IsFixedDoubleArray(object)) return false;
  // Conservatively return true for other objects.
  return true;
}
}  // namespace

void Heap::OnMoveEvent(Tagged<HeapObject> source, Tagged<HeapObject> target,
                       int size_in_bytes) {
  HeapProfiler* heap_profiler = isolate_->heap_profiler();
  if (heap_profiler->is_tracking_object_moves()) {
    heap_profiler->ObjectMoveEvent(source.address(), target.address(),
                                   size_in_bytes, /*is_embedder_object=*/false);
  }
  for (auto& tracker : allocation_trackers_) {
    tracker->MoveEvent(source.address(), target.address(), size_in_bytes);
  }
  if (IsSharedFunctionInfo(target, isolate_)) {
    LOG_CODE_EVENT(isolate_, SharedFunctionInfoMoveEvent(source.address(),
                                                         target.address()));
  } else if (IsNativeContext(target, isolate_)) {
    if (isolate_->current_embedder_state() != nullptr) {
      isolate_->current_embedder_state()->OnMoveEvent(source.address(),
                                                      target.address());
    }
    PROFILE(isolate_,
            NativeContextMoveEvent(source.address(), target.address()));
  } else if (IsMap(target, isolate_)) {
    LOG(isolate_, MapMoveEvent(Cast<Map>(source), Cast<Map>(target)));
  }
}

Tagged<FixedArrayBase> Heap::LeftTrimFixedArray(Tagged<FixedArrayBase> object,
                                                int elements_to_trim) {
  if (elements_to_trim == 0) {
    // This simplifies reasoning in the rest of the function.
    return object;
  }
  CHECK(!object.is_null());
  DCHECK(CanMoveObjectStart(object));
  // Add custom visitor to concurrent marker if new left-trimmable type
  // is added.
  DCHECK(IsFixedArray(object) || IsFixedDoubleArray(object));
  const int element_size = IsFixedArray(object) ? kTaggedSize : kDoubleSize;
  const int bytes_to_trim = elements_to_trim * element_size;
  Tagged<Map> map = object->map();

  // For now this trick is only applied to fixed arrays which may be in new
  // space or old space. In a large object space the object's start must
  // coincide with chunk and thus the trick is just not applicable.
  DCHECK(!IsLargeObject(object));
  DCHECK(object->map() != ReadOnlyRoots(this).fixed_cow_array_map());

  static_assert(FixedArrayBase::kMapOffset == 0);
  static_assert(FixedArrayBase::kLengthOffset == kTaggedSize);
  static_assert(FixedArrayBase::kHeaderSize == 2 * kTaggedSize);

  const int len = object->length();
  DCHECK(elements_to_trim <= len);

  // Calculate location of new array start.
  Address old_start = object.address();
  Address new_start = old_start + bytes_to_trim;

  // Technically in new space this write might be omitted (except for
  // debug mode which iterates through the heap), but to play safer
  // we still do it.
  CreateFillerObjectAtRaw(
      WritableFreeSpace::ForNonExecutableMemory(old_start, bytes_to_trim),
      ClearFreedMemoryMode::kClearFreedMemory,
      MayContainRecordedSlots(object) ? ClearRecordedSlots::kYes
                                      : ClearRecordedSlots::kNo,
      VerifyNoSlotsRecorded::kYes);

  // Initialize header of the trimmed array. Since left trimming is only
  // performed on pages which are not concurrently swept creating a filler
  // object does not require synchronization.
  RELAXED_WRITE_FIELD(object, bytes_to_trim,
                      Tagged<Object>(MapWord::FromMap(map).ptr()));
  RELAXED_WRITE_FIELD(object, bytes_to_trim + kTaggedSize,
                      Smi::FromInt(len - elements_to_trim));

  Tagged<FixedArrayBase> new_object =
      Cast<FixedArrayBase>(HeapObject::FromAddress(new_start));

  if (isolate()->log_object_relocation()) {
    // Notify the heap profiler of change in object layout.
    OnMoveEvent(object, new_object, new_object->Size());
  }

#ifdef ENABLE_SLOW_DCHECKS
  if (v8_flags.enable_slow_asserts) {
    // Make sure the stack or other roots (e.g., Handles) don't contain pointers
    // to the original FixedArray (which is now the filler object).
    std::optional<IsolateSafepointScope> safepoint_scope;

    {
      AllowGarbageCollection allow_gc;
      safepoint_scope.emplace(this);
    }

    LeftTrimmerVerifierRootVisitor root_visitor(object);
    ReadOnlyRoots(this).Iterate(&root_visitor);

    // Stale references are allowed in some locations. IterateRoots() uses
    // ClearStaleLeftTrimmedPointerVisitor internally to clear such references
    // beforehand.
    IterateRoots(&root_visitor,
                 base::EnumSet<SkipRoot>{SkipRoot::kConservativeStack});
  }
#endif  // ENABLE_SLOW_DCHECKS

  return new_object;
}

template <typename Array>
void Heap::RightTrimArray(Tagged<Array> object, int new_capacity,
                          int old_capacity) {
  DCHECK_EQ(old_capacity, object->capacity());
  DCHECK_LT(new_capacity, old_capacity);
  DCHECK_GE(new_capacity, 0);

  if constexpr (Array::kElementsAreMaybeObject) {
    // For MaybeObject elements, this function is safe to use only at the end
    // of the mark compact collection: When marking, we record the weak slots,
    // and shrinking invalidates them.
    DCHECK_EQ(gc_state(), MARK_COMPACT);
  }

  const int bytes_to_trim =
      (old_capacity - new_capacity) * Array::Shape::kElementSize;

  // Calculate location of new array end.
  const int old_size = Array::SizeFor(old_capacity);
  DCHECK_EQ(object->AllocatedSize(), old_size);
  Address old_end = object.address() + old_size;
  Address new_end = old_end - bytes_to_trim;

  const bool clear_slots = MayContainRecordedSlots(object);

  // Technically in new space this write might be omitted (except for debug
  // mode which iterates through the heap), but to play safer we still do it.
  // We do not create a filler for objects in a large object space.
  if (!IsLargeObject(object)) {
    NotifyObjectSizeChange(
        object, old_size, old_size - bytes_to_trim,
        clear_slots ? ClearRecordedSlots::kYes : ClearRecordedSlots::kNo);
    Tagged<HeapObject> filler = HeapObject::FromAddress(new_end);
    // Clear the mark bits of the black area that belongs now to the filler.
    // This is an optimization. The sweeper will release black fillers anyway.
    if (incremental_marking()->black_allocation() &&
        marking_state()->IsMarked(filler)) {
      PageMetadata* page = PageMetadata::FromAddress(new_end);
      page->marking_bitmap()->ClearRange<AccessMode::ATOMIC>(
          MarkingBitmap::AddressToIndex(new_end),
          MarkingBitmap::LimitAddressToIndex(new_end + bytes_to_trim));
    }
  } else if (clear_slots) {
    // Large objects are not swept, so it is not necessary to clear the
    // recorded slot.
    MemsetTagged(ObjectSlot(new_end), Tagged<Object>(kClearedFreeMemoryValue),
                 (old_end - new_end) / kTaggedSize);
  }

  // Initialize header of the trimmed array. We are storing the new capacity
  // using release store after creating a filler for the left-over space to
  // avoid races with the sweeper thread.
  object->set_capacity(new_capacity, kReleaseStore);

  // Notify the heap object allocation tracker of change in object layout. The
  // array may not be moved during GC, and size has to be adjusted nevertheless.
  for (auto& tracker : allocation_trackers_) {
    tracker->UpdateObjectSizeEvent(object.address(),
                                   Array::SizeFor(new_capacity));
  }
}

#define DEF_RIGHT_TRIM(T)                                     \
  template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void     \
  Heap::RightTrimArray<T>(Tagged<T> object, int new_capacity, \
                          int old_capacity);
RIGHT_TRIMMABLE_ARRAY_LIST(DEF_RIGHT_TRIM)
#undef DEF_RIGHT_TRIM

void Heap::MakeHeapIterable() {
  EnsureSweepingCompleted(SweepingForcedFinalizationMode::kV8Only);

  MakeLinearAllocationAreasIterable();
}

void Heap::MakeLinearAllocationAreasIterable() {
  allocator()->MakeLinearAllocationAreasIterable();

  safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->MakeLinearAllocationAreasIterable();
  });

  if (isolate()->is_shared_space_isolate()) {
    isolate()->global_safepoint()->IterateClientIsolates([](Isolate* client) {
      client->heap()->MakeLinearAllocationAreasIterable();
    });
  }
}

void Heap::FreeLinearAllocationAreas() {
  FreeMainThreadLinearAllocationAreas();

  safepoint()->IterateLocalHeaps(
      [](LocalHeap* local_heap) { local_heap->FreeLinearAllocationAreas(); });

  if (isolate()->is_shared_space_isolate()) {
    isolate()->global_safepoint()->IterateClientIsolates(
        [](Isolate* client) { client->heap()->FreeLinearAllocationAreas(); });
  }
}

void Heap::FreeMainThreadLinearAllocationAreas() {
  allocator()->FreeLinearAllocationAreas();
}

void Heap::MarkSharedLinearAllocationAreasBlack() {
  allocator()->MarkSharedLinearAllocationAreasBlack();
  main_thread_local_heap()->MarkSharedLinearAllocationAreasBlack();

  safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->MarkSharedLinearAllocationAreasBlack();
  });
}

void Heap::UnmarkSharedLinearAllocationAreas() {
  allocator()->UnmarkSharedLinearAllocationAreas();
  main_thread_local_heap()->UnmarkSharedLinearAllocationsArea();
  safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->UnmarkSharedLinearAllocationsArea();
  });
}

void Heap::Unmark() {
  DCHECK(v8_flags.sticky_mark_bits);
  DCHECK_NULL(new_space());

  auto unmark_space = [](auto& space) {
    for (auto* page : space) {
      page->marking_bitmap()->template Clear<AccessMode::NON_ATOMIC>();
      page->Chunk()->SetMajorGCInProgress();
      page->SetLiveBytes(0);
    }
  };

  unmark_space(*old_space());
  unmark_space(*lo_space());

  if (isolate()->is_shared_space_isolate()) {
    unmark_space(*shared_space());
    unmark_space(*shared_lo_space());
  }

  {
    RwxMemoryWriteScope scope("For writing flags.");
    unmark_space(*code_space());
    unmark_space(*code_lo_space());
  }

  unmark_space(*trusted_space());
  unmark_space(*trusted_lo_space());
}

void Heap::DeactivateMajorGCInProgressFlag() {
  DCHECK(v8_flags.sticky_mark_bits);
  DCHECK_NULL(new_space());

  auto deactivate_space = [](auto& space) {
    for (auto* metadata : space) {
      metadata->Chunk()->ResetMajorGCInProgress();
    }
  };

  deactivate_space(*old_space());
  deactivate_space(*lo_space());

  {
    RwxMemoryWriteScope scope("For writing flags.");
    deactivate_space(*code_space());
    deactivate_space(*code_lo_space());
  }

  if (isolate()->is_shared_space_isolate()) {
    deactivate_space(*shared_space());
    deactivate_space(*shared_lo_space());
  }

  deactivate_space(*trusted_space());
  deactivate_space(*trusted_lo_space());
}

namespace {

double ComputeMutatorUtilizationImpl(double mutator_speed, double gc_speed) {
  constexpr double kMinMutatorUtilization = 0.0;
  constexpr double kConservativeGcSpeedInBytesPerMillisecond = 200000;
  if (mutator_speed == 0) return kMinMutatorUtilization;
  if (gc_speed == 0) gc_speed = kConservativeGcSpeedInBytesPerMillisecond;
  // Derivation:
  // mutator_utilization = mutator_time / (mutator_time + gc_time)
  // mutator_time = 1 / mutator_speed
  // gc_time = 1 / gc_speed
  // mutator_utilization = (1 / mutator_speed) /
  //                       (1 / mutator_speed + 1 / gc_speed)
  // mutator_utilization = gc_speed / (mutator_speed + gc_speed)
  return gc_speed / (mutator_speed + gc_speed);
}

}  // namespace

double Heap::ComputeMutatorUtilization(const char* tag, double mutator_speed,
                                       double gc_speed) {
  double result = ComputeMutatorUtilizationImpl(mutator_speed, gc_speed);
  if (v8_flags.trace_mutator_utilization) {
    isolate()->PrintWithTimestamp(
        "%s mutator utilization = %.3f ("
        "mutator_speed=%.f, gc_speed=%.f)\n",
        tag, result, mutator_speed, gc_speed);
  }
  return result;
}

bool Heap::HasLowYoungGenerationAllocationRate() {
  double mu = ComputeMutatorUtilization(
      "Young generation",
      tracer()->NewSpaceAllocationThroughputInBytesPerMillisecond(),
      tracer()->YoungGenerationSpeedInBytesPerMillisecond(
          YoungGenerationSpeedMode::kOnlyAtomicPause));
  constexpr double kHighMutatorUtilization = 0.993;
  return mu > kHighMutatorUtilization;
}

bool Heap::HasLowOldGenerationAllocationRate() {
  double mu = ComputeMutatorUtilization(
      "Old generation",
      tracer()->OldGenerationAllocationThroughputInBytesPerMillisecond(),
      tracer()->CombinedMarkCompactSpeedInBytesPerMillisecond());
  const double kHighMutatorUtilization = 0.993;
  return mu > kHighMutatorUtilization;
}

bool Heap::HasLowEmbedderAllocationRate() {
  double mu = ComputeMutatorUtilization(
      "Embedder",
      tracer()->CurrentEmbedderAllocationThroughputInBytesPerMillisecond(),
      tracer()->EmbedderSpeedInBytesPerMillisecond());
  const double kHighMutatorUtilization = 0.993;
  return mu > kHighMutatorUtilization;
}

bool Heap::HasLowAllocationRate() {
  return HasLowYoungGenerationAllocationRate() &&
         HasLowOldGenerationAllocationRate() && HasLowEmbedderAllocationRate();
}

bool Heap::IsIneffectiveMarkCompact(size_t old_generation_size,
                                    double mutator_utilization) {
  const double kHighHeapPercentage = 0.8;
  const double kLowMutatorUtilization = 0.4;
  return old_generation_size >=
             kHighHeapPercentage * max_old_generation_size() &&
         mutator_utilization < kLowMutatorUtilization;
}

namespace {
static constexpr int kMaxConsecutiveIneffectiveMarkCompacts = 4;
}

void Heap::CheckIneffectiveMarkCompact(size_t old_generation_size,
                                       double mutator_utilization) {
  if (!v8_flags.detect_ineffective_gcs_near_heap_limit) return;
  if (!IsIneffectiveMarkCompact(old_generation_size, mutator_utilization)) {
    consecutive_ineffective_mark_compacts_ = 0;
    return;
  }
  ++consecutive_ineffective_mark_compacts_;
  if (consecutive_ineffective_mark_compacts_ ==
      kMaxConsecutiveIneffectiveMarkCompacts) {
    if (InvokeNearHeapLimitCallback()) {
      // The callback increased the heap limit.
      consecutive_ineffective_mark_compacts_ = 0;
      return;
    }
  }
}

void Heap::ReportIneffectiveMarkCompactIfNeeded() {
  DCHECK_IMPLIES(!v8_flags.detect_ineffective_gcs_near_heap_limit,
                 consecutive_ineffective_mark_compacts_ == 0);
  if (consecutive_ineffective_mark_compacts_ ==
      kMaxConsecutiveIneffectiveMarkCompacts) {
    if (v8_flags.heap_snapshot_on_oom) {
      isolate()->heap_profiler()->WriteSnapshotToDiskAfterGC();
    }
    FatalProcessOutOfMemory("Ineffective mark-compacts near heap limit");
  }
}

bool Heap::HasHighFragmentation() {
  const size_t used = OldGenerationSizeOfObjects();
  const size_t committed = CommittedOldGenerationMemory();

  // Background thread allocation could result in committed memory being less
  // than used memory in some situations.
  if (committed < used) return false;

  constexpr size_t kSlack = 16 * MB;

  // Fragmentation is high if committed > 2 * used + kSlack.
  // Rewrite the expression to avoid overflow.
  return committed - used > used + kSlack;
}

bool Heap::ShouldOptimizeForMemoryUsage() {
  const size_t kOldGenerationSlack = max_old_generation_size() / 8;
  return v8_flags.optimize_for_size || isolate()->EfficiencyModeEnabled() ||
         HighMemoryPressure() || !CanExpandOldGeneration(kOldGenerationSlack);
}

class ActivateMemoryReducerTask : public CancelableTask {
 public:
  explicit ActivateMemoryReducerTask(Heap* heap)
      : CancelableTask(heap->isolate()), heap_(heap) {}

  ~ActivateMemoryReducerTask() override = default;
  ActivateMemoryReducerTask(const ActivateMemoryReducerTask&) = delete;
  ActivateMemoryReducerTask& operator=(const ActivateMemoryReducerTask&) =
      delete;

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override {
    heap_->ActivateMemoryReducerIfNeededOnMainThread();
  }

  Heap* heap_;
};

void Heap::ActivateMemoryReducerIfNeeded() {
  if (memory_reducer_ == nullptr) return;
  // This method may be called from any thread. Post a task to run it on the
  // isolate's main thread to avoid synchronization.
  task_runner_->PostTask(std::make_unique<ActivateMemoryReducerTask>(this));
}

void Heap::ActivateMemoryReducerIfNeededOnMainThread() {
  // Activate memory reducer when switching to background if
  // - there was no mark compact since the start.
  // - the committed memory can be potentially reduced.
  // 2 pages for the old, code, and map space + 1 page for new space.
  const int kMinCommittedMemory = 7 * PageMetadata::kPageSize;
  if (ms_count_ == 0 && CommittedMemory() > kMinCommittedMemory &&
      isolate()->is_backgrounded()) {
    memory_reducer_->NotifyPossibleGarbage();
  }
}

Heap::ResizeNewSpaceMode Heap::ShouldResizeNewSpace() {
  if (ShouldReduceMemory()) {
    return (v8_flags.predictable) ? ResizeNewSpaceMode::kNone
                                  : ResizeNewSpaceMode::kShrink;
  }

  static const size_t kLowAllocationThroughput = 1000;
  const double allocation_throughput =
      tracer_->CurrentAllocationThroughputInBytesPerMillisecond();
  const bool should_shrink = !v8_flags.predictable &&
                             (allocation_throughput != 0) &&
                             (allocation_throughput < kLowAllocationThroughput);

  const bool should_grow =
      (new_space_->TotalCapacity() < new_space_->MaximumCapacity()) &&
      (survived_since_last_expansion_ > new_space_->TotalCapacity());

  if (should_grow) survived_since_last_expansion_ = 0;

  if (should_grow == should_shrink) return ResizeNewSpaceMode::kNone;
  return should_grow ? ResizeNewSpaceMode::kGrow : ResizeNewSpaceMode::kShrink;
}

void Heap::ExpandNewSpaceSize() {
  // Grow the size of new space if there is room to grow, and enough data
  // has survived scavenge since the last expansion.
  new_space_->Grow();
  new_lo_space()->SetCapacity(new_space()->TotalCapacity());
}

void Heap::ReduceNewSpaceSize() {
  // MinorMS shrinks new space as part of sweeping.
  if (!v8_flags.minor_ms) {
    SemiSpaceNewSpace::From(new_space())->Shrink();
  } else {
    paged_new_space()->FinishShrinking();
  }
  new_lo_space_->SetCapacity(new_space()->TotalCapacity());
}

size_t Heap::NewSpaceSize() {
  if (v8_flags.sticky_mark_bits) {
    return sticky_space()->young_objects_size();
  }
  return new_space() ? new_space()->Size() : 0;
}

size_t Heap::NewSpaceCapacity() const {
  if (v8_flags.sticky_mark_bits) {
    return sticky_space()->Capacity() - sticky_space()->young_objects_size();
  }
  return new_space() ? new_space()->Capacity() : 0;
}

size_t Heap::NewSpaceTargetCapacity() const {
  if (v8_flags.sticky_mark_bits) {
    // TODO(333906585): Adjust target capacity for new sticky-space.
    return sticky_space()->Capacity() - sticky_space()->young_objects_size();
  }
  return new_space() ? new_space()->TotalCapacity() : 0;
}

void Heap::FinalizeIncrementalMarkingAtomically(
    GarbageCollectionReason gc_reason) {
  DCHECK(!incremental_marking()->IsStopped());
  CollectAllGarbage(current_gc_flags_, gc_reason, current_gc_callback_flags_);
}

void Heap::InvokeIncrementalMarkingPrologueCallbacks() {
  AllowGarbageCollection allow_allocation;
  VMState<EXTERNAL> state(isolate_);
  CallGCPrologueCallbacks(kGCTypeIncrementalMarking, kNoGCCallbackFlags,
                          GCTracer::Scope::MC_INCREMENTAL_EXTERNAL_PROLOGUE);
}

void Heap::InvokeIncrementalMarkingEpilogueCallbacks() {
  AllowGarbageCollection allow_allocation;
  VMState<EXTERNAL> state(isolate_);
  CallGCEpilogueCallbacks(kGCTypeIncrementalMarking, kNoGCCallbackFlags,
                          GCTracer::Scope::MC_INCREMENTAL_EXTERNAL_EPILOGUE);
}

namespace {
thread_local Address pending_layout_change_object_address = kNullAddress;

#ifdef V8_ENABLE_SANDBOX
class ExternalPointerSlotInvalidator : public ObjectVisitor {
 public:
  explicit ExternalPointerSlotInvalidator(Isolate* isolate)
      : isolate_(isolate) {}

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override {}
  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) override {}
  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {}
  void VisitMapPointer(Tagged<HeapObject> host) override {}

  void VisitExternalPointer(Tagged<HeapObject> host,
                            ExternalPointerSlot slot) override {
    DCHECK_EQ(target_, host);
    ExternalPointerTable::Space* space =
        IsolateForSandbox(isolate_).GetExternalPointerTableSpaceFor(
            slot.tag(), host.address());
    space->NotifyExternalPointerFieldInvalidated(slot.address(), slot.tag());
    num_invalidated_slots++;
  }

  int Visit(Tagged<HeapObject> target) {
    target_ = target;
    num_invalidated_slots = 0;
    target->IterateFast(isolate_, this);
    return num_invalidated_slots;
  }

 private:
  Isolate* isolate_;
  Tagged<HeapObject> target_;
  int num_invalidated_slots = 0;
};
#endif  // V8_ENABLE_SANDBOX

}  // namespace

void Heap::NotifyObjectLayoutChange(
    Tagged<HeapObject> object, const DisallowGarbageCollection&,
    InvalidateRecordedSlots invalidate_recorded_slots,
    InvalidateExternalPointerSlots invalidate_external_pointer_slots,
    int new_size) {
  if (invalidate_recorded_slots == InvalidateRecordedSlots::kYes) {
    const bool may_contain_recorded_slots = MayContainRecordedSlots(object);
    MutablePageMetadata* const chunk =
        MutablePageMetadata::FromHeapObject(object);
    // Do not remove the recorded slot in the map word as this one can never be
    // invalidated.
    const Address clear_range_start = object.address() + kTaggedSize;
    // Only slots in the range of the new object size (which is potentially
    // smaller than the original one) can be invalidated. Clearing of recorded
    // slots up to the original object size even conflicts with concurrent
    // sweeping.
    const Address clear_range_end = object.address() + new_size;

    if (incremental_marking()->IsMarking()) {
      ExclusiveObjectLock::Lock(object);
      DCHECK_EQ(pending_layout_change_object_address, kNullAddress);
      pending_layout_change_object_address = object.address();
      if (may_contain_recorded_slots && incremental_marking()->IsCompacting()) {
        RememberedSet<OLD_TO_OLD>::RemoveRange(
            chunk, clear_range_start, clear_range_end,
            SlotSet::EmptyBucketMode::KEEP_EMPTY_BUCKETS);
      }
    }

    if (may_contain_recorded_slots) {
      RememberedSet<OLD_TO_NEW>::RemoveRange(
          chunk, clear_range_start, clear_range_end,
          SlotSet::EmptyBucketMode::KEEP_EMPTY_BUCKETS);
      RememberedSet<OLD_TO_NEW_BACKGROUND>::RemoveRange(
          chunk, clear_range_start, clear_range_end,
          SlotSet::EmptyBucketMode::KEEP_EMPTY_BUCKETS);
      RememberedSet<OLD_TO_SHARED>::RemoveRange(
          chunk, clear_range_start, clear_range_end,
          SlotSet::EmptyBucketMode::KEEP_EMPTY_BUCKETS);
    }

    DCHECK(!chunk->InTrustedSpace());
  }

  // During external pointer table compaction, the external pointer table
  // records addresses of fields that index into the external pointer table. As
  // such, it needs to be informed when such a field is invalidated.
  if (invalidate_external_pointer_slots ==
      InvalidateExternalPointerSlots::kYes) {
    // Currently, the only time this function receives
    // InvalidateExternalPointerSlots::kYes is when an external string
    // transitions to a thin string.  If this ever changed to happen for array
    // buffer extension slots, we would have to run the invalidator in
    // pointer-compression-but-no-sandbox configurations as well.
    DCHECK(IsString(object));
#ifdef V8_ENABLE_SANDBOX
    if (V8_ENABLE_SANDBOX_BOOL) {
      ExternalPointerSlotInvalidator slot_invalidator(isolate());
      int num_invalidated_slots = slot_invalidator.Visit(object);
      USE(num_invalidated_slots);
      DCHECK_GT(num_invalidated_slots, 0);
    }

    // During concurrent marking for a minor GC, the heap also builds up a
    // RememberedSet of external pointer field locations, and uses that set to
    // evacuate external pointer table entries when promoting objects.  Here we
    // would need to invalidate that set too; until we do, assert that
    // NotifyObjectLayoutChange is never called on young objects.
    CHECK(!InYoungGeneration(object));
#endif
  }

#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    HeapVerifier::SetPendingLayoutChangeObject(this, object);
  }
#endif
}

// static
void Heap::NotifyObjectLayoutChangeDone(Tagged<HeapObject> object) {
  if (pending_layout_change_object_address != kNullAddress) {
    DCHECK_EQ(pending_layout_change_object_address, object.address());
    ExclusiveObjectLock::Unlock(object);
    pending_layout_change_object_address = kNullAddress;
  }
}

void Heap::NotifyObjectSizeChange(Tagged<HeapObject> object, int old_size,
                                  int new_size,
                                  ClearRecordedSlots clear_recorded_slots) {
  old_size = ALIGN_TO_ALLOCATION_ALIGNMENT(old_size);
  new_size = ALIGN_TO_ALLOCATION_ALIGNMENT(new_size);
  DCHECK_LE(new_size, old_size);
  DCHECK(!IsLargeObject(object));
  if (new_size == old_size) return;

  const bool is_main_thread = LocalHeap::Current() == nullptr;

  DCHECK_IMPLIES(!is_main_thread,
                 clear_recorded_slots == ClearRecordedSlots::kNo);

  const auto verify_no_slots_recorded =
      is_main_thread ? VerifyNoSlotsRecorded::kYes : VerifyNoSlotsRecorded::kNo;

  const auto clear_memory_mode = ClearFreedMemoryMode::kDontClearFreedMemory;

  const Address filler = object.address() + new_size;
  const int filler_size = old_size - new_size;
  CreateFillerObjectAtRaw(
      WritableFreeSpace::ForNonExecutableMemory(filler, filler_size),
      clear_memory_mode, clear_recorded_slots, verify_no_slots_recorded);
}

double Heap::MonotonicallyIncreasingTimeInMs() const {
  return V8::GetCurrentPlatform()->MonotonicallyIncreasingTime() *
         static_cast<double>(base::Time::kMillisecondsPerSecond);
}

#if DEBUG
void Heap::VerifyNewSpaceTop() {
  if (!new_space()) return;
  allocator()->new_space_allocator()->Verify();
}
#endif  // DEBUG

class MemoryPressureInterruptTask : public CancelableTask {
 public:
  explicit MemoryPressureInterruptTask(Heap* heap)
      : CancelableTask(heap->isolate()), heap_(heap) {}

  ~MemoryPressureInterruptTask() override = default;
  MemoryPressureInterruptTask(const MemoryPressureInterruptTask&) = delete;
  MemoryPressureInterruptTask& operator=(const MemoryPressureInterruptTask&) =
      delete;

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override { heap_->CheckMemoryPressure(); }

  Heap* heap_;
};

void Heap::CheckMemoryPressure() {
  if (HighMemoryPressure()) {
    // The optimizing compiler may be unnecessarily holding on to memory.
    isolate()->AbortConcurrentOptimization(BlockingBehavior::kDontBlock);
  }
  // Reset the memory pressure level to avoid recursive GCs triggered by
  // CheckMemoryPressure from AdjustAmountOfExternalMemory called by
  // the finalizers.
  MemoryPressureLevel memory_pressure_level = memory_pressure_level_.exchange(
      MemoryPressureLevel::kNone, std::memory_order_relaxed);
  if (memory_pressure_level == MemoryPressureLevel::kCritical) {
    TRACE_EVENT0("devtools.timeline,v8", "V8.CheckMemoryPressure");
    CollectGarbageOnMemoryPressure();
  } else if (memory_pressure_level == MemoryPressureLevel::kModerate) {
    if (v8_flags.incremental_marking && incremental_marking()->IsStopped()) {
      TRACE_EVENT0("devtools.timeline,v8", "V8.CheckMemoryPressure");
      StartIncrementalMarking(GCFlag::kReduceMemoryFootprint,
                              GarbageCollectionReason::kMemoryPressure);
    }
  }
}

void Heap::CollectGarbageOnMemoryPressure() {
  const int kGarbageThresholdInBytes = 8 * MB;
  const double kGarbageThresholdAsFractionOfTotalMemory = 0.1;
  // This constant is the maximum response time in RAIL performance model.
  const double kMaxMemoryPressurePauseMs = 100;

  double start = MonotonicallyIncreasingTimeInMs();
  CollectAllGarbage(GCFlag::kReduceMemoryFootprint,
                    GarbageCollectionReason::kMemoryPressure,
                    kGCCallbackFlagCollectAllAvailableGarbage);
  EagerlyFreeExternalMemoryAndWasmCode();
  double end = MonotonicallyIncreasingTimeInMs();

  // Estimate how much memory we can free.
  int64_t potential_garbage =
      (CommittedMemory() - SizeOfObjects()) + external_memory_.total();
  // If we can potentially free large amount of memory, then start GC right
  // away instead of waiting for memory reducer.
  if (potential_garbage >= kGarbageThresholdInBytes &&
      potential_garbage >=
          CommittedMemory() * kGarbageThresholdAsFractionOfTotalMemory) {
    // If we spent less than half of the time budget, then perform full GC
    // Otherwise, start incremental marking.
    if (end - start < kMaxMemoryPressurePauseMs / 2) {
      CollectAllGarbage(GCFlag::kReduceMemoryFootprint,
                        GarbageCollectionReason::kMemoryPressure,
                        kGCCallbackFlagCollectAllAvailableGarbage);
    } else {
      if (v8_flags.incremental_marking && incremental_marking()->IsStopped()) {
        StartIncrementalMarking(GCFlag::kReduceMemoryFootprint,
                                GarbageCollectionReason::kMemoryPressure);
      }
    }
  }
}

void Heap::MemoryPressureNotification(MemoryPressureLevel level,
                                      bool is_isolate_locked) {
  TRACE_EVENT1("devtools.timeline,v8", "V8.MemoryPressureNotification", "level",
               static_cast<int>(level));
  MemoryPressureLevel previous =
      memory_pressure_level_.exchange(level, std::memory_order_relaxed);
  if ((previous != MemoryPressureLevel::kCritical &&
       level == MemoryPressureLevel::kCritical) ||
      (previous == MemoryPressureLevel::kNone &&
       level == MemoryPressureLevel::kModerate)) {
    if (is_isolate_locked) {
      CheckMemoryPressure();
    } else {
      ExecutionAccess access(isolate());
      isolate()->stack_guard()->RequestGC();
      task_runner_->PostTask(
          std::make_unique<MemoryPressureInterruptTask>(this));
    }
  }
}

void Heap::EagerlyFreeExternalMemoryAndWasmCode() {
#if V8_ENABLE_WEBASSEMBLY
  if (v8_flags.flush_liftoff_code) {
    // Flush Liftoff code and record the flushed code size.
    auto [code_size, metadata_size] = wasm::GetWasmEngine()->FlushLiftoffCode();
    isolate_->counters()->wasm_flushed_liftoff_code_size_bytes()->AddSample(
        static_cast<int>(code_size));
    isolate_->counters()->wasm_flushed_liftoff_metadata_size_bytes()->AddSample(
        static_cast<int>(metadata_size));
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  CompleteArrayBufferSweeping(this);
}

void Heap::AddNearHeapLimitCallback(v8::NearHeapLimitCallback callback,
                                    void* data) {
  const size_t kMaxCallbacks = 100;
  CHECK_LT(near_heap_limit_callbacks_.size(), kMaxCallbacks);
  for (auto callback_data : near_heap_limit_callbacks_) {
    CHECK_NE(callback_data.first, callback);
  }
  near_heap_limit_callbacks_.push_back(std::make_pair(callback, data));
}

void Heap::RemoveNearHeapLimitCallback(v8::NearHeapLimitCallback callback,
                                       size_t heap_limit) {
  for (size_t i = 0; i < near_heap_limit_callbacks_.size(); i++) {
    if (near_heap_limit_callbacks_[i].first == callback) {
      near_heap_limit_callbacks_.erase(near_heap_limit_callbacks_.begin() + i);
      if (heap_limit) {
        RestoreHeapLimit(heap_limit);
      }
      return;
    }
  }
  UNREACHABLE();
}

void Heap::AppendArrayBufferExtension(Tagged<JSArrayBuffer> object,
                                      ArrayBufferExtension* extension) {
  // ArrayBufferSweeper is managing all counters and updating Heap counters.
  array_buffer_sweeper_->Append(object, extension);
}

void Heap::DetachArrayBufferExtension(ArrayBufferExtension* extension) {
  // ArrayBufferSweeper is managing all counters and updating Heap counters.
  return array_buffer_sweeper_->Detach(extension);
}

void Heap::AutomaticallyRestoreInitialHeapLimit(double threshold_percent) {
  initial_max_old_generation_size_threshold_ =
      initial_max_old_generation_size_ * threshold_percent;
}

bool Heap::InvokeNearHeapLimitCallback() {
  if (!near_heap_limit_callbacks_.empty()) {
    AllowGarbageCollection allow_gc;
    TRACE_GC(tracer(), GCTracer::Scope::HEAP_EXTERNAL_NEAR_HEAP_LIMIT);
    VMState<EXTERNAL> callback_state(isolate());
    HandleScope scope(isolate());
    v8::NearHeapLimitCallback callback =
        near_heap_limit_callbacks_.back().first;
    void* data = near_heap_limit_callbacks_.back().second;
    size_t heap_limit = callback(data, max_old_generation_size(),
                                 initial_max_old_generation_size_);
    if (heap_limit > max_old_generation_size()) {
      SetOldGenerationAndGlobalMaximumSize(
          std::min(heap_limit, AllocatorLimitOnMaxOldGenerationSize()));
      return true;
    }
  }
  return false;
}

bool Heap::MeasureMemory(std::unique_ptr<v8::MeasureMemoryDelegate> delegate,
                         v8::MeasureMemoryExecution execution) {
  HandleScope handle_scope(isolate());
  std::vector<Handle<NativeContext>> contexts = FindAllNativeContexts();
  std::vector<Handle<NativeContext>> to_measure;
  for (auto& current : contexts) {
    if (delegate->ShouldMeasure(v8::Utils::ToLocal(current))) {
      to_measure.push_back(current);
    }
  }
  return memory_measurement_->EnqueueRequest(std::move(delegate), execution,
                                             to_measure);
}

std::unique_ptr<v8::MeasureMemoryDelegate> Heap::MeasureMemoryDelegate(
    Handle<NativeContext> context, Handle<JSPromise> promise,
    v8::MeasureMemoryMode mode) {
  return i::MemoryMeasurement::DefaultDelegate(isolate_, context, promise,
                                               mode);
}

void Heap::CollectCodeStatistics() {
  TRACE_EVENT0("v8", "Heap::CollectCodeStatistics");
  IsolateSafepointScope safepoint_scope(this);
  MakeHeapIterable();
  CodeStatistics::ResetCodeAndMetadataStatistics(isolate());
  // We do not look for code in new space, or map space.  If code
  // somehow ends up in those spaces, we would miss it here.
  CodeStatistics::CollectCodeStatistics(code_space_, isolate());
  CodeStatistics::CollectCodeStatistics(old_space_, isolate());
  CodeStatistics::CollectCodeStatistics(code_lo_space_, isolate());
  CodeStatistics::CollectCodeStatistics(trusted_space_, isolate());
  CodeStatistics::CollectCodeStatistics(trusted_lo_space_, isolate());
}

#ifdef DEBUG

void Heap::Print() {
  if (!HasBeenSetUp()) return;
  isolate()->PrintStack(stdout);

  for (SpaceIterator it(this); it.HasNext();) {
    it.Next()->Print();
  }
}

void Heap::ReportCodeStatistics(const char* title) {
  PrintF("###### Code Stats (%s) ######\n", title);
  CollectCodeStatistics();
  CodeStatistics::ReportCodeStatistics(isolate());
}

#endif  // DEBUG

bool Heap::Contains(Tagged<HeapObject> value) const {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    return true;
  }
  if (ReadOnlyHeap::Contains(value)) {
    return false;
  }
  if (memory_allocator()->IsOutsideAllocatedSpace(value.address())) {
    return false;
  }

  if (!HasBeenSetUp()) return false;

  return (new_space_ && new_space_->Contains(value)) ||
         old_space_->Contains(value) || code_space_->Contains(value) ||
         (shared_space_ && shared_space_->Contains(value)) ||
         (shared_trusted_space_ && shared_trusted_space_->Contains(value)) ||
         lo_space_->Contains(value) || code_lo_space_->Contains(value) ||
         (new_lo_space_ && new_lo_space_->Contains(value)) ||
         trusted_space_->Contains(value) ||
         trusted_lo_space_->Contains(value) ||
         (shared_lo_space_ && shared_lo_space_->Contains(value)) ||
         (shared_trusted_lo_space_ &&
          shared_trusted_lo_space_->Contains(value));
}

bool Heap::ContainsCode(Tagged<HeapObject> value) const {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    return true;
  }
  // TODO(v8:11880): support external code space.
  if (memory_allocator()->IsOutsideAllocatedSpace(value.address(),
                                                  EXECUTABLE)) {
    return false;
  }
  return HasBeenSetUp() &&
         (code_space_->Contains(value) || code_lo_space_->Contains(value));
}

bool Heap::SharedHeapContains(Tagged<HeapObject> value) const {
  if (shared_allocation_space_) {
    if (shared_allocation_space_->Contains(value)) return true;
    if (shared_lo_allocation_space_->Contains(value)) return true;
    if (shared_trusted_allocation_space_->Contains(value)) return true;
    if (shared_trusted_lo_allocation_space_->Contains(value)) return true;
  }

  return false;
}

bool Heap::MustBeInSharedOldSpace(Tagged<HeapObject> value) {
  if (isolate()->OwnsStringTables()) return false;
  if (ReadOnlyHeap::Contains(value)) return false;
  if (Heap::InYoungGeneration(value)) return false;
  if (IsExternalString(value)) return false;
  if (IsInternalizedString(value)) return true;
  return false;
}

bool Heap::InSpace(Tagged<HeapObject> value, AllocationSpace space) const {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL)
    return third_party_heap::Heap::InSpace(value.address(), space);
  if (memory_allocator()->IsOutsideAllocatedSpace(
          value.address(),
          IsAnyCodeSpace(space) ? EXECUTABLE : NOT_EXECUTABLE)) {
    return false;
  }
  if (!HasBeenSetUp()) return false;

  switch (space) {
    case NEW_SPACE:
      return new_space_->Contains(value);
    case OLD_SPACE:
      return old_space_->Contains(value);
    case CODE_SPACE:
      return code_space_->Contains(value);
    case SHARED_SPACE:
      return shared_space_->Contains(value);
    case TRUSTED_SPACE:
      return trusted_space_->Contains(value);
    case SHARED_TRUSTED_SPACE:
      return shared_trusted_space_->Contains(value);
    case LO_SPACE:
      return lo_space_->Contains(value);
    case CODE_LO_SPACE:
      return code_lo_space_->Contains(value);
    case NEW_LO_SPACE:
      return new_lo_space_->Contains(value);
    case SHARED_LO_SPACE:
      return shared_lo_space_->Contains(value);
    case SHARED_TRUSTED_LO_SPACE:
      return shared_trusted_lo_space_->Contains(value);
    case TRUSTED_LO_SPACE:
      return trusted_lo_space_->Contains(value);
    case RO_SPACE:
      return ReadOnlyHeap::Contains(value);
  }
  UNREACHABLE();
}

bool Heap::InSpaceSlow(Address addr, AllocationSpace space) const {
  if (memory_allocator()->IsOutsideAllocatedSpace(
          addr, IsAnyCodeSpace(space) ? EXECUTABLE : NOT_EXECUTABLE)) {
    return false;
  }
  if (!HasBeenSetUp()) return false;

  switch (space) {
    case NEW_SPACE:
      return new_space_->ContainsSlow(addr);
    case OLD_SPACE:
      return old_space_->ContainsSlow(addr);
    case CODE_SPACE:
      return code_space_->ContainsSlow(addr);
    case SHARED_SPACE:
      return shared_space_->ContainsSlow(addr);
    case TRUSTED_SPACE:
      return trusted_space_->ContainsSlow(addr);
    case SHARED_TRUSTED_SPACE:
      return shared_trusted_space_->ContainsSlow(addr);
    case LO_SPACE:
      return lo_space_->ContainsSlow(addr);
    case CODE_LO_SPACE:
      return code_lo_space_->ContainsSlow(addr);
    case NEW_LO_SPACE:
      return new_lo_space_->ContainsSlow(addr);
    case SHARED_LO_SPACE:
      return shared_lo_space_->ContainsSlow(addr);
    case SHARED_TRUSTED_LO_SPACE:
      return shared_trusted_lo_space_->ContainsSlow(addr);
    case TRUSTED_LO_SPACE:
      return trusted_lo_space_->ContainsSlow(addr);
    case RO_SPACE:
      return read_only_space_->ContainsSlow(addr);
  }
  UNREACHABLE();
}

bool Heap::IsValidAllocationSpace(AllocationSpace space) {
  switch (space) {
    case NEW_SPACE:
    case OLD_SPACE:
    case CODE_SPACE:
    case SHARED_SPACE:
    case LO_SPACE:
    case NEW_LO_SPACE:
    case CODE_LO_SPACE:
    case SHARED_LO_SPACE:
    case TRUSTED_SPACE:
    case SHARED_TRUSTED_SPACE:
    case TRUSTED_LO_SPACE:
    case SHARED_TRUSTED_LO_SPACE:
    case RO_SPACE:
      return true;
    default:
      return false;
  }
}

#ifdef DEBUG
void Heap::VerifyCountersAfterSweeping() {
  MakeHeapIterable();
  PagedSpaceIterator spaces(this);
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    space->VerifyCountersAfterSweeping(this);
  }
}

void Heap::VerifyCountersBeforeConcurrentSweeping(GarbageCollector collector) {
  if (v8_flags.minor_ms && new_space()) {
    PagedSpaceBase* space = paged_new_space()->paged_space();
    space->RefillFreeList();
    space->VerifyCountersBeforeConcurrentSweeping();
  }
  if (collector != GarbageCollector::MARK_COMPACTOR) return;
  PagedSpaceIterator spaces(this);
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    // We need to refine the counters on pages that are already swept and have
    // not been moved over to the actual space. Otherwise, the AccountingStats
    // are just an over approximation.
    space->RefillFreeList();
    space->VerifyCountersBeforeConcurrentSweeping();
  }
}

void Heap::VerifyCommittedPhysicalMemory() {
  PagedSpaceIterator spaces(this);
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    space->VerifyCommittedPhysicalMemory();
  }
  if (v8_flags.minor_ms && new_space()) {
    paged_new_space()->paged_space()->VerifyCommittedPhysicalMemory();
  }
}
#endif  // DEBUG

void Heap::IterateWeakRoots(RootVisitor* v, base::EnumSet<SkipRoot> options) {
  DCHECK(!options.contains(SkipRoot::kWeak));

  if (!options.contains(SkipRoot::kUnserializable)) {
    // Isolate::topmost_script_having_context_address is treated weakly.
    v->VisitRootPointer(
        Root::kWeakRoots, nullptr,
        FullObjectSlot(isolate()->topmost_script_having_context_address()));
  }

  if (!options.contains(SkipRoot::kOldGeneration) &&
      !options.contains(SkipRoot::kUnserializable) &&
      isolate()->OwnsStringTables()) {
    // Do not visit for the following reasons.
    // - Serialization, since the string table is custom serialized.
    // - If we are skipping old generation, since all internalized strings
    //   are in old space.
    // - If the string table is shared and this is not the shared heap,
    //   since all internalized strings are in the shared heap.
    isolate()->string_table()->IterateElements(v);
  }
  v->Synchronize(VisitorSynchronization::kStringTable);
  if (!options.contains(SkipRoot::kExternalStringTable) &&
      !options.contains(SkipRoot::kUnserializable)) {
    // Scavenge collections have special processing for this.
    // Do not visit for serialization, since the external string table will
    // be populated from scratch upon deserialization.
    external_string_table_.IterateAll(v);
  }
  v->Synchronize(VisitorSynchronization::kExternalStringsTable);
  if (!options.contains(SkipRoot::kOldGeneration) &&
      !options.contains(SkipRoot::kUnserializable) &&
      isolate()->is_shared_space_isolate() &&
      isolate()->shared_struct_type_registry()) {
    isolate()->shared_struct_type_registry()->IterateElements(isolate(), v);
  }
  v->Synchronize(VisitorSynchronization::kSharedStructTypeRegistry);
}

void Heap::IterateSmiRoots(RootVisitor* v) {
  // Acquire execution access since we are going to read stack limit values.
  ExecutionAccess access(isolate());
  v->VisitRootPointers(Root::kSmiRootList, nullptr,
                       roots_table().smi_roots_begin(),
                       roots_table().smi_roots_end());
  v->Synchronize(VisitorSynchronization::kSmiRootList);
}

// We cannot avoid stale handles to left-trimmed objects, but can only make
// sure all handles still needed are updated. Filter out a stale pointer
// and clear the slot to allow post processing of handles (needed because
// the sweeper might actually free the underlying page).
class ClearStaleLeftTrimmedPointerVisitor : public RootVisitor {
 public:
  ClearStaleLeftTrimmedPointerVisitor(Heap* heap, RootVisitor* visitor)
      : heap_(heap),
        visitor_(visitor)
#if V8_COMPRESS_POINTERS
        ,
        cage_base_(heap->isolate())
#endif  // V8_COMPRESS_POINTERS
  {
    USE(heap_);
  }

  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) override {
    ClearLeftTrimmedOrForward(root, description, p);
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot p = start; p < end; ++p) {
      ClearLeftTrimmedOrForward(root, description, p);
    }
  }

  void Synchronize(VisitorSynchronization::SyncTag tag) override {
    visitor_->Synchronize(tag);
  }

  void VisitRunningCode(FullObjectSlot code_slot,
                        FullObjectSlot istream_or_smi_zero_slot) override {
    // Directly forward to actualy visitor here. Code objects and instruction
    // stream will not be left-trimmed.
    DCHECK(!IsLeftTrimmed(code_slot));
    DCHECK(!IsLeftTrimmed(istream_or_smi_zero_slot));
    visitor_->VisitRunningCode(code_slot, istream_or_smi_zero_slot);
  }

  // The pointer compression cage base value used for decompression of all
  // tagged values except references to InstructionStream objects.
  PtrComprCageBase cage_base() const {
#if V8_COMPRESS_POINTERS
    return cage_base_;
#else
    return PtrComprCageBase{};
#endif  // V8_COMPRESS_POINTERS
  }

 private:
  inline void ClearLeftTrimmedOrForward(Root root, const char* description,
                                        FullObjectSlot p) {
    if (!IsHeapObject(*p)) return;

    if (IsLeftTrimmed(p)) {
      p.store(Smi::zero());
    } else {
      visitor_->VisitRootPointer(root, description, p);
    }
  }

  inline bool IsLeftTrimmed(FullObjectSlot p) {
    if (!IsHeapObject(*p)) return false;
    Tagged<HeapObject> current = Cast<HeapObject>(*p);
    if (!current->map_word(cage_base(), kRelaxedLoad).IsForwardingAddress() &&
        IsFreeSpaceOrFiller(current, cage_base())) {
#ifdef DEBUG
      // We need to find a FixedArrayBase map after walking the fillers.
      while (
          !current->map_word(cage_base(), kRelaxedLoad).IsForwardingAddress() &&
          IsFreeSpaceOrFiller(current, cage_base())) {
        Address next = current.ptr();
        if (current->map(cage_base()) ==
            ReadOnlyRoots(heap_).one_pointer_filler_map()) {
          next += kTaggedSize;
        } else if (current->map(cage_base()) ==
                   ReadOnlyRoots(heap_).two_pointer_filler_map()) {
          next += 2 * kTaggedSize;
        } else {
          next += current->Size();
        }
        current = Cast<HeapObject>(Tagged<Object>(next));
      }
      DCHECK(
          current->map_word(cage_base(), kRelaxedLoad).IsForwardingAddress() ||
          IsFixedArrayBase(current, cage_base()));
#endif  // DEBUG
      return true;
    } else {
      return false;
    }
  }

  Heap* heap_;
  RootVisitor* visitor_;

#if V8_COMPRESS_POINTERS
  const PtrComprCageBase cage_base_;
#endif  // V8_COMPRESS_POINTERS
};

void Heap::IterateRoots(RootVisitor* v, base::EnumSet<SkipRoot> options,
                        IterateRootsMode roots_mode) {
  v->VisitRootPointers(Root::kStrongRootList, nullptr,
                       roots_table().strong_roots_begin(),
                       roots_table().strong_roots_end());
  v->Synchronize(VisitorSynchronization::kStrongRootList);

  isolate_->bootstrapper()->Iterate(v);
  v->Synchronize(VisitorSynchronization::kBootstrapper);
  Relocatable::Iterate(isolate_, v);
  v->Synchronize(VisitorSynchronization::kRelocatable);
  isolate_->debug()->Iterate(v);
  v->Synchronize(VisitorSynchronization::kDebug);

  isolate_->compilation_cache()->Iterate(v);
  v->Synchronize(VisitorSynchronization::kCompilationCache);

  const bool skip_iterate_builtins =
      options.contains(SkipRoot::kOldGeneration) ||
      (Builtins::kCodeObjectsAreInROSpace &&
       options.contains(SkipRoot::kReadOnlyBuiltins) &&
       // Prior to ReadOnlyPromotion, builtins may be on the mutable heap.
       !isolate_->serializer_enabled());
  if (!skip_iterate_builtins) {
    IterateBuiltins(v);
    v->Synchronize(VisitorSynchronization::kBuiltins);
  }

  // Iterate over pointers being held by inactive threads.
  isolate_->thread_manager()->Iterate(v);
  v->Synchronize(VisitorSynchronization::kThreadManager);

  // Visitors in this block only run when not serializing. These include:
  //
  // - Thread-local and stack.
  // - Handles.
  // - Microtasks.
  // - The startup object cache.
  //
  // When creating real startup snapshot, these areas are expected to be empty.
  // It is also possible to create a snapshot of a *running* isolate for testing
  // purposes. In this case, these areas are likely not empty and will simply be
  // skipped.
  //
  // The general guideline for adding visitors to this section vs. adding them
  // above is that non-transient heap state is always visited, transient heap
  // state is visited only when not serializing.
  if (!options.contains(SkipRoot::kUnserializable)) {
    if (!options.contains(SkipRoot::kTracedHandles)) {
      // Young GCs always skip traced handles and visit them manually.
      DCHECK(!options.contains(SkipRoot::kOldGeneration));

      isolate_->traced_handles()->Iterate(v);
    }

    if (!options.contains(SkipRoot::kGlobalHandles)) {
      // Young GCs always skip global handles and visit them manually.
      DCHECK(!options.contains(SkipRoot::kOldGeneration));

      if (options.contains(SkipRoot::kWeak)) {
        isolate_->global_handles()->IterateStrongRoots(v);
      } else {
        isolate_->global_handles()->IterateAllRoots(v);
      }
    }
    v->Synchronize(VisitorSynchronization::kGlobalHandles);

    if (!options.contains(SkipRoot::kStack)) {
      ClearStaleLeftTrimmedPointerVisitor left_trim_visitor(this, v);
      IterateStackRoots(&left_trim_visitor);
      if (!options.contains(SkipRoot::kConservativeStack)) {
        IterateConservativeStackRoots(v, roots_mode);
      }
      v->Synchronize(VisitorSynchronization::kStackRoots);
    }

    // Iterate over main thread handles in handle scopes.
    if (!options.contains(SkipRoot::kMainThreadHandles)) {
      // Clear main thread handles with stale references to left-trimmed
      // objects. The GC would crash on such stale references.
      ClearStaleLeftTrimmedPointerVisitor left_trim_visitor(this, v);
      isolate_->handle_scope_implementer()->Iterate(&left_trim_visitor);
    }
    // Iterate local handles for all local heaps.
    safepoint_->Iterate(v);
    // Iterates all persistent handles.
    isolate_->persistent_handles_list()->Iterate(v, isolate_);
    v->Synchronize(VisitorSynchronization::kHandleScope);

    if (options.contains(SkipRoot::kOldGeneration)) {
      isolate_->eternal_handles()->IterateYoungRoots(v);
    } else {
      isolate_->eternal_handles()->IterateAllRoots(v);
    }
    v->Synchronize(VisitorSynchronization::kEternalHandles);

    // Iterate over pending Microtasks stored in MicrotaskQueues.
    MicrotaskQueue* default_microtask_queue =
        isolate_->default_microtask_queue();
    if (default_microtask_queue) {
      MicrotaskQueue* microtask_queue = default_microtask_queue;
      do {
        microtask_queue->IterateMicrotasks(v);
        microtask_queue = microtask_queue->next();
      } while (microtask_queue != default_microtask_queue);
    }
    v->Synchronize(VisitorSynchronization::kMicroTasks);

    // Iterate over other strong roots (currently only identity maps and
    // deoptimization entries).
    for (StrongRootsEntry* current = strong_roots_head_; current;
         current = current->next) {
      v->VisitRootPointers(Root::kStrongRoots, current->label, current->start,
                           current->end);
    }
    v->Synchronize(VisitorSynchronization::kStrongRoots);

    // Iterate over the startup and shared heap object caches unless
    // serializing or deserializing.
    SerializerDeserializer::IterateStartupObjectCache(isolate_, v);
    v->Synchronize(VisitorSynchronization::kStartupObjectCache);

    // Iterate over shared heap object cache when the isolate owns this data
    // structure. Isolates which own the shared heap object cache are:
    //   * Shared isolate
    //   * Shared space/main isolate
    //   * All isolates which do not use the shared heap feature.
    //
    // However, worker/client isolates do not own the shared heap object cache
    // and should not iterate it.
    if (isolate_->is_shared_space_isolate() || !isolate_->has_shared_space()) {
      SerializerDeserializer::IterateSharedHeapObjectCache(isolate_, v);
      v->Synchronize(VisitorSynchronization::kSharedHeapObjectCache);
    }
  }

  if (!options.contains(SkipRoot::kWeak)) {
    IterateWeakRoots(v, options);
  }
}

void Heap::IterateRootsIncludingClients(RootVisitor* v,
                                        base::EnumSet<SkipRoot> options) {
  IterateRoots(v, options, IterateRootsMode::kMainIsolate);

  if (isolate()->is_shared_space_isolate()) {
    ClientRootVisitor<> client_root_visitor(v);
    isolate()->global_safepoint()->IterateClientIsolates(
        [v = &client_root_visitor, options](Isolate* client) {
          client->heap()->IterateRoots(v, options,
                                       IterateRootsMode::kClientIsolate);
        });
  }
}

void Heap::IterateWeakGlobalHandles(RootVisitor* v) {
  isolate_->global_handles()->IterateWeakRoots(v);
  isolate_->traced_handles()->Iterate(v);
}

void Heap::IterateBuiltins(RootVisitor* v) {
  Builtins* builtins = isolate()->builtins();
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    const char* name = Builtins::name(builtin);
    v->VisitRootPointer(Root::kBuiltins, name, builtins->builtin_slot(builtin));
  }

  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLastTier0;
       ++builtin) {
    v->VisitRootPointer(Root::kBuiltins, Builtins::name(builtin),
                        builtins->builtin_tier0_slot(builtin));
  }

  // The entry table doesn't need to be updated since all builtins are embedded.
  static_assert(Builtins::AllBuiltinsAreIsolateIndependent());
}

void Heap::IterateStackRoots(RootVisitor* v) { isolate_->Iterate(v); }

void Heap::IterateConservativeStackRoots(RootVisitor* v,
                                         IterateRootsMode roots_mode) {
#ifdef V8_ENABLE_CONSERVATIVE_STACK_SCANNING
  if (!IsGCWithStack()) return;

  // In case of a shared GC, we're interested in the main isolate for CSS.
  Isolate* main_isolate = roots_mode == IterateRootsMode::kClientIsolate
                              ? isolate()->shared_space_isolate()
                              : isolate();

  ConservativeStackVisitor stack_visitor(main_isolate, v);
  if (IsGCWithMainThreadStack()) {
    stack().IteratePointersUntilMarker(&stack_visitor);
  }
  stack().IterateBackgroundStacks(&stack_visitor);
#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING
}

// static
size_t Heap::DefaultMinSemiSpaceSize() {
#if ENABLE_HUGEPAGE
  static constexpr size_t kMinSemiSpaceSize =
      kHugePageSize * kPointerMultiplier;
#else
  static constexpr size_t kMinSemiSpaceSize = 512 * KB * kPointerMultiplier;
#endif
  static_assert(kMinSemiSpaceSize % (1 << kPageSizeBits) == 0);

  return kMinSemiSpaceSize;
}

// static
size_t Heap::DefaultMaxSemiSpaceSize() {
#if ENABLE_HUGEPAGE
  static constexpr size_t kMaxSemiSpaceCapacityBaseUnit =
      kHugePageSize * 2 * kPointerMultiplier;
#else
  static constexpr size_t kMaxSemiSpaceCapacityBaseUnit =
      MB * kPointerMultiplier;
#endif
  static_assert(kMaxSemiSpaceCapacityBaseUnit % (1 << kPageSizeBits) == 0);

  size_t max_semi_space_size =
      (v8_flags.minor_ms ? v8_flags.minor_ms_max_new_space_capacity_mb
                         : v8_flags.scavenger_max_new_space_capacity_mb) *
      kMaxSemiSpaceCapacityBaseUnit;
  DCHECK_EQ(0, max_semi_space_size % (1 << kPageSizeBits));
  return max_semi_space_size;
}

// static
size_t Heap::OldGenerationToSemiSpaceRatio() {
  DCHECK(!v8_flags.minor_ms);
  static constexpr size_t kOldGenerationToSemiSpaceRatio =
      128 * kHeapLimitMultiplier / kPointerMultiplier;
  return kOldGenerationToSemiSpaceRatio;
}

// static
size_t Heap::OldGenerationToSemiSpaceRatioLowMemory() {
  static constexpr size_t kOldGenerationToSemiSpaceRatioLowMemory =
      256 * kHeapLimitMultiplier / kPointerMultiplier;
  return kOldGenerationToSemiSpaceRatioLowMemory / (v8_flags.minor_ms ? 2 : 1);
}

void Heap::ConfigureHeap(const v8::ResourceConstraints& constraints,
                         v8::CppHeap* cpp_heap) {
  CHECK(!configured_);
  // Initialize max_semi_space_size_.
  {
    max_semi_space_size_ = DefaultMaxSemiSpaceSize();
    if (constraints.max_young_generation_size_in_bytes() > 0) {
      max_semi_space_size_ = SemiSpaceSizeFromYoungGenerationSize(
          constraints.max_young_generation_size_in_bytes());
    }
    if (v8_flags.max_semi_space_size > 0) {
      max_semi_space_size_ =
          static_cast<size_t>(v8_flags.max_semi_space_size) * MB;
    } else if (v8_flags.max_heap_size > 0) {
      size_t max_heap_size = static_cast<size_t>(v8_flags.max_heap_size) * MB;
      size_t young_generation_size, old_generation_size;
      if (v8_flags.max_old_space_size > 0) {
        old_generation_size =
            static_cast<size_t>(v8_flags.max_old_space_size) * MB;
        young_generation_size = max_heap_size > old_generation_size
                                    ? max_heap_size - old_generation_size
                                    : 0;
      } else {
        GenerationSizesFromHeapSize(max_heap_size, &young_generation_size,
                                    &old_generation_size);
      }
      max_semi_space_size_ =
          SemiSpaceSizeFromYoungGenerationSize(young_generation_size);
    }
    if (v8_flags.stress_compaction) {
      // This will cause more frequent GCs when stressing.
      max_semi_space_size_ = MB;
    }
    if (!v8_flags.minor_ms) {
      // TODO(dinfuehr): Rounding to a power of 2 is technically no longer
      // needed but yields best performance on Pixel2.
      max_semi_space_size_ =
          static_cast<size_t>(base::bits::RoundUpToPowerOfTwo64(
              static_cast<uint64_t>(max_semi_space_size_)));
    }
    max_semi_space_size_ =
        std::max(max_semi_space_size_, DefaultMinSemiSpaceSize());
    max_semi_space_size_ =
        RoundDown<PageMetadata::kPageSize>(max_semi_space_size_);
  }

  // Initialize max_old_generation_size_ and max_global_memory_.
  {
    size_t max_old_generation_size = 700ul * (kSystemPointerSize / 4) * MB;
    if (constraints.max_old_generation_size_in_bytes() > 0) {
      max_old_generation_size = constraints.max_old_generation_size_in_bytes();
    }
    if (v8_flags.max_old_space_size > 0) {
      max_old_generation_size =
          static_cast<size_t>(v8_flags.max_old_space_size) * MB;
    } else if (v8_flags.max_heap_size > 0) {
      size_t max_heap_size = static_cast<size_t>(v8_flags.max_heap_size) * MB;
      size_t young_generation_size =
          YoungGenerationSizeFromSemiSpaceSize(max_semi_space_size_);
      max_old_generation_size = max_heap_size > young_generation_size
                                    ? max_heap_size - young_generation_size
                                    : 0;
    }
    max_old_generation_size =
        std::max(max_old_generation_size, MinOldGenerationSize());
    max_old_generation_size = std::min(max_old_generation_size,
                                       AllocatorLimitOnMaxOldGenerationSize());
    max_old_generation_size =
        RoundDown<PageMetadata::kPageSize>(max_old_generation_size);

    SetOldGenerationAndGlobalMaximumSize(max_old_generation_size);
  }

  CHECK_IMPLIES(
      v8_flags.max_heap_size > 0,
      v8_flags.max_semi_space_size == 0 || v8_flags.max_old_space_size == 0);

  // Initialize initial_semispace_size_.
  {
    initial_semispace_size_ = DefaultMinSemiSpaceSize();
    if (max_semi_space_size_ == DefaultMaxSemiSpaceSize()) {
      // Start with at least 1*MB semi-space on machines with a lot of memory.
      initial_semispace_size_ =
          std::max(initial_semispace_size_, static_cast<size_t>(1 * MB));
    }
    if (constraints.initial_young_generation_size_in_bytes() > 0) {
      initial_semispace_size_ = SemiSpaceSizeFromYoungGenerationSize(
          constraints.initial_young_generation_size_in_bytes());
    }
    if (v8_flags.initial_heap_size > 0) {
      size_t young_generation, old_generation;
      Heap::GenerationSizesFromHeapSize(
          static_cast<size_t>(v8_flags.initial_heap_size) * MB,
          &young_generation, &old_generation);
      initial_semispace_size_ =
          SemiSpaceSizeFromYoungGenerationSize(young_generation);
    }
    if (v8_flags.min_semi_space_size > 0) {
      initial_semispace_size_ =
          static_cast<size_t>(v8_flags.min_semi_space_size) * MB;
    }
    initial_semispace_size_ =
        std::min(initial_semispace_size_, max_semi_space_size_);
    initial_semispace_size_ =
        RoundDown<PageMetadata::kPageSize>(initial_semispace_size_);
  }

  if (v8_flags.lazy_new_space_shrinking) {
    initial_semispace_size_ = max_semi_space_size_;
  }

  // Initialize initial_old_space_size_.
  {
    initial_old_generation_size_ = kMaxInitialOldGenerationSize;
    if (constraints.initial_old_generation_size_in_bytes() > 0) {
      initial_old_generation_size_ =
          constraints.initial_old_generation_size_in_bytes();
      set_old_generation_allocation_limit_configured(true);
    }
    if (v8_flags.initial_heap_size > 0) {
      size_t initial_heap_size =
          static_cast<size_t>(v8_flags.initial_heap_size) * MB;
      size_t young_generation_size =
          YoungGenerationSizeFromSemiSpaceSize(initial_semispace_size_);
      initial_old_generation_size_ =
          initial_heap_size > young_generation_size
              ? initial_heap_size - young_generation_size
              : 0;
      set_old_generation_allocation_limit_configured(true);
    }
    if (v8_flags.initial_old_space_size > 0) {
      initial_old_generation_size_ =
          static_cast<size_t>(v8_flags.initial_old_space_size) * MB;
      set_old_generation_allocation_limit_configured(true);
    }
    initial_old_generation_size_ =
        std::min(initial_old_generation_size_, max_old_generation_size() / 2);
    initial_old_generation_size_ =
        RoundDown<PageMetadata::kPageSize>(initial_old_generation_size_);
  }

  if (old_generation_allocation_limit_configured()) {
    // If the embedder pre-configures the initial old generation size,
    // then allow V8 to skip full GCs below that threshold.
    min_old_generation_size_ = initial_old_generation_size_;
    min_global_memory_size_ =
        GlobalMemorySizeFromV8Size(min_old_generation_size_);
  }

  initial_max_old_generation_size_ = max_old_generation_size();
  ResetOldGenerationAndGlobalAllocationLimit();

  // We rely on being able to allocate new arrays in paged spaces.
  DCHECK(kMaxRegularHeapObjectSize >=
         (JSArray::kHeaderSize +
          FixedArray::SizeFor(JSArray::kInitialMaxFastElementArray) +
          ALIGN_TO_ALLOCATION_ALIGNMENT(AllocationMemento::kSize)));

  code_range_size_ = constraints.code_range_size_in_bytes();

  if (cpp_heap) {
    AttachCppHeap(cpp_heap);
    owning_cpp_heap_.reset(CppHeap::From(cpp_heap));
  }

  configured_ = true;
}

void Heap::AddToRingBuffer(const char* string) {
  size_t first_part =
      std::min(strlen(string), kTraceRingBufferSize - ring_buffer_end_);
  memcpy(trace_ring_buffer_ + ring_buffer_end_, string, first_part);
  ring_buffer_end_ += first_part;
  if (first_part < strlen(string)) {
    ring_buffer_full_ = true;
    size_t second_part = strlen(string) - first_part;
    memcpy(trace_ring_buffer_, string + first_part, second_part);
    ring_buffer_end_ = second_part;
  }
}

void Heap::GetFromRingBuffer(char* buffer) {
  size_t copied = 0;
  if (ring_buffer_full_) {
    copied = kTraceRingBufferSize - ring_buffer_end_;
    memcpy(buffer, trace_ring_buffer_ + ring_buffer_end_, copied);
  }
  memcpy(buffer + copied, trace_ring_buffer_, ring_buffer_end_);
}

void Heap::ConfigureHeapDefault() {
  v8::ResourceConstraints constraints;
  ConfigureHeap(constraints, nullptr);
}

void Heap::RecordStats(HeapStats* stats, bool take_snapshot) {
  *stats->start_marker = HeapStats::kStartMarker;
  *stats->end_marker = HeapStats::kEndMarker;
  *stats->ro_space_size = read_only_space_->Size();
  *stats->ro_space_capacity = read_only_space_->Capacity();
  *stats->new_space_size = NewSpaceSize();
  *stats->new_space_capacity = NewSpaceCapacity();
  *stats->old_space_size = old_space_->SizeOfObjects();
  *stats->old_space_capacity = old_space_->Capacity();
  *stats->code_space_size = code_space_->SizeOfObjects();
  *stats->code_space_capacity = code_space_->Capacity();
  *stats->map_space_size = 0;
  *stats->map_space_capacity = 0;
  *stats->lo_space_size = lo_space_->Size();
  *stats->code_lo_space_size = code_lo_space_->Size();
  isolate_->global_handles()->RecordStats(stats);
  *stats->memory_allocator_size = memory_allocator()->Size();
  *stats->memory_allocator_capacity =
      memory_allocator()->Size() + memory_allocator()->Available();
  *stats->os_error = base::OS::GetLastError();
  // TODO(leszeks): Include the string table in both current and peak usage.
  *stats->malloced_memory = isolate_->allocator()->GetCurrentMemoryUsage();
  *stats->malloced_peak_memory = isolate_->allocator()->GetMaxMemoryUsage();
  if (take_snapshot) {
    HeapObjectIterator iterator(this);
    for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
         obj = iterator.Next()) {
      InstanceType type = obj->map()->instance_type();
      DCHECK(0 <= type && type <= LAST_TYPE);
      stats->objects_per_type[type]++;
      stats->size_per_type[type] += obj->Size();
    }
  }
  if (stats->last_few_messages != nullptr)
    GetFromRingBuffer(stats->last_few_messages);
}

size_t Heap::OldGenerationSizeOfObjects() const {
  size_t total = 0;
  if (v8_flags.sticky_mark_bits)
    total += sticky_space()->old_objects_size();
  else
    total += old_space()->SizeOfObjects();
  total += lo_space()->SizeOfObjects();
  total += code_space()->SizeOfObjects();
  total += code_lo_space()->SizeOfObjects();
  if (shared_space()) {
    total += shared_space()->SizeOfObjects();
  }
  if (shared_lo_space()) {
    total += shared_lo_space()->SizeOfObjects();
  }
  total += trusted_space()->SizeOfObjects();
  total += trusted_lo_space()->SizeOfObjects();
  return total;
}

size_t Heap::OldGenerationWastedBytes() const {
  PagedSpaceIterator spaces(this);
  size_t total = 0;
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    total += space->Waste();
  }
  return total;
}

size_t Heap::OldGenerationConsumedBytes() const {
  return OldGenerationSizeOfObjects() + OldGenerationWastedBytes();
}

size_t Heap::YoungGenerationSizeOfObjects() const {
  if (v8_flags.sticky_mark_bits) {
    DCHECK_NOT_NULL(new_lo_space());
    return sticky_space()->young_objects_size() +
           new_lo_space()->SizeOfObjects();
  }
  if (!new_space()) return 0;
  DCHECK_NOT_NULL(new_lo_space());
  return new_space()->SizeOfObjects() + new_lo_space()->SizeOfObjects();
}

size_t Heap::YoungGenerationWastedBytes() const {
  if (!new_space()) return 0;
  DCHECK(v8_flags.minor_ms);
  return paged_new_space()->paged_space()->Waste();
}

size_t Heap::YoungGenerationConsumedBytes() const {
  return YoungGenerationSizeOfObjects() + YoungGenerationWastedBytes();
}

size_t Heap::EmbedderSizeOfObjects() const {
  return cpp_heap_ ? CppHeap::From(cpp_heap_)->used_size() : 0;
}

size_t Heap::GlobalSizeOfObjects() const {
  return OldGenerationSizeOfObjects() + EmbedderSizeOfObjects();
}

size_t Heap::GlobalWastedBytes() const { return OldGenerationWastedBytes(); }

size_t Heap::GlobalConsumedBytes() const {
  return GlobalSizeOfObjects() + GlobalWastedBytes();
}

uint64_t Heap::AllocatedExternalMemorySinceMarkCompact() const {
  return external_memory_.AllocatedSinceMarkCompact();
}

bool Heap::AllocationLimitOvershotByLargeMargin() const {
  // This guards against too eager finalization in small heaps.
  // The number is chosen based on v8.browsing_mobile on Nexus 7v2.
  constexpr size_t kMarginForSmallHeaps = 32u * MB;

  uint64_t size_now =
      OldGenerationConsumedBytes() + AllocatedExternalMemorySinceMarkCompact();
  if (v8_flags.minor_ms && incremental_marking()->IsMajorMarking()) {
    size_now += YoungGenerationConsumedBytes();
  }

  const size_t v8_overshoot = old_generation_allocation_limit() < size_now
                                  ? size_now - old_generation_allocation_limit()
                                  : 0;
  const size_t global_limit = global_allocation_limit();
  const size_t global_size = GlobalConsumedBytes();
  const size_t global_overshoot =
      global_limit < global_size ? global_size - global_limit : 0;

  // Bail out if the V8 and global sizes are still below their respective
  // limits.
  if (v8_overshoot == 0 && global_overshoot == 0) {
    return false;
  }

  // Overshoot margin is 50% of allocation limit or half-way to the max heap
  // with special handling of small heaps.
  const size_t v8_margin = std::min(
      std::max(old_generation_allocation_limit() / 2, kMarginForSmallHeaps),
      (max_old_generation_size() - old_generation_allocation_limit()) / 2);
  const size_t global_margin =
      std::min(std::max(global_limit / 2, kMarginForSmallHeaps),
               (max_global_memory_size_ - global_limit) / 2);

  return v8_overshoot >= v8_margin || global_overshoot >= global_margin;
}

bool Heap::ShouldOptimizeForLoadTime() const {
  return isolate()->rail_mode() == PERFORMANCE_LOAD &&
         !AllocationLimitOvershotByLargeMargin() &&
         MonotonicallyIncreasingTimeInMs() <
             (load_start_time_ms_.load(std::memory_order_relaxed) +
              kMaxLoadTimeMs);
}

// This predicate is called when an old generation space cannot allocated from
// the free list and is about to add a new page. Returning false will cause a
// major GC. It happens when the old generation allocation limit is reached and
// - either we need to optimize for memory usage,
// - or the incremental marking is not in progress and we cannot start it.
bool Heap::ShouldExpandOldGenerationOnSlowAllocation(LocalHeap* local_heap,
                                                     AllocationOrigin origin) {
  if (always_allocate() || OldGenerationSpaceAvailable() > 0) return true;
  // We reached the old generation allocation limit.

  // Allocations in the GC should always succeed if possible.
  if (origin == AllocationOrigin::kGC) return true;

  // Background threads need to be allowed to allocate without GC after teardown
  // was initiated.
  if (gc_state() == TEAR_DOWN) return true;

  // Allocations need to succeed during isolate deserialization. With shared
  // heap allocations, a client isolate may perform shared heap allocations
  // during isolate deserialization as well.
  if (!deserialization_complete() ||
      !local_heap->heap()->deserialization_complete()) {
    return true;
  }

  // Make it more likely that retry of allocations succeeds.
  if (local_heap->IsRetryOfFailedAllocation()) return true;

  // Background thread requested GC, allocation should fail
  if (CollectionRequested()) return false;

  if (ShouldOptimizeForMemoryUsage()) return false;

  if (ShouldOptimizeForLoadTime()) return true;

  if (incremental_marking()->IsMajorMarking() &&
      AllocationLimitOvershotByLargeMargin()) {
    return false;
  }

  if (incremental_marking()->IsStopped() &&
      IncrementalMarkingLimitReached() == IncrementalMarkingLimit::kNoLimit) {
    // We cannot start incremental marking.
    return false;
  }
  return true;
}

// This predicate is called when an young generation space cannot allocated
// from the free list and is about to add a new page. Returning false will
// cause a GC.
bool Heap::ShouldExpandYoungGenerationOnSlowAllocation() {
  DCHECK(deserialization_complete());
  DCHECK(sweeper()->IsSweepingDoneForSpace(NEW_SPACE));

  if (always_allocate()) return true;

  if (gc_state() == TEAR_DOWN) return true;

  if (!CanPromoteYoungAndExpandOldGeneration(PageMetadata::kPageSize)) {
    // Assuming all of new space is alive, doing a full GC and promoting all
    // objects should still succeed. Don't let new space grow if it means it
    // will exceed the available size of old space.
    return false;
  }

  if (incremental_marking()->IsMajorMarking() &&
      !AllocationLimitOvershotByLargeMargin()) {
    // Allocate a new page during full GC incremental marking to avoid
    // prematurely finalizing the incremental GC. Once the full GC is over, new
    // space will be empty and capacity will be reset.
    return true;
  }

  return false;
}

Heap::HeapGrowingMode Heap::CurrentHeapGrowingMode() {
  if (ShouldReduceMemory() || v8_flags.stress_compaction) {
    return Heap::HeapGrowingMode::kMinimal;
  }

  if (ShouldOptimizeForMemoryUsage()) {
    return Heap::HeapGrowingMode::kConservative;
  }

  if (memory_reducer() != nullptr && memory_reducer()->ShouldGrowHeapSlowly()) {
    return Heap::HeapGrowingMode::kSlow;
  }

  return Heap::HeapGrowingMode::kDefault;
}

size_t Heap::GlobalMemoryAvailable() {
  size_t global_size = GlobalConsumedBytes();
  size_t global_limit = global_allocation_limit();

  if (global_size < global_limit) {
    return global_limit - global_size;
  } else {
    return 0;
  }
}

namespace {

double PercentToLimit(size_t size_at_gc, size_t size_now, size_t limit) {
  if (size_now < size_at_gc) {
    return 0.0;
  }
  if (size_now > limit) {
    return 100.0;
  }
  const size_t current_bytes = size_now - size_at_gc;
  const size_t total_bytes = limit - size_at_gc;
  DCHECK_LE(current_bytes, total_bytes);
  return static_cast<double>(current_bytes) * 100 / total_bytes;
}

}  // namespace

double Heap::PercentToOldGenerationLimit() const {
  return PercentToLimit(
      old_generation_size_at_last_gc_ + old_generation_wasted_at_last_gc_,
      OldGenerationConsumedBytes(), old_generation_allocation_limit());
}

double Heap::PercentToGlobalMemoryLimit() const {
  return PercentToLimit(global_consumed_memory_at_last_gc_,
                        GlobalConsumedBytes(), global_allocation_limit());
}

// - kNoLimit means that either incremental marking is disabled or it is too
// early to start incremental marking.
// - kSoftLimit means that incremental marking should be started soon.
// - kHardLimit means that incremental marking should be started immediately.
// - kFallbackForEmbedderLimit means that incremental marking should be
// started as soon as the embedder does not allocate with high throughput
// anymore.
Heap::IncrementalMarkingLimit Heap::IncrementalMarkingLimitReached() {
  // InstructionStream using an AlwaysAllocateScope assumes that the GC state
  // does not change; that implies that no marking steps must be performed.
  if (!incremental_marking()->CanAndShouldBeStarted() || always_allocate()) {
    // Incremental marking is disabled or it is too early to start.
    return IncrementalMarkingLimit::kNoLimit;
  }
  if (v8_flags.stress_incremental_marking) {
    return IncrementalMarkingLimit::kHardLimit;
  }
  if (incremental_marking()->IsBelowActivationThresholds()) {
    // Incremental marking is disabled or it is too early to start.
    return IncrementalMarkingLimit::kNoLimit;
  }
  if (ShouldStressCompaction() || HighMemoryPressure()) {
    // If there is high memory pressure or stress testing is enabled, then
    // start marking immediately.
    return IncrementalMarkingLimit::kHardLimit;
  }

  if (v8_flags.stress_marking > 0) {
    int current_percent = static_cast<int>(
        std::max(PercentToOldGenerationLimit(), PercentToGlobalMemoryLimit()));
    if (current_percent > 0) {
      if (v8_flags.trace_stress_marking) {
        isolate()->PrintWithTimestamp(
            "[IncrementalMarking] %d%% of the memory limit reached\n",
            current_percent);
      }
      if (v8_flags.fuzzer_gc_analysis) {
        // Skips values >=100% since they already trigger marking.
        if (current_percent < 100) {
          double max_marking_limit_reached =
              max_marking_limit_reached_.load(std::memory_order_relaxed);
          while (current_percent > max_marking_limit_reached) {
            max_marking_limit_reached_.compare_exchange_weak(
                max_marking_limit_reached, current_percent,
                std::memory_order_relaxed);
          }
        }
      } else if (current_percent >= stress_marking_percentage_) {
        return IncrementalMarkingLimit::kHardLimit;
      }
    }
  }

  if (v8_flags.incremental_marking_soft_trigger > 0 ||
      v8_flags.incremental_marking_hard_trigger > 0) {
    int current_percent = static_cast<int>(
        std::max(PercentToOldGenerationLimit(), PercentToGlobalMemoryLimit()));
    if (current_percent > v8_flags.incremental_marking_hard_trigger &&
        v8_flags.incremental_marking_hard_trigger > 0) {
      return IncrementalMarkingLimit::kHardLimit;
    }
    if (current_percent > v8_flags.incremental_marking_soft_trigger &&
        v8_flags.incremental_marking_soft_trigger > 0) {
      return IncrementalMarkingLimit::kSoftLimit;
    }
    return IncrementalMarkingLimit::kNoLimit;
  }

#if defined(V8_USE_PERFETTO)
  TRACE_COUNTER(
      TRACE_DISABLED_BY_DEFAULT("v8.gc"), "OldGenerationConsumedBytes",
      OldGenerationConsumedBytes() + AllocatedExternalMemorySinceMarkCompact());
  TRACE_COUNTER(TRACE_DISABLED_BY_DEFAULT("v8.gc"), "GlobalConsumedBytes",
                GlobalConsumedBytes());
#endif
  size_t old_generation_space_available = OldGenerationSpaceAvailable();
  size_t global_memory_available = GlobalMemoryAvailable();

  if (old_generation_space_available > NewSpaceTargetCapacity() &&
      (global_memory_available > NewSpaceTargetCapacity())) {
    if (cpp_heap() && gc_count_ == 0 &&
        !old_generation_allocation_limit_configured()) {
      // At this point the embedder memory is above the activation
      // threshold. No GC happened so far and it's thus unlikely to get a
      // configured heap any time soon. Start a memory reducer in this case
      // which will wait until the allocation rate is low to trigger garbage
      // collection.
      return IncrementalMarkingLimit::kFallbackForEmbedderLimit;
    }
    return IncrementalMarkingLimit::kNoLimit;
  }
  if (ShouldOptimizeForMemoryUsage()) {
    return IncrementalMarkingLimit::kHardLimit;
  }
  if (ShouldOptimizeForLoadTime()) {
    return IncrementalMarkingLimit::kNoLimit;
  }
  if (old_generation_space_available == 0) {
    return IncrementalMarkingLimit::kHardLimit;
  }
  if (global_memory_available == 0) {
    return IncrementalMarkingLimit::kHardLimit;
  }
  return IncrementalMarkingLimit::kSoftLimit;
}

bool Heap::ShouldStressCompaction() const {
  return v8_flags.stress_compaction && (gc_count_ & 1) != 0;
}

void Heap::EnableInlineAllocation() { inline_allocation_enabled_ = true; }

void Heap::DisableInlineAllocation() {
  inline_allocation_enabled_ = false;
  FreeMainThreadLinearAllocationAreas();
}

void Heap::SetUp(LocalHeap* main_thread_local_heap) {
  DCHECK_NULL(main_thread_local_heap_);
  DCHECK_NULL(heap_allocator_);
  main_thread_local_heap_ = main_thread_local_heap;
  heap_allocator_ = &main_thread_local_heap->heap_allocator_;
  DCHECK_NOT_NULL(heap_allocator_);

  // Set the stack start for the main thread that sets up the heap.
  SetStackStart();

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  heap_allocator_->UpdateAllocationTimeout();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

#ifdef V8_ENABLE_THIRD_PARTY_HEAP
  tp_heap_ = third_party_heap::Heap::New(isolate());
#endif

  // Initialize heap spaces and initial maps and objects.
  //
  // If the heap is not yet configured (e.g. through the API), configure it.
  // Configuration is based on the flags new-space-size (really the semispace
  // size) and old-space-size if set or the initial values of semispace_size_
  // and old_generation_size_ otherwise.
  if (!configured_) ConfigureHeapDefault();

  mmap_region_base_ =
      reinterpret_cast<uintptr_t>(v8::internal::GetRandomMmapAddr()) &
      ~kMmapRegionMask;

  v8::PageAllocator* code_page_allocator;
  if (isolate_->RequiresCodeRange() || code_range_size_ != 0) {
    const size_t requested_size =
        code_range_size_ == 0 ? kMaximalCodeRangeSize : code_range_size_;
    // When a target requires the code range feature, we put all code objects in
    // a contiguous range of virtual address space, so that they can call each
    // other with near calls.
#ifdef V8_COMPRESS_POINTERS
    // When pointer compression is enabled, isolates in the same group share the
    // same CodeRange, owned by the IsolateGroup.
    code_range_ = isolate_->isolate_group()->EnsureCodeRange(requested_size);
#else
    // Otherwise, each isolate has its own CodeRange, owned by the heap.
    code_range_ = std::make_unique<CodeRange>();
    if (!code_range_->InitReservation(isolate_->page_allocator(),
                                      requested_size)) {
      V8::FatalProcessOutOfMemory(
          isolate_, "Failed to reserve virtual memory for CodeRange");
    }
#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE

    LOG(isolate_,
        NewEvent("CodeRange",
                 reinterpret_cast<void*>(code_range_->reservation()->address()),
                 code_range_size_));

    isolate_->AddCodeRange(code_range_->reservation()->region().begin(),
                           code_range_->reservation()->region().size());
    code_page_allocator = code_range_->page_allocator();
  } else {
    code_page_allocator = isolate_->page_allocator();
  }

  v8::PageAllocator* trusted_page_allocator;
#ifdef V8_ENABLE_SANDBOX
  trusted_range_ = TrustedRange::GetProcessWideTrustedRange();
  trusted_page_allocator = trusted_range_->page_allocator();
#else
  trusted_page_allocator = isolate_->page_allocator();
#endif

  task_runner_ = V8::GetCurrentPlatform()->GetForegroundTaskRunner(
      reinterpret_cast<v8::Isolate*>(isolate()));

  collection_barrier_.reset(new CollectionBarrier(this, this->task_runner_));

  // Set up memory allocator.
  memory_allocator_.reset(new MemoryAllocator(
      isolate_, code_page_allocator, trusted_page_allocator, MaxReserved()));

  sweeper_.reset(new Sweeper(this));

  mark_compact_collector_.reset(new MarkCompactCollector(this));

  scavenger_collector_.reset(new ScavengerCollector(this));
  minor_mark_sweep_collector_.reset(new MinorMarkSweepCollector(this));
  ephemeron_remembered_set_.reset(new EphemeronRememberedSet());

  incremental_marking_.reset(
      new IncrementalMarking(this, mark_compact_collector_->weak_objects()));

  if (v8_flags.concurrent_marking || v8_flags.parallel_marking) {
    concurrent_marking_.reset(
        new ConcurrentMarking(this, mark_compact_collector_->weak_objects()));
  } else {
    concurrent_marking_.reset(new ConcurrentMarking(this, nullptr));
  }

  // Set up layout tracing callback.
  if (V8_UNLIKELY(v8_flags.trace_gc_heap_layout)) {
    v8::GCType gc_type = kGCTypeMarkSweepCompact;
    if (V8_UNLIKELY(!v8_flags.trace_gc_heap_layout_ignore_minor_gc)) {
      gc_type = static_cast<v8::GCType>(gc_type | kGCTypeScavenge |
                                        kGCTypeMinorMarkSweep);
    }
    AddGCPrologueCallback(HeapLayoutTracer::GCProloguePrintHeapLayout, gc_type,
                          nullptr);
    AddGCEpilogueCallback(HeapLayoutTracer::GCEpiloguePrintHeapLayout, gc_type,
                          nullptr);
  }
}

void Heap::SetUpFromReadOnlyHeap(ReadOnlyHeap* ro_heap) {
  DCHECK_NOT_NULL(ro_heap);
  DCHECK_IMPLIES(read_only_space_ != nullptr,
                 read_only_space_ == ro_heap->read_only_space());
  DCHECK_NULL(space_[RO_SPACE].get());
  read_only_space_ = ro_heap->read_only_space();
  heap_allocator_->SetReadOnlySpace(read_only_space_);
}

void Heap::ReplaceReadOnlySpace(SharedReadOnlySpace* space) {
  CHECK(V8_SHARED_RO_HEAP_BOOL);
  if (read_only_space_) {
    read_only_space_->TearDown(memory_allocator());
    delete read_only_space_;
  }

  read_only_space_ = space;
  heap_allocator_->SetReadOnlySpace(read_only_space_);
}

class StressConcurrentAllocationTask : public CancelableTask {
 public:
  explicit StressConcurrentAllocationTask(Isolate* isolate)
      : CancelableTask(isolate), isolate_(isolate) {}

  void RunInternal() override {
    Heap* heap = isolate_->heap();
    LocalHeap local_heap(heap, ThreadKind::kBackground);
    UnparkedScope unparked_scope(&local_heap);

    const int kNumIterations = 2000;
    const int kSmallObjectSize = 10 * kTaggedSize;
    const int kMediumObjectSize = 8 * KB;
    const int kLargeObjectSize =
        static_cast<int>(MutablePageMetadata::kPageSize -
                         MemoryChunkLayout::ObjectStartOffsetInDataPage());

    for (int i = 0; i < kNumIterations; i++) {
      // Isolate tear down started, stop allocation...
      if (heap->gc_state() == Heap::TEAR_DOWN) return;

      AllocationResult result = local_heap.AllocateRaw(
          kSmallObjectSize, AllocationType::kOld, AllocationOrigin::kRuntime,
          AllocationAlignment::kTaggedAligned);
      if (!result.IsFailure()) {
        heap->CreateFillerObjectAtBackground(
            WritableFreeSpace::ForNonExecutableMemory(result.ToAddress(),
                                                      kSmallObjectSize));
      } else {
        heap->CollectGarbageFromAnyThread(&local_heap);
      }

      result = local_heap.AllocateRaw(kMediumObjectSize, AllocationType::kOld,
                                      AllocationOrigin::kRuntime,
                                      AllocationAlignment::kTaggedAligned);
      if (!result.IsFailure()) {
        heap->CreateFillerObjectAtBackground(
            WritableFreeSpace::ForNonExecutableMemory(result.ToAddress(),
                                                      kMediumObjectSize));
      } else {
        heap->CollectGarbageFromAnyThread(&local_heap);
      }

      result = local_heap.AllocateRaw(kLargeObjectSize, AllocationType::kOld,
                                      AllocationOrigin::kRuntime,
                                      AllocationAlignment::kTaggedAligned);
      if (!result.IsFailure()) {
        heap->CreateFillerObjectAtBackground(
            WritableFreeSpace::ForNonExecutableMemory(result.ToAddress(),
                                                      kLargeObjectSize));
      } else {
        heap->CollectGarbageFromAnyThread(&local_heap);
      }
      local_heap.Safepoint();
    }

    Schedule(isolate_);
  }

  // Schedules task on background thread
  static void Schedule(Isolate* isolate) {
    auto task = std::make_unique<StressConcurrentAllocationTask>(isolate);
    const double kDelayInSeconds = 0.1;
    V8::GetCurrentPlatform()->CallDelayedOnWorkerThread(std::move(task),
                                                        kDelayInSeconds);
  }

 private:
  Isolate* isolate_;
};

class StressConcurrentAllocationObserver : public AllocationObserver {
 public:
  explicit StressConcurrentAllocationObserver(Heap* heap)
      : AllocationObserver(1024), heap_(heap) {}

  void Step(int bytes_allocated, Address, size_t) override {
    DCHECK(heap_->deserialization_complete());
    if (v8_flags.stress_concurrent_allocation) {
      // Only schedule task if --stress-concurrent-allocation is enabled. This
      // allows tests to disable flag even when Isolate was already initialized.
      StressConcurrentAllocationTask::Schedule(heap_->isolate());
    }
    heap_->RemoveAllocationObserversFromAllSpaces(this, this);
    heap_->need_to_remove_stress_concurrent_allocation_observer_ = false;
  }

 private:
  Heap* heap_;
};

namespace {

size_t ReturnNull() { return 0; }

}  // namespace

void Heap::SetUpSpaces(LinearAllocationArea& new_allocation_info,
                       LinearAllocationArea& old_allocation_info) {
  // Ensure SetUpFromReadOnlySpace has been ran.
  DCHECK_NOT_NULL(read_only_space_);

  if (v8_flags.sticky_mark_bits) {
    space_[OLD_SPACE] = std::make_unique<StickySpace>(this);
    old_space_ = static_cast<OldSpace*>(space_[OLD_SPACE].get());
  } else {
    space_[OLD_SPACE] = std::make_unique<OldSpace>(this);
    old_space_ = static_cast<OldSpace*>(space_[OLD_SPACE].get());
  }

  if (!v8_flags.single_generation) {
    if (!v8_flags.sticky_mark_bits) {
      if (v8_flags.minor_ms) {
        space_[NEW_SPACE] = std::make_unique<PagedNewSpace>(
            this, initial_semispace_size_, max_semi_space_size_);
      } else {
        space_[NEW_SPACE] = std::make_unique<SemiSpaceNewSpace>(
            this, initial_semispace_size_, max_semi_space_size_);
      }
      new_space_ = static_cast<NewSpace*>(space_[NEW_SPACE].get());
    }

    space_[NEW_LO_SPACE] =
        std::make_unique<NewLargeObjectSpace>(this, NewSpaceCapacity());
    new_lo_space_ =
        static_cast<NewLargeObjectSpace*>(space_[NEW_LO_SPACE].get());
  }

  space_[CODE_SPACE] = std::make_unique<CodeSpace>(this);
  code_space_ = static_cast<CodeSpace*>(space_[CODE_SPACE].get());

  if (isolate()->is_shared_space_isolate()) {
    space_[SHARED_SPACE] = std::make_unique<SharedSpace>(this);
    shared_space_ = static_cast<SharedSpace*>(space_[SHARED_SPACE].get());
  }

  space_[LO_SPACE] = std::make_unique<OldLargeObjectSpace>(this);
  lo_space_ = static_cast<OldLargeObjectSpace*>(space_[LO_SPACE].get());

  space_[CODE_LO_SPACE] = std::make_unique<CodeLargeObjectSpace>(this);
  code_lo_space_ =
      static_cast<CodeLargeObjectSpace*>(space_[CODE_LO_SPACE].get());

  space_[TRUSTED_SPACE] = std::make_unique<TrustedSpace>(this);
  trusted_space_ = static_cast<TrustedSpace*>(space_[TRUSTED_SPACE].get());

  space_[TRUSTED_LO_SPACE] = std::make_unique<TrustedLargeObjectSpace>(this);
  trusted_lo_space_ =
      static_cast<TrustedLargeObjectSpace*>(space_[TRUSTED_LO_SPACE].get());

  if (isolate()->is_shared_space_isolate()) {
    DCHECK(!v8_flags.sticky_mark_bits);
    space_[SHARED_LO_SPACE] = std::make_unique<SharedLargeObjectSpace>(this);
    shared_lo_space_ =
        static_cast<SharedLargeObjectSpace*>(space_[SHARED_LO_SPACE].get());

    space_[SHARED_TRUSTED_SPACE] = std::make_unique<SharedTrustedSpace>(this);
    shared_trusted_space_ =
        static_cast<SharedTrustedSpace*>(space_[SHARED_TRUSTED_SPACE].get());

    space_[SHARED_TRUSTED_LO_SPACE] =
        std::make_unique<SharedTrustedLargeObjectSpace>(this);
    shared_trusted_lo_space_ = static_cast<SharedTrustedLargeObjectSpace*>(
        space_[SHARED_TRUSTED_LO_SPACE].get());
  }

  if (isolate()->has_shared_space()) {
    Heap* heap = isolate()->shared_space_isolate()->heap();
    shared_allocation_space_ = heap->shared_space_;
    shared_lo_allocation_space_ = heap->shared_lo_space_;

    shared_trusted_allocation_space_ = heap->shared_trusted_space_;
    shared_trusted_lo_allocation_space_ = heap->shared_trusted_lo_space_;
  }

  main_thread_local_heap()->SetUpMainThread(new_allocation_info,
                                            old_allocation_info);

  base::TimeTicks startup_time = base::TimeTicks::Now();

  tracer_.reset(new GCTracer(this, startup_time));
  array_buffer_sweeper_.reset(new ArrayBufferSweeper(this));
  memory_measurement_.reset(new MemoryMeasurement(isolate()));
  if (v8_flags.memory_reducer) memory_reducer_.reset(new MemoryReducer(this));
  if (V8_UNLIKELY(TracingFlags::is_gc_stats_enabled())) {
    live_object_stats_.reset(new ObjectStats(this));
    dead_object_stats_.reset(new ObjectStats(this));
  }
  if (Heap::AllocationTrackerForDebugging::IsNeeded()) {
    allocation_tracker_for_debugging_ =
        std::make_unique<Heap::AllocationTrackerForDebugging>(this);
  }

  LOG(isolate_, IntPtrTEvent("heap-capacity", Capacity()));
  LOG(isolate_, IntPtrTEvent("heap-available", Available()));

  SetGetExternallyAllocatedMemoryInBytesCallback(ReturnNull);

  if (new_space() || v8_flags.sticky_mark_bits) {
    minor_gc_job_.reset(new MinorGCJob(this));
    minor_gc_task_observer_.reset(new ScheduleMinorGCTaskObserver(this));
  }

  if (v8_flags.stress_marking > 0) {
    stress_marking_percentage_ = NextStressMarkingLimit();
  }
  if (IsStressingScavenge()) {
    stress_scavenge_observer_ = new StressScavengeObserver(this);
    allocator()->new_space_allocator()->AddAllocationObserver(
        stress_scavenge_observer_);
  }

  if (v8_flags.memory_balancer) {
    mb_.reset(new MemoryBalancer(this, startup_time));
  }
}

void Heap::InitializeHashSeed() {
  DCHECK(!deserialization_complete_);
  uint64_t new_hash_seed;
  if (v8_flags.hash_seed == 0) {
    int64_t rnd = isolate()->random_number_generator()->NextInt64();
    new_hash_seed = static_cast<uint64_t>(rnd);
  } else {
    new_hash_seed = static_cast<uint64_t>(v8_flags.hash_seed);
  }
  Tagged<ByteArray> hash_seed = ReadOnlyRoots(this).hash_seed();
  MemCopy(hash_seed->begin(), reinterpret_cast<uint8_t*>(&new_hash_seed),
          kInt64Size);
}

std::shared_ptr<v8::TaskRunner> Heap::GetForegroundTaskRunner() const {
  return task_runner_;
}

// static
void Heap::InitializeOncePerProcess() {
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  HeapAllocator::InitializeOncePerProcess();
#endif
  MemoryAllocator::InitializeOncePerProcess();
  if (v8_flags.predictable) {
    ::heap::base::WorklistBase::EnforcePredictableOrder();
  }
}

void Heap::PrintMaxMarkingLimitReached() {
  PrintF("\n### Maximum marking limit reached = %.02lf\n",
         max_marking_limit_reached_.load(std::memory_order_relaxed));
}

void Heap::PrintMaxNewSpaceSizeReached() {
  PrintF("\n### Maximum new space size reached = %.02lf\n",
         stress_scavenge_observer_->MaxNewSpaceSizeReached());
}

int Heap::NextStressMarkingLimit() {
  return isolate()->fuzzer_rng()->NextInt(v8_flags.stress_marking + 1);
}

void Heap::WeakenDescriptorArrays(
    GlobalHandleVector<DescriptorArray> strong_descriptor_arrays) {
  if (incremental_marking()->IsMajorMarking()) {
    // During incremental/concurrent marking regular DescriptorArray objects are
    // treated with custom weakness. This weakness depends on
    // DescriptorArray::raw_gc_state() which is not set up properly upon
    // deserialization. The strong arrays are transitioned to weak ones at the
    // end of the GC.
    mark_compact_collector()->RecordStrongDescriptorArraysForWeakening(
        std::move(strong_descriptor_arrays));
    return;
  }

  // No GC is running, weaken the arrays right away.
  DisallowGarbageCollection no_gc;
  Tagged<Map> descriptor_array_map =
      ReadOnlyRoots(isolate()).descriptor_array_map();
  for (auto it = strong_descriptor_arrays.begin();
       it != strong_descriptor_arrays.end(); ++it) {
    Tagged<DescriptorArray> array = it.raw();
    DCHECK(IsStrongDescriptorArray(array));
    array->set_map_safe_transition_no_write_barrier(descriptor_array_map);
    DCHECK_EQ(array->raw_gc_state(kRelaxedLoad), 0);
  }
}

void Heap::NotifyDeserializationComplete() {
  // There are no concurrent/background threads yet.
  safepoint()->AssertMainThreadIsOnlyThread();

  FreeMainThreadLinearAllocationAreas();

  PagedSpaceIterator spaces(this);
  for (PagedSpace* s = spaces.Next(); s != nullptr; s = spaces.Next()) {
    // Shared space is used concurrently and cannot be shrunk.
    if (s->identity() == SHARED_SPACE) continue;
    if (isolate()->snapshot_available()) s->ShrinkImmortalImmovablePages();
#ifdef DEBUG
    // All pages right after bootstrapping must be marked as never-evacuate.
    for (PageMetadata* p : *s) {
      DCHECK(p->Chunk()->NeverEvacuate());
    }
#endif  // DEBUG
  }

  if (v8_flags.stress_concurrent_allocation) {
    stress_concurrent_allocation_observer_.reset(
        new StressConcurrentAllocationObserver(this));
    AddAllocationObserversToAllSpaces(
        stress_concurrent_allocation_observer_.get(),
        stress_concurrent_allocation_observer_.get());
    need_to_remove_stress_concurrent_allocation_observer_ = true;
  }

  // Deserialization will never create objects in new space.
  DCHECK_IMPLIES(new_space(), new_space()->Size() == 0);
  DCHECK_IMPLIES(new_lo_space(), new_lo_space()->Size() == 0);

  deserialization_complete_ = true;
}

void Heap::NotifyBootstrapComplete() {
  // This function is invoked for each native context creation. We are
  // interested only in the first native context.
  if (old_generation_capacity_after_bootstrap_ == 0) {
    old_generation_capacity_after_bootstrap_ = OldGenerationCapacity();
  }
}

void Heap::NotifyOldGenerationExpansion(LocalHeap* local_heap,
                                        AllocationSpace space,
                                        MutablePageMetadata* chunk_metadata) {
  // Pages created during bootstrapping may contain immortal immovable objects.
  if (!deserialization_complete()) {
    DCHECK_NE(NEW_SPACE, chunk_metadata->owner()->identity());
    chunk_metadata->Chunk()->MarkNeverEvacuate();
  }
  if (IsAnyCodeSpace(space)) {
    isolate()->AddCodeMemoryChunk(chunk_metadata);
  }

  const size_t kMemoryReducerActivationThreshold = 1 * MB;
  if (local_heap->is_main_thread_for(this) && memory_reducer() != nullptr &&
      old_generation_capacity_after_bootstrap_ && ms_count_ == 0 &&
      OldGenerationCapacity() >= old_generation_capacity_after_bootstrap_ +
                                     kMemoryReducerActivationThreshold &&
      v8_flags.memory_reducer_for_small_heaps) {
    memory_reducer()->NotifyPossibleGarbage();
  }
}

void Heap::SetEmbedderRootsHandler(EmbedderRootsHandler* handler) {
  embedder_roots_handler_ = handler;
}

EmbedderRootsHandler* Heap::GetEmbedderRootsHandler() const {
  return embedder_roots_handler_;
}

void Heap::AttachCppHeap(v8::CppHeap* cpp_heap) {
  // The API function should be a noop in case a CppHeap was passed on Isolate
  // creation.
  if (owning_cpp_heap_) {
    return;
  }

  CHECK_IMPLIES(incremental_marking(), !incremental_marking()->IsMarking());
  CppHeap::From(cpp_heap)->AttachIsolate(isolate());
  cpp_heap_ = cpp_heap;
}

void Heap::DetachCppHeap() {
  // The API function should be a noop in case a CppHeap was passed on Isolate
  // creation.
  if (owning_cpp_heap_) {
    return;
  }

  CppHeap::From(cpp_heap_)->DetachIsolate();
  cpp_heap_ = nullptr;
}

std::optional<StackState> Heap::overridden_stack_state() const {
  if (!embedder_stack_state_origin_) return {};
  return embedder_stack_state_;
}

void Heap::SetStackStart() {
  // If no main thread local heap has been set up (we're still in the
  // deserialization process), we don't need to set the stack start.
  if (main_thread_local_heap_ == nullptr) return;
  stack().SetStackStart();
}

::heap::base::Stack& Heap::stack() {
  CHECK_NOT_NULL(main_thread_local_heap_);
  return main_thread_local_heap_->stack_;
}

const ::heap::base::Stack& Heap::stack() const {
  CHECK_NOT_NULL(main_thread_local_heap_);
  return main_thread_local_heap_->stack_;
}

void Heap::StartTearDown() {
  if (owning_cpp_heap_) {
    // Release the pointer. The non-owning pointer is still set which allows
    // DetachCppHeap() to work properly.
    auto* cpp_heap = owning_cpp_heap_.release();
    DetachCppHeap();
    // Termination will free up all managed C++ memory and invoke destructors.
    cpp_heap->Terminate();
  }

  // Finish any ongoing sweeping to avoid stray background tasks still accessing
  // the heap during teardown.
  CompleteSweepingFull();

  if (v8_flags.concurrent_marking) {
    concurrent_marking()->Pause();
  }

  SetGCState(TEAR_DOWN);

  // Background threads may allocate and block until GC is performed. However
  // this might never happen when the main thread tries to quit and doesn't
  // process the event queue anymore. Avoid this deadlock by allowing all
  // allocations after tear down was requested to make sure all background
  // threads finish.
  collection_barrier_->NotifyShutdownRequested();

  // Main thread isn't going to allocate anymore.
  main_thread_local_heap()->FreeLinearAllocationAreas();

  FreeMainThreadLinearAllocationAreas();
}

void Heap::TearDownWithSharedHeap() {
  DCHECK_EQ(gc_state(), TEAR_DOWN);

  // Assert that there are no background threads left and no executable memory
  // chunks are unprotected.
  safepoint()->AssertMainThreadIsOnlyThread();

  // Now that all threads are stopped, verify the heap before tearing down the
  // heap/isolate.
  HeapVerifier::VerifyHeapIfEnabled(this);

  // Might use the external pointer which might be in the shared heap.
  external_string_table_.TearDown();

  // Publish shared object worklist for the main thread if incremental marking
  // is enabled for the shared heap.
  main_thread_local_heap()->marking_barrier()->PublishSharedIfNeeded();
}

void Heap::TearDown() {
  DCHECK_EQ(gc_state(), TEAR_DOWN);

  // Assert that there are no background threads left and no executable memory
  // chunks are unprotected.
  safepoint()->AssertMainThreadIsOnlyThread();

  DCHECK(concurrent_marking()->IsStopped());

  // It's too late for Heap::Verify() here, as parts of the Isolate are
  // already gone by the time this is called.

  UpdateMaximumCommitted();

  if (v8_flags.fuzzer_gc_analysis) {
    if (v8_flags.stress_marking > 0) {
      PrintMaxMarkingLimitReached();
    }
    if (IsStressingScavenge()) {
      PrintMaxNewSpaceSizeReached();
    }
  }

  minor_gc_task_observer_.reset();
  minor_gc_job_.reset();

  if (need_to_remove_stress_concurrent_allocation_observer_) {
    RemoveAllocationObserversFromAllSpaces(
        stress_concurrent_allocation_observer_.get(),
        stress_concurrent_allocation_observer_.get());
  }
  stress_concurrent_allocation_observer_.reset();

  if (IsStressingScavenge()) {
    allocator()->new_space_allocator()->RemoveAllocationObserver(
        stress_scavenge_observer_);
    delete stress_scavenge_observer_;
    stress_scavenge_observer_ = nullptr;
  }

  if (mark_compact_collector_) {
    mark_compact_collector_->TearDown();
    mark_compact_collector_.reset();
  }

  if (minor_mark_sweep_collector_) {
    minor_mark_sweep_collector_->TearDown();
    minor_mark_sweep_collector_.reset();
  }

  sweeper_->TearDown();
  sweeper_.reset();

  scavenger_collector_.reset();
  array_buffer_sweeper_.reset();
  incremental_marking_.reset();
  concurrent_marking_.reset();

  memory_measurement_.reset();
  allocation_tracker_for_debugging_.reset();
  ephemeron_remembered_set_.reset();

  if (memory_reducer_ != nullptr) {
    memory_reducer_->TearDown();
    memory_reducer_.reset();
  }

  live_object_stats_.reset();
  dead_object_stats_.reset();

  embedder_roots_handler_ = nullptr;

  if (cpp_heap_) {
    CppHeap::From(cpp_heap_)->DetachIsolate();
    cpp_heap_ = nullptr;
  }

  tracer_.reset();

  pretenuring_handler_.reset();

  for (int i = FIRST_MUTABLE_SPACE; i <= LAST_MUTABLE_SPACE; i++) {
    space_[i].reset();
  }

  isolate()->read_only_heap()->OnHeapTearDown(this);
  read_only_space_ = nullptr;

  memory_allocator()->TearDown();

  StrongRootsEntry* next = nullptr;
  for (StrongRootsEntry* current = strong_roots_head_; current;
       current = next) {
    next = current->next;
    delete current;
  }
  strong_roots_head_ = nullptr;

  memory_allocator_.reset();
}

void Heap::AddGCPrologueCallback(v8::Isolate::GCCallbackWithData callback,
                                 GCType gc_type, void* data) {
  gc_prologue_callbacks_.Add(
      callback, reinterpret_cast<v8::Isolate*>(isolate()), gc_type, data);
}

void Heap::RemoveGCPrologueCallback(v8::Isolate::GCCallbackWithData callback,
                                    void* data) {
  gc_prologue_callbacks_.Remove(callback, data);
}

void Heap::AddGCEpilogueCallback(v8::Isolate::GCCallbackWithData callback,
                                 GCType gc_type, void* data) {
  gc_epilogue_callbacks_.Add(
      callback, reinterpret_cast<v8::Isolate*>(isolate()), gc_type, data);
}

void Heap::RemoveGCEpilogueCallback(v8::Isolate::GCCallbackWithData callback,
                                    void* data) {
  gc_epilogue_callbacks_.Remove(callback, data);
}

namespace {
Handle<WeakArrayList> CompactWeakArrayList(Heap* heap,
                                           Handle<WeakArrayList> array,
                                           AllocationType allocation) {
  if (array->length() == 0) {
    return array;
  }
  int new_length = array->CountLiveWeakReferences();
  if (new_length == array->length()) {
    return array;
  }

  Handle<WeakArrayList> new_array = WeakArrayList::EnsureSpace(
      heap->isolate(),
      handle(ReadOnlyRoots(heap).empty_weak_array_list(), heap->isolate()),
      new_length, allocation);
  // Allocation might have caused GC and turned some of the elements into
  // cleared weak heap objects. Count the number of live references again and
  // fill in the new array.
  int copy_to = 0;
  for (int i = 0; i < array->length(); i++) {
    Tagged<MaybeObject> element = array->Get(i);
    if (element.IsCleared()) continue;
    new_array->Set(copy_to++, element);
  }
  new_array->set_length(copy_to);
  return new_array;
}

}  // anonymous namespace

void Heap::CompactWeakArrayLists() {
  // Find known PrototypeUsers and compact them.
  std::vector<Handle<PrototypeInfo>> prototype_infos;
  {
    HeapObjectIterator iterator(this);
    for (Tagged<HeapObject> o = iterator.Next(); !o.is_null();
         o = iterator.Next()) {
      if (IsPrototypeInfo(*o)) {
        Tagged<PrototypeInfo> prototype_info = Cast<PrototypeInfo>(o);
        if (IsWeakArrayList(prototype_info->prototype_users())) {
          prototype_infos.emplace_back(handle(prototype_info, isolate()));
        }
      }
    }
  }
  for (auto& prototype_info : prototype_infos) {
    DirectHandle<WeakArrayList> array(
        Cast<WeakArrayList>(prototype_info->prototype_users()), isolate());
    DCHECK(InOldSpace(*array) ||
           *array == ReadOnlyRoots(this).empty_weak_array_list());
    Tagged<WeakArrayList> new_array = PrototypeUsers::Compact(
        array, this, JSObject::PrototypeRegistryCompactionCallback,
        AllocationType::kOld);
    prototype_info->set_prototype_users(new_array);
  }

  // Find known WeakArrayLists and compact them.
  Handle<WeakArrayList> scripts(script_list(), isolate());
  DCHECK_IMPLIES(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL, InOldSpace(*scripts));
  scripts = CompactWeakArrayList(this, scripts, AllocationType::kOld);
  set_script_list(*scripts);
}

void Heap::AddRetainedMaps(DirectHandle<NativeContext> context,
                           GlobalHandleVector<Map> maps) {
  Handle<WeakArrayList> array(Cast<WeakArrayList>(context->retained_maps()),
                              isolate());
  if (array->IsFull()) {
    CompactRetainedMaps(*array);
  }
  int cur_length = array->length();
  array = WeakArrayList::EnsureSpace(
      isolate(), array, cur_length + static_cast<int>(maps.size()) * 2);
  if (*array != context->retained_maps()) {
    context->set_retained_maps(*array);
  }

  {
    DisallowGarbageCollection no_gc;
    Tagged<WeakArrayList> raw_array = *array;
    for (DirectHandle<Map> map : maps) {
      DCHECK(!InAnySharedSpace(*map));

      if (map->is_in_retained_map_list()) {
        continue;
      }

      raw_array->Set(cur_length, MakeWeak(*map));
      raw_array->Set(cur_length + 1,
                     Smi::FromInt(v8_flags.retain_maps_for_n_gc));
      cur_length += 2;
      raw_array->set_length(cur_length);

      map->set_is_in_retained_map_list(true);
    }
  }
}

void Heap::CompactRetainedMaps(Tagged<WeakArrayList> retained_maps) {
  int length = retained_maps->length();
  int new_length = 0;
  // This loop compacts the array by removing cleared weak cells.
  for (int i = 0; i < length; i += 2) {
    Tagged<MaybeObject> maybe_object = retained_maps->Get(i);
    if (maybe_object.IsCleared()) {
      continue;
    }

    DCHECK(maybe_object.IsWeak());

    Tagged<MaybeObject> age = retained_maps->Get(i + 1);
    DCHECK(IsSmi(age));
    if (i != new_length) {
      retained_maps->Set(new_length, maybe_object);
      retained_maps->Set(new_length + 1, age);
    }
    new_length += 2;
  }
  Tagged<HeapObject> undefined = ReadOnlyRoots(this).undefined_value();
  for (int i = new_length; i < length; i++) {
    retained_maps->Set(i, undefined);
  }
  if (new_length != length) retained_maps->set_length(new_length);
}

void Heap::FatalProcessOutOfMemory(const char* location) {
  V8::FatalProcessOutOfMemory(isolate(), location, V8::kHeapOOM);
}

#ifdef DEBUG

class PrintHandleVisitor : public RootVisitor {
 public:
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot p = start; p < end; ++p)
      PrintF("  handle %p to %p\n", p.ToVoidPtr(),
             reinterpret_cast<void*>((*p).ptr()));
  }
};

void Heap::PrintHandles() {
  PrintF("Handles:\n");
  PrintHandleVisitor v;
  isolate_->handle_scope_implementer()->Iterate(&v);
}

#endif

class CheckHandleCountVisitor : public RootVisitor {
 public:
  CheckHandleCountVisitor() : handle_count_(0) {}
  ~CheckHandleCountVisitor() override {
    CHECK_GT(HandleScope::kCheckHandleThreshold, handle_count_);
  }
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    handle_count_ += end - start;
  }

 private:
  ptrdiff_t handle_count_;
};

void Heap::CheckHandleCount() {
  CheckHandleCountVisitor v;
  isolate_->handle_scope_implementer()->Iterate(&v);
}

// static
int Heap::InsertIntoRememberedSetFromCode(MutablePageMetadata* chunk,
                                          size_t slot_offset) {
  // This is called during runtime by a builtin, therefore it is run in the main
  // thread.
  DCHECK_NULL(LocalHeap::Current());
  RememberedSet<OLD_TO_NEW>::Insert<AccessMode::NON_ATOMIC>(chunk, slot_offset);
  return 0;
}

#ifdef DEBUG
void Heap::VerifySlotRangeHasNoRecordedSlots(Address start, Address end) {
#ifndef V8_DISABLE_WRITE_BARRIERS
  PageMetadata* page = PageMetadata::FromAddress(start);
  RememberedSet<OLD_TO_NEW>::CheckNoneInRange(page, start, end);
  RememberedSet<OLD_TO_NEW_BACKGROUND>::CheckNoneInRange(page, start, end);
  RememberedSet<OLD_TO_SHARED>::CheckNoneInRange(page, start, end);
#endif
}
#endif

void Heap::ClearRecordedSlotRange(Address start, Address end) {
#ifndef V8_DISABLE_WRITE_BARRIERS
  MemoryChunk* chunk = MemoryChunk::FromAddress(start);
  DCHECK(!chunk->IsLargePage());
#if !V8_ENABLE_STICKY_MARK_BITS_BOOL
  if (!chunk->InYoungGeneration())
#endif
  {
    PageMetadata* page = PageMetadata::cast(chunk->Metadata());
    // This method will be invoked on objects in shared space for
    // internalization and string forwarding during GC.
    DCHECK(page->owner_identity() == OLD_SPACE ||
           page->owner_identity() == TRUSTED_SPACE ||
           page->owner_identity() == SHARED_SPACE);

    if (!page->SweepingDone()) {
      RememberedSet<OLD_TO_NEW>::RemoveRange(page, start, end,
                                             SlotSet::KEEP_EMPTY_BUCKETS);
      RememberedSet<OLD_TO_NEW_BACKGROUND>::RemoveRange(
          page, start, end, SlotSet::KEEP_EMPTY_BUCKETS);
      RememberedSet<OLD_TO_SHARED>::RemoveRange(page, start, end,
                                                SlotSet::KEEP_EMPTY_BUCKETS);
    }
  }
#endif
}

PagedSpace* PagedSpaceIterator::Next() {
  DCHECK_GE(counter_, FIRST_GROWABLE_PAGED_SPACE);
  while (counter_ <= LAST_GROWABLE_PAGED_SPACE) {
    PagedSpace* space = heap_->paged_space(counter_++);
    if (space) return space;
  }
  return nullptr;
}

class HeapObjectsFilter {
 public:
  virtual ~HeapObjectsFilter() = default;
  virtual bool SkipObject(Tagged<HeapObject> object) = 0;
};

class UnreachableObjectsFilter : public HeapObjectsFilter {
 public:
  explicit UnreachableObjectsFilter(Heap* heap) : heap_(heap) {
    MarkReachableObjects();
  }

  ~UnreachableObjectsFilter() override = default;

  bool SkipObject(Tagged<HeapObject> object) override {
    // Space object iterators should skip free space or filler objects.
    DCHECK(!IsFreeSpaceOrFiller(object));
    // If the bucket corresponding to the object's chunk does not exist, or the
    // object is not found in the bucket, return true.
    MemoryChunkMetadata* chunk = MemoryChunkMetadata::FromHeapObject(object);
    if (reachable_.count(chunk) == 0) return true;
    return reachable_[chunk]->count(object) == 0;
  }

 private:
  using BucketType = std::unordered_set<Tagged<HeapObject>, Object::Hasher>;

  bool MarkAsReachable(Tagged<HeapObject> object) {
    // If the bucket corresponding to the object's chunk does not exist, then
    // create an empty bucket.
    MemoryChunkMetadata* chunk = MemoryChunkMetadata::FromHeapObject(object);
    if (reachable_.count(chunk) == 0) {
      reachable_[chunk] = std::make_unique<BucketType>();
    }
    // Insert the object if not present; return whether it was indeed inserted.
    if (reachable_[chunk]->count(object)) return false;
    reachable_[chunk]->insert(object);
    return true;
  }

  class MarkingVisitor : public ObjectVisitorWithCageBases, public RootVisitor {
   public:
    explicit MarkingVisitor(UnreachableObjectsFilter* filter)
        : ObjectVisitorWithCageBases(filter->heap_), filter_(filter) {}

    void VisitMapPointer(Tagged<HeapObject> object) override {
      MarkHeapObject(UncheckedCast<Map>(object->map(cage_base())));
    }
    void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                       ObjectSlot end) override {
      MarkPointersImpl(start, end);
    }

    void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                       MaybeObjectSlot end) final {
      MarkPointersImpl(start, end);
    }

    void VisitInstructionStreamPointer(Tagged<Code> host,
                                       InstructionStreamSlot slot) override {
      Tagged<Object> maybe_code = slot.load(code_cage_base());
      Tagged<HeapObject> heap_object;
      if (maybe_code.GetHeapObject(&heap_object)) {
        MarkHeapObject(heap_object);
      }
    }

    void VisitCodeTarget(Tagged<InstructionStream> host,
                         RelocInfo* rinfo) final {
      Tagged<InstructionStream> target =
          InstructionStream::FromTargetAddress(rinfo->target_address());
      MarkHeapObject(target);
    }
    void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) final {
      MarkHeapObject(rinfo->target_object(cage_base()));
    }

    void VisitRootPointers(Root root, const char* description,
                           FullObjectSlot start, FullObjectSlot end) override {
      MarkPointersImpl(start, end);
    }
    void VisitRootPointers(Root root, const char* description,
                           OffHeapObjectSlot start,
                           OffHeapObjectSlot end) override {
      MarkPointersImpl(start, end);
    }

    void TransitiveClosure() {
      while (!marking_stack_.empty()) {
        Tagged<HeapObject> obj = marking_stack_.back();
        marking_stack_.pop_back();
        obj->Iterate(cage_base(), this);
      }
    }

   private:
    template <typename TSlot>
    V8_INLINE void MarkPointersImpl(TSlot start, TSlot end) {
      // Treat weak references as strong.
      for (TSlot p = start; p < end; ++p) {
        typename TSlot::TObject object = p.load(cage_base());
#ifdef V8_ENABLE_DIRECT_HANDLE
        if (object.ptr() == kTaggedNullAddress) continue;
#endif
        Tagged<HeapObject> heap_object;
        if (object.GetHeapObject(&heap_object)) {
          MarkHeapObject(heap_object);
        }
      }
    }

    V8_INLINE void MarkHeapObject(Tagged<HeapObject> heap_object) {
      if (filter_->MarkAsReachable(heap_object)) {
        marking_stack_.push_back(heap_object);
      }
    }

    UnreachableObjectsFilter* filter_;
    std::vector<Tagged<HeapObject>> marking_stack_;
  };

  friend class MarkingVisitor;

  void MarkReachableObjects() {
    MarkingVisitor visitor(this);
    heap_->stack().SetMarkerIfNeededAndCallback(
        [this, &visitor]() { heap_->IterateRoots(&visitor, {}); });
    visitor.TransitiveClosure();
  }

  Heap* heap_;
  DISALLOW_GARBAGE_COLLECTION(no_gc_)
  std::unordered_map<MemoryChunkMetadata*, std::unique_ptr<BucketType>,
                     base::hash<MemoryChunkMetadata*>>
      reachable_;
};

HeapObjectIterator::HeapObjectIterator(
    Heap* heap, HeapObjectIterator::HeapObjectsFiltering filtering)
    : HeapObjectIterator(
          heap,
          new SafepointScope(heap->isolate(),
                             heap->isolate()->is_shared_space_isolate()
                                 ? SafepointKind::kGlobal
                                 : SafepointKind::kIsolate),
          filtering) {}

HeapObjectIterator::HeapObjectIterator(Heap* heap,
                                       const SafepointScope& safepoint_scope,
                                       HeapObjectsFiltering filtering)
    : HeapObjectIterator(heap, nullptr, filtering) {}

HeapObjectIterator::HeapObjectIterator(
    Heap* heap, SafepointScope* safepoint_scope_or_nullptr,
    HeapObjectsFiltering filtering)
    : heap_(heap),
      safepoint_scope_(safepoint_scope_or_nullptr),
      space_iterator_(heap_) {
  heap_->MakeHeapIterable();
  switch (filtering) {
    case kFilterUnreachable:
      filter_ = std::make_unique<UnreachableObjectsFilter>(heap_);
      break;
    default:
      break;
  }
  // Start the iteration.
  CHECK(space_iterator_.HasNext());
  object_iterator_ = space_iterator_.Next()->GetObjectIterator(heap_);
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) heap_->tp_heap_->ResetIterator();
}

HeapObjectIterator::~HeapObjectIterator() = default;

Tagged<HeapObject> HeapObjectIterator::Next() {
  if (!filter_) return NextObject();

  Tagged<HeapObject> obj = NextObject();
  while (!obj.is_null() && filter_->SkipObject(obj)) obj = NextObject();
  return obj;
}

Tagged<HeapObject> HeapObjectIterator::NextObject() {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) return heap_->tp_heap_->NextObject();
  // No iterator means we are done.
  if (!object_iterator_) return Tagged<HeapObject>();

  Tagged<HeapObject> obj = object_iterator_->Next();
  // If the current iterator has more objects we are fine.
  if (!obj.is_null()) return obj;
  // Go though the spaces looking for one that has objects.
  while (space_iterator_.HasNext()) {
    object_iterator_ = space_iterator_.Next()->GetObjectIterator(heap_);
    obj = object_iterator_->Next();
    if (!obj.is_null()) return obj;
  }
  // Done with the last space.
  object_iterator_.reset();
  return Tagged<HeapObject>();
}

void Heap::UpdateTotalGCTime(base::TimeDelta duration) {
  total_gc_time_ms_ += duration;
}

void Heap::ExternalStringTable::CleanUpYoung() {
  int last = 0;
  Isolate* isolate = heap_->isolate();
  for (size_t i = 0; i < young_strings_.size(); ++i) {
    Tagged<Object> o = young_strings_[i];
    if (IsTheHole(o, isolate)) {
      continue;
    }
    // The real external string is already in one of these vectors and was or
    // will be processed. Re-processing it will add a duplicate to the vector.
    if (IsThinString(o)) continue;
    DCHECK(IsExternalString(o));
    if (InYoungGeneration(o)) {
      young_strings_[last++] = o;
    } else {
      old_strings_.push_back(o);
    }
  }
  young_strings_.resize(last);
}

void Heap::ExternalStringTable::CleanUpAll() {
  CleanUpYoung();
  int last = 0;
  Isolate* isolate = heap_->isolate();
  for (size_t i = 0; i < old_strings_.size(); ++i) {
    Tagged<Object> o = old_strings_[i];
    if (IsTheHole(o, isolate)) {
      continue;
    }
    // The real external string is already in one of these vectors and was or
    // will be processed. Re-processing it will add a duplicate to the vector.
    if (IsThinString(o)) continue;
    DCHECK(IsExternalString(o));
    DCHECK(!InYoungGeneration(o));
    old_strings_[last++] = o;
  }
  old_strings_.resize(last);
  if (v8_flags.verify_heap && !v8_flags.enable_third_party_heap) {
    Verify();
  }
}

void Heap::ExternalStringTable::TearDown() {
  for (size_t i = 0; i < young_strings_.size(); ++i) {
    Tagged<Object> o = young_strings_[i];
    // Dont finalize thin strings.
    if (IsThinString(o)) continue;
    heap_->FinalizeExternalString(Cast<ExternalString>(o));
  }
  young_strings_.clear();
  for (size_t i = 0; i < old_strings_.size(); ++i) {
    Tagged<Object> o = old_strings_[i];
    // Dont finalize thin strings.
    if (IsThinString(o)) continue;
    heap_->FinalizeExternalString(Cast<ExternalString>(o));
  }
  old_strings_.clear();
}

void Heap::RememberUnmappedPage(Address page, bool compacted) {
  // Tag the page pointer to make it findable in the dump file.
  if (compacted) {
    page ^= 0xC1EAD & (PageMetadata::kPageSize - 1);  // Cleared.
  } else {
    page ^= 0x1D1ED & (PageMetadata::kPageSize - 1);  // I died.
  }
  remembered_unmapped_pages_[remembered_unmapped_pages_index_] = page;
  remembered_unmapped_pages_index_++;
  remembered_unmapped_pages_index_ %= kRememberedUnmappedPages;
}

size_t Heap::YoungArrayBufferBytes() {
  return array_buffer_sweeper()->YoungBytes();
}

size_t Heap::OldArrayBufferBytes() {
  return array_buffer_sweeper()->OldBytes();
}

StrongRootsEntry* Heap::RegisterStrongRoots(const char* label,
                                            FullObjectSlot start,
                                            FullObjectSlot end) {
  // We're either on the main thread, or in a background thread with an active
  // local heap.
  DCHECK(isolate()->CurrentLocalHeap()->IsRunning());

  base::MutexGuard guard(&strong_roots_mutex_);

  StrongRootsEntry* entry = new StrongRootsEntry(label);
  entry->start = start;
  entry->end = end;
  entry->prev = nullptr;
  entry->next = strong_roots_head_;

  if (strong_roots_head_) {
    DCHECK_NULL(strong_roots_head_->prev);
    strong_roots_head_->prev = entry;
  }
  strong_roots_head_ = entry;

  return entry;
}

void Heap::UpdateStrongRoots(StrongRootsEntry* entry, FullObjectSlot start,
                             FullObjectSlot end) {
  entry->start = start;
  entry->end = end;
}

void Heap::UnregisterStrongRoots(StrongRootsEntry* entry) {
  // We're either on the main thread, or in a background thread with an active
  // local heap.
  DCHECK(isolate()->CurrentLocalHeap()->IsRunning());

  base::MutexGuard guard(&strong_roots_mutex_);

  StrongRootsEntry* prev = entry->prev;
  StrongRootsEntry* next = entry->next;

  if (prev) prev->next = next;
  if (next) next->prev = prev;

  if (strong_roots_head_ == entry) {
    DCHECK_NULL(prev);
    strong_roots_head_ = next;
  }

  delete entry;
}

void Heap::SetBuiltinsConstantsTable(Tagged<FixedArray> cache) {
  set_builtins_constants_table(cache);
}

void Heap::SetDetachedContexts(Tagged<WeakArrayList> detached_contexts) {
  set_detached_contexts(detached_contexts);
}

void Heap::PostFinalizationRegistryCleanupTaskIfNeeded() {
  // Only one cleanup task is posted at a time.
  if (!HasDirtyJSFinalizationRegistries() ||
      is_finalization_registry_cleanup_task_posted_) {
    return;
  }
  auto task = std::make_unique<FinalizationRegistryCleanupTask>(this);
  task_runner_->PostNonNestableTask(std::move(task));
  is_finalization_registry_cleanup_task_posted_ = true;
}

void Heap::EnqueueDirtyJSFinalizationRegistry(
    Tagged<JSFinalizationRegistry> finalization_registry,
    std::function<void(Tagged<HeapObject> object, ObjectSlot slot,
                       Tagged<Object> target)>
        gc_notify_updated_slot) {
  // Add a FinalizationRegistry to the tail of the dirty list.
  DCHECK(!HasDirtyJSFinalizationRegistries() ||
         IsJSFinalizationRegistry(dirty_js_finalization_registries_list()));
  DCHECK(IsUndefined(finalization_registry->next_dirty(), isolate()));
  DCHECK(!finalization_registry->scheduled_for_cleanup());
  finalization_registry->set_scheduled_for_cleanup(true);
  if (IsUndefined(dirty_js_finalization_registries_list_tail(), isolate())) {
    DCHECK(IsUndefined(dirty_js_finalization_registries_list(), isolate()));
    set_dirty_js_finalization_registries_list(finalization_registry);
    // dirty_js_finalization_registries_list_ is rescanned by
    // ProcessWeakListRoots.
  } else {
    Tagged<JSFinalizationRegistry> tail = Cast<JSFinalizationRegistry>(
        dirty_js_finalization_registries_list_tail());
    tail->set_next_dirty(finalization_registry);
    gc_notify_updated_slot(
        tail, tail->RawField(JSFinalizationRegistry::kNextDirtyOffset),
        finalization_registry);
  }
  set_dirty_js_finalization_registries_list_tail(finalization_registry);
  // dirty_js_finalization_registries_list_tail_ is rescanned by
  // ProcessWeakListRoots.
}

MaybeHandle<JSFinalizationRegistry> Heap::DequeueDirtyJSFinalizationRegistry() {
  // Take a FinalizationRegistry from the head of the dirty list for fairness.
  if (HasDirtyJSFinalizationRegistries()) {
    Handle<JSFinalizationRegistry> head(
        Cast<JSFinalizationRegistry>(dirty_js_finalization_registries_list()),
        isolate());
    set_dirty_js_finalization_registries_list(head->next_dirty());
    head->set_next_dirty(ReadOnlyRoots(this).undefined_value());
    if (*head == dirty_js_finalization_registries_list_tail()) {
      set_dirty_js_finalization_registries_list_tail(
          ReadOnlyRoots(this).undefined_value());
    }
    return head;
  }
  return {};
}

void Heap::RemoveDirtyFinalizationRegistriesOnContext(
    Tagged<NativeContext> context) {
  DisallowGarbageCollection no_gc;

  Isolate* isolate = this->isolate();
  Tagged<Object> prev = ReadOnlyRoots(isolate).undefined_value();
  Tagged<Object> current = dirty_js_finalization_registries_list();
  while (!IsUndefined(current, isolate)) {
    Tagged<JSFinalizationRegistry> finalization_registry =
        Cast<JSFinalizationRegistry>(current);
    if (finalization_registry->native_context() == context) {
      if (IsUndefined(prev, isolate)) {
        set_dirty_js_finalization_registries_list(
            finalization_registry->next_dirty());
      } else {
        Cast<JSFinalizationRegistry>(prev)->set_next_dirty(
            finalization_registry->next_dirty());
      }
      finalization_registry->set_scheduled_for_cleanup(false);
      current = finalization_registry->next_dirty();
      finalization_registry->set_next_dirty(
          ReadOnlyRoots(isolate).undefined_value());
    } else {
      prev = current;
      current = finalization_registry->next_dirty();
    }
  }
  set_dirty_js_finalization_registries_list_tail(prev);
}

void Heap::KeepDuringJob(DirectHandle<HeapObject> target) {
  DCHECK(IsUndefined(weak_refs_keep_during_job()) ||
         IsOrderedHashSet(weak_refs_keep_during_job()));
  Handle<OrderedHashSet> table;
  if (IsUndefined(weak_refs_keep_during_job(), isolate())) {
    table = isolate()->factory()->NewOrderedHashSet();
  } else {
    table =
        handle(Cast<OrderedHashSet>(weak_refs_keep_during_job()), isolate());
  }
  table = OrderedHashSet::Add(isolate(), table, target).ToHandleChecked();
  set_weak_refs_keep_during_job(*table);
}

void Heap::ClearKeptObjects() {
  set_weak_refs_keep_during_job(ReadOnlyRoots(isolate()).undefined_value());
}

size_t Heap::NumberOfTrackedHeapObjectTypes() {
  return ObjectStats::OBJECT_STATS_COUNT;
}

size_t Heap::ObjectCountAtLastGC(size_t index) {
  if (live_object_stats_ == nullptr || index >= ObjectStats::OBJECT_STATS_COUNT)
    return 0;
  return live_object_stats_->object_count_last_gc(index);
}

size_t Heap::ObjectSizeAtLastGC(size_t index) {
  if (live_object_stats_ == nullptr || index >= ObjectStats::OBJECT_STATS_COUNT)
    return 0;
  return live_object_stats_->object_size_last_gc(index);
}

bool Heap::GetObjectTypeName(size_t index, const char** object_type,
                             const char** object_sub_type) {
  if (index >= ObjectStats::OBJECT_STATS_COUNT) return false;

  switch (static_cast<int>(index)) {
#define COMPARE_AND_RETURN_NAME(name) \
  case name:                          \
    *object_type = #name;             \
    *object_sub_type = "";            \
    return true;
    INSTANCE_TYPE_LIST(COMPARE_AND_RETURN_NAME)
#undef COMPARE_AND_RETURN_NAME

#define COMPARE_AND_RETURN_NAME(name)                       \
  case ObjectStats::FIRST_VIRTUAL_TYPE + ObjectStats::name: \
    *object_type = #name;                                   \
    *object_sub_type = "";                                  \
    return true;
    VIRTUAL_INSTANCE_TYPE_LIST(COMPARE_AND_RETURN_NAME)
#undef COMPARE_AND_RETURN_NAME
  }
  return false;
}

size_t Heap::NumberOfNativeContexts() {
  int result = 0;
  Tagged<Object> context = native_contexts_list();
  while (!IsUndefined(context, isolate())) {
    ++result;
    Tagged<Context> native_context = Cast<Context>(context);
    context = native_context->next_context_link();
  }
  return result;
}

std::vector<Handle<NativeContext>> Heap::FindAllNativeContexts() {
  std::vector<Handle<NativeContext>> result;
  Tagged<Object> context = native_contexts_list();
  while (!IsUndefined(context, isolate())) {
    Tagged<NativeContext> native_context = Cast<NativeContext>(context);
    result.push_back(handle(native_context, isolate()));
    context = native_context->next_context_link();
  }
  return result;
}

std::vector<Tagged<WeakArrayList>> Heap::FindAllRetainedMaps() {
  std::vector<Tagged<WeakArrayList>> result;
  Tagged<Object> context = native_contexts_list();
  while (!IsUndefined(context, isolate())) {
    Tagged<NativeContext> native_context = Cast<NativeContext>(context);
    result.push_back(Cast<WeakArrayList>(native_context->retained_maps()));
    context = native_context->next_context_link();
  }
  return result;
}

size_t Heap::NumberOfDetachedContexts() {
  // The detached_contexts() array has two entries per detached context.
  return detached_contexts()->length() / 2;
}

bool Heap::AllowedToBeMigrated(Tagged<Map> map, Tagged<HeapObject> obj,
                               AllocationSpace dst) {
  // Object migration is governed by the following rules:
  //
  // 1) Objects in new-space can be migrated to the old space
  //    that matches their target space or they stay in new-space.
  // 2) Objects in old-space stay in the same space when migrating.
  // 3) Fillers (two or more words) can migrate due to left-trimming of
  //    fixed arrays in new-space or old space.
  // 4) Fillers (one word) can never migrate, they are skipped by
  //    incremental marking explicitly to prevent invalid pattern.
  //
  // Since this function is used for debugging only, we do not place
  // asserts here, but check everything explicitly.
  if (map == ReadOnlyRoots(this).one_pointer_filler_map()) return false;
  InstanceType type = map->instance_type();
  MutablePageMetadata* chunk = MutablePageMetadata::FromHeapObject(obj);
  AllocationSpace src = chunk->owner_identity();
  switch (src) {
    case NEW_SPACE:
      return dst == NEW_SPACE || dst == OLD_SPACE;
    case OLD_SPACE:
      return dst == OLD_SPACE;
    case CODE_SPACE:
      return dst == CODE_SPACE && type == INSTRUCTION_STREAM_TYPE;
    case SHARED_SPACE:
      return dst == SHARED_SPACE;
    case TRUSTED_SPACE:
      return dst == TRUSTED_SPACE;
    case SHARED_TRUSTED_SPACE:
      return dst == SHARED_TRUSTED_SPACE;
    case LO_SPACE:
    case CODE_LO_SPACE:
    case NEW_LO_SPACE:
    case SHARED_LO_SPACE:
    case TRUSTED_LO_SPACE:
    case SHARED_TRUSTED_LO_SPACE:
    case RO_SPACE:
      return false;
  }
  UNREACHABLE();
}

size_t Heap::EmbedderAllocationCounter() const {
  return cpp_heap_ ? CppHeap::From(cpp_heap_)->allocated_size() : 0;
}

void Heap::CreateObjectStats() {
  if (V8_LIKELY(!TracingFlags::is_gc_stats_enabled())) return;
  if (!live_object_stats_) {
    live_object_stats_.reset(new ObjectStats(this));
  }
  if (!dead_object_stats_) {
    dead_object_stats_.reset(new ObjectStats(this));
  }
}

Tagged<Map> Heap::GcSafeMapOfHeapObject(Tagged<HeapObject> object) {
  PtrComprCageBase cage_base(isolate());
  MapWord map_word = object->map_word(cage_base, kRelaxedLoad);
  if (map_word.IsForwardingAddress()) {
    return map_word.ToForwardingAddress(object)->map(cage_base);
  }
  return map_word.ToMap();
}

Tagged<GcSafeCode> Heap::GcSafeGetCodeFromInstructionStream(
    Tagged<HeapObject> instruction_stream, Address inner_pointer) {
  Tagged<InstructionStream> istream =
      UncheckedCast<InstructionStream>(instruction_stream);
  DCHECK(!istream.is_null());
  DCHECK(GcSafeInstructionStreamContains(istream, inner_pointer));
  return UncheckedCast<GcSafeCode>(istream->raw_code(kAcquireLoad));
}

bool Heap::GcSafeInstructionStreamContains(Tagged<InstructionStream> istream,
                                           Address addr) {
  Tagged<Map> map = GcSafeMapOfHeapObject(istream);
  DCHECK_EQ(map, ReadOnlyRoots(this).instruction_stream_map());

  Builtin builtin_lookup_result =
      OffHeapInstructionStream::TryLookupCode(isolate(), addr);
  if (Builtins::IsBuiltinId(builtin_lookup_result)) {
    // Builtins don't have InstructionStream objects.
    DCHECK(!Builtins::IsBuiltinId(istream->code(kAcquireLoad)->builtin_id()));
    return false;
  }

  Address start = istream.address();
  Address end = start + istream->SizeFromMap(map);
  return start <= addr && addr < end;
}

std::optional<Tagged<InstructionStream>>
Heap::GcSafeTryFindInstructionStreamForInnerPointer(Address inner_pointer) {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    Address start = tp_heap_->GetObjectFromInnerPointer(inner_pointer);
    return UncheckedCast<InstructionStream>(HeapObject::FromAddress(start));
  }

  std::optional<Address> start =
      ThreadIsolation::StartOfJitAllocationAt(inner_pointer);
  if (start.has_value()) {
    return UncheckedCast<InstructionStream>(HeapObject::FromAddress(*start));
  }

  return {};
}

std::optional<Tagged<GcSafeCode>> Heap::GcSafeTryFindCodeForInnerPointer(
    Address inner_pointer) {
  Builtin maybe_builtin =
      OffHeapInstructionStream::TryLookupCode(isolate(), inner_pointer);
  if (Builtins::IsBuiltinId(maybe_builtin)) {
    return Cast<GcSafeCode>(isolate()->builtins()->code(maybe_builtin));
  }

  std::optional<Tagged<InstructionStream>> maybe_istream =
      GcSafeTryFindInstructionStreamForInnerPointer(inner_pointer);
  if (!maybe_istream) return {};

  return GcSafeGetCodeFromInstructionStream(*maybe_istream, inner_pointer);
}

Tagged<Code> Heap::FindCodeForInnerPointer(Address inner_pointer) {
  return GcSafeFindCodeForInnerPointer(inner_pointer)->UnsafeCastToCode();
}

Tagged<GcSafeCode> Heap::GcSafeFindCodeForInnerPointer(Address inner_pointer) {
  std::optional<Tagged<GcSafeCode>> maybe_code =
      GcSafeTryFindCodeForInnerPointer(inner_pointer);
  // Callers expect that the code object is found.
  CHECK(maybe_code.has_value());
  return UncheckedCast<GcSafeCode>(maybe_code.value());
}

std::optional<Tagged<Code>> Heap::TryFindCodeForInnerPointerForPrinting(
    Address inner_pointer) {
  if (InSpaceSlow(inner_pointer, i::CODE_SPACE) ||
      InSpaceSlow(inner_pointer, i::CODE_LO_SPACE) ||
      i::OffHeapInstructionStream::PcIsOffHeap(isolate(), inner_pointer)) {
    std::optional<Tagged<GcSafeCode>> maybe_code =
        GcSafeTryFindCodeForInnerPointer(inner_pointer);
    if (maybe_code.has_value()) {
      return maybe_code.value()->UnsafeCastToCode();
    }
  }
  return {};
}

void Heap::CombinedGenerationalAndSharedBarrierSlow(Tagged<HeapObject> object,
                                                    Address slot,
                                                    Tagged<HeapObject> value) {
  if (HeapObjectInYoungGeneration(value)) {
    Heap::GenerationalBarrierSlow(object, slot, value);

  } else {
    DCHECK(MemoryChunk::FromHeapObject(value)->InWritableSharedSpace());
    DCHECK(!InWritableSharedSpace(object));
    Heap::SharedHeapBarrierSlow(object, slot);
  }
}

void Heap::CombinedGenerationalAndSharedEphemeronBarrierSlow(
    Tagged<EphemeronHashTable> table, Address slot, Tagged<HeapObject> value) {
  if (HeapObjectInYoungGeneration(value)) {
    MutablePageMetadata* table_chunk =
        MutablePageMetadata::FromHeapObject(table);
    table_chunk->heap()->RecordEphemeronKeyWrite(table, slot);

  } else {
    DCHECK(MemoryChunk::FromHeapObject(value)->InWritableSharedSpace());
    DCHECK(!InWritableSharedSpace(table));
    Heap::SharedHeapBarrierSlow(table, slot);
  }
}

void Heap::GenerationalBarrierSlow(Tagged<HeapObject> object, Address slot,
                                   Tagged<HeapObject> value) {
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  MutablePageMetadata* metadata = MutablePageMetadata::cast(chunk->Metadata());
  if (LocalHeap::Current() == nullptr) {
    RememberedSet<OLD_TO_NEW>::Insert<AccessMode::NON_ATOMIC>(
        metadata, chunk->Offset(slot));
  } else {
    RememberedSet<OLD_TO_NEW_BACKGROUND>::Insert<AccessMode::ATOMIC>(
        metadata, chunk->Offset(slot));
  }
}

void Heap::SharedHeapBarrierSlow(Tagged<HeapObject> object, Address slot) {
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  DCHECK(!chunk->InWritableSharedSpace());
  RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(
      MutablePageMetadata::cast(chunk->Metadata()), chunk->Offset(slot));
}

void Heap::RecordEphemeronKeyWrite(Tagged<EphemeronHashTable> table,
                                   Address slot) {
  ephemeron_remembered_set_->RecordEphemeronKeyWrite(table, slot);
}

void Heap::EphemeronKeyWriteBarrierFromCode(Address raw_object,
                                            Address key_slot_address,
                                            Isolate* isolate) {
  Tagged<EphemeronHashTable> table =
      Cast<EphemeronHashTable>(Tagged<Object>(raw_object));
  ObjectSlot key_slot(key_slot_address);
  CombinedEphemeronWriteBarrier(table, key_slot, *key_slot,
                                UPDATE_WRITE_BARRIER);
}

enum RangeWriteBarrierMode {
  kDoGenerationalOrShared = 1 << 0,
  kDoMarking = 1 << 1,
  kDoEvacuationSlotRecording = 1 << 2,
};

template <int kModeMask, typename TSlot>
void Heap::WriteBarrierForRangeImpl(MemoryChunk* source_chunk,
                                    Tagged<HeapObject> object, TSlot start_slot,
                                    TSlot end_slot) {
  // At least one of generational or marking write barrier should be requested.
  static_assert(kModeMask & (kDoGenerationalOrShared | kDoMarking));
  // kDoEvacuationSlotRecording implies kDoMarking.
  static_assert(!(kModeMask & kDoEvacuationSlotRecording) ||
                (kModeMask & kDoMarking));

  MarkingBarrier* marking_barrier = nullptr;
  static constexpr Tagged_t kPageMask =
      ~static_cast<Tagged_t>(PageMetadata::kPageSize - 1);
  Tagged_t cached_uninteresting_page =
      static_cast<Tagged_t>(read_only_space_->FirstPageAddress()) & kPageMask;

  if (kModeMask & kDoMarking) {
    marking_barrier = WriteBarrier::CurrentMarkingBarrier(object);
  }

  MarkCompactCollector* collector = this->mark_compact_collector();
  MutablePageMetadata* source_page_metadata =
      MutablePageMetadata::cast(source_chunk->Metadata());

  for (TSlot slot = start_slot; slot < end_slot; ++slot) {
    // If we *only* need the generational or shared WB, we can skip objects
    // residing on uninteresting pages.
    Tagged_t compressed_page;
    if (kModeMask == kDoGenerationalOrShared) {
      Tagged_t tagged_value = *slot.location();
      if (HAS_SMI_TAG(tagged_value)) continue;
      compressed_page = tagged_value & kPageMask;
      if (compressed_page == cached_uninteresting_page) {
#if DEBUG
        typename TSlot::TObject value = *slot;
        Tagged<HeapObject> value_heap_object;
        if (value.GetHeapObject(&value_heap_object)) {
          CHECK(!Heap::InYoungGeneration(value_heap_object));
          CHECK(!InWritableSharedSpace(value_heap_object));
        }
#endif  // DEBUG
        continue;
      }
      // Fall through to decompressing the pointer and fetching its actual
      // page header flags.
    }
    typename TSlot::TObject value = *slot;
    Tagged<HeapObject> value_heap_object;
    if (!value.GetHeapObject(&value_heap_object)) continue;

    if (kModeMask & kDoGenerationalOrShared) {
      if (Heap::InYoungGeneration(value_heap_object)) {
        RememberedSet<OLD_TO_NEW>::Insert<AccessMode::NON_ATOMIC>(
            source_page_metadata, source_chunk->Offset(slot.address()));
      } else if (InWritableSharedSpace(value_heap_object)) {
        RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(
            source_page_metadata, source_chunk->Offset(slot.address()));
      } else if (kModeMask == kDoGenerationalOrShared) {
        cached_uninteresting_page = compressed_page;
      }
    }

    if (kModeMask & kDoMarking) {
      marking_barrier->MarkValue(object, value_heap_object);
      if (kModeMask & kDoEvacuationSlotRecording) {
        collector->RecordSlot(source_chunk, HeapObjectSlot(slot),
                              value_heap_object);
      }
    }
  }
}

// Instantiate Heap::WriteBarrierForRange() for ObjectSlot and MaybeObjectSlot.
template void Heap::WriteBarrierForRange<ObjectSlot>(Tagged<HeapObject> object,
                                                     ObjectSlot start_slot,
                                                     ObjectSlot end_slot);
template void Heap::WriteBarrierForRange<MaybeObjectSlot>(
    Tagged<HeapObject> object, MaybeObjectSlot start_slot,
    MaybeObjectSlot end_slot);

template <typename TSlot>
void Heap::WriteBarrierForRange(Tagged<HeapObject> object, TSlot start_slot,
                                TSlot end_slot) {
  if (v8_flags.disable_write_barriers) return;
  MemoryChunk* source_chunk = MemoryChunk::FromHeapObject(object);
  base::Flags<RangeWriteBarrierMode> mode;

  if (!HeapObjectInYoungGeneration(object) &&
      !source_chunk->InWritableSharedSpace()) {
    mode |= kDoGenerationalOrShared;
  }

  if (incremental_marking()->IsMarking()) {
    mode |= kDoMarking;
    if (!source_chunk->ShouldSkipEvacuationSlotRecording()) {
      mode |= kDoEvacuationSlotRecording;
    }
  }

  switch (mode) {
    // Nothing to be done.
    case 0:
      return;

    // Generational only.
    case kDoGenerationalOrShared:
      return WriteBarrierForRangeImpl<kDoGenerationalOrShared>(
          source_chunk, object, start_slot, end_slot);
    // Marking, no evacuation slot recording.
    case kDoMarking:
      return WriteBarrierForRangeImpl<kDoMarking>(source_chunk, object,
                                                  start_slot, end_slot);
    // Marking with evacuation slot recording.
    case kDoMarking | kDoEvacuationSlotRecording:
      return WriteBarrierForRangeImpl<kDoMarking | kDoEvacuationSlotRecording>(
          source_chunk, object, start_slot, end_slot);

    // Generational and marking, no evacuation slot recording.
    case kDoGenerationalOrShared | kDoMarking:
      return WriteBarrierForRangeImpl<kDoGenerationalOrShared | kDoMarking>(
          source_chunk, object, start_slot, end_slot);

    // Generational and marking with evacuation slot recording.
    case kDoGenerationalOrShared | kDoMarking | kDoEvacuationSlotRecording:
      return WriteBarrierForRangeImpl<kDoGenerationalOrShared | kDoMarking |
                                      kDoEvacuationSlotRecording>(
          source_chunk, object, start_slot, end_slot);

    default:
      UNREACHABLE();
  }
}

void Heap::GenerationalBarrierForCodeSlow(Tagged<InstructionStream> host,
                                          RelocInfo* rinfo,
                                          Tagged<HeapObject> object) {
  DCHECK(InYoungGeneration(object));
  const MarkCompactCollector::RecordRelocSlotInfo info =
      MarkCompactCollector::ProcessRelocInfo(host, rinfo, object);

  base::MutexGuard write_scope(info.page_metadata->mutex());
  RememberedSet<OLD_TO_NEW>::InsertTyped(info.page_metadata, info.slot_type,
                                         info.offset);
}

bool Heap::PageFlagsAreConsistent(Tagged<HeapObject> object) {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    return true;
  }
  MemoryChunkMetadata* metadata = MemoryChunkMetadata::FromHeapObject(object);
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);

  // Slim chunk flags consistency.
  CHECK_EQ(chunk->IsFlagSet(MemoryChunk::INCREMENTAL_MARKING),
           chunk->IsMarking());

  if (!v8_flags.sticky_mark_bits) {
    AllocationSpace identity = metadata->owner()->identity();

    // Generation consistency.
    CHECK_EQ(identity == NEW_SPACE || identity == NEW_LO_SPACE,
             chunk->InYoungGeneration());
  }

  // Marking consistency.
  if (metadata->IsWritable()) {
    // RO_SPACE can be shared between heaps, so we can't use RO_SPACE objects to
    // find a heap. The exception is when the ReadOnlySpace is writeable, during
    // bootstrapping, so explicitly allow this case.
    Heap* heap = Heap::FromWritableHeapObject(object);
    if (chunk->InWritableSharedSpace()) {
      // The marking bit is not set for chunks in shared spaces during MinorMS
      // concurrent marking.
      CHECK_EQ(chunk->IsMarking(),
               heap->incremental_marking()->IsMajorMarking());
    } else {
      CHECK_EQ(chunk->IsMarking(), heap->incremental_marking()->IsMarking());
    }
  } else {
    // Non-writable RO_SPACE must never have marking flag set.
    CHECK(!chunk->IsMarking());
  }
  return true;
}

#ifdef DEBUG
void Heap::IncrementObjectCounters() {
  isolate_->counters()->objs_since_last_full()->Increment();
  isolate_->counters()->objs_since_last_young()->Increment();
}
#endif  // DEBUG

bool Heap::IsStressingScavenge() {
  return v8_flags.stress_scavenge > 0 && new_space();
}

void Heap::SetIsMarkingFlag(bool value) {
  isolate()->isolate_data()->is_marking_flag_ = value;
}

uint8_t* Heap::IsMarkingFlagAddress() {
  return &isolate()->isolate_data()->is_marking_flag_;
}

void Heap::SetIsMinorMarkingFlag(bool value) {
  isolate()->isolate_data()->is_minor_marking_flag_ = value;
}

uint8_t* Heap::IsMinorMarkingFlagAddress() {
  return &isolate()->isolate_data()->is_minor_marking_flag_;
}

StrongRootAllocatorBase::StrongRootAllocatorBase(Isolate* isolate)
    : StrongRootAllocatorBase(isolate->heap()) {}

// StrongRootBlocks are allocated as a block of addresses, prefixed with a
// StrongRootsEntry pointer:
//
//   | StrongRootsEntry*
//   | Address 1
//   | ...
//   | Address N
//
// The allocate method registers the range "Address 1" to "Address N" with the
// heap as a strong root array, saves that entry in StrongRootsEntry*, and
// returns a pointer to Address 1.
Address* StrongRootAllocatorBase::allocate_impl(size_t n) {
  void* block = base::Malloc(sizeof(StrongRootsEntry*) + n * sizeof(Address));

  StrongRootsEntry** header = reinterpret_cast<StrongRootsEntry**>(block);
  Address* ret = reinterpret_cast<Address*>(reinterpret_cast<char*>(block) +
                                            sizeof(StrongRootsEntry*));

  memset(ret, kNullAddress, n * sizeof(Address));
  *header = heap()->RegisterStrongRoots(
      "StrongRootAllocator", FullObjectSlot(ret), FullObjectSlot(ret + n));

  return ret;
}

void StrongRootAllocatorBase::deallocate_impl(Address* p, size_t n) noexcept {
  // The allocate method returns a pointer to Address 1, so the deallocate
  // method has to offset that pointer back by sizeof(StrongRootsEntry*).
  void* block = reinterpret_cast<char*>(p) - sizeof(StrongRootsEntry*);
  StrongRootsEntry** header = reinterpret_cast<StrongRootsEntry**>(block);

  heap()->UnregisterStrongRoots(*header);

  base::Free(block);
}

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
void Heap::set_allocation_timeout(int allocation_timeout) {
  heap_allocator_->SetAllocationTimeout(allocation_timeout);
}
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

void Heap::FinishSweepingIfOutOfWork() {
  if (sweeper()->major_sweeping_in_progress() &&
      sweeper()->UsingMajorSweeperTasks() &&
      !sweeper()->AreMajorSweeperTasksRunning()) {
    // At this point we know that all concurrent sweeping tasks have run
    // out of work and quit: all pages are swept. The main thread still needs
    // to complete sweeping though.
    DCHECK_IMPLIES(!delay_sweeper_tasks_for_testing_,
                   !sweeper()->HasUnsweptPagesForMajorSweeping());
    EnsureSweepingCompleted(SweepingForcedFinalizationMode::kV8Only);
  }
  if (cpp_heap()) {
    // Ensure that sweeping is also completed for the C++ managed heap, if one
    // exists and it's out of work.
    CppHeap::From(cpp_heap())->FinishSweepingIfOutOfWork();
  }
}

void Heap::EnsureSweepingCompleted(SweepingForcedFinalizationMode mode) {
  CompleteArrayBufferSweeping(this);

  if (sweeper()->sweeping_in_progress()) {
    bool was_minor_sweeping_in_progress = minor_sweeping_in_progress();
    bool was_major_sweeping_in_progress = major_sweeping_in_progress();
    sweeper()->EnsureMajorCompleted();

    if (was_major_sweeping_in_progress) {
      TRACE_GC_EPOCH_WITH_FLOW(tracer(), GCTracer::Scope::MC_COMPLETE_SWEEPING,
                               ThreadKind::kMain,
                               sweeper_->GetTraceIdForFlowEvent(
                                   GCTracer::Scope::MC_COMPLETE_SWEEPING),
                               TRACE_EVENT_FLAG_FLOW_IN);
      old_space()->RefillFreeList();
      code_space()->RefillFreeList();
      if (shared_space()) {
        shared_space()->RefillFreeList();
      }

      trusted_space()->RefillFreeList();
    }

    if (!v8_flags.sticky_mark_bits && v8_flags.minor_ms && use_new_space() &&
        was_minor_sweeping_in_progress) {
      TRACE_GC_EPOCH_WITH_FLOW(
          tracer(), GCTracer::Scope::MINOR_MS_COMPLETE_SWEEPING,
          ThreadKind::kMain,
          sweeper_->GetTraceIdForFlowEvent(
              GCTracer::Scope::MINOR_MS_COMPLETE_SWEEPING),
          TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
      paged_new_space()->paged_space()->RefillFreeList();
    }

    tracer()->NotifyFullSweepingCompleted();

#ifdef VERIFY_HEAP
    if (v8_flags.verify_heap) {
      EvacuationVerifier verifier(this);
      verifier.Run();
    }
#endif
  }

  if (mode == SweepingForcedFinalizationMode::kUnifiedHeap && cpp_heap()) {
    // Ensure that sweeping is also completed for the C++ managed heap, if one
    // exists.
    CppHeap::From(cpp_heap())->FinishSweepingIfRunning();
    DCHECK(!CppHeap::From(cpp_heap())->sweeper().IsSweepingInProgress());
  }

  DCHECK_IMPLIES(
      mode == SweepingForcedFinalizationMode::kUnifiedHeap || !cpp_heap(),
      !tracer()->IsSweepingInProgress());
}

void Heap::EnsureYoungSweepingCompleted() {
  if (!sweeper()->minor_sweeping_in_progress()) return;
  DCHECK(!v8_flags.sticky_mark_bits);

  TRACE_GC_EPOCH_WITH_FLOW(
      tracer(), GCTracer::Scope::MINOR_MS_COMPLETE_SWEEPING, ThreadKind::kMain,
      sweeper_->GetTraceIdForFlowEvent(
          GCTracer::Scope::MINOR_MS_COMPLETE_SWEEPING),
      TRACE_EVENT_FLAG_FLOW_IN);

  sweeper()->EnsureMinorCompleted();
  paged_new_space()->paged_space()->RefillFreeList();

  tracer()->NotifyYoungSweepingCompleted();
}

void Heap::NotifyLoadingStarted() {
  update_allocation_limits_after_loading_ = true;
  UpdateLoadStartTime();
}

void Heap::NotifyLoadingEnded() {
  RecomputeLimitsAfterLoadingIfNeeded();
  if (auto* job = incremental_marking()->incremental_marking_job()) {
    // The task will start incremental marking (if needed not already started)
    // and advance marking if incremental marking is active.
    job->ScheduleTask();
  }
}

void Heap::UpdateLoadStartTime() {
  load_start_time_ms_.store(MonotonicallyIncreasingTimeInMs(),
                            std::memory_order_relaxed);
}

EmbedderStackStateScope::EmbedderStackStateScope(
    Heap* heap, EmbedderStackStateOrigin origin, StackState stack_state)
    : heap_(heap),
      old_stack_state_(heap_->embedder_stack_state_),
      old_origin_(heap->embedder_stack_state_origin_) {
  // Explicit scopes take precedence over implicit scopes.
  if (origin == EmbedderStackStateOrigin::kExplicitInvocation ||
      heap_->embedder_stack_state_origin_ !=
          EmbedderStackStateOrigin::kExplicitInvocation) {
    heap_->embedder_stack_state_ = stack_state;
    heap_->embedder_stack_state_origin_ = origin;
  }
}

EmbedderStackStateScope::~EmbedderStackStateScope() {
  heap_->embedder_stack_state_ = old_stack_state_;
  heap_->embedder_stack_state_origin_ = old_origin_;
}

CppClassNamesAsHeapObjectNameScope::CppClassNamesAsHeapObjectNameScope(
    v8::CppHeap* heap)
    : scope_(std::make_unique<cppgc::internal::ClassNameAsHeapObjectNameScope>(
          *CppHeap::From(heap))) {}

CppClassNamesAsHeapObjectNameScope::~CppClassNamesAsHeapObjectNameScope() =
    default;

#if V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT || \
    V8_HEAP_USE_PKU_JIT_WRITE_PROTECT || V8_HEAP_USE_BECORE_JIT_WRITE_PROTECT

CodePageMemoryModificationScopeForDebugging::
    CodePageMemoryModificationScopeForDebugging(Heap* heap,
                                                VirtualMemory* reservation,
                                                base::AddressRegion region)
    : rwx_write_scope_("Write access for zapping.") {
#if !defined(DEBUG) && !defined(VERIFY_HEAP) && !defined(USE_SIMULATOR)
  UNREACHABLE();
#endif
}

CodePageMemoryModificationScopeForDebugging::
    CodePageMemoryModificationScopeForDebugging(MemoryChunkMetadata* chunk)
    : rwx_write_scope_("Write access for zapping.") {
#if !defined(DEBUG) && !defined(VERIFY_HEAP) && !defined(USE_SIMULATOR)
  UNREACHABLE();
#endif
}

CodePageMemoryModificationScopeForDebugging::
    ~CodePageMemoryModificationScopeForDebugging() {}

#else  // V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT ||
       // V8_HEAP_USE_PKU_JIT_WRITE_PROTECT ||
       // V8_HEAP_USE_BECORE_JIT_WRITE_PROTECT

CodePageMemoryModificationScopeForDebugging::
    CodePageMemoryModificationScopeForDebugging(Heap* heap,
                                                VirtualMemory* reservation,
                                                base::AddressRegion region) {
#if !defined(DEBUG) && !defined(VERIFY_HEAP) && !defined(USE_SIMULATOR)
  UNREACHABLE();
#endif
}

CodePageMemoryModificationScopeForDebugging::
    CodePageMemoryModificationScopeForDebugging(MemoryChunkMetadata* chunk) {
#if !defined(DEBUG) && !defined(VERIFY_HEAP) && !defined(USE_SIMULATOR)
  UNREACHABLE();
#endif
}

CodePageMemoryModificationScopeForDebugging::
    ~CodePageMemoryModificationScopeForDebugging() {}

#endif

#include "src/objects/object-macros-undef.h"

}  // namespace internal
}  // namespace v8
