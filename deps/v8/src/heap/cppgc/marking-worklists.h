// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MARKING_WORKLISTS_H_
#define V8_HEAP_CPPGC_MARKING_WORKLISTS_H_

#include <unordered_set>

#include "include/cppgc/visitor.h"
#include "src/base/platform/mutex.h"
#include "src/heap/base/worklist.h"
#include "src/heap/cppgc/heap-object-header.h"

namespace cppgc {
namespace internal {

class MarkingWorklists {
 private:
  class V8_EXPORT_PRIVATE ExternalMarkingWorklist {
   public:
    template <AccessMode = AccessMode::kNonAtomic>
    void Push(HeapObjectHeader*);
    template <AccessMode = AccessMode::kNonAtomic>
    bool Contains(HeapObjectHeader*);
    template <AccessMode = AccessMode::kNonAtomic>
    std::unordered_set<HeapObjectHeader*> Extract();
    template <AccessMode = AccessMode::kNonAtomic>
    void Clear();
    template <AccessMode = AccessMode::kNonAtomic>
    bool IsEmpty();

    ~ExternalMarkingWorklist();

   private:
    template <AccessMode>
    struct ConditionalMutexGuard;

    void* operator new(size_t) = delete;
    void* operator new[](size_t) = delete;
    void operator delete(void*) = delete;
    void operator delete[](void*) = delete;

    v8::base::SpinningMutex lock_;
    std::unordered_set<HeapObjectHeader*> objects_;
  };

 public:
  static constexpr int kMutatorThreadId = 0;

  using MarkingItem = cppgc::TraceDescriptor;

  struct WeakCallbackItem {
    cppgc::WeakCallback callback;
    const void* parameter;
  };

  struct ConcurrentMarkingBailoutItem {
    const void* parameter;
    TraceCallback callback;
    size_t bailedout_size;
  };

  struct EphemeronPairItem {
    const void* key;
    const void* value;
    TraceDescriptor value_desc;
  };

  // Segment size of 512 entries necessary to avoid throughput regressions.
  // Since the work list is currently a temporary object this is not a problem.
  using MarkingWorklist =
      heap::base::Worklist<MarkingItem, 512 /* local entries */>;
  using NotFullyConstructedWorklist = ExternalMarkingWorklist;
  using PreviouslyNotFullyConstructedWorklist =
      heap::base::Worklist<HeapObjectHeader*, 16 /* local entries */>;
  using WeakCallbackWorklist =
      heap::base::Worklist<WeakCallbackItem, 64 /* local entries */>;
  using WeakCustomCallbackWorklist =
      heap::base::Worklist<WeakCallbackItem, 16 /* local entries */>;
  using WriteBarrierWorklist =
      heap::base::Worklist<HeapObjectHeader*, 64 /*local entries */>;
  using ConcurrentMarkingBailoutWorklist =
      heap::base::Worklist<ConcurrentMarkingBailoutItem,
                           64 /* local entries */>;
  using EphemeronPairsWorklist =
      heap::base::Worklist<EphemeronPairItem, 64 /* local entries */>;
  using WeakContainersWorklist = ExternalMarkingWorklist;
  using RetraceMarkedObjectsWorklist =
      heap::base::Worklist<HeapObjectHeader*, 16 /* local entries */>;

