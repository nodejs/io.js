// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_HEAP_H_
#define V8_HEAP_LOCAL_HEAP_H_

#include <atomic>
#include <memory>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/common/assert-scope.h"
#include "src/common/ptr-compr.h"
#include "src/execution/isolate.h"
#include "src/handles/global-handles.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/base/stack.h"
#include "src/heap/gc-callbacks.h"

namespace v8 {
namespace internal {

class Heap;
class LocalHandles;
class MarkingBarrier;
class MutablePageMetadata;
class Safepoint;

// LocalHeap is used by the GC to track all threads with heap access in order to
// stop them before performing a collection. LocalHeaps can be either Parked or
// Running and are in Parked mode when initialized.
//   Running: Thread is allowed to access the heap but needs to give the GC the
//            chance to run regularly by manually invoking Safepoint(). The
//            thread can be parked using ParkedScope.
//   Parked:  Heap access is not allowed, so the GC will not stop this thread
//            for a collection. Useful when threads do not need heap access for
//            some time or for blocking operations like locking a mutex.
class V8_EXPORT_PRIVATE LocalHeap {
 public:
  using GCEpilogueCallback = void(void*);

  explicit LocalHeap(
      Heap* heap, ThreadKind kind,
      std::unique_ptr<PersistentHandles> persistent_handles = nullptr);
  ~LocalHeap();

  // Frequently invoked by local thread to check whether safepoint was requested
  // from the main thread.
  void Safepoint() {
    DCHECK(AllowSafepoints::IsAllowed());
    ThreadState current = state_.load_relaxed();

    if (V8_UNLIKELY(current.IsRunningWithSlowPathFlag())) {
      SafepointSlowPath();
    }
  }

  LocalHandles* handles() { return handles_.get(); }

  template <typename T>
  Handle<T> NewPersistentHandle(Tagged<T> object) {
    if (!persistent_handles_) {
      EnsurePersistentHandles();
    }
    return persistent_handles_->NewHandle(object);
  }

  template <typename T>
  Handle<T> NewPersistentHandle(Handle<T> object) {
    return NewPersistentHandle(*object);
  }

  template <typename T>
  Handle<T> NewPersistentHandle(T object) {
    static_assert(kTaggedCanConvertToRawObjects);
    return NewPersistentHandle(Tagged<T>(object));
  }

  template <typename T>
  MaybeHandle<T> NewPersistentMaybeHandle(MaybeHandle<T> maybe_handle) {
    Handle<T> handle;
    if (maybe_handle.ToHandle(&handle)) {
      return NewPersistentHandle(handle);
    }
    return kNullMaybeHandle;
  }

  void AttachPersistentHandles(
      std::unique_ptr<PersistentHandles> persistent_handles);
  std::unique_ptr<PersistentHandles> DetachPersistentHandles();
#ifdef DEBUG
  bool HasPersistentHandles() { return !!persistent_handles_; }
  bool ContainsPersistentHandle(Address* location);
  bool ContainsLocalHandle(Address* location);
  bool IsHandleDereferenceAllowed();
#endif

  bool IsParked() const;
  bool IsRunning() const;

  Heap* heap() const { return heap_; }
  Heap* AsHeap() const { return heap(); }

  // Heap root getters.
#define ROOT_ACCESSOR(type, name, CamelName) inline Tagged<type> name();
  MUTABLE_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

  MarkingBarrier* marking_barrier() { return marking_barrier_.get(); }

  // Give up all LABs. Used for e.g. full GCs.
  void FreeLinearAllocationAreas();

#if DEBUG
  void VerifyLinearAllocationAreas() const;
#endif  // DEBUG

  // Make all LABs iterable.
  void MakeLinearAllocationAreasIterable();

  // Mark/Unmark all LABs except for new and shared space. Use for black
  // allocation.
  void MarkLinearAllocationAreasBlack();
  void UnmarkLinearAllocationsArea();

  // Mark/Unmark linear allocation areas in shared heap black. Used for black
  // allocation.
  void MarkSharedLinearAllocationAreasBlack();
  void UnmarkSharedLinearAllocationsArea();

  // Fetches a pointer to the local heap from the thread local storage.
  // It is intended to be used in handle and write barrier code where it is
  // difficult to get a pointer to the current instance of local heap otherwise.
  // The result may be a nullptr if there is no local heap instance associated
  // with the current thread.
  static LocalHeap* Current();

#ifdef DEBUG
  void VerifyCurrent() const;
#endif

