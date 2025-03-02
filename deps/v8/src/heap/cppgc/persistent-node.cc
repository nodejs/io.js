// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/persistent-node.h"

#include <algorithm>
#include <numeric>

#include "include/cppgc/cross-thread-persistent.h"
#include "include/cppgc/persistent.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/platform.h"
#include "src/heap/cppgc/process-heap.h"

namespace cppgc {
namespace internal {

PersistentRegionBase::PersistentRegionBase(
    const FatalOutOfMemoryHandler& oom_handler)
    : oom_handler_(oom_handler) {}

PersistentRegionBase::~PersistentRegionBase() { ClearAllUsedNodes(); }

template <typename PersistentBaseClass>
void PersistentRegionBase::ClearAllUsedNodes() {
  for (auto& slots : nodes_) {
    for (auto& node : *slots) {
      if (!node.IsUsed()) continue;

      static_cast<PersistentBaseClass*>(node.owner())->ClearFromGC();

      // Add nodes back to the free list to allow reusing for subsequent
      // creation calls.
      node.InitializeAsFreeNode(free_list_head_);
      free_list_head_ = &node;
      CPPGC_DCHECK(nodes_in_use_ > 0);
      nodes_in_use_--;
    }
  }
  CPPGC_DCHECK(0u == nodes_in_use_);
}

template void
PersistentRegionBase::ClearAllUsedNodes<CrossThreadPersistentBase>();
template void PersistentRegionBase::ClearAllUsedNodes<PersistentBase>();

void PersistentRegionBase::ClearAllUsedNodes() {
  ClearAllUsedNodes<PersistentBase>();
}

size_t PersistentRegionBase::NodesInUse() const {
#ifdef DEBUG
  const size_t accumulated_nodes_in_use_ = std::accumulate(
      nodes_.cbegin(), nodes_.cend(), 0u, [](size_t acc, const auto& slots) {
        return acc + std::count_if(slots->cbegin(), slots->cend(),
                                   [](const PersistentNode& node) {
                                     return node.IsUsed();
                                   });
      });
  DCHECK_EQ(accumulated_nodes_in_use_, nodes_in_use_);
#endif  // DEBUG
  return nodes_in_use_;
}

void PersistentRegionBase::RefillFreeList() {
  auto node_slots = std::make_unique<PersistentNodeSlots>();
  if (!node_slots.get()) {
    oom_handler_("Oilpan: PersistentRegionBase::RefillFreeList()");
  }
  nodes_.push_back(std::move(node_slots));
  for (auto& node : *nodes_.back()) {
    node.InitializeAsFreeNode(free_list_head_);
    free_list_head_ = &node;
  }
}

PersistentNode* PersistentRegionBase::RefillFreeListAndAllocateNode(
    void* owner, TraceRootCallback trace) {
  RefillFreeList();
  auto* node = TryAllocateNodeFromFreeList(owner, trace);
  CPPGC_DCHECK(node);
  return node;
}

void PersistentRegionBase::Iterate(RootVisitor& root_visitor) {
  free_list_head_ = nullptr;
  for (auto& slots : nodes_) {
    bool is_empty = true;
    for (auto& node : *slots) {
      if (node.IsUsed()) {
        node.Trace(root_visitor);
        is_empty = false;
      } else {
        node.InitializeAsFreeNode(free_list_head_);
        free_list_head_ = &node;
      }
    }
    if (is_empty) {
      PersistentNode* first_next = (*slots)[0].FreeListNext();
      // First next was processed first in the loop above, guaranteeing that it
      // either points to null or into a different node block.
      CPPGC_DCHECK(!first_next || first_next < &slots->front() ||
                   first_next > &slots->back());
      free_list_head_ = first_next;
      slots.reset();
    }
  }
  nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                              [](const auto& ptr) { return !ptr; }),
               nodes_.end());
}

bool PersistentRegion::IsCreationThread() {
  return heap_.CurrentThreadIsHeapThread();
}

PersistentRegionLock::PersistentRegionLock() {
  g_process_mutex.Pointer()->Lock();
}

PersistentRegionLock::~PersistentRegionLock() {
  g_process_mutex.Pointer()->Unlock();
}

// static
void PersistentRegionLock::AssertLocked() {
  return g_process_mutex.Pointer()->AssertHeld();
}

CrossThreadPersistentRegion::CrossThreadPersistentRegion(
    const FatalOutOfMemoryHandler& oom_handler)
    : PersistentRegionBase(oom_handler) {}

CrossThreadPersistentRegion::~CrossThreadPersistentRegion() {
  PersistentRegionLock guard;
  PersistentRegionBase::ClearAllUsedNodes<CrossThreadPersistentBase>();
  nodes_.clear();
  // PersistentRegionBase destructor will be a noop.
}

void CrossThreadPersistentRegion::Iterate(RootVisitor& root_visitor) {
  PersistentRegionLock::AssertLocked();
  PersistentRegionBase::Iterate(root_visitor);
}

size_t CrossThreadPersistentRegion::NodesInUse() const {
  // This method does not require a lock.
  return PersistentRegionBase::NodesInUse();
}

void CrossThreadPersistentRegion::ClearAllUsedNodes() {
  PersistentRegionLock::AssertLocked();
  PersistentRegionBase::ClearAllUsedNodes<CrossThreadPersistentBase>();
}

}  // namespace internal
}  // namespace cppgc
