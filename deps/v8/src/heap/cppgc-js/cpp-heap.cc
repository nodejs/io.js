// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc-js/cpp-heap.h"

#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>

#include "include/cppgc/heap-consistency.h"
#include "include/cppgc/platform.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-platform.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/time.h"
#include "src/execution/isolate-inl.h"
#include "src/flags/flags.h"
#include "src/handles/handles.h"
#include "src/handles/traced-handles.h"
#include "src/heap/base/stack.h"
#include "src/heap/cppgc-js/cpp-marking-state.h"
#include "src/heap/cppgc-js/cpp-snapshot.h"
#include "src/heap/cppgc-js/unified-heap-marking-state-inl.h"
#include "src/heap/cppgc-js/unified-heap-marking-state.h"
#include "src/heap/cppgc-js/unified-heap-marking-verifier.h"
#include "src/heap/cppgc-js/unified-heap-marking-visitor.h"
#include "src/heap/cppgc/concurrent-marker.h"
#include "src/heap/cppgc/gc-info-table.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/marking-state.h"
#include "src/heap/cppgc/marking-visitor.h"
#include "src/heap/cppgc/metric-recorder.h"
#include "src/heap/cppgc/object-allocator.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/platform.h"
#include "src/heap/cppgc/prefinalizer-handler.h"
#include "src/heap/cppgc/raw-heap.h"
#include "src/heap/cppgc/stats-collector.h"
#include "src/heap/cppgc/sweeper.h"
#include "src/heap/cppgc/unmarker.h"
#include "src/heap/cppgc/visitor.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/minor-mark-sweep.h"
#include "src/heap/traced-handles-marking-visitor.h"
#include "src/init/v8.h"
#include "src/profiler/heap-profiler.h"

namespace v8 {

namespace internal {

class MinorGCHeapGrowing
    : public cppgc::internal::StatsCollector::AllocationObserver {
 public:
  explicit MinorGCHeapGrowing(cppgc::internal::StatsCollector& stats_collector)
      : stats_collector_(stats_collector) {
    stats_collector.RegisterObserver(this);
  }
  virtual ~MinorGCHeapGrowing() = default;

  void AllocatedObjectSizeIncreased(size_t) final {}
  void AllocatedObjectSizeDecreased(size_t) final {}
  void ResetAllocatedObjectSize(size_t allocated_object_size) final {
    ConfigureLimit(allocated_object_size);
  }

  bool LimitReached() const {
    return stats_collector_.allocated_object_size() >= limit_for_atomic_gc_;
  }

 private:
  void ConfigureLimit(size_t allocated_object_size) {
    // Constant growing factor for growing the heap limit.
    static constexpr double kGrowingFactor = 1.5;
    // For smaller heaps, allow allocating at least LAB in each regular space
    // before triggering GC again.
    static constexpr size_t kMinLimitIncrease =
        cppgc::internal::kPageSize *
        cppgc::internal::RawHeap::kNumberOfRegularSpaces;

    const size_t size = std::max(allocated_object_size, initial_heap_size_);
    limit_for_atomic_gc_ = std::max(static_cast<size_t>(size * kGrowingFactor),
                                    size + kMinLimitIncrease);
  }

  cppgc::internal::StatsCollector& stats_collector_;
  size_t initial_heap_size_ = 1 * cppgc::internal::kMB;
  size_t limit_for_atomic_gc_ = 0;  // See ConfigureLimit().
};

}  // namespace internal

// static
std::unique_ptr<CppHeap> CppHeap::Create(v8::Platform* platform,
                                         const CppHeapCreateParams& params) {
  return std::make_unique<internal::CppHeap>(platform, params.custom_spaces,
                                             params.marking_support,
                                             params.sweeping_support);
}

cppgc::AllocationHandle& CppHeap::GetAllocationHandle() {
  return internal::CppHeap::From(this)->object_allocator();
}

cppgc::HeapHandle& CppHeap::GetHeapHandle() {
  return *internal::CppHeap::From(this);
}

void CppHeap::Terminate() { internal::CppHeap::From(this)->Terminate(); }

cppgc::HeapStatistics CppHeap::CollectStatistics(
    cppgc::HeapStatistics::DetailLevel detail_level) {
  return internal::CppHeap::From(this)->AsBase().CollectStatistics(
      detail_level);
}

void CppHeap::CollectCustomSpaceStatisticsAtLastGC(
    std::vector<cppgc::CustomSpaceIndex> custom_spaces,
    std::unique_ptr<CustomSpaceStatisticsReceiver> receiver) {
  return internal::CppHeap::From(this)->CollectCustomSpaceStatisticsAtLastGC(
      std::move(custom_spaces), std::move(receiver));
}

void CppHeap::EnableDetachedGarbageCollectionsForTesting() {
  return internal::CppHeap::From(this)
      ->EnableDetachedGarbageCollectionsForTesting();
}

void CppHeap::CollectGarbageForTesting(cppgc::EmbedderStackState stack_state) {
  return internal::CppHeap::From(this)->CollectGarbageForTesting(
      internal::CppHeap::CollectionType::kMajor, stack_state);
}

void CppHeap::CollectGarbageInYoungGenerationForTesting(
    cppgc::EmbedderStackState stack_state) {
  return internal::CppHeap::From(this)->CollectGarbageForTesting(
      internal::CppHeap::CollectionType::kMinor, stack_state);
}

namespace internal {

namespace {

class CppgcPlatformAdapter final : public cppgc::Platform {
 public:
  explicit CppgcPlatformAdapter(v8::Platform* platform)
      : platform_(platform),
        page_allocator_(platform->GetPageAllocator()
                            ? platform->GetPageAllocator()
                            : &cppgc::internal::GetGlobalPageAllocator()) {}

  CppgcPlatformAdapter(const CppgcPlatformAdapter&) = delete;
  CppgcPlatformAdapter& operator=(const CppgcPlatformAdapter&) = delete;

  PageAllocator* GetPageAllocator() final { return page_allocator_; }

  double MonotonicallyIncreasingTime() final {
    return platform_->MonotonicallyIncreasingTime();
  }

  std::shared_ptr<TaskRunner> GetForegroundTaskRunner() final {
    // If no Isolate has been set, there's no task runner to leverage for
    // foreground tasks. In detached mode the original platform handles the
    // task runner retrieval.
    if (!isolate_ && !is_in_detached_mode_) return nullptr;

    return platform_->GetForegroundTaskRunner(isolate_);
  }