  // Allocate an uninitialized object.
  V8_WARN_UNUSED_RESULT inline AllocationResult AllocateRaw(
      int size_in_bytes, AllocationType allocation,
      AllocationOrigin origin = AllocationOrigin::kRuntime,
      AllocationAlignment alignment = kTaggedAligned);

  // Allocate an uninitialized object.
  enum AllocationRetryMode { kLightRetry, kRetryOrFail };
  template <AllocationRetryMode mode>
  Tagged<HeapObject> AllocateRawWith(
      int size_in_bytes, AllocationType allocation,
      AllocationOrigin origin = AllocationOrigin::kRuntime,
      AllocationAlignment alignment = kTaggedAligned);

  // Allocates an uninitialized object and crashes when object
  // cannot be allocated.
  V8_WARN_UNUSED_RESULT inline Address AllocateRawOrFail(
      int size_in_bytes, AllocationType allocation,
      AllocationOrigin origin = AllocationOrigin::kRuntime,
      AllocationAlignment alignment = kTaggedAligned);

  void NotifyObjectSizeChange(Tagged<HeapObject> object, int old_size,
                              int new_size,
                              ClearRecordedSlots clear_recorded_slots);

  bool is_main_thread() const { return is_main_thread_; }
  bool is_main_thread_for(Heap* heap) const {
    return is_main_thread() && heap_ == heap;
  }
  bool is_in_trampoline() const { return heap_->stack().IsMarkerSet(); }
  bool deserialization_complete() const {
    return heap_->deserialization_complete();
  }
  ReadOnlySpace* read_only_space() { return heap_->read_only_space(); }

  // Adds a callback that is invoked with the given |data| after each GC.
  // The callback is invoked on the main thread before any background thread
  // resumes. The callback must not allocate or make any other calls that
  // can trigger GC.
  void AddGCEpilogueCallback(GCEpilogueCallback* callback, void* data,
                             GCCallbacksInSafepoint::GCType gc_type =
                                 GCCallbacksInSafepoint::GCType::kAll);
  void RemoveGCEpilogueCallback(GCEpilogueCallback* callback, void* data);

  // Weakens StrongDescriptorArray objects into regular DescriptorArray objects.
  void WeakenDescriptorArrays(
      GlobalHandleVector<DescriptorArray> strong_descriptor_arrays);

  // Used to make SetupMainThread() available to unit tests.
  void SetUpMainThreadForTesting();

  // Execute the callback while the local heap is parked. All threads must
  // always park via this method, not directly with `ParkedScope`. The callback
  // is only allowed to execute blocking operations.
  //
  // The callback must be a callable object, expecting either no parameters or a
  // const ParkedScope&, which serves as a witness for parking. The first
  // variant checks if we are on the main thread or not. Use the other two
  // variants if this already known.
  template <typename Callback>
  V8_INLINE void BlockWhileParked(Callback callback);
  template <typename Callback>
  V8_INLINE void BlockMainThreadWhileParked(Callback callback);
  template <typename Callback>
  V8_INLINE void BlockBackgroundThreadWhileParked(Callback callback);

 private:
  using ParkedBit = base::BitField8<bool, 0, 1>;
  using SafepointRequestedBit = ParkedBit::Next<bool, 1>;
  using CollectionRequestedBit = SafepointRequestedBit::Next<bool, 1>;

  class ThreadState final {
   public:
    static constexpr ThreadState Parked() {
      return ThreadState(ParkedBit::kMask);
    }
    static constexpr ThreadState Running() { return ThreadState(0); }

    constexpr bool IsRunning() const { return !ParkedBit::decode(raw_state_); }

    constexpr ThreadState SetRunning() const V8_WARN_UNUSED_RESULT {
      return ThreadState(raw_state_ & ~ParkedBit::kMask);
    }

    constexpr bool IsParked() const { return ParkedBit::decode(raw_state_); }

    constexpr ThreadState SetParked() const V8_WARN_UNUSED_RESULT {
      return ThreadState(ParkedBit::kMask | raw_state_);
    }

    constexpr bool IsSafepointRequested() const {
      return SafepointRequestedBit::decode(raw_state_);
    }

    constexpr bool IsCollectionRequested() const {
      return CollectionRequestedBit::decode(raw_state_);
    }

    constexpr bool IsRunningWithSlowPathFlag() const {
      return IsRunning() && (raw_state_ & (SafepointRequestedBit::kMask |
                                           CollectionRequestedBit::kMask));
    }

