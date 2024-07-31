// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_BASE_H_
#define V8_HEAP_CPPGC_HEAP_BASE_H_

#include <memory>
#include <set>

#include "include/cppgc/heap-handle.h"
#include "include/cppgc/heap-statistics.h"
#include "include/cppgc/heap.h"
#include "include/cppgc/internal/persistent-node.h"
#include "include/cppgc/macros.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/compactor.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/metric-recorder.h"
#include "src/heap/cppgc/object-allocator.h"
#include "src/heap/cppgc/platform.h"
#include "src/heap/cppgc/process-heap-statistics.h"
#include "src/heap/cppgc/process-heap.h"
#include "src/heap/cppgc/raw-heap.h"
#include "src/heap/cppgc/sweeper.h"
#include "src/heap/cppgc/write-barrier.h"
#include "v8config.h"  // NOLINT(build/include_directory)

#if defined(CPPGC_YOUNG_GENERATION)
#include "src/heap/cppgc/remembered-set.h"
#endif

namespace v8 {
namespace base {
class LsanPageAllocator;
}  // namespace base
}  // namespace v8

namespace heap {
namespace base {
class Stack;
}  // namespace base
}  // namespace heap

namespace cppgc {
namespace subtle {
class DisallowGarbageCollectionScope;
class NoGarbageCollectionScope;
}  // namespace subtle

namespace testing {
class Heap;
}  // namespace testing

class Platform;

namespace internal {

class FatalOutOfMemoryHandler;
class GarbageCollector;
class PageBackend;
class PreFinalizerHandler;
class StatsCollector;

enum class HeapObjectNameForUnnamedObject : uint8_t;

class MoveListener {
 public:
  // This function may be called simultaneously on multiple threads.
  // Implementations must not attempt to allocate or do any other actions
  // which could trigger reentrant GC.
  virtual void OnMove(Address from, Address to,
                      size_t size_including_header) = 0;
};

// Base class for heap implementations.
class V8_EXPORT_PRIVATE HeapBase : public cppgc::HeapHandle {
 public:
  using StackSupport = cppgc::Heap::StackSupport;
  using MarkingType = cppgc::Heap::MarkingType;
  using SweepingType = cppgc::Heap::SweepingType;

  static HeapBase& From(cppgc::HeapHandle& heap_handle) {
    return static_cast<HeapBase&>(heap_handle);
  }
  static const HeapBase& From(const cppgc::HeapHandle& heap_handle) {
    return static_cast<const HeapBase&>(heap_handle);
  }

  HeapBase(std::shared_ptr<cppgc::Platform> platform,
           const std::vector<std::unique_ptr<CustomSpaceBase>>& custom_spaces,
           StackSupport stack_support, MarkingType marking_support,
           SweepingType sweeping_support, GarbageCollector& garbage_collector);
  virtual ~HeapBase();

  HeapBase(const HeapBase&) = delete;
  HeapBase& operator=(const HeapBase&) = delete;

  RawHeap& raw_heap() { return raw_heap_; }
  const RawHeap& raw_heap() const { return raw_heap_; }

  cppgc::Platform* platform() { return platform_.get(); }
  const cppgc::Platform* platform() const { return platform_.get(); }

  FatalOutOfMemoryHandler& oom_handler() { return *oom_handler_.get(); }
  const FatalOutOfMemoryHandler& oom_handler() const {
    return *oom_handler_.get();
  }

  PageBackend* page_backend() { return page_backend_.get(); }
  const PageBackend* page_backend() const { return page_backend_.get(); }

  StatsCollector* stats_collector() { return stats_collector_.get(); }
  const StatsCollector* stats_collector() const {
    return stats_collector_.get();
  }

  PreFinalizerHandler* prefinalizer_handler() {
    return prefinalizer_handler_.get();
  }
  const PreFinalizerHandler* prefinalizer_handler() const {
    return prefinalizer_handler_.get();
  }

  MarkerBase* marker() const { return marker_.get(); }
  std::unique_ptr<MarkerBase>& GetMarkerRefForTesting() { return marker_; }

  Compactor& compactor() { return compactor_; }

  ObjectAllocator& object_allocator() { return object_allocator_; }
  const ObjectAllocator& object_allocator() const { return object_allocator_; }