  std::unique_ptr<JobHandle> PostJob(TaskPriority priority,
                                     std::unique_ptr<JobTask> job_task) final {
    return platform_->PostJob(priority, std::move(job_task));
  }

  TracingController* GetTracingController() override {
    return platform_->GetTracingController();
  }

  void SetIsolate(v8::Isolate* isolate) { isolate_ = isolate; }
  void EnableDetachedModeForTesting() { is_in_detached_mode_ = true; }

 private:
  v8::Platform* platform_;
  cppgc::PageAllocator* page_allocator_;
  v8::Isolate* isolate_ = nullptr;
  bool is_in_detached_mode_ = false;
};

class UnifiedHeapConcurrentMarker
    : public cppgc::internal::ConcurrentMarkerBase {
 public:
  UnifiedHeapConcurrentMarker(
      cppgc::internal::HeapBase& heap, Heap* v8_heap,
      cppgc::internal::MarkingWorklists& marking_worklists,
      ::heap::base::IncrementalMarkingSchedule& incremental_marking_schedule,
      cppgc::Platform* platform,
      UnifiedHeapMarkingState& unified_heap_marking_state,
      CppHeap::CollectionType collection_type)
      : cppgc::internal::ConcurrentMarkerBase(
            heap, marking_worklists, incremental_marking_schedule, platform),
        v8_heap_(v8_heap),
        collection_type_(collection_type) {}

  std::unique_ptr<cppgc::Visitor> CreateConcurrentMarkingVisitor(
      cppgc::internal::ConcurrentMarkingState&) const final;

 private:
  Heap* const v8_heap_;
  CppHeap::CollectionType collection_type_;
};

std::unique_ptr<cppgc::Visitor>
UnifiedHeapConcurrentMarker::CreateConcurrentMarkingVisitor(
    cppgc::internal::ConcurrentMarkingState& marking_state) const {
  return std::make_unique<ConcurrentUnifiedHeapMarkingVisitor>(
      heap(), v8_heap_, marking_state, collection_type_);
}

void FatalOutOfMemoryHandlerImpl(const std::string& reason,
                                 const SourceLocation&, HeapBase* heap) {
  V8::FatalProcessOutOfMemory(
      static_cast<v8::internal::CppHeap*>(heap)->isolate(), reason.c_str());
}

void GlobalFatalOutOfMemoryHandlerImpl(const std::string& reason,
                                       const SourceLocation&, HeapBase* heap) {
  V8::FatalProcessOutOfMemory(nullptr, reason.c_str());
}

class UnifiedHeapConservativeMarkingVisitor final
    : public cppgc::internal::ConservativeMarkingVisitor {
 public:
  UnifiedHeapConservativeMarkingVisitor(
      HeapBase& heap, MutatorMarkingState& mutator_marking_state,
      cppgc::Visitor& visitor)
      : ConservativeMarkingVisitor(heap, mutator_marking_state, visitor) {}
  ~UnifiedHeapConservativeMarkingVisitor() override = default;

  void SetConservativeTracedHandlesMarkingVisitor(
      std::unique_ptr<ConservativeTracedHandlesMarkingVisitor>
          global_handle_marking_visitor) {
    marking_visitor_ = std::move(global_handle_marking_visitor);
  }

  void TraceConservativelyIfNeeded(const void* address) override {
    ConservativeMarkingVisitor::TraceConservativelyIfNeeded(address);
    if (marking_visitor_) {
      marking_visitor_->VisitPointer(address);
    }
  }

 private:
  std::unique_ptr<ConservativeTracedHandlesMarkingVisitor> marking_visitor_;
};

}  // namespace

class UnifiedHeapMarker final : public cppgc::internal::MarkerBase {
 public:
  UnifiedHeapMarker(Heap* v8_heap, cppgc::internal::HeapBase& cpp_heap,
                    cppgc::Platform* platform,
                    cppgc::internal::MarkingConfig config);

  ~UnifiedHeapMarker() final = default;

  cppgc::internal::MarkingWorklists& GetMarkingWorklists() {
    return marking_worklists_;
  }

  cppgc::internal::MutatorMarkingState& GetMutatorMarkingState() {
    return static_cast<cppgc::internal::MutatorMarkingState&>(
        marking_visitor_->marking_state_);
  }

  UnifiedHeapMarkingState& GetMutatorUnifiedHeapMarkingState() {
    return mutator_unified_heap_marking_state_;
  }

  UnifiedHeapConservativeMarkingVisitor& conservative_visitor() final {
    return conservative_marking_visitor_;
  }

 protected:
  cppgc::Visitor& visitor() final { return *marking_visitor_; }
  ::heap::base::StackVisitor& stack_visitor() final {
    return conservative_marking_visitor_;
  }

