// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/local-heap.h"

#include <atomic>
#include <memory>
#include <optional>

#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/handles/local-handles.h"
#include "src/heap/collection-barrier.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/main-allocator.h"
#include "src/heap/marking-barrier.h"
#include "src/heap/parked-scope.h"
#include "src/heap/safepoint.h"

namespace v8 {
namespace internal {

namespace {
thread_local LocalHeap* current_local_heap = nullptr;
}  // namespace

LocalHeap* LocalHeap::Current() { return current_local_heap; }

#ifdef DEBUG
void LocalHeap::VerifyCurrent() const {
  LocalHeap* current = LocalHeap::Current();

  if (is_main_thread())
    DCHECK_NULL(current);
  else
    DCHECK_EQ(current, this);
}
#endif

LocalHeap::LocalHeap(Heap* heap, ThreadKind kind,
                     std::unique_ptr<PersistentHandles> persistent_handles)
    : heap_(heap),
      ptr_compr_cage_access_scope_(heap->isolate()),
      is_main_thread_(kind == ThreadKind::kMain),
      state_(ThreadState::Parked()),
      allocation_failed_(false),
      nested_parked_scopes_(0),
      prev_(nullptr),
      next_(nullptr),
      handles_(new LocalHandles),
      persistent_handles_(std::move(persistent_handles)),
      heap_allocator_(this) {
  DCHECK_IMPLIES(!is_main_thread(), heap_->deserialization_complete());
  if (!is_main_thread()) {
    heap_allocator_.Setup();
    SetUpMarkingBarrier();
  }

  heap_->safepoint()->AddLocalHeap(this, [this] {
    if (!is_main_thread()) {
      saved_marking_barrier_ =
          WriteBarrier::SetForThread(marking_barrier_.get());
      if (heap_->incremental_marking()->IsMarking()) {
        marking_barrier_->Activate(
            heap_->incremental_marking()->IsCompacting(),
            heap_->incremental_marking()->marking_mode());
      }

      SetUpSharedMarking();
    }
  });

  if (persistent_handles_) {
    persistent_handles_->Attach(this);
  }
  DCHECK_NULL(current_local_heap);
  if (!is_main_thread()) current_local_heap = this;
}

LocalHeap::~LocalHeap() {
  // Park thread since removing the local heap could block.
  EnsureParkedBeforeDestruction();

  heap_->safepoint()->RemoveLocalHeap(this, [this] {
    FreeLinearAllocationAreas();

    if (!is_main_thread()) {
      marking_barrier_->PublishIfNeeded();
      marking_barrier_->PublishSharedIfNeeded();
      MarkingBarrier* overwritten =
          WriteBarrier::SetForThread(saved_marking_barrier_);
      DCHECK_EQ(overwritten, marking_barrier_.get());
      USE(overwritten);
    }
  });

  if (!is_main_thread()) {
    DCHECK_EQ(current_local_heap, this);
    current_local_heap = nullptr;
  }

  DCHECK(gc_epilogue_callbacks_.IsEmpty());
}

void LocalHeap::SetUpMainThreadForTesting() {
  Unpark();
  DCHECK(is_main_thread());
  DCHECK(IsRunning());
  heap_allocator_.Setup();
  SetUpMarkingBarrier();
  SetUpSharedMarking();
}

void LocalHeap::SetUpMainThread(LinearAllocationArea& new_allocation_info,
                                LinearAllocationArea& old_allocation_info) {
  DCHECK(is_main_thread());
  DCHECK(IsRunning());
  heap_allocator_.Setup(&new_allocation_info, &old_allocation_info);
  SetUpMarkingBarrier();
  SetUpSharedMarking();
}

void LocalHeap::SetUpMarkingBarrier() {
  DCHECK_NULL(marking_barrier_);
  marking_barrier_ = std::make_unique<MarkingBarrier>(this);
}

void LocalHeap::SetUpSharedMarking() {
#if DEBUG
  // Ensure the thread is either in the running state or holds the safepoint
  // lock. This guarantees that the state of incremental marking can't change
  // concurrently (this requires a safepoint).
  if (is_main_thread()) {
    DCHECK(IsRunning());
  } else {
    heap()->safepoint()->AssertActive();
  }
#endif  // DEBUG

  Isolate* isolate = heap_->isolate();

  if (isolate->has_shared_space() && !isolate->is_shared_space_isolate()) {
    if (isolate->shared_space_isolate()
            ->heap()
            ->incremental_marking()
            ->IsMajorMarking()) {
      marking_barrier_->ActivateShared();
    }
  }
}

void LocalHeap::EnsurePersistentHandles() {
  if (!persistent_handles_) {
    persistent_handles_ = heap_->isolate()->NewPersistentHandles();
    persistent_handles_->Attach(this);
  }
}

void LocalHeap::AttachPersistentHandles(
    std::unique_ptr<PersistentHandles> persistent_handles) {
  DCHECK_NULL(persistent_handles_);
  persistent_handles_ = std::move(persistent_handles);
  persistent_handles_->Attach(this);
}

std::unique_ptr<PersistentHandles> LocalHeap::DetachPersistentHandles() {
  if (persistent_handles_) persistent_handles_->Detach();
  return std::move(persistent_handles_);
}

#ifdef DEBUG
bool LocalHeap::ContainsPersistentHandle(Address* location) {
  return persistent_handles_ ? persistent_handles_->Contains(location) : false;
}

bool LocalHeap::ContainsLocalHandle(Address* location) {
  return handles_ ? handles_->Contains(location) : false;
}

bool LocalHeap::IsHandleDereferenceAllowed() {
  VerifyCurrent();
  return IsRunning();
}
#endif

bool LocalHeap::IsParked() const {
#ifdef DEBUG
  VerifyCurrent();
#endif
  return state_.load_relaxed().IsParked();
}

bool LocalHeap::IsRunning() const {
#ifdef DEBUG
  VerifyCurrent();
#endif
  return state_.load_relaxed().IsRunning();
}

void LocalHeap::ParkSlowPath() {
  while (true) {
    ThreadState current_state = ThreadState::Running();
    if (state_.CompareExchangeStrong(current_state, ThreadState::Parked()))
      return;

    // CAS above failed, so state is Running with some additional flag.
    DCHECK(current_state.IsRunning());

    if (is_main_thread()) {
      DCHECK(current_state.IsSafepointRequested() ||
             current_state.IsCollectionRequested());

      if (current_state.IsSafepointRequested()) {
        ThreadState old_state = state_.SetParked();
        heap_->safepoint()->NotifyPark();
        if (old_state.IsCollectionRequested())
          heap_->collection_barrier_->CancelCollectionAndResumeThreads();
        return;
      }

      if (current_state.IsCollectionRequested()) {
        if (!heap()->ignore_local_gc_requests()) {
          heap_->CollectGarbageForBackground(this);
          continue;
        }

        DCHECK(!current_state.IsSafepointRequested());

        if (state_.CompareExchangeStrong(current_state,
                                         current_state.SetParked())) {
          heap_->collection_barrier_->CancelCollectionAndResumeThreads();
          return;
        } else {
          continue;
        }
      }
    } else {
      DCHECK(current_state.IsSafepointRequested());
      DCHECK(!current_state.IsCollectionRequested());

      ThreadState old_state = state_.SetParked();
      CHECK(old_state.IsRunning());
      CHECK(old_state.IsSafepointRequested());
      CHECK(!old_state.IsCollectionRequested());

      heap_->safepoint()->NotifyPark();
      return;
    }
  }
}

void LocalHeap::UnparkSlowPath() {
  while (true) {
    ThreadState current_state = ThreadState::Parked();
    if (state_.CompareExchangeStrong(current_state, ThreadState::Running()))
      return;

    // CAS above failed, so state is Parked with some additional flag.
    DCHECK(current_state.IsParked());

    if (is_main_thread()) {
      DCHECK(current_state.IsSafepointRequested() ||
             current_state.IsCollectionRequested());

      if (current_state.IsSafepointRequested()) {
        SleepInUnpark();
        continue;
      }

      if (current_state.IsCollectionRequested()) {
        DCHECK(!current_state.IsSafepointRequested());

        if (!state_.CompareExchangeStrong(current_state,
                                          current_state.SetRunning()))
          continue;

        if (!heap()->ignore_local_gc_requests()) {
          heap_->CollectGarbageForBackground(this);
        }

        return;
      }
    } else {
      DCHECK(current_state.IsSafepointRequested());
      DCHECK(!current_state.IsCollectionRequested());

      SleepInUnpark();
    }
  }
}

void LocalHeap::SleepInUnpark() {
  GCTracer::Scope::ScopeId scope_id;
  ThreadKind thread_kind;

  if (is_main_thread()) {
    scope_id = GCTracer::Scope::UNPARK;
    thread_kind = ThreadKind::kMain;
  } else {
    scope_id = GCTracer::Scope::BACKGROUND_UNPARK;
    thread_kind = ThreadKind::kBackground;
  }

  TRACE_GC1(heap_->tracer(), scope_id, thread_kind);
  heap_->safepoint()->WaitInUnpark();
}

void LocalHeap::EnsureParkedBeforeDestruction() {
  DCHECK_IMPLIES(!is_main_thread(), IsParked());
}

void LocalHeap::SafepointSlowPath() {
  ThreadState current_state = state_.load_relaxed();
  DCHECK(current_state.IsRunning());

  if (is_main_thread()) {
    DCHECK(current_state.IsSafepointRequested() ||
           current_state.IsCollectionRequested());

    if (current_state.IsSafepointRequested()) {
      SleepInSafepoint();
    }

    if (current_state.IsCollectionRequested()) {
      heap_->CollectGarbageForBackground(this);
    }
  } else {
    DCHECK(current_state.IsSafepointRequested());
    DCHECK(!current_state.IsCollectionRequested());

    SleepInSafepoint();
  }
}

void LocalHeap::SleepInSafepoint() {
  GCTracer::Scope::ScopeId scope_id;
  ThreadKind thread_kind;

  if (is_main_thread()) {
    scope_id = GCTracer::Scope::SAFEPOINT;
    thread_kind = ThreadKind::kMain;
  } else {
    scope_id = GCTracer::Scope::BACKGROUND_SAFEPOINT;
    thread_kind = ThreadKind::kBackground;
  }

  TRACE_GC1(heap_->tracer(), scope_id, thread_kind);

  ExecuteWithStackMarker([this]() {
    // Parking the running thread here is an optimization. We do not need to
    // wake this thread up to reach the next safepoint.
    ThreadState old_state = state_.SetParked();
    CHECK(old_state.IsRunning());
    CHECK(old_state.IsSafepointRequested());
    CHECK_IMPLIES(old_state.IsCollectionRequested(), is_main_thread());

    heap_->safepoint()->WaitInSafepoint();

    std::optional<IgnoreLocalGCRequests> ignore_gc_requests;
    if (is_main_thread()) ignore_gc_requests.emplace(heap());
    Unpark();
  });
}

#ifdef DEBUG
bool LocalHeap::IsSafeForConservativeStackScanning() const {
#ifdef V8_ENABLE_DIRECT_HANDLE
  // There must be no direct handles on the stack below the stack marker.
  if (DirectHandleBase::NumberOfHandles() > 0) return false;
#endif
  // Check if we are inside at least one ParkedScope.
  if (nested_parked_scopes_ > 0) {
    // The main thread can avoid the trampoline, if it's not the main thread of
    // a client isolate.
    if (is_main_thread() && (heap()->isolate()->is_shared_space_isolate() ||
                             !heap()->isolate()->has_shared_space()))
      return true;
    // Otherwise, require that we're inside the trampoline.
    return is_in_trampoline();
  }
  // Otherwise, we are reaching the initial parked state and the stack should
  // not be interesting.
  return true;
}
#endif  // DEBUG

void LocalHeap::FreeLinearAllocationAreas() {
  heap_allocator_.FreeLinearAllocationAreas();
}

#if DEBUG
void LocalHeap::VerifyLinearAllocationAreas() const {
  heap_allocator_.VerifyLinearAllocationAreas();
}
#endif  // DEBUG

void LocalHeap::MakeLinearAllocationAreasIterable() {
  heap_allocator_.MakeLinearAllocationAreasIterable();
}

void LocalHeap::MarkLinearAllocationAreasBlack() {
  heap_allocator_.MarkLinearAllocationAreasBlack();
}

void LocalHeap::UnmarkLinearAllocationsArea() {
  heap_allocator_.UnmarkLinearAllocationsArea();
}

void LocalHeap::MarkSharedLinearAllocationAreasBlack() {
  if (heap_allocator_.shared_space_allocator()) {
    heap_allocator_.shared_space_allocator()->MarkLinearAllocationAreaBlack();
  }
}

void LocalHeap::UnmarkSharedLinearAllocationsArea() {
  if (heap_allocator_.shared_space_allocator()) {
    heap_allocator_.shared_space_allocator()->UnmarkLinearAllocationArea();
  }
}

void LocalHeap::AddGCEpilogueCallback(GCEpilogueCallback* callback, void* data,
                                      GCCallbacksInSafepoint::GCType gc_type) {
  DCHECK(IsRunning());
  gc_epilogue_callbacks_.Add(callback, data, gc_type);
}

void LocalHeap::RemoveGCEpilogueCallback(GCEpilogueCallback* callback,
                                         void* data) {
  DCHECK(IsRunning());
  gc_epilogue_callbacks_.Remove(callback, data);
}

void LocalHeap::InvokeGCEpilogueCallbacksInSafepoint(
    GCCallbacksInSafepoint::GCType gc_type) {
  gc_epilogue_callbacks_.Invoke(gc_type);
}

void LocalHeap::NotifyObjectSizeChange(
    Tagged<HeapObject> object, int old_size, int new_size,
    ClearRecordedSlots clear_recorded_slots) {
  heap()->NotifyObjectSizeChange(object, old_size, new_size,
                                 clear_recorded_slots);
}

void LocalHeap::WeakenDescriptorArrays(
    GlobalHandleVector<DescriptorArray> strong_descriptor_arrays) {
  AsHeap()->WeakenDescriptorArrays(std::move(strong_descriptor_arrays));
}

}  // namespace internal
}  // namespace v8