  Sweeper& sweeper() { return sweeper_; }
  const Sweeper& sweeper() const { return sweeper_; }

  PersistentRegion& GetStrongPersistentRegion() {
    return strong_persistent_region_;
  }
  const PersistentRegion& GetStrongPersistentRegion() const {
    return strong_persistent_region_;
  }
  PersistentRegion& GetWeakPersistentRegion() {
    return weak_persistent_region_;
  }
  const PersistentRegion& GetWeakPersistentRegion() const {
    return weak_persistent_region_;
  }
  CrossThreadPersistentRegion& GetStrongCrossThreadPersistentRegion() {
    return strong_cross_thread_persistent_region_;
  }
  const CrossThreadPersistentRegion& GetStrongCrossThreadPersistentRegion()
      const {
    return strong_cross_thread_persistent_region_;
  }
  CrossThreadPersistentRegion& GetWeakCrossThreadPersistentRegion() {
    return weak_cross_thread_persistent_region_;
  }
  const CrossThreadPersistentRegion& GetWeakCrossThreadPersistentRegion()
      const {
    return weak_cross_thread_persistent_region_;
  }

#if defined(CPPGC_YOUNG_GENERATION)
  OldToNewRememberedSet& remembered_set() { return remembered_set_; }
#endif  // defined(CPPGC_YOUNG_GENERATION)

  size_t ObjectPayloadSize() const;

  virtual heap::base::Stack* stack() { return stack_.get(); }

  StackSupport stack_support() const { return stack_support_; }

  // These virtual methods are also present in class GarbageCollector.
  virtual void set_override_stack_state(EmbedderStackState state) = 0;
  virtual void clear_overridden_stack_state() = 0;

  // Termination drops all roots (clears them out) and runs garbage collections
  // in a bounded fixed point loop  until no new objects are created in
  // destructors. Exceeding the loop bound results in a crash.
  void Terminate();

  virtual bool IsGCForbidden() const;
  bool in_atomic_pause() const { return in_atomic_pause_; }

  HeapStatistics CollectStatistics(HeapStatistics::DetailLevel);

  EmbedderStackState stack_state_of_prev_gc() const {
    return stack_state_of_prev_gc_;
  }
  void SetStackStateOfPrevGC(EmbedderStackState stack_state) {
    stack_state_of_prev_gc_ = stack_state;
  }

  void SetInAtomicPauseForTesting(bool value) { in_atomic_pause_ = value; }

  virtual void StartIncrementalGarbageCollectionForTesting() = 0;
  virtual void FinalizeIncrementalGarbageCollectionForTesting(
      EmbedderStackState) = 0;

  void SetMetricRecorder(std::unique_ptr<MetricRecorder> histogram_recorder) {
    stats_collector_->SetMetricRecorder(std::move(histogram_recorder));
  }

  int GetCreationThreadId() const { return creation_thread_id_; }

  MarkingType marking_support() const { return marking_support_; }
  SweepingType sweeping_support() const { return sweeping_support_; }

  bool incremental_marking_supported() const {
    return marking_support_ != MarkingType::kAtomic;
  }

  bool generational_gc_supported() const {
    const bool supported = is_young_generation_enabled();
#if defined(CPPGC_YOUNG_GENERATION)
    DCHECK_IMPLIES(supported, YoungGenerationEnabler::IsEnabled());
#endif  // defined(CPPGC_YOUNG_GENERATION)
    return supported;
  }

  // Returns whether objects should derive their name from C++ class names. Also
  // requires build-time support through `CPPGC_SUPPORTS_OBJECT_NAMES`.
  HeapObjectNameForUnnamedObject name_of_unnamed_object() const {
    return name_for_unnamed_object_;
  }
  void set_name_of_unnamed_object(HeapObjectNameForUnnamedObject value) {
    name_for_unnamed_object_ = value;
  }

  // Callback support so that other components can listen to when objects are
  // moved.
  bool HasMoveListeners() const { return !move_listeners_.empty(); }
  void CallMoveListeners(Address from, Address to,
                         size_t size_including_header);
  void RegisterMoveListener(MoveListener* listener);
  void UnregisterMoveListener(MoveListener* listener);