 private:
  UnifiedHeapMarkingState mutator_unified_heap_marking_state_;
  std::unique_ptr<MutatorUnifiedHeapMarkingVisitor> marking_visitor_;
  UnifiedHeapConservativeMarkingVisitor conservative_marking_visitor_;
};

UnifiedHeapMarker::UnifiedHeapMarker(Heap* v8_heap,
                                     cppgc::internal::HeapBase& heap,
                                     cppgc::Platform* platform,
                                     cppgc::internal::MarkingConfig config)
    : cppgc::internal::MarkerBase(heap, platform, config),
      mutator_unified_heap_marking_state_(v8_heap, nullptr,
                                          config.collection_type),
      marking_visitor_(std::make_unique<MutatorUnifiedHeapMarkingVisitor>(
          heap, mutator_marking_state_, mutator_unified_heap_marking_state_)),
      conservative_marking_visitor_(heap, mutator_marking_state_,
                                    *marking_visitor_) {
  concurrent_marker_ = std::make_unique<UnifiedHeapConcurrentMarker>(
      heap_, v8_heap, marking_worklists_, *schedule_, platform_,
      mutator_unified_heap_marking_state_, config.collection_type);
}

void CppHeap::MetricRecorderAdapter::AddMainThreadEvent(
    const GCCycle& cppgc_event) {
  auto* tracer = GetIsolate()->heap()->tracer();
  if (cppgc_event.type == MetricRecorder::GCCycle::Type::kMinor) {
    DCHECK(!last_young_gc_event_);
    last_young_gc_event_ = cppgc_event;
    tracer->NotifyYoungCppGCCompleted();
  } else {
    DCHECK(!last_full_gc_event_);
    last_full_gc_event_ = cppgc_event;
    tracer->NotifyFullCppGCCompleted();
  }
}

void CppHeap::MetricRecorderAdapter::AddMainThreadEvent(
    const MainThreadIncrementalMark& cppgc_event) {
  // Incremental marking steps might be nested in V8 marking steps. In such
  // cases, stash the relevant values and delegate to V8 to report them. For
  // non-nested steps, report to the Recorder directly.
  if (cpp_heap_.is_in_v8_marking_step_) {
    last_incremental_mark_event_ = cppgc_event;
    return;
  }
  // This is a standalone incremental marking step.
  const std::shared_ptr<metrics::Recorder>& recorder =
      GetIsolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) return;
  incremental_mark_batched_events_.events.emplace_back();
  incremental_mark_batched_events_.events.back().cpp_wall_clock_duration_in_us =
      cppgc_event.duration_us;
  if (incremental_mark_batched_events_.events.size() == kMaxBatchedEvents) {
    recorder->AddMainThreadEvent(std::move(incremental_mark_batched_events_),
                                 GetContextId());
    incremental_mark_batched_events_ = {};
  }
}

void CppHeap::MetricRecorderAdapter::AddMainThreadEvent(
    const MainThreadIncrementalSweep& cppgc_event) {
  // Incremental sweeping steps are never nested inside V8 sweeping steps, so
  // report to the Recorder directly.
  const std::shared_ptr<metrics::Recorder>& recorder =
      GetIsolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) return;
  incremental_sweep_batched_events_.events.emplace_back();
  incremental_sweep_batched_events_.events.back()
      .cpp_wall_clock_duration_in_us = cppgc_event.duration_us;
  if (incremental_sweep_batched_events_.events.size() == kMaxBatchedEvents) {
    recorder->AddMainThreadEvent(std::move(incremental_sweep_batched_events_),
                                 GetContextId());
    incremental_sweep_batched_events_ = {};
  }
}

void CppHeap::MetricRecorderAdapter::FlushBatchedIncrementalEvents() {
  const std::shared_ptr<metrics::Recorder>& recorder =
      GetIsolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!incremental_mark_batched_events_.events.empty()) {
    recorder->AddMainThreadEvent(std::move(incremental_mark_batched_events_),
                                 GetContextId());
    incremental_mark_batched_events_ = {};
  }
  if (!incremental_sweep_batched_events_.events.empty()) {
    recorder->AddMainThreadEvent(std::move(incremental_sweep_batched_events_),
                                 GetContextId());
    incremental_sweep_batched_events_ = {};
  }
}

bool CppHeap::MetricRecorderAdapter::FullGCMetricsReportPending() const {
  return last_full_gc_event_.has_value();
}

bool CppHeap::MetricRecorderAdapter::YoungGCMetricsReportPending() const {
  return last_young_gc_event_.has_value();
}

const std::optional<cppgc::internal::MetricRecorder::GCCycle>
CppHeap::MetricRecorderAdapter::ExtractLastFullGcEvent() {
  auto res = std::move(last_full_gc_event_);
  last_full_gc_event_.reset();
  return res;
}

const std::optional<cppgc::internal::MetricRecorder::GCCycle>
CppHeap::MetricRecorderAdapter::ExtractLastYoungGcEvent() {
  auto res = std::move(last_young_gc_event_);
  last_young_gc_event_.reset();
  return res;
}

const std::optional<cppgc::internal::MetricRecorder::MainThreadIncrementalMark>
CppHeap::MetricRecorderAdapter::ExtractLastIncrementalMarkEvent() {
  auto res = std::move(last_incremental_mark_event_);
  last_incremental_mark_event_.reset();
  return res;
}

void CppHeap::MetricRecorderAdapter::ClearCachedEvents() {
  incremental_mark_batched_events_.events.clear();
  incremental_sweep_batched_events_.events.clear();
  last_incremental_mark_event_.reset();
  last_full_gc_event_.reset();
  last_young_gc_event_.reset();
}

Isolate* CppHeap::MetricRecorderAdapter::GetIsolate() const {
  DCHECK_NOT_NULL(cpp_heap_.isolate());
  return reinterpret_cast<Isolate*>(cpp_heap_.isolate());
}

v8::metrics::Recorder::ContextId CppHeap::MetricRecorderAdapter::GetContextId()
    const {
  DCHECK_NOT_NULL(GetIsolate());
  if (GetIsolate()->context().is_null())
    return v8::metrics::Recorder::ContextId::Empty();
  HandleScope scope(GetIsolate());
  return GetIsolate()->GetOrRegisterRecorderContextId(
      GetIsolate()->native_context());
}

// static
void CppHeap::InitializeOncePerProcess() {
  cppgc::internal::GetGlobalOOMHandler().SetCustomHandler(
      &GlobalFatalOutOfMemoryHandlerImpl);
}

CppHeap::CppHeap(
    v8::Platform* platform,
    const std::vector<std::unique_ptr<cppgc::CustomSpaceBase>>& custom_spaces,
    cppgc::Heap::MarkingType marking_support,
    cppgc::Heap::SweepingType sweeping_support)
    : cppgc::internal::HeapBase(
          std::make_shared<CppgcPlatformAdapter>(platform), custom_spaces,
          cppgc::internal::HeapBase::StackSupport::
              kSupportsConservativeStackScan,
          marking_support, sweeping_support, *this),
      minor_gc_heap_growing_(
          std::make_unique<MinorGCHeapGrowing>(*stats_collector())),
      cross_heap_remembered_set_(*this) {
  // Enter no GC scope. `AttachIsolate()` removes this and allows triggering
  // garbage collections.
  no_gc_scope_++;
  stats_collector()->RegisterObserver(this);
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  object_allocator().UpdateAllocationTimeout();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
}

CppHeap::~CppHeap() {
  if (isolate_) {
    isolate_->heap()->DetachCppHeap();
  }
}

void CppHeap::Terminate() {
  // Must not be attached to a heap when invoking termination GCs.
  CHECK(!isolate_);
  // Gracefully terminate the C++ heap invoking destructors.
  HeapBase::Terminate();
}

namespace {

class SweepingOnMutatorThreadForGlobalHandlesScope final {
 public:
  explicit SweepingOnMutatorThreadForGlobalHandlesScope(
      TracedHandles& traced_handles)
      : traced_handles_(traced_handles) {
    traced_handles_.SetIsSweepingOnMutatorThread(true);
  }
  ~SweepingOnMutatorThreadForGlobalHandlesScope() {
    traced_handles_.SetIsSweepingOnMutatorThread(false);
  }