  MarkingWorklist* marking_worklist() { return &marking_worklist_; }
  NotFullyConstructedWorklist* not_fully_constructed_worklist() {
    return &not_fully_constructed_worklist_;
  }
  PreviouslyNotFullyConstructedWorklist*
  previously_not_fully_constructed_worklist() {
    return &previously_not_fully_constructed_worklist_;
  }
  WriteBarrierWorklist* write_barrier_worklist() {
    return &write_barrier_worklist_;
  }
  WeakCallbackWorklist* weak_container_callback_worklist() {
    return &weak_container_callback_worklist_;
  }
  WeakCallbackWorklist* parallel_weak_callback_worklist() {
    return &parallel_weak_callback_worklist_;
  }
  WeakCustomCallbackWorklist* weak_custom_callback_worklist() {
    return &weak_custom_callback_worklist_;
  }
  const ConcurrentMarkingBailoutWorklist* concurrent_marking_bailout_worklist()
      const {
    return &concurrent_marking_bailout_worklist_;
  }
  ConcurrentMarkingBailoutWorklist* concurrent_marking_bailout_worklist() {
    return &concurrent_marking_bailout_worklist_;
  }
  EphemeronPairsWorklist* discovered_ephemeron_pairs_worklist() {
    return &discovered_ephemeron_pairs_worklist_;
  }
  EphemeronPairsWorklist* ephemeron_pairs_for_processing_worklist() {
    return &ephemeron_pairs_for_processing_worklist_;
  }
  WeakContainersWorklist* weak_containers_worklist() {
    return &weak_containers_worklist_;
  }
  RetraceMarkedObjectsWorklist* retrace_marked_objects_worklist() {
    return &retrace_marked_objects_worklist_;
  }

  void ClearForTesting();

 private:
  MarkingWorklist marking_worklist_;
  NotFullyConstructedWorklist not_fully_constructed_worklist_;
  PreviouslyNotFullyConstructedWorklist
      previously_not_fully_constructed_worklist_;
  WriteBarrierWorklist write_barrier_worklist_;
  // Hold weak callbacks for weak containers (e.g. containers with WeakMembers).
  WeakCallbackWorklist weak_container_callback_worklist_;
  // Hold weak custom callbacks (e.g. for containers with UntracedMembers).
  WeakCustomCallbackWorklist weak_custom_callback_worklist_;
  // Hold weak callbacks which can invoke on main or worker thread (used for
  // regular WeakMember).
  WeakCallbackWorklist parallel_weak_callback_worklist_;
  ConcurrentMarkingBailoutWorklist concurrent_marking_bailout_worklist_;
  EphemeronPairsWorklist discovered_ephemeron_pairs_worklist_;
  EphemeronPairsWorklist ephemeron_pairs_for_processing_worklist_;
  WeakContainersWorklist weak_containers_worklist_;
  RetraceMarkedObjectsWorklist retrace_marked_objects_worklist_;
};

template <>
struct MarkingWorklists::ExternalMarkingWorklist::ConditionalMutexGuard<
    AccessMode::kNonAtomic> {
  explicit ConditionalMutexGuard(v8::base::SpinningMutex*) {}
};

template <>
struct MarkingWorklists::ExternalMarkingWorklist::ConditionalMutexGuard<
    AccessMode::kAtomic> {
  explicit ConditionalMutexGuard(v8::base::SpinningMutex* lock)
      : guard_(lock) {}

 private:
  v8::base::SpinningMutexGuard guard_;
};

template <AccessMode mode>
void MarkingWorklists::ExternalMarkingWorklist::Push(HeapObjectHeader* object) {
  DCHECK_NOT_NULL(object);
  ConditionalMutexGuard<mode> guard(&lock_);
  objects_.insert(object);
}

template <AccessMode mode>
bool MarkingWorklists::ExternalMarkingWorklist::Contains(
    HeapObjectHeader* object) {
  ConditionalMutexGuard<mode> guard(&lock_);
  return objects_.find(object) != objects_.end();
}

template <AccessMode mode>
std::unordered_set<HeapObjectHeader*>
MarkingWorklists::ExternalMarkingWorklist::Extract() {
  ConditionalMutexGuard<mode> guard(&lock_);
  std::unordered_set<HeapObjectHeader*> extracted;
  std::swap(extracted, objects_);
  DCHECK(objects_.empty());
  return extracted;
}

template <AccessMode mode>
void MarkingWorklists::ExternalMarkingWorklist::Clear() {
  ConditionalMutexGuard<mode> guard(&lock_);
  objects_.clear();
}

template <AccessMode mode>
bool MarkingWorklists::ExternalMarkingWorklist::IsEmpty() {
  ConditionalMutexGuard<mode> guard(&lock_);
  return objects_.empty();
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MARKING_WORKLISTS_H_
