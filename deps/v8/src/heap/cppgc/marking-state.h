// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MARKING_STATE_H_
#define V8_HEAP_CPPGC_MARKING_STATE_H_

#include <algorithm>

#include "include/cppgc/trace-trait.h"
#include "include/cppgc/visitor.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/heap/base/cached-unordered-map.h"
#include "src/heap/base/stack.h"
#include "src/heap/cppgc/compaction-worklists.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/liveness-broker.h"
#include "src/heap/cppgc/marking-worklists.h"

namespace cppgc {
namespace internal {

// C++ marking implementation.
class MarkingStateBase {
 public:
  inline MarkingStateBase(HeapBase&, MarkingWorklists&);
  virtual ~MarkingStateBase() = default;

  MarkingStateBase(const MarkingStateBase&) = delete;
  MarkingStateBase& operator=(const MarkingStateBase&) = delete;

  inline void MarkAndPush(const void*, TraceDescriptor);
  inline void MarkAndPush(HeapObjectHeader&);

  inline void PushMarked(HeapObjectHeader&, TraceDescriptor desc);

  V8_EXPORT_PRIVATE virtual void Publish();

  MarkingWorklists::MarkingWorklist::Local& marking_worklist() {
    return marking_worklist_;
  }
  MarkingWorklists::NotFullyConstructedWorklist&
  not_fully_constructed_worklist() {
    return not_fully_constructed_worklist_;
  }

 protected:
  inline void MarkAndPush(HeapObjectHeader&, TraceDescriptor);

  inline bool MarkNoPush(HeapObjectHeader&);

  HeapBase& heap_;

  MarkingWorklists::MarkingWorklist::Local marking_worklist_;
  MarkingWorklists::NotFullyConstructedWorklist&
      not_fully_constructed_worklist_;
};

MarkingStateBase::MarkingStateBase(HeapBase& heap,
                                   MarkingWorklists& marking_worklists)
    : heap_(heap),
      marking_worklist_(*marking_worklists.marking_worklist()),
      not_fully_constructed_worklist_(
          *marking_worklists.not_fully_constructed_worklist()) {}

void MarkingStateBase::MarkAndPush(const void* object, TraceDescriptor desc) {
  DCHECK_NOT_NULL(object);
  MarkAndPush(
      HeapObjectHeader::FromObject(const_cast<void*>(desc.base_object_payload)),
      desc);
}

void MarkingStateBase::MarkAndPush(HeapObjectHeader& header,
                                   TraceDescriptor desc) {
  DCHECK_NOT_NULL(desc.callback);

  if (header.IsInConstruction<AccessMode::kAtomic>()) {
    not_fully_constructed_worklist_.Push<AccessMode::kAtomic>(&header);
  } else if (MarkNoPush(header)) {
    PushMarked(header, desc);
  }
}

bool MarkingStateBase::MarkNoPush(HeapObjectHeader& header) {
  // A GC should only mark the objects that belong in its heap.
  DCHECK_EQ(&heap_, &BasePage::FromPayload(&header)->heap());
  // Never mark free space objects. This would e.g. hint to marking a promptly
  // freed backing store.
  DCHECK(!header.IsFree<AccessMode::kAtomic>());
  return header.TryMarkAtomic();
}

void MarkingStateBase::MarkAndPush(HeapObjectHeader& header) {
  MarkAndPush(
      header,
      {header.ObjectStart(),
       GlobalGCInfoTable::GCInfoFromIndex(header.GetGCInfoIndex()).trace});
}

void MarkingStateBase::PushMarked(HeapObjectHeader& header,
                                  TraceDescriptor desc) {
  DCHECK(header.IsMarked<AccessMode::kAtomic>());
  DCHECK(!header.IsInConstruction<AccessMode::kAtomic>());
  DCHECK_NOT_NULL(desc.callback);

  marking_worklist_.Push(desc);
}

class BasicMarkingState : public MarkingStateBase {
 public:
  BasicMarkingState(HeapBase& heap, MarkingWorklists&, CompactionWorklists*);
  ~BasicMarkingState() override = default;

  BasicMarkingState(const BasicMarkingState&) = delete;
  BasicMarkingState& operator=(const BasicMarkingState&) = delete;