  TracedHandles& traced_handles_;
};

class SweepingOnMutatorThreadForGlobalHandlesObserver final
    : public cppgc::internal::Sweeper::SweepingOnMutatorThreadObserver {
 public:
  SweepingOnMutatorThreadForGlobalHandlesObserver(CppHeap& cpp_heap,
                                                  TracedHandles& traced_handles)
      : cppgc::internal::Sweeper::SweepingOnMutatorThreadObserver(
            cpp_heap.sweeper()),
        traced_handles_(traced_handles) {}

  void Start() override { traced_handles_.SetIsSweepingOnMutatorThread(true); }

  void End() override { traced_handles_.SetIsSweepingOnMutatorThread(false); }

 private:
  TracedHandles& traced_handles_;
};

class MoveListenerImpl final : public HeapProfilerNativeMoveListener,
                               public cppgc::internal::MoveListener {
 public:
  MoveListenerImpl(HeapProfiler* profiler, CppHeap* heap)
      : HeapProfilerNativeMoveListener(profiler), heap_(heap) {}
  ~MoveListenerImpl() {
    if (active_) {
      heap_->UnregisterMoveListener(this);
    }
  }

  // HeapProfilerNativeMoveListener implementation:
  void StartListening() override {
    if (active_) return;
    active_ = true;
    heap_->RegisterMoveListener(this);
  }
  void StopListening() override {
    if (!active_) return;
    active_ = false;
    heap_->UnregisterMoveListener(this);
  }

  // cppgc::internal::MoveListener implementation:
  void OnMove(uint8_t* from, uint8_t* to,
              size_t size_including_header) override {
    ObjectMoveEvent(reinterpret_cast<Address>(from),
                    reinterpret_cast<Address>(to),
                    static_cast<int>(size_including_header));
  }

 private:
  CppHeap* heap_;
  bool active_ = false;
};

}  // namespace

void CppHeap::AttachIsolate(Isolate* isolate) {
  CHECK(!in_detached_testing_mode_);
  CHECK_NULL(isolate_);
  isolate_ = isolate;
  heap_ = isolate->heap();
  static_cast<CppgcPlatformAdapter*>(platform())
      ->SetIsolate(reinterpret_cast<v8::Isolate*>(isolate_));
  if (auto* heap_profiler = isolate_->heap_profiler()) {
    heap_profiler->AddBuildEmbedderGraphCallback(&CppGraphBuilder::Run, this);
    heap_profiler->set_native_move_listener(
        std::make_unique<MoveListenerImpl>(heap_profiler, this));
  }
  SetMetricRecorder(std::make_unique<MetricRecorderAdapter>(*this));
  oom_handler().SetCustomHandler(&FatalOutOfMemoryHandlerImpl);
  UpdateGCCapabilitiesFromFlags();
  sweeping_on_mutator_thread_observer_ =
      std::make_unique<SweepingOnMutatorThreadForGlobalHandlesObserver>(
          *this, *isolate_->traced_handles());
  no_gc_scope_--;

  // Propagate overridden stack state to the attached heap, if necessary.
  // TODO(b/326503098): This should not be required, to be removed when the
  // issue is resolved.
  CHECK(!override_stack_state_scope_);
  if (detached_override_stack_state_) {
    override_stack_state_scope_ = std::make_unique<EmbedderStackStateScope>(
        heap_, EmbedderStackStateOrigin::kExplicitInvocation,
        detached_override_stack_state_.value());
    detached_override_stack_state_.reset();
  }
}

void CppHeap::DetachIsolate() {
  // TODO(chromium:1056170): Investigate whether this can be enforced with a
  // CHECK across all relevant embedders and setups.
  if (!isolate_) return;

  // Finish any ongoing garbage collection.
  if (isolate_->heap()->incremental_marking()->IsMarking()) {
    isolate_->heap()->FinalizeIncrementalMarkingAtomically(
        i::GarbageCollectionReason::kExternalFinalize);
  }
  sweeper_.FinishIfRunning();

  sweeping_on_mutator_thread_observer_.reset();

  if (auto* heap_profiler = isolate_->heap_profiler()) {
    heap_profiler->RemoveBuildEmbedderGraphCallback(&CppGraphBuilder::Run,
                                                    this);
    heap_profiler->set_native_move_listener(nullptr);
  }
  SetMetricRecorder(nullptr);

  // Propagate overridden stack state from the attached heap, if necessary.
  // TODO(b/326503098): This should not be required, to be removed when the
  // issue is resolved.
  CHECK(!detached_override_stack_state_);
  if (override_stack_state_scope_) {
    detached_override_stack_state_ = heap_->overridden_stack_state();
    override_stack_state_scope_.reset();
  }

  isolate_ = nullptr;
  heap_ = nullptr;
  // Any future garbage collections will ignore the V8->C++ references.
  oom_handler().SetCustomHandler(nullptr);
  // Enter no GC scope.
  no_gc_scope_++;
}

::heap::base::Stack* CppHeap::stack() {
  return isolate_ ? &isolate_->heap()->stack() : HeapBase::stack();
}

namespace {

bool IsMemoryReducingGC(CppHeap::GarbageCollectionFlags flags) {
  return flags & CppHeap::GarbageCollectionFlagValues::kReduceMemory;
}

bool IsForceGC(CppHeap::GarbageCollectionFlags flags) {
  return flags & CppHeap::GarbageCollectionFlagValues::kForced;
}

bool ShouldReduceMemory(CppHeap::GarbageCollectionFlags flags) {
  return IsMemoryReducingGC(flags) || IsForceGC(flags);
}

constexpr size_t kIncrementalMarkingCheckInterval = 128 * KB;

}  // namespace

CppHeap::MarkingType CppHeap::SelectMarkingType() const {
  // For now, force atomic marking for minor collections.
  if (*collection_type_ == CollectionType::kMinor) return MarkingType::kAtomic;

  if (IsForceGC(current_gc_flags_) && !force_incremental_marking_for_testing_)
    return MarkingType::kAtomic;

  const MarkingType marking_type = marking_support();

  // CollectionType is major at this point. Check the surrounding
  // MarkCompactCollector for whether we should rely on background threads in
  // this GC cycle.
  if (marking_type == MarkingType::kIncrementalAndConcurrent && heap_ &&
      !heap_->mark_compact_collector()->UseBackgroundThreadsInCycle()) {
    return MarkingType::kIncremental;
  }

  return marking_support();
}

