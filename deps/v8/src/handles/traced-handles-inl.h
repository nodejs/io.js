// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_TRACED_HANDLES_INL_H_
#define V8_HANDLES_TRACED_HANDLES_INL_H_

#include "src/handles/traced-handles.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/slots-inl.h"

namespace v8::internal {

TracedNode* TracedNodeBlock::AllocateNode() {
  DCHECK_NE(used_, capacity_);
  DCHECK_NE(first_free_node_, kInvalidFreeListNodeIndex);
  auto* node = at(first_free_node_);
  first_free_node_ = node->next_free();
  used_++;
  DCHECK(!node->is_in_use());
  return node;
}

std::pair<TracedNodeBlock*, TracedNode*> TracedHandles::AllocateNode() {
  if (V8_UNLIKELY(usable_blocks_.empty())) {
    RefillUsableNodeBlocks();
  }
  TracedNodeBlock* block = usable_blocks_.Front();
  auto* node = block->AllocateNode();
  DCHECK(node->IsMetadataCleared());
  if (V8_UNLIKELY(block->IsFull())) {
    usable_blocks_.Remove(block);
  }
  used_nodes_++;
  return std::make_pair(block, node);
}

bool TracedHandles::NeedsTrackingInYoungNodes(Tagged<Object> object,
                                              TracedNode* node) const {
  DCHECK(!node->is_in_young_list());
  return ObjectInYoungGeneration(object);
}

CppHeap* TracedHandles::GetCppHeapIfUnifiedYoungGC(Isolate* isolate) const {
  // TODO(v8:13475) Consider removing this check when unified-young-gen becomes
  // default.
  if (!v8_flags.cppgc_young_generation) return nullptr;
  auto* cpp_heap = CppHeap::From(isolate->heap()->cpp_heap());
  if (cpp_heap && cpp_heap->generational_gc_supported()) return cpp_heap;
  return nullptr;
}

bool TracedHandles::IsCppGCHostOld(CppHeap& cpp_heap, Address host) const {
  DCHECK(host);
  DCHECK(cpp_heap.generational_gc_supported());
  auto* host_ptr = reinterpret_cast<void*>(host);
  auto* page = cppgc::internal::BasePage::FromInnerAddress(&cpp_heap, host_ptr);
  // TracedReference may be created on stack, in which case assume it's young
  // and doesn't need to be remembered, since it'll anyway be scanned.
  if (!page) return false;
  return !page->ObjectHeaderFromInnerAddress(host_ptr).IsYoung();
}

bool TracedHandles::NeedsToBeRemembered(
    Tagged<Object> object, TracedNode* node, Address* slot,
    TracedReferenceStoreMode store_mode) const {
  DCHECK(!node->has_old_host());

  auto* cpp_heap = GetCppHeapIfUnifiedYoungGC(isolate_);
  if (!cpp_heap) {
    return false;
  }
  if (store_mode == TracedReferenceStoreMode::kInitializingStore) {
    // Don't record initializing stores.
    return false;
  }
  if (is_marking_) {
    // If marking is in progress, the marking barrier will be issued later.
    return false;
  }
  if (!ObjectInYoungGeneration(object)) {
    return false;
  }
  return IsCppGCHostOld(*cpp_heap, reinterpret_cast<Address>(slot));
}

// Publishes all internal state to be consumed by other threads.
FullObjectSlot TracedNode::Publish(Tagged<Object> object,
                                   bool needs_young_bit_update,
                                   bool needs_black_allocation,
                                   bool has_old_host, bool is_droppable_value) {
  DCHECK(IsMetadataCleared());

  flags_ = needs_young_bit_update << IsInYoungList::kShift |
           has_old_host << HasOldHost::kShift |
           is_droppable_value << IsDroppable::kShift | 1 << IsInUse::kShift;
  if (needs_black_allocation) set_markbit();
  reinterpret_cast<std::atomic<Address>*>(&object_)->store(
      object.ptr(), std::memory_order_release);
  return FullObjectSlot(&object_);
}

FullObjectSlot TracedHandles::Create(
    Address value, Address* slot, TracedReferenceStoreMode store_mode,
    TracedReferenceHandling reference_handling) {
  DCHECK_NOT_NULL(slot);
  Tagged<Object> object(value);
  auto [block, node] = AllocateNode();
  const bool needs_young_bit_update = NeedsTrackingInYoungNodes(object, node);
  const bool has_old_host = NeedsToBeRemembered(object, node, slot, store_mode);
  const bool needs_black_allocation =
      is_marking_ && store_mode != TracedReferenceStoreMode::kInitializingStore;
  const bool is_droppable =
      reference_handling == TracedReferenceHandling::kDroppable;
  auto result_slot =
      node->Publish(object, needs_young_bit_update, needs_black_allocation,
                    has_old_host, is_droppable);
  // Write barrier and young node tracking may be reordered, so move them below
  // `Publish()`.
  if (needs_young_bit_update && !block->InYoungList()) {
    young_blocks_.PushFront(block);
    block->SetInYoungList(true);
    DCHECK(block->InYoungList());
  }
  if (needs_black_allocation) {
    WriteBarrier::MarkingFromGlobalHandle(object);
  }
#ifdef VERIFY_HEAP
  if (i::v8_flags.verify_heap) {
    Object::ObjectVerify(*result_slot, isolate_);
  }
#endif  // VERIFY_HEAP
  return result_slot;
}

}  // namespace v8::internal

#endif  // V8_HANDLES_TRACED_HANDLES_INL_H_