  inline void RegisterWeakReferenceIfNeeded(const void*, TraceDescriptor,
                                            WeakCallback, const void*);
  inline void RegisterWeakContainerCallback(WeakCallback, const void*);
  inline void RegisterWeakCustomCallback(WeakCallback, const void*);

  void RegisterMovableReference(const void** slot) {
    if (V8_LIKELY(!movable_slots_worklist_)) return;
#if defined(CPPGC_CAGED_HEAP)
    if (V8_UNLIKELY(!CagedHeapBase::IsWithinCage(slot))) return;
#else   // !defined(CPPGC_CAGED_HEAP)
    if (V8_UNLIKELY(heap::base::Stack::IsOnStack(slot))) return;
#endif  // !defined(CPPGC_CAGED_HEAP)

    movable_slots_worklist_->Push(slot);
  }

  // Weak containers are special in that they may require re-tracing if
  // reachable through stack, even if the container was already traced before.
  // ProcessWeakContainer records which weak containers were already marked so
  // that conservative stack scanning knows to retrace them.
  inline void ProcessWeakContainer(const void*, TraceDescriptor, WeakCallback,
                                   const void*);

  inline void ProcessEphemeron(const void*, const void*, TraceDescriptor,
                               Visitor&);

  inline void AccountMarkedBytes(const HeapObjectHeader&);
  inline void AccountMarkedBytes(BasePage*, size_t);
  size_t marked_bytes() const { return marked_bytes_; }

  V8_EXPORT_PRIVATE void Publish() override;

  MarkingWorklists::PreviouslyNotFullyConstructedWorklist::Local&
  previously_not_fully_constructed_worklist() {
    return previously_not_fully_constructed_worklist_;
  }
  MarkingWorklists::WeakCallbackWorklist::Local&
  weak_container_callback_worklist() {
    return weak_container_callback_worklist_;
  }
  MarkingWorklists::WeakCallbackWorklist::Local&
  parallel_weak_callback_worklist() {
    return parallel_weak_callback_worklist_;
  }
  MarkingWorklists::WeakCustomCallbackWorklist::Local&
  weak_custom_callback_worklist() {
    return weak_custom_callback_worklist_;
  }
  MarkingWorklists::WriteBarrierWorklist::Local& write_barrier_worklist() {
    return write_barrier_worklist_;
  }
  MarkingWorklists::ConcurrentMarkingBailoutWorklist::Local&
  concurrent_marking_bailout_worklist() {
    return concurrent_marking_bailout_worklist_;
  }
  MarkingWorklists::EphemeronPairsWorklist::Local&
  discovered_ephemeron_pairs_worklist() {
    return discovered_ephemeron_pairs_worklist_;
  }
  MarkingWorklists::EphemeronPairsWorklist::Local&
  ephemeron_pairs_for_processing_worklist() {
    return ephemeron_pairs_for_processing_worklist_;
  }
  MarkingWorklists::WeakContainersWorklist& weak_containers_worklist() {
    return weak_containers_worklist_;
  }

  CompactionWorklists::MovableReferencesWorklist::Local*
  movable_slots_worklist() {
    return movable_slots_worklist_.get();
  }

  bool DidDiscoverNewEphemeronPairs() const {
    return discovered_new_ephemeron_pairs_;
  }

  void ResetDidDiscoverNewEphemeronPairs() {
    discovered_new_ephemeron_pairs_ = false;
  }

  void set_in_atomic_pause() { in_atomic_pause_ = true; }

 protected:
  inline void RegisterWeakContainer(HeapObjectHeader&);

  MarkingWorklists::PreviouslyNotFullyConstructedWorklist::Local
      previously_not_fully_constructed_worklist_;
  MarkingWorklists::WeakCallbackWorklist::Local
      weak_container_callback_worklist_;
  MarkingWorklists::WeakCallbackWorklist::Local
      parallel_weak_callback_worklist_;
  MarkingWorklists::WeakCustomCallbackWorklist::Local
      weak_custom_callback_worklist_;
  MarkingWorklists::WriteBarrierWorklist::Local write_barrier_worklist_;
  MarkingWorklists::ConcurrentMarkingBailoutWorklist::Local
      concurrent_marking_bailout_worklist_;
  MarkingWorklists::EphemeronPairsWorklist::Local
      discovered_ephemeron_pairs_worklist_;
  MarkingWorklists::EphemeronPairsWorklist::Local
      ephemeron_pairs_for_processing_worklist_;
  MarkingWorklists::WeakContainersWorklist& weak_containers_worklist_;
  // Existence of the worklist (|movable_slot_worklist_| != nullptr) denotes
  // that compaction is currently enabled and slots must be recorded.
  std::unique_ptr<CompactionWorklists::MovableReferencesWorklist::Local>
      movable_slots_worklist_;