CppHeap::SweepingType CppHeap::SelectSweepingType() const {
  if (IsForceGC(current_gc_flags_)) return SweepingType::kAtomic;

  return sweeping_support();
}

void CppHeap::UpdateGCCapabilitiesFromFlags() {
  CHECK_IMPLIES(v8_flags.cppheap_concurrent_marking,
                v8_flags.cppheap_incremental_marking);
  if (v8_flags.cppheap_concurrent_marking) {
    marking_support_ = static_cast<MarkingType>(
        std::min(marking_support_, MarkingType::kIncrementalAndConcurrent));
  } else if (v8_flags.cppheap_incremental_marking) {
    marking_support_ = static_cast<MarkingType>(
        std::min(marking_support_, MarkingType::kIncremental));
  } else {
    marking_support_ = MarkingType::kAtomic;
  }

  sweeping_support_ = v8_flags.single_threaded_gc
                          ? CppHeap::SweepingType::kIncremental
                          : CppHeap::SweepingType::kIncrementalAndConcurrent;

  page_backend_->page_pool().SetDecommitPooledPages(
      v8_flags.decommit_pooled_pages);
}

void CppHeap::InitializeMarking(CollectionType collection_type,
                                GarbageCollectionFlags gc_flags) {
  DCHECK(!collection_type_);

  if (collection_type == CollectionType::kMinor) {
    if (!generational_gc_supported()) return;
    // Notify GC tracer that CppGC started young GC cycle.
    isolate_->heap()->tracer()->NotifyYoungCppGCRunning();
  }

  collection_type_ = collection_type;

  CHECK(!sweeper_.IsSweepingInProgress());

  // Check that previous cycle metrics for the same collection type have been
  // reported.
  if (GetMetricRecorder()) {
    if (collection_type == CollectionType::kMajor)
      DCHECK(!GetMetricRecorder()->FullGCMetricsReportPending());
    else
      DCHECK(!GetMetricRecorder()->YoungGCMetricsReportPending());
  }

#if defined(CPPGC_YOUNG_GENERATION)
  if (generational_gc_supported() &&
      *collection_type_ == CollectionType::kMajor) {
    stats_collector()->NotifyUnmarkingStarted(*collection_type_);
    cppgc::internal::StatsCollector::EnabledScope stats_scope(
        stats_collector(), cppgc::internal::StatsCollector::kUnmark);
    cppgc::internal::SequentialUnmarker unmarker(raw_heap());
  }
#endif  // defined(CPPGC_YOUNG_GENERATION)

  if (gc_flags == GarbageCollectionFlagValues::kNoFlags) {
    if (heap()->is_current_gc_forced()) {
      gc_flags |= CppHeap::GarbageCollectionFlagValues::kForced;
    }
    if (heap()->ShouldReduceMemory()) {
      gc_flags |= CppHeap::GarbageCollectionFlagValues::kReduceMemory;
    }
  }
  current_gc_flags_ = gc_flags;

  const cppgc::internal::MarkingConfig marking_config{
      *collection_type_,
      StackState::kNoHeapPointers,
      SelectMarkingType(),
      IsForceGC(current_gc_flags_)
          ? cppgc::internal::MarkingConfig::IsForcedGC::kForced
          : cppgc::internal::MarkingConfig::IsForcedGC::kNotForced,
      v8::base::TimeDelta::FromMilliseconds(
          v8_flags.incremental_marking_task_delay_ms),
      v8_flags.incremental_marking_bailout_when_ahead_of_schedule};
  DCHECK_IMPLIES(!isolate_,
                 (MarkingType::kAtomic == marking_config.marking_type) ||
                     force_incremental_marking_for_testing_);
  if (ShouldReduceMemory(current_gc_flags_)) {
    // Only enable compaction when in a memory reduction garbage collection as
    // it may significantly increase the final garbage collection pause.
    compactor_.InitializeIfShouldCompact(marking_config.marking_type,
                                         marking_config.stack_state);
  }
  marker_ = std::make_unique<UnifiedHeapMarker>(
      isolate_ ? isolate()->heap() : nullptr, AsBase(), platform_.get(),
      marking_config);
}

namespace {
MarkingWorklists::Local* GetV8MarkingWorklists(
    Isolate* isolate, cppgc::internal::CollectionType collection_type) {
  auto* heap = isolate->heap();
  if (collection_type == cppgc::internal::CollectionType::kMajor) {
    return heap->mark_compact_collector()->local_marking_worklists();
  } else {
    return heap->minor_mark_sweep_collector()->local_marking_worklists();
  }
}
}  // namespace

void CppHeap::StartMarking() {
  CHECK(marking_done_);
  if (!TracingInitialized()) return;
  if (isolate_) {
    // Reuse the same local worklist for the mutator marking state which results
    // in directly processing the objects by the JS logic. Also avoids
    // publishing local objects.
    marker_->To<UnifiedHeapMarker>().GetMutatorUnifiedHeapMarkingState().Update(
        GetV8MarkingWorklists(isolate_, *collection_type_));
  }
  marker_->StartMarking();
  marking_done_ = false;
}

bool CppHeap::AdvanceTracing(v8::base::TimeDelta max_duration) {
  if (!TracingInitialized()) return true;
  is_in_v8_marking_step_ = true;
  cppgc::internal::StatsCollector::EnabledScope stats_scope(
      stats_collector(),
      in_atomic_pause_ ? cppgc::internal::StatsCollector::kAtomicMark
                       : cppgc::internal::StatsCollector::kIncrementalMark);
  const v8::base::TimeDelta deadline =
      in_atomic_pause_ ? v8::base::TimeDelta::Max() : max_duration;
  const size_t marked_bytes_limit = in_atomic_pause_ ? SIZE_MAX : 0;
  DCHECK_NOT_NULL(marker_);
  if (in_atomic_pause_) {
    marker_->NotifyConcurrentMarkingOfWorkIfNeeded(
        cppgc::TaskPriority::kUserBlocking);
  }
  // TODO(chromium:1056170): Replace when unified heap transitions to
  // bytes-based deadline.
  marking_done_ =
      marker_->AdvanceMarkingWithLimits(deadline, marked_bytes_limit);
  DCHECK_IMPLIES(in_atomic_pause_, marking_done_);
  is_in_v8_marking_step_ = false;
  return marking_done_;
}

bool CppHeap::IsTracingDone() const {
  return !TracingInitialized() || marking_done_;
}