   private:
    constexpr explicit ThreadState(uint8_t value) : raw_state_(value) {}

    constexpr uint8_t raw() const { return raw_state_; }

    uint8_t raw_state_;

    friend class LocalHeap;
  };

  class AtomicThreadState final {
   public:
    constexpr explicit AtomicThreadState(ThreadState state)
        : raw_state_(state.raw()) {}

    bool CompareExchangeStrong(ThreadState& expected, ThreadState updated) {
      return raw_state_.compare_exchange_strong(expected.raw_state_,
                                                updated.raw());
    }

    bool CompareExchangeWeak(ThreadState& expected, ThreadState updated) {
      return raw_state_.compare_exchange_weak(expected.raw_state_,
                                              updated.raw());
    }

    ThreadState SetParked() {
      return ThreadState(raw_state_.fetch_or(ParkedBit::kMask));
    }

    ThreadState SetSafepointRequested() {
      return ThreadState(raw_state_.fetch_or(SafepointRequestedBit::kMask));
    }

    ThreadState ClearSafepointRequested() {
      return ThreadState(raw_state_.fetch_and(~SafepointRequestedBit::kMask));
    }

    ThreadState SetCollectionRequested() {
      return ThreadState(raw_state_.fetch_or(CollectionRequestedBit::kMask));
    }

    ThreadState ClearCollectionRequested() {
      return ThreadState(raw_state_.fetch_and(~CollectionRequestedBit::kMask));
    }

    ThreadState load_relaxed() const {
      return ThreadState(raw_state_.load(std::memory_order_relaxed));
    }

   private:
    std::atomic<uint8_t> raw_state_;
  };

  // Slow path of allocation that performs GC and then retries allocation in
  // loop.
  AllocationResult PerformCollectionAndAllocateAgain(
      int object_size, AllocationType type, AllocationOrigin origin,
      AllocationAlignment alignment);

#ifdef DEBUG
  bool IsSafeForConservativeStackScanning() const;
#endif

  template <typename Callback>
  V8_INLINE void ExecuteWithStackMarker(Callback callback);

  void Park() {
    DCHECK(AllowSafepoints::IsAllowed());
    DCHECK(IsSafeForConservativeStackScanning());
    ThreadState expected = ThreadState::Running();
    if (!state_.CompareExchangeWeak(expected, ThreadState::Parked())) {
      ParkSlowPath();
    }
  }

  void Unpark() {
    DCHECK(AllowSafepoints::IsAllowed());
    ThreadState expected = ThreadState::Parked();
    if (!state_.CompareExchangeWeak(expected, ThreadState::Running())) {
      UnparkSlowPath();
    }
  }

  void ParkSlowPath();
  void UnparkSlowPath();
  void EnsureParkedBeforeDestruction();
  void SafepointSlowPath();
  void SleepInSafepoint();
  void SleepInUnpark();

  template <typename Callback>
  V8_INLINE void ParkAndExecuteCallback(Callback callback);

  void EnsurePersistentHandles();

  void InvokeGCEpilogueCallbacksInSafepoint(
      GCCallbacksInSafepoint::GCType gc_type);

  // Set up this LocalHeap as main thread.
  void SetUpMainThread(LinearAllocationArea& new_allocation_info,
                       LinearAllocationArea& old_allocation_info);

  void SetUpMarkingBarrier();
  void SetUpSharedMarking();

  Heap* heap_;
  V8_NO_UNIQUE_ADDRESS PtrComprCageAccessScope ptr_compr_cage_access_scope_;
  bool is_main_thread_;

  AtomicThreadState state_;

  bool allocation_failed_;
  bool main_thread_parked_;

  LocalHeap* prev_;
  LocalHeap* next_;

  std::unique_ptr<LocalHandles> handles_;
  std::unique_ptr<PersistentHandles> persistent_handles_;
  std::unique_ptr<MarkingBarrier> marking_barrier_;

  GCCallbacksInSafepoint gc_epilogue_callbacks_;

  HeapAllocator heap_allocator_;

  MarkingBarrier* saved_marking_barrier_ = nullptr;

  // Stack information for the thread using this local heap.
  ::heap::base::Stack stack_;

  friend class CollectionBarrier;
  friend class GlobalSafepoint;
  friend class Heap;
  friend class Isolate;
  friend class IsolateSafepoint;
  friend class IsolateSafepointScope;
  friend class ParkedScope;
  friend class UnparkedScope;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_HEAP_H_