  size_t marked_bytes_ = 0;
  bool in_ephemeron_processing_ = false;
  bool discovered_new_ephemeron_pairs_ = false;
  bool in_atomic_pause_ = false;
  heap::base::CachedUnorderedMap<BasePage*, int64_t, v8::base::hash<BasePage*>>
      marked_bytes_map_;
};

void BasicMarkingState::RegisterWeakReferenceIfNeeded(
    const void* object, TraceDescriptor desc, WeakCallback weak_callback,
    const void* parameter) {
  // Filter out already marked values. The write barrier for WeakMember
  // ensures that any newly set value after this point is kept alive and does
  // not require the callback.
  const HeapObjectHeader& header =
      HeapObjectHeader::FromObject(desc.base_object_payload);
  if (!header.IsInConstruction<AccessMode::kAtomic>() &&
      header.IsMarked<AccessMode::kAtomic>())
    return;
  parallel_weak_callback_worklist_.Push({weak_callback, parameter});
}

void BasicMarkingState::RegisterWeakContainerCallback(WeakCallback callback,
                                                      const void* object) {
  DCHECK_NOT_NULL(callback);
  weak_container_callback_worklist_.Push({callback, object});
}

void BasicMarkingState::RegisterWeakCustomCallback(WeakCallback callback,
                                                   const void* object) {
  DCHECK_NOT_NULL(callback);
  weak_custom_callback_worklist_.Push({callback, object});
}

void BasicMarkingState::RegisterWeakContainer(HeapObjectHeader& header) {
  weak_containers_worklist_.Push<AccessMode::kAtomic>(&header);
}

void BasicMarkingState::ProcessWeakContainer(const void* object,
                                             TraceDescriptor desc,
                                             WeakCallback callback,
                                             const void* data) {
  DCHECK_NOT_NULL(object);

  HeapObjectHeader& header =
      HeapObjectHeader::FromObject(const_cast<void*>(object));

  if (header.IsInConstruction<AccessMode::kAtomic>()) {
    not_fully_constructed_worklist_.Push<AccessMode::kAtomic>(&header);
    return;
  }

  RegisterWeakContainer(header);

  // Only mark the container initially. Its buckets will be processed after
  // marking.
  if (!MarkNoPush(header)) return;

  // Register final weak processing of the backing store.
  RegisterWeakContainerCallback(callback, data);

  // Weak containers might not require tracing. In such cases the callback in
  // the TraceDescriptor will be nullptr. For ephemerons the callback will be
  // non-nullptr so that the container is traced and the ephemeron pairs are
  // processed.
  if (desc.callback) {
    PushMarked(header, desc);
  } else {
    // For weak containers, there's no trace callback and no processing loop to
    // update the marked bytes, hence inline that here.
    AccountMarkedBytes(header);
  }
}

void BasicMarkingState::ProcessEphemeron(const void* key, const void* value,
                                         TraceDescriptor value_desc,
                                         Visitor& visitor) {
  // ProcessEphemeron is not expected to find new ephemerons recursively, which
  // would break the main marking loop.
  DCHECK(!in_ephemeron_processing_);
  in_ephemeron_processing_ = true;
  // Keys are considered live even in incremental/concurrent marking settings
  // because the write barrier for WeakMember ensures that any newly set value
  // after this point is kept alive and does not require the callback.
  const bool key_in_construction =
      HeapObjectHeader::FromObject(key).IsInConstruction<AccessMode::kAtomic>();
  const bool key_considered_as_live =
      key_in_construction
          ? in_atomic_pause_
          : HeapObjectHeader::FromObject(key).IsMarked<AccessMode::kAtomic>();
  DCHECK_IMPLIES(
      key_in_construction && in_atomic_pause_,
      HeapObjectHeader::FromObject(key).IsMarked<AccessMode::kAtomic>());
  if (key_considered_as_live) {
    if (value_desc.base_object_payload) {
      MarkAndPush(value_desc.base_object_payload, value_desc);
    } else {
      // If value_desc.base_object_payload is nullptr, the value is not GCed and
      // should be immediately traced.
      value_desc.callback(&visitor, value);
    }
  } else {
    discovered_ephemeron_pairs_worklist_.Push({key, value, value_desc});
    discovered_new_ephemeron_pairs_ = true;
  }
  in_ephemeron_processing_ = false;
}

void BasicMarkingState::AccountMarkedBytes(const HeapObjectHeader& header) {
  const size_t marked_bytes =
      header.IsLargeObject<AccessMode::kAtomic>()
          ? reinterpret_cast<const LargePage*>(BasePage::FromPayload(&header))
                ->PayloadSize()
          : header.AllocatedSize<AccessMode::kAtomic>();
  auto* base_page =
      BasePage::FromPayload(&const_cast<HeapObjectHeader&>(header));
  AccountMarkedBytes(base_page, marked_bytes);
}

void BasicMarkingState::AccountMarkedBytes(BasePage* base_page,
                                           size_t marked_bytes) {
  marked_bytes_ += marked_bytes;
  marked_bytes_map_[base_page] += static_cast<int64_t>(marked_bytes);
}

class MutatorMarkingState final : public BasicMarkingState {
 public:
  MutatorMarkingState(HeapBase& heap, MarkingWorklists& marking_worklists,
                      CompactionWorklists* compaction_worklists)
      : BasicMarkingState(heap, marking_worklists, compaction_worklists),
        retrace_marked_objects_worklist_(
            *marking_worklists.retrace_marked_objects_worklist()) {}
  ~MutatorMarkingState() override = default;