bool CppHeap::ShouldFinalizeIncrementalMarking() const {
  return !incremental_marking_supported() || IsTracingDone();
}

void CppHeap::EnterProcessGlobalAtomicPause() {
  if (!TracingInitialized()) {
    return;
  }
  DCHECK(in_atomic_pause_);
  marker_->To<UnifiedHeapMarker>().EnterProcessGlobalAtomicPause();
}

void CppHeap::EnterFinalPause(cppgc::EmbedderStackState stack_state) {
  CHECK(!IsGCForbidden());
  // Enter atomic pause even if tracing is not initialized. This is needed to
  // make sure that we always enable young generation from the atomic pause.
  in_atomic_pause_ = true;
  if (!TracingInitialized()) return;
  auto& marker = marker_->To<UnifiedHeapMarker>();
  // Scan global handles conservatively in case we are attached to an Isolate.
  // TODO(1029379): Support global handle marking visitors with minor GC.
  if (isolate_) {
    auto& heap = *isolate()->heap();
    marker.conservative_visitor().SetConservativeTracedHandlesMarkingVisitor(
        std::make_unique<ConservativeTracedHandlesMarkingVisitor>(
            heap, *GetV8MarkingWorklists(isolate_, *collection_type_),
            *collection_type_));
  }
  marker.EnterAtomicPause(stack_state);
  compactor_.CancelIfShouldNotCompact(MarkingType::kAtomic, stack_state);
}

bool CppHeap::FinishConcurrentMarkingIfNeeded() {
  if (!TracingInitialized()) return true;
  return marker_->JoinConcurrentMarkingIfNeeded();
}

void CppHeap::ReEnableConcurrentMarking() {
  CHECK(!in_atomic_pause_);
  marker_->ReEnableConcurrentMarking();
}

void CppHeap::WriteBarrier(void* object) {
  isolate()
      ->heap()
      ->mark_compact_collector()
      ->local_marking_worklists()
      ->cpp_marking_state()
      ->MarkAndPush(object);
}

namespace {

void RecordEmbedderSpeed(GCTracer* tracer, base::TimeDelta marking_time,
                         size_t marked_bytes) {
  constexpr auto kMinReportingTime = base::TimeDelta::FromMillisecondsD(0.5);
  if (marking_time > kMinReportingTime) {
    tracer->RecordEmbedderSpeed(marked_bytes, marking_time.InMillisecondsF());
  }
}

}  // namespace

void CppHeap::FinishMarkingAndProcessWeakness() {
  CHECK(in_atomic_pause_);
  CHECK(marking_done_);

#if defined(CPPGC_YOUNG_GENERATION)
  // Check if the young generation was enabled via flag. We must enable young
  // generation before calling the custom weak callbacks to make sure that the
  // callbacks for old objects are registered in the remembered set.
  if (v8_flags.cppgc_young_generation) {
    EnableGenerationalGC();
  }
#endif  // defined(CPPGC_YOUNG_GENERATION)

  if (!TracingInitialized()) {
    in_atomic_pause_ = false;
    return;
  }

  {
    cppgc::subtle::DisallowGarbageCollectionScope disallow_gc_scope(*this);
    marker_->LeaveAtomicPause();
  }
  marker_.reset();

  if (isolate_) {
    // The size is used for recomputing the global heap limit.
    used_size_ = stats_collector_->marked_bytes();
    // Force a check next time increased memory is reported. This allows for
    // setting limits close to actual heap sizes.
    allocated_size_limit_for_check_ = 0;

    RecordEmbedderSpeed(isolate_->heap()->tracer(),
                        stats_collector_->marking_time(), used_size_);
  }
}

void CppHeap::CompactAndSweep() {
  if (!TracingInitialized()) {
    return;
  }

  // The allocated bytes counter in v8 was reset to the current marked bytes, so
  // any pending allocated bytes updates should be discarded.
  buffered_allocated_bytes_ = 0;
  const size_t bytes_allocated_in_prefinalizers = ExecutePreFinalizers();
#if CPPGC_VERIFY_HEAP
  UnifiedHeapMarkingVerifier verifier(*this, *collection_type_);
  verifier.Run(stack_state_of_prev_gc(),
               stats_collector()->marked_bytes_on_current_cycle() +
                   bytes_allocated_in_prefinalizers);
#endif  // CPPGC_VERIFY_HEAP
  USE(bytes_allocated_in_prefinalizers);

#if defined(CPPGC_YOUNG_GENERATION)
  ResetRememberedSet();
  // We can reset the remembered set on each GC because surviving Oilpan objects
  // are immediately considered old.
  ResetCrossHeapRememberedSet();
#endif  // defined(CPPGC_YOUNG_GENERATION)

  {
    cppgc::subtle::NoGarbageCollectionScope no_gc(*this);
    cppgc::internal::SweepingConfig::CompactableSpaceHandling
        compactable_space_handling;
    {
      std::optional<SweepingOnMutatorThreadForGlobalHandlesScope>
          global_handles_scope;
      if (isolate_) {
        global_handles_scope.emplace(*isolate_->traced_handles());
      }
      compactable_space_handling = compactor_.CompactSpacesIfEnabled();
    }
    const cppgc::internal::SweepingConfig sweeping_config{
        SelectSweepingType(), compactable_space_handling,
        ShouldReduceMemory(current_gc_flags_)
            ? cppgc::internal::SweepingConfig::FreeMemoryHandling::
                  kDiscardWherePossible
            : cppgc::internal::SweepingConfig::FreeMemoryHandling::
                  kDoNotDiscard,
        // CppHeap is initialized before V8 flags are necessarily set which
        // prohibits us from reading the flag at creation.
        v8_flags.cppheap_optimize_sweep_for_mutator
            ? cppgc::internal::SweepingStrategy::kMinimizeMutatorInterference
            : cppgc::internal::SweepingStrategy::kMinimizeMemory};
    DCHECK_IMPLIES(!isolate_,
                   SweepingType::kAtomic == sweeping_config.sweeping_type);
    sweeper().Start(sweeping_config);
  }

  in_atomic_pause_ = false;
  collection_type_.reset();
}

void CppHeap::AllocatedObjectSizeIncreased(size_t bytes) {
  buffered_allocated_bytes_ += static_cast<int64_t>(bytes);
  ReportBufferedAllocationSizeIfPossible();
}

void CppHeap::AllocatedObjectSizeDecreased(size_t bytes) {
  buffered_allocated_bytes_ -= static_cast<int64_t>(bytes);
  ReportBufferedAllocationSizeIfPossible();
}