  void set_incremental_marking_in_progress(bool value) {
    is_incremental_marking_in_progress_ = value;
  }

  void EnterNoGCScope() { ++no_gc_scope_; }
  void LeaveNoGCScope() {
    DCHECK_GT(no_gc_scope_, 0);
    --no_gc_scope_;
  }

  void EnterDisallowGCScope() { ++disallow_gc_scope_; }
  void LeaveDisallowGCScope() {
    DCHECK_GT(disallow_gc_scope_, 0);
    --disallow_gc_scope_;
  }

  using HeapHandle::is_incremental_marking_in_progress;

 protected:
  static std::unique_ptr<PageBackend> InitializePageBackend(
      PageAllocator& allocator);

  // Used by the incremental scheduler to finalize a GC if supported.
  virtual void FinalizeIncrementalGarbageCollectionIfNeeded(
      cppgc::Heap::StackState) = 0;

  virtual bool IsGCAllowed() const;

  bool in_no_gc_scope() const { return no_gc_scope_ > 0; }

  bool IsMarking() const { return marker_.get(); }

  // Returns amount of bytes allocated while executing prefinalizers.
  size_t ExecutePreFinalizers();

#if defined(CPPGC_YOUNG_GENERATION)
  void EnableGenerationalGC();
  void ResetRememberedSet();
#endif  // defined(CPPGC_YOUNG_GENERATION)

  PageAllocator* page_allocator() const;

  RawHeap raw_heap_;
  std::shared_ptr<cppgc::Platform> platform_;
  std::unique_ptr<FatalOutOfMemoryHandler> oom_handler_;

#if defined(LEAK_SANITIZER)
  std::unique_ptr<v8::base::LsanPageAllocator> lsan_page_allocator_;
#endif  // LEAK_SANITIZER

  std::unique_ptr<PageBackend> page_backend_;

  // HeapRegistry requires access to page_backend_.
  HeapRegistry::Subscription heap_registry_subscription_{*this};

  std::unique_ptr<StatsCollector> stats_collector_;
  std::unique_ptr<heap::base::Stack> stack_;
  std::unique_ptr<PreFinalizerHandler> prefinalizer_handler_;
  std::unique_ptr<MarkerBase> marker_;

  Compactor compactor_;
  ObjectAllocator object_allocator_;
  Sweeper sweeper_;

  PersistentRegion strong_persistent_region_;
  PersistentRegion weak_persistent_region_;
  CrossThreadPersistentRegion strong_cross_thread_persistent_region_;
  CrossThreadPersistentRegion weak_cross_thread_persistent_region_;

  ProcessHeapStatisticsUpdater::AllocationObserverImpl
      allocation_observer_for_PROCESS_HEAP_STATISTICS_;
#if defined(CPPGC_YOUNG_GENERATION)
  OldToNewRememberedSet remembered_set_;
#endif  // defined(CPPGC_YOUNG_GENERATION)

  size_t no_gc_scope_ = 0;
  size_t disallow_gc_scope_ = 0;

  const StackSupport stack_support_;
  EmbedderStackState stack_state_of_prev_gc_ =
      EmbedderStackState::kNoHeapPointers;

  bool in_atomic_pause_ = false;

  int creation_thread_id_ = v8::base::OS::GetCurrentThreadId();

  MarkingType marking_support_;
  SweepingType sweeping_support_;

  HeapObjectNameForUnnamedObject name_for_unnamed_object_ =
      HeapObjectNameForUnnamedObject::kUseHiddenName;

  std::vector<MoveListener*> move_listeners_;

  friend class MarkerBase::IncrementalMarkingTask;
  friend class cppgc::subtle::DisallowGarbageCollectionScope;
  friend class cppgc::testing::Heap;
};

class V8_NODISCARD V8_EXPORT_PRIVATE ClassNameAsHeapObjectNameScope final {
 public:
  explicit ClassNameAsHeapObjectNameScope(HeapBase& heap);
  ~ClassNameAsHeapObjectNameScope();

 private:
  HeapBase& heap_;
  const HeapObjectNameForUnnamedObject saved_heap_object_name_value_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_BASE_H_