  inline bool MarkNoPush(HeapObjectHeader& header) {
    return MutatorMarkingState::BasicMarkingState::MarkNoPush(header);
  }

  inline void ReTraceMarkedWeakContainer(cppgc::Visitor&, HeapObjectHeader&);

  inline void DynamicallyMarkAddress(ConstAddress);

  // Moves objects in not_fully_constructed_worklist_ to
  // previously_not_full_constructed_worklists_.
  void FlushNotFullyConstructedObjects();

  // Moves ephemeron pairs in discovered_ephemeron_pairs_worklist_ to
  // ephemeron_pairs_for_processing_worklist_.
  void FlushDiscoveredEphemeronPairs();

  inline void InvokeWeakRootsCallbackIfNeeded(const void*, TraceDescriptor,
                                              WeakCallback, const void*);

  inline bool IsMarkedWeakContainer(HeapObjectHeader&);

  MarkingWorklists::RetraceMarkedObjectsWorklist::Local&
  retrace_marked_objects_worklist() {
    return retrace_marked_objects_worklist_;
  }

  V8_EXPORT_PRIVATE void Publish() override;

 private:
  // Weak containers are strongly retraced during conservative stack scanning.
  // Stack scanning happens once per GC at the start of the atomic pause.
  // Because the visitor is not retained between GCs, there is no need to clear
  // the set at the end of GC.
  class RecentlyRetracedWeakContainers {
    static constexpr size_t kMaxCacheSize = 8;

   public:
    inline bool Contains(const HeapObjectHeader*) const;
    inline void Insert(const HeapObjectHeader*);

   private:
    std::vector<const HeapObjectHeader*> recently_retraced_cache_;
    size_t last_used_index_ = -1;
  } recently_retraced_weak_containers_;

