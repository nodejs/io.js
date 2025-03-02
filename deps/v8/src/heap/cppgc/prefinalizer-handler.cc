// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/prefinalizer-handler.h"

#include <algorithm>
#include <memory>

#include "src/base/platform/platform.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/liveness-broker.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc {
namespace internal {

PrefinalizerRegistration::PrefinalizerRegistration(void* object,
                                                   Callback callback) {
  auto* page = BasePage::FromPayload(object);
  DCHECK(!page->space().is_compactable());
  page->heap().prefinalizer_handler()->RegisterPrefinalizer({object, callback});
}

bool PreFinalizer::operator==(const PreFinalizer& other) const {
  return (object == other.object) && (callback == other.callback);
}

PreFinalizerHandler::PreFinalizerHandler(HeapBase& heap)
    : current_ordered_pre_finalizers_(&ordered_pre_finalizers_),
      heap_(heap)
{
  DCHECK(CurrentThreadIsCreationThread());
}

void PreFinalizerHandler::RegisterPrefinalizer(PreFinalizer pre_finalizer) {
  DCHECK(CurrentThreadIsCreationThread());
  DCHECK_EQ(ordered_pre_finalizers_.end(),
            std::find(ordered_pre_finalizers_.begin(),
                      ordered_pre_finalizers_.end(), pre_finalizer));
  DCHECK_EQ(current_ordered_pre_finalizers_->end(),
            std::find(current_ordered_pre_finalizers_->begin(),
                      current_ordered_pre_finalizers_->end(), pre_finalizer));
  current_ordered_pre_finalizers_->push_back(pre_finalizer);
}

void PreFinalizerHandler::InvokePreFinalizers() {
  StatsCollector::EnabledScope stats_scope(heap_.stats_collector(),
                                           StatsCollector::kAtomicSweep);
  StatsCollector::EnabledScope nested_stats_scope(
      heap_.stats_collector(), StatsCollector::kSweepInvokePreFinalizers);

  DCHECK(CurrentThreadIsCreationThread());
  LivenessBroker liveness_broker = LivenessBrokerFactory::Create();
  is_invoking_ = true;
  DCHECK_EQ(0u, bytes_allocated_in_prefinalizers);
  // Reset all LABs to force allocations to the slow path for black allocation.
  // This also ensures that a CHECK() hits in case prefinalizers allocate in the
  // configuration that prohibits this.
  heap_.object_allocator().ResetLinearAllocationBuffers();
  // Prefinalizers can allocate other objects with prefinalizers, which will
  // modify ordered_pre_finalizers_ and break iterators.
  std::vector<PreFinalizer> new_ordered_pre_finalizers;
  current_ordered_pre_finalizers_ = &new_ordered_pre_finalizers;
  ordered_pre_finalizers_.erase(
      ordered_pre_finalizers_.begin(),
      std::remove_if(ordered_pre_finalizers_.rbegin(),
                     ordered_pre_finalizers_.rend(),
                     [liveness_broker](const PreFinalizer& pf) {
                       return (pf.callback)(liveness_broker, pf.object);
                     })
          .base());
#ifndef CPPGC_ALLOW_ALLOCATIONS_IN_PREFINALIZERS
  CHECK(new_ordered_pre_finalizers.empty());
#else   // CPPGC_ALLOW_ALLOCATIONS_IN_PREFINALIZERS
  // Newly added objects with prefinalizers will always survive the current GC
  // cycle, so it's safe to add them after clearing out the older prefinalizers.
  ordered_pre_finalizers_.insert(ordered_pre_finalizers_.end(),
                                 new_ordered_pre_finalizers.begin(),
                                 new_ordered_pre_finalizers.end());
#endif  // CPPGC_ALLOW_ALLOCATIONS_IN_PREFINALIZERS
  current_ordered_pre_finalizers_ = &ordered_pre_finalizers_;
  is_invoking_ = false;
  ordered_pre_finalizers_.shrink_to_fit();
}

bool PreFinalizerHandler::CurrentThreadIsCreationThread() {
#ifdef DEBUG
  return heap_.CurrentThreadIsHeapThread();
#else
  return true;
#endif
}

void PreFinalizerHandler::NotifyAllocationInPrefinalizer(size_t size) {
  DCHECK_GT(bytes_allocated_in_prefinalizers + size,
            bytes_allocated_in_prefinalizers);
  bytes_allocated_in_prefinalizers += size;
}

}  // namespace internal
}  // namespace cppgc