void CppHeap::ReportBufferedAllocationSizeIfPossible() {
  // Reporting memory to V8 may trigger GC.
  if (!IsGCAllowed()) {
    return;
  }

  // We are in attached state.
  DCHECK_NOT_NULL(isolate_);

  // The calls below may trigger full GCs that are synchronous and also execute
  // epilogue callbacks. Since such callbacks may allocate, the counter must
  // already be zeroed by that time.
  const int64_t bytes_to_report = buffered_allocated_bytes_;
  buffered_allocated_bytes_ = 0;

  if (bytes_to_report < 0) {
    DCHECK_GE(used_size_.load(std::memory_order_relaxed), bytes_to_report);
    used_size_.fetch_sub(static_cast<size_t>(-bytes_to_report),
                         std::memory_order_relaxed);
  } else {
    used_size_.fetch_add(static_cast<size_t>(bytes_to_report),
                         std::memory_order_relaxed);
    allocated_size_ += bytes_to_report;

    if (v8_flags.incremental_marking) {
      if (allocated_size_ > allocated_size_limit_for_check_) {
        Heap* heap = isolate_->heap();
        heap->StartIncrementalMarkingIfAllocationLimitIsReached(
            heap->main_thread_local_heap(),
            heap->GCFlagsForIncrementalMarking(),
            kGCCallbackScheduleIdleGarbageCollection);
        if (heap->incremental_marking()->IsMajorMarking()) {
          if (heap->AllocationLimitOvershotByLargeMargin()) {
            heap->FinalizeIncrementalMarkingAtomically(
                i::GarbageCollectionReason::kExternalFinalize);
          } else {
            heap->incremental_marking()->AdvanceOnAllocation();
          }
        }
        allocated_size_limit_for_check_ =
            allocated_size_ + kIncrementalMarkingCheckInterval;
      }
    }
  }
}

void CppHeap::CollectGarbageForTesting(CollectionType collection_type,
                                       StackState stack_state) {
  if (!IsDetachedGCAllowed()) {
    return;
  }

  // Finish sweeping in case it is still running.
  sweeper().FinishIfRunning();

  if (isolate_) {
    reinterpret_cast<v8::Isolate*>(isolate_)
        ->RequestGarbageCollectionForTesting(
            v8::Isolate::kFullGarbageCollection, stack_state);
    return;
  }

  stack()->SetMarkerIfNeededAndCallback([this, collection_type, stack_state]() {
    // Perform an atomic GC, with starting incremental/concurrent marking and
    // immediately finalizing the garbage collection.
    if (!IsMarking()) {
      InitializeMarking(collection_type, GarbageCollectionFlagValues::kForced);
      StartMarking();
    }
    EnterFinalPause(stack_state);
    EnterProcessGlobalAtomicPause();
    CHECK(AdvanceTracing(v8::base::TimeDelta::Max()));
    if (FinishConcurrentMarkingIfNeeded()) {
      CHECK(AdvanceTracing(v8::base::TimeDelta::Max()));
    }
    FinishMarkingAndProcessWeakness();
    CompactAndSweep();
    FinishAtomicSweepingIfRunning();
  });
}

void CppHeap::EnableDetachedGarbageCollectionsForTesting() {
  CHECK(!in_detached_testing_mode_);
  CHECK_NULL(isolate_);
  no_gc_scope_--;
  in_detached_testing_mode_ = true;
  static_cast<CppgcPlatformAdapter*>(platform())
      ->EnableDetachedModeForTesting();
}

void CppHeap::StartIncrementalGarbageCollectionForTesting() {
  DCHECK(!in_no_gc_scope());
  DCHECK_NULL(isolate_);
  if (IsMarking()) return;
  force_incremental_marking_for_testing_ = true;
  InitializeMarking(CollectionType::kMajor,
                    GarbageCollectionFlagValues::kForced);
  StartMarking();
  force_incremental_marking_for_testing_ = false;
}

void CppHeap::FinalizeIncrementalGarbageCollectionForTesting(
    cppgc::EmbedderStackState stack_state) {
  DCHECK(!in_no_gc_scope());
  DCHECK_NULL(isolate_);
  DCHECK(IsMarking());
  if (IsMarking()) {
    CollectGarbageForTesting(CollectionType::kMajor, stack_state);
  }
  sweeper_.FinishIfRunning();
}

namespace {

void ReportCustomSpaceStatistics(
    cppgc::internal::RawHeap& raw_heap,
    std::vector<cppgc::CustomSpaceIndex> custom_spaces,
    std::unique_ptr<CustomSpaceStatisticsReceiver> receiver) {
  for (auto custom_space_index : custom_spaces) {
    const cppgc::internal::BaseSpace* space =
        raw_heap.CustomSpace(custom_space_index);
    size_t allocated_bytes = std::accumulate(
        space->begin(), space->end(), 0, [](size_t sum, auto* page) {
          return sum + page->AllocatedBytesAtLastGC();
        });
    receiver->AllocatedBytes(custom_space_index, allocated_bytes);
  }
}

class CollectCustomSpaceStatisticsAtLastGCTask final : public v8::Task {
 public:
  static constexpr v8::base::TimeDelta kTaskDelayMs =
      v8::base::TimeDelta::FromMilliseconds(10);

  CollectCustomSpaceStatisticsAtLastGCTask(
      cppgc::internal::HeapBase& heap,
      std::vector<cppgc::CustomSpaceIndex> custom_spaces,
      std::unique_ptr<CustomSpaceStatisticsReceiver> receiver)
      : heap_(heap),
        custom_spaces_(std::move(custom_spaces)),
        receiver_(std::move(receiver)) {}

  void Run() final {
    cppgc::internal::Sweeper& sweeper = heap_.sweeper();
    if (sweeper.PerformSweepOnMutatorThread(
            kStepSizeMs,
            cppgc::internal::StatsCollector::kSweepInTaskForStatistics)) {
      // Sweeping is done.
      DCHECK(!sweeper.IsSweepingInProgress());
      ReportCustomSpaceStatistics(heap_.raw_heap(), std::move(custom_spaces_),
                                  std::move(receiver_));
    } else {
      heap_.platform()->GetForegroundTaskRunner()->PostDelayedTask(
          std::make_unique<CollectCustomSpaceStatisticsAtLastGCTask>(
              heap_, std::move(custom_spaces_), std::move(receiver_)),
          kTaskDelayMs.InSecondsF());
    }
  }