  MarkingWorklists::RetraceMarkedObjectsWorklist::Local
      retrace_marked_objects_worklist_;
};

void MutatorMarkingState::ReTraceMarkedWeakContainer(cppgc::Visitor& visitor,
                                                     HeapObjectHeader& header) {
  DCHECK(weak_containers_worklist_.Contains<AccessMode::kAtomic>(&header));
  recently_retraced_weak_containers_.Insert(&header);
  retrace_marked_objects_worklist().Push(&header);
}

void MutatorMarkingState::DynamicallyMarkAddress(ConstAddress address) {
  HeapObjectHeader& header =
      BasePage::FromPayload(address)->ObjectHeaderFromInnerAddress(
          const_cast<Address>(address));
  DCHECK(!header.IsInConstruction());
  if (MarkNoPush(header)) {
    marking_worklist_.Push(
        {reinterpret_cast<void*>(header.ObjectStart()),
         GlobalGCInfoTable::GCInfoFromIndex(header.GetGCInfoIndex()).trace});
  }
}

void MutatorMarkingState::InvokeWeakRootsCallbackIfNeeded(
    const void* object, TraceDescriptor desc, WeakCallback weak_callback,
    const void* parameter) {
  // Since weak roots are only traced at the end of marking, we can execute
  // the callback instead of registering it.
#if DEBUG
  const HeapObjectHeader& header =
      HeapObjectHeader::FromObject(desc.base_object_payload);
  DCHECK_IMPLIES(header.IsInConstruction(),
                 header.IsMarked<AccessMode::kAtomic>());
#endif  // DEBUG
  weak_callback(LivenessBrokerFactory::Create(), parameter);
}

bool MutatorMarkingState::IsMarkedWeakContainer(HeapObjectHeader& header) {
  const bool result =
      weak_containers_worklist_.Contains<AccessMode::kAtomic>(&header) &&
      !recently_retraced_weak_containers_.Contains(&header);
  DCHECK_IMPLIES(result, header.IsMarked<AccessMode::kAtomic>());
  DCHECK_IMPLIES(result, !header.IsInConstruction());
  return result;
}

bool MutatorMarkingState::RecentlyRetracedWeakContainers::Contains(
    const HeapObjectHeader* header) const {
  return std::find(recently_retraced_cache_.begin(),
                   recently_retraced_cache_.end(),
                   header) != recently_retraced_cache_.end();
}

void MutatorMarkingState::RecentlyRetracedWeakContainers::Insert(
    const HeapObjectHeader* header) {
  last_used_index_ = (last_used_index_ + 1) % kMaxCacheSize;
  if (recently_retraced_cache_.size() <= last_used_index_)
    recently_retraced_cache_.push_back(header);
  else
    recently_retraced_cache_[last_used_index_] = header;
}

class ConcurrentMarkingState final : public BasicMarkingState {
 public:
  ConcurrentMarkingState(HeapBase& heap, MarkingWorklists& marking_worklists,
                         CompactionWorklists* compaction_worklists)
      : BasicMarkingState(heap, marking_worklists, compaction_worklists) {}

  ~ConcurrentMarkingState() override {
    DCHECK_EQ(last_marked_bytes_, marked_bytes_);
  }

  size_t RecentlyMarkedBytes() {
    return marked_bytes_ - std::exchange(last_marked_bytes_, marked_bytes_);
  }

  inline void AccountDeferredMarkedBytes(BasePage* base_page,
                                         size_t deferred_bytes) {
    // AccountDeferredMarkedBytes is called from Trace methods, which are always
    // called after AccountMarkedBytes, so there should be no underflow here.
    DCHECK_LE(deferred_bytes, marked_bytes_);
    marked_bytes_ -= deferred_bytes;
    marked_bytes_map_[base_page] -= static_cast<int64_t>(deferred_bytes);
  }

 private:
  size_t last_marked_bytes_ = 0;
};

template <size_t deadline_check_interval, typename WorklistLocal,
          typename Callback, typename Predicate>
bool DrainWorklistWithPredicate(Predicate should_yield,
                                WorklistLocal& worklist_local,
                                Callback callback) {
  if (worklist_local.IsLocalAndGlobalEmpty()) return true;
  // For concurrent markers, should_yield also reports marked bytes.
  if (should_yield()) return false;
  size_t processed_callback_count = deadline_check_interval;
  typename WorklistLocal::ItemType item;
  while (worklist_local.Pop(&item)) {
    callback(item);
    if (--processed_callback_count == 0) {
      if (should_yield()) {
        return false;
      }
      processed_callback_count = deadline_check_interval;
    }
  }
  return true;
}

template <AccessMode mode>
void DynamicallyTraceMarkedObject(Visitor& visitor,
                                  const HeapObjectHeader& header) {
  DCHECK(!header.IsInConstruction<mode>());
  DCHECK(header.IsMarked<AccessMode::kAtomic>());
  header.Trace<mode>(&visitor);
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MARKING_STATE_H_