 private:
  static constexpr v8::base::TimeDelta kStepSizeMs =
      v8::base::TimeDelta::FromMilliseconds(5);

  cppgc::internal::HeapBase& heap_;
  std::vector<cppgc::CustomSpaceIndex> custom_spaces_;
  std::unique_ptr<CustomSpaceStatisticsReceiver> receiver_;
};

constexpr v8::base::TimeDelta
    CollectCustomSpaceStatisticsAtLastGCTask::kTaskDelayMs;
constexpr v8::base::TimeDelta
    CollectCustomSpaceStatisticsAtLastGCTask::kStepSizeMs;

}  // namespace

void CppHeap::CollectCustomSpaceStatisticsAtLastGC(
    std::vector<cppgc::CustomSpaceIndex> custom_spaces,
    std::unique_ptr<CustomSpaceStatisticsReceiver> receiver) {
  if (sweeper().IsSweepingInProgress()) {
    platform()->GetForegroundTaskRunner()->PostDelayedTask(
        std::make_unique<CollectCustomSpaceStatisticsAtLastGCTask>(
            AsBase(), std::move(custom_spaces), std::move(receiver)),
        CollectCustomSpaceStatisticsAtLastGCTask::kTaskDelayMs.InSecondsF());
    return;
  }
  ReportCustomSpaceStatistics(raw_heap(), std::move(custom_spaces),
                              std::move(receiver));
}

CppHeap::MetricRecorderAdapter* CppHeap::GetMetricRecorder() const {
  return static_cast<MetricRecorderAdapter*>(
      stats_collector_->GetMetricRecorder());
}

void CppHeap::FinishSweepingIfRunning() {
  sweeper_.FinishIfRunning();
  if (isolate_ && ShouldReduceMemory(current_gc_flags_)) {
    isolate_->traced_handles()->DeleteEmptyBlocks();
  }
}

void CppHeap::FinishAtomicSweepingIfRunning() {
  // Young generation GCs are optional and as such sweeping is not necessarily
  // running.
  if (sweeper_.IsSweepingInProgress() &&
      SelectSweepingType() == SweepingType::kAtomic) {
    FinishSweepingIfRunning();
  }
}

void CppHeap::FinishSweepingIfOutOfWork() { sweeper_.FinishIfOutOfWork(); }

std::unique_ptr<CppMarkingState> CppHeap::CreateCppMarkingState() {
  if (!TracingInitialized()) return {};
  DCHECK(IsMarking());
  return std::make_unique<CppMarkingState>(
      std::make_unique<cppgc::internal::MarkingStateBase>(
          AsBase(), marker()->To<UnifiedHeapMarker>().GetMarkingWorklists()));
}

std::unique_ptr<CppMarkingState>
CppHeap::CreateCppMarkingStateForMutatorThread() {
  if (!TracingInitialized()) return {};
  DCHECK(IsMarking());
  return std::make_unique<CppMarkingState>(
      marker()->To<UnifiedHeapMarker>().GetMutatorMarkingState());
}

CppHeap::PauseConcurrentMarkingScope::PauseConcurrentMarkingScope(
    CppHeap* cpp_heap) {
  if (cpp_heap && cpp_heap->marker()) {
    pause_scope_.emplace(*cpp_heap->marker());
  }
}

void CppHeap::CollectGarbage(cppgc::internal::GCConfig config) {
  if (!IsGCAllowed()) {
    return;
  }
  // TODO(mlippautz): Respect full config.
  const auto flags =
      (config.free_memory_handling ==
       cppgc::internal::GCConfig::FreeMemoryHandling::kDiscardWherePossible)
          ? GCFlag::kReduceMemoryFootprint
          : GCFlag::kNoFlags;
  isolate_->heap()->CollectAllGarbage(
      flags, GarbageCollectionReason::kCppHeapAllocationFailure);
}

std::optional<cppgc::EmbedderStackState> CppHeap::overridden_stack_state()
    const {
  return heap_ ? heap_->overridden_stack_state()
               : detached_override_stack_state_;
}

void CppHeap::set_override_stack_state(cppgc::EmbedderStackState state) {
  CHECK(!detached_override_stack_state_);
  CHECK(!override_stack_state_scope_);
  if (heap_) {
    override_stack_state_scope_ = std::make_unique<EmbedderStackStateScope>(
        heap_, EmbedderStackStateOrigin::kExplicitInvocation, state);
  } else {
    detached_override_stack_state_ = state;
  }
}

void CppHeap::clear_overridden_stack_state() {
  if (heap_) {
    CHECK(!detached_override_stack_state_);
    CHECK(override_stack_state_scope_);
    override_stack_state_scope_.reset();
  } else {
    CHECK(detached_override_stack_state_);
    CHECK(!override_stack_state_scope_);
    detached_override_stack_state_.reset();
  }
}

void CppHeap::StartIncrementalGarbageCollection(cppgc::internal::GCConfig) {
  UNIMPLEMENTED();
}

size_t CppHeap::epoch() const { UNIMPLEMENTED(); }

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
std::optional<int> CppHeap::UpdateAllocationTimeout() {
  if (!v8_flags.cppgc_random_gc_interval) {
    return std::nullopt;
  }
  if (!allocation_timeout_rng_) {
    allocation_timeout_rng_.emplace(v8_flags.fuzzer_random_seed);
  }
  return allocation_timeout_rng_->NextInt(v8_flags.cppgc_random_gc_interval) +
         1;
}
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

void CppHeap::ResetCrossHeapRememberedSet() {
  if (!generational_gc_supported()) {
    DCHECK(cross_heap_remembered_set_.IsEmpty());
    return;
  }
  DCHECK(isolate_);
  cross_heap_remembered_set_.Reset(*isolate_);
}

void CppHeap::UpdateGCCapabilitiesFromFlagsForTesting() {
  UpdateGCCapabilitiesFromFlags();
}

bool CppHeap::IsDetachedGCAllowed() const {
  return (isolate_ || in_detached_testing_mode_) && HeapBase::IsGCAllowed();
}

bool CppHeap::IsGCAllowed() const {
  return isolate_ && HeapBase::IsGCAllowed();
}

bool CppHeap::IsGCForbidden() const {
  return (isolate_ && isolate_->InFastCCall() &&
          !v8_flags.allow_allocation_in_fast_api_call) ||
         HeapBase::IsGCForbidden();
}

}  // namespace internal
}  // namespace v8
