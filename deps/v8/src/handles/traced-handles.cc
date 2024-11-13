// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/traced-handles.h"

#include <limits>

#include "include/v8-embedder-heap.h"
#include "include/v8-internal.h"
#include "include/v8-traced-handle.h"
#include "src/base/logging.h"
#include "src/base/platform/memory.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/handles/traced-handles-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/objects.h"
#include "src/objects/slots.h"
#include "src/objects/visitors.h"

namespace v8::internal {

class TracedNodeBlock;

TracedNode::TracedNode(IndexType index, IndexType next_free_index)
    : next_free_index_(next_free_index), index_(index) {
  // TracedNode size should stay within 2 words.
  static_assert(sizeof(TracedNode) <= (2 * kSystemPointerSize));
  DCHECK(!is_in_use());
  DCHECK(!is_in_young_list());
  DCHECK(!is_weak());
  DCHECK(!markbit());
  DCHECK(!has_old_host());
  DCHECK(!is_droppable());
}

void TracedNode::Release(Address zap_value) {
  DCHECK(is_in_use());
  // Clear all flags.
  flags_ = 0;
  clear_markbit();
  set_raw_object(zap_value);
  DCHECK(IsMetadataCleared());
}

// static
TracedNodeBlock* TracedNodeBlock::Create(TracedHandles& traced_handles) {
  static_assert(alignof(TracedNodeBlock) >= alignof(TracedNode));
  static_assert(sizeof(TracedNodeBlock) % alignof(TracedNode) == 0,
                "TracedNodeBlock size is used to auto-align node FAM storage.");
  const size_t min_wanted_size =
      sizeof(TracedNodeBlock) +
      sizeof(TracedNode) * TracedNodeBlock::kMinCapacity;
  const auto raw_result = v8::base::AllocateAtLeast<char>(min_wanted_size);
  const size_t capacity = std::min(
      (raw_result.count - sizeof(TracedNodeBlock)) / sizeof(TracedNode),
      kMaxCapacity);
  CHECK_LT(capacity, std::numeric_limits<TracedNode::IndexType>::max());
  const auto result = std::make_pair(raw_result.ptr, capacity);
  return new (result.first) TracedNodeBlock(
      traced_handles, static_cast<TracedNode::IndexType>(result.second));
}

// static
void TracedNodeBlock::Delete(TracedNodeBlock* block) { free(block); }

TracedNodeBlock::TracedNodeBlock(TracedHandles& traced_handles,
                                 TracedNode::IndexType capacity)
    : traced_handles_(traced_handles), capacity_(capacity) {
  for (TracedNode::IndexType i = 0; i < (capacity_ - 1); i++) {
    new (at(i)) TracedNode(i, i + 1);
  }
  new (at(capacity_ - 1)) TracedNode(capacity_ - 1, kInvalidFreeListNodeIndex);
}

// static
TracedNodeBlock& TracedNodeBlock::From(TracedNode& node) {
  TracedNode* first_node = &node - node.index();
  return *reinterpret_cast<TracedNodeBlock*>(
      reinterpret_cast<uintptr_t>(first_node) - sizeof(TracedNodeBlock));
}

// static
const TracedNodeBlock& TracedNodeBlock::From(const TracedNode& node) {
  return From(const_cast<TracedNode&>(node));
}

void TracedNodeBlock::FreeNode(TracedNode* node, Address zap_value) {
  DCHECK(node->is_in_use());
  node->Release(zap_value);
  DCHECK(!node->is_in_use());
  node->set_next_free(first_free_node_);
  first_free_node_ = node->index();
  used_--;
}

void SetSlotThreadSafe(Address** slot, Address* val) {
  reinterpret_cast<std::atomic<Address*>*>(slot)->store(
      val, std::memory_order_relaxed);
}

void TracedHandles::RefillUsableNodeBlocks() {
  TracedNodeBlock* block;
  if (empty_blocks_.empty()) {
    block = TracedNodeBlock::Create(*this);
    block_size_bytes_ += block->size_bytes();
  } else {
    block = empty_blocks_.back();
    empty_blocks_.pop_back();
  }
  usable_blocks_.PushFront(block);
  blocks_.PushFront(block);
  num_blocks_++;
  DCHECK(!block->InYoungList());
  DCHECK(block->IsEmpty());
  DCHECK_EQ(usable_blocks_.Front(), block);
  DCHECK(!usable_blocks_.empty());
}

void TracedHandles::FreeNode(TracedNode* node, Address zap_value) {
  auto& block = TracedNodeBlock::From(*node);
  if (V8_UNLIKELY(block.IsFull())) {
    DCHECK(!usable_blocks_.ContainsSlow(&block));
    usable_blocks_.PushFront(&block);
  }
  block.FreeNode(node, zap_value);
  if (block.IsEmpty()) {
    usable_blocks_.Remove(&block);
    blocks_.Remove(&block);
    if (block.InYoungList()) {
      young_blocks_.Remove(&block);
      block.SetInYoungList(false);
    }
    num_blocks_--;
    empty_blocks_.push_back(&block);
  }
  used_nodes_--;
}

TracedHandles::TracedHandles(Isolate* isolate) : isolate_(isolate) {}

TracedHandles::~TracedHandles() {
  size_t block_size_bytes = 0;
  while (!blocks_.empty()) {
    auto* block = blocks_.Front();
    blocks_.PopFront();
    block_size_bytes += block->size_bytes();
    TracedNodeBlock::Delete(block);
  }
  for (auto* block : empty_blocks_) {
    block_size_bytes += block->size_bytes();
    TracedNodeBlock::Delete(block);
  }
  USE(block_size_bytes);
  DCHECK_EQ(block_size_bytes, block_size_bytes_);
}

void TracedHandles::Destroy(TracedNodeBlock& node_block, TracedNode& node) {
  DCHECK_IMPLIES(is_marking_, !is_sweeping_on_mutator_thread_);
  DCHECK_IMPLIES(is_sweeping_on_mutator_thread_, !is_marking_);

  // If sweeping on the mutator thread is running then the handle destruction
  // may be a result of a Reset() call from a destructor. The node will be
  // reclaimed on the next cycle.
  //
  // This allows v8::TracedReference::Reset() calls from destructors on
  // objects that may be used from stack and heap.
  if (is_sweeping_on_mutator_thread_) {
    return;
  }

  if (is_marking_) {
    // Incremental/concurrent marking is running. This also covers the scavenge
    // case which prohibits eagerly reclaiming nodes when marking is on during a
    // scavenge.
    //
    // On-heap traced nodes are released in the atomic pause in
    // `ResetDeadNodes()` when they are discovered as not marked. Eagerly clear
    // out the object here to avoid needlessly marking it from this point on.
    // The node will be reclaimed on the next cycle.
    node.set_raw_object<AccessMode::ATOMIC>(kNullAddress);
    return;
  }

  // In case marking and sweeping are off, the handle may be freed immediately.
  // Note that this includes also the case when invoking the first pass
  // callbacks during the atomic pause which requires releasing a node fully.
  FreeNode(&node, kTracedHandleEagerResetZapValue);
}

void TracedHandles::Copy(const TracedNode& from_node, Address** to) {
  DCHECK_NE(kGlobalHandleZapValue, from_node.raw_object());
  FullObjectSlot o =
      Create(from_node.raw_object(), reinterpret_cast<Address*>(to),
             TracedReferenceStoreMode::kAssigningStore,
             TracedReferenceHandling::kDefault);
  SetSlotThreadSafe(to, o.location());
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    Object::ObjectVerify(Tagged<Object>(**to), isolate_);
  }
#endif  // VERIFY_HEAP
}

void TracedHandles::Move(TracedNode& from_node, Address** from, Address** to) {
  DCHECK(from_node.is_in_use());

  // Deal with old "to".
  auto* to_node = TracedNode::FromLocation(*to);
  DCHECK_IMPLIES(*to, to_node->is_in_use());
  DCHECK_IMPLIES(*to, kGlobalHandleZapValue != to_node->raw_object());
  DCHECK_NE(kGlobalHandleZapValue, from_node.raw_object());
  if (*to) {
    auto& to_node_block = TracedNodeBlock::From(*to_node);
    Destroy(to_node_block, *to_node);
  }

  // Set "to" to "from".
  SetSlotThreadSafe(to, *from);
  to_node = &from_node;

  // Deal with new "to"
  DCHECK_NOT_NULL(*to);
  DCHECK_EQ(*from, *to);
  if (is_marking_) {
    // Write barrier needs to cover node as well as object.
    to_node->set_markbit();
    WriteBarrier::MarkingFromGlobalHandle(to_node->object());
  } else if (auto* cpp_heap = GetCppHeapIfUnifiedYoungGC(isolate_)) {
    const bool object_is_young_and_not_yet_recorded =
        !from_node.has_old_host() &&
        ObjectInYoungGeneration(from_node.object());
    if (object_is_young_and_not_yet_recorded &&
        IsCppGCHostOld(*cpp_heap, reinterpret_cast<Address>(to))) {
      DCHECK(from_node.is_in_young_list());
      from_node.set_has_old_host(true);
    }
  }
  SetSlotThreadSafe(from, nullptr);
}

void TracedHandles::SetIsMarking(bool value) {
  DCHECK_EQ(is_marking_, !value);
  is_marking_ = value;
}

void TracedHandles::SetIsSweepingOnMutatorThread(bool value) {
  DCHECK_EQ(is_sweeping_on_mutator_thread_, !value);
  is_sweeping_on_mutator_thread_ = value;
}

const TracedHandles::NodeBounds TracedHandles::GetNodeBounds() const {
  TracedHandles::NodeBounds block_bounds;
  block_bounds.reserve(num_blocks_);
  for (const auto* block : blocks_) {
    block_bounds.push_back(
        {block->nodes_begin_address(), block->nodes_end_address()});
  }
  std::sort(block_bounds.begin(), block_bounds.end(),
            [](const auto& pair1, const auto& pair2) {
              return pair1.first < pair2.first;
            });
  return block_bounds;
}

void TracedHandles::UpdateListOfYoungNodes() {
  const bool needs_to_mark_as_old =
      static_cast<bool>(GetCppHeapIfUnifiedYoungGC(isolate_));

  for (auto it = young_blocks_.begin(); it != young_blocks_.end();) {
    bool contains_young_node = false;
    TracedNodeBlock* const block = *it;
    DCHECK(block->InYoungList());

    for (auto* node : *block) {
      if (!node->is_in_young_list()) continue;
      DCHECK(node->is_in_use());
      if (ObjectInYoungGeneration(node->object())) {
        contains_young_node = true;
        // The node was discovered through a cppgc object, which will be
        // immediately promoted. Remember the object.
        if (needs_to_mark_as_old) node->set_has_old_host(true);
      } else {
        node->set_is_in_young_list(false);
        node->set_has_old_host(false);
      }
    }
    if (contains_young_node) {
      ++it;
    } else {
      it = young_blocks_.RemoveAt(it);
      block->SetInYoungList(false);
    }
  }
}

void TracedHandles::DeleteEmptyBlocks() {
  // Keep one node block around for fast allocation/deallocation patterns.
  if (empty_blocks_.size() <= 1) return;

  for (size_t i = 1; i < empty_blocks_.size(); i++) {
    auto* block = empty_blocks_[i];
    DCHECK(block->IsEmpty());
    DCHECK_GE(block_size_bytes_, block->size_bytes());
    block_size_bytes_ -= block->size_bytes();
    TracedNodeBlock::Delete(block);
  }
  empty_blocks_.resize(1);
  empty_blocks_.shrink_to_fit();
}

void TracedHandles::ResetDeadNodes(
    WeakSlotCallbackWithHeap should_reset_handle) {
  // Manual iteration as the block may be deleted in `FreeNode()`.
  for (auto it = blocks_.begin(); it != blocks_.end();) {
    auto* block = *(it++);
    for (auto* node : *block) {
      if (!node->is_in_use()) continue;

      // Detect unreachable nodes first.
      if (!node->markbit()) {
        FreeNode(node, kTracedHandleFullGCResetZapValue);
        continue;
      }

      // Node was reachable. Clear the markbit for the next GC.
      node->clear_markbit();
      // TODO(v8:13141): Turn into a DCHECK after some time.
      CHECK(!should_reset_handle(isolate_->heap(), node->location()));
    }

    if (block->InYoungList()) {
      young_blocks_.Remove(block);
      block->SetInYoungList(false);
    }
  }

  CHECK(young_blocks_.empty());
}

void TracedHandles::ResetYoungDeadNodes(
    WeakSlotCallbackWithHeap should_reset_handle) {
  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_young_list()) continue;
      DCHECK(node->is_in_use());
      DCHECK_IMPLIES(node->has_old_host(), node->markbit());

      if (!node->markbit()) {
        FreeNode(node, kTracedHandleMinorGCResetZapValue);
        continue;
      }

      // Node was reachable. Clear the markbit for the next GC.
      node->clear_markbit();
      // TODO(v8:13141): Turn into a DCHECK after some time.
      CHECK(!should_reset_handle(isolate_->heap(), node->location()));
    }
  }
}

namespace {
void ComputeWeaknessForYoungObject(
    EmbedderRootsHandler* handler, TracedNode* node,
    bool should_call_is_root_for_default_traced_reference) {
  DCHECK(!node->is_weak());
  bool is_unmodified_api_object =
      JSObject::IsUnmodifiedApiObject(node->location());
  if (is_unmodified_api_object) {
    FullObjectSlot slot = node->location();
    START_ALLOW_USE_DEPRECATED();
    const bool is_weak =
        node->is_droppable() ||
        (should_call_is_root_for_default_traced_reference &&
         !handler->IsRoot(
             *reinterpret_cast<v8::TracedReference<v8::Value>*>(&slot)));
    END_ALLOW_USE_DEPRECATED();
    node->set_weak(is_weak);
  }
}
}  // namespace

void TracedHandles::ComputeWeaknessForYoungObjects() {
  if (!v8_flags.reclaim_unmodified_wrappers) return;

  // Treat all objects as roots during incremental marking to avoid corrupting
  // marking worklists.
  DCHECK_IMPLIES(v8_flags.minor_ms, !is_marking_);
  if (is_marking_) return;

  auto* const handler = isolate_->heap()->GetEmbedderRootsHandler();
  if (!handler) return;

  const bool should_call_is_root_for_default_traced_reference =
      handler->default_traced_reference_handling_ ==
      EmbedderRootsHandler::RootHandling::
          kQueryEmbedderForNonDroppableReferences;
  for (auto* block : young_blocks_) {
    DCHECK(block->InYoungList());
    for (auto* node : *block) {
      if (!node->is_in_young_list()) continue;
      DCHECK(node->is_in_use());
      ComputeWeaknessForYoungObject(
          handler, node, should_call_is_root_for_default_traced_reference);
    }
  }
}

void TracedHandles::ProcessYoungObjects(
    RootVisitor* visitor, WeakSlotCallbackWithHeap should_reset_handle) {
  if (!v8_flags.reclaim_unmodified_wrappers) return;

  auto* const handler = isolate_->heap()->GetEmbedderRootsHandler();
  if (!handler) return;

  // ResetRoot should not trigger allocations in CppGC.
  if (auto* cpp_heap = CppHeap::From(isolate_->heap()->cpp_heap())) {
    cpp_heap->EnterDisallowGCScope();
    cpp_heap->EnterNoGCScope();
  }

  for (auto it = young_blocks_.begin(); it != young_blocks_.end();) {
    TracedNodeBlock* block = *it;
    DCHECK(block->InYoungList());

    // Avoid iterator invalidation by incrementing iterator here before
    // ResetRoot().
    it++;

    for (auto* node : *block) {
      if (!node->is_in_young_list()) continue;
      DCHECK(node->is_in_use());

      bool should_reset =
          should_reset_handle(isolate_->heap(), node->location());
      if (should_reset) {
        CHECK(node->is_weak());
        CHECK(!is_marking_);
        FullObjectSlot slot = node->location();
        handler->ResetRoot(
            *reinterpret_cast<v8::TracedReference<v8::Value>*>(&slot));
        // Mark as cleared due to weak semantics.
        node->set_raw_object(kTracedHandleMinorGCWeakResetZapValue);
        CHECK(!node->is_in_use());
      } else {
        if (node->is_weak()) {
          node->set_weak(false);
          if (visitor) {
            visitor->VisitRootPointer(Root::kGlobalHandles, nullptr,
                                      node->location());
          }
        }
      }
    }
  }

  if (auto* cpp_heap = CppHeap::From(isolate_->heap()->cpp_heap())) {
    cpp_heap->LeaveNoGCScope();
    cpp_heap->LeaveDisallowGCScope();
  }
}

void TracedHandles::Iterate(RootVisitor* visitor) {
  for (auto* block : blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_use()) continue;

      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandles::IterateYoung(RootVisitor* visitor) {
  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_young_list()) continue;
      DCHECK(node->is_in_use());
      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandles::IterateYoungRoots(RootVisitor* visitor) {
  for (auto* block : young_blocks_) {
    DCHECK(block->InYoungList());

    for (auto* node : *block) {
      if (!node->is_in_young_list()) continue;
      DCHECK(node->is_in_use());

      CHECK_IMPLIES(is_marking_, !node->is_weak());

      if (node->is_weak()) continue;

      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandles::IterateAndMarkYoungRootsWithOldHosts(RootVisitor* visitor) {
  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_young_list()) continue;
      DCHECK(node->is_in_use());
      if (!node->has_old_host()) continue;

      CHECK_IMPLIES(is_marking_, !node->is_weak());

      if (node->is_weak()) continue;

      node->set_markbit();
      CHECK(ObjectInYoungGeneration(node->object()));
      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandles::IterateYoungRootsWithOldHostsForTesting(
    RootVisitor* visitor) {
  for (auto* block : young_blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_young_list()) continue;
      DCHECK(node->is_in_use());
      if (!node->has_old_host()) continue;

      CHECK_IMPLIES(is_marking_, !node->is_weak());

      if (node->is_weak()) continue;

      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

// static
void TracedHandles::Destroy(Address* location) {
  if (!location) return;

  auto* node = TracedNode::FromLocation(location);
  auto& node_block = TracedNodeBlock::From(*node);
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Destroy(node_block, *node);
}

// static
void TracedHandles::Copy(const Address* const* from, Address** to) {
  DCHECK_NOT_NULL(*from);
  DCHECK_NULL(*to);

  const TracedNode* from_node = TracedNode::FromLocation(*from);
  const auto& node_block = TracedNodeBlock::From(*from_node);
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Copy(*from_node, to);
}

// static
void TracedHandles::Move(Address** from, Address** to) {
  // Fast path for moving from an empty reference.
  if (!*from) {
    Destroy(*to);
    SetSlotThreadSafe(to, nullptr);
    return;
  }

  TracedNode* from_node = TracedNode::FromLocation(*from);
  auto& node_block = TracedNodeBlock::From(*from_node);
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Move(*from_node, from, to);
}

namespace {
Tagged<Object> MarkObject(Tagged<Object> obj, TracedNode& node,
                          TracedHandles::MarkMode mark_mode) {
  if (mark_mode == TracedHandles::MarkMode::kOnlyYoung &&
      !node.is_in_young_list())
    return Smi::zero();
  node.set_markbit();
  // Being in the young list, the node may still point to an old object, in
  // which case we want to keep the node marked, but not follow the reference.
  if (mark_mode == TracedHandles::MarkMode::kOnlyYoung &&
      !ObjectInYoungGeneration(obj))
    return Smi::zero();
  return obj;
}
}  // namespace

// static
Tagged<Object> TracedHandles::Mark(Address* location, MarkMode mark_mode) {
  // The load synchronizes internal bitfields that are also read atomically
  // from the concurrent marker. The counterpart is `TracedNode::Publish()`.
  Tagged<Object> object =
      Tagged<Object>(reinterpret_cast<std::atomic<Address>*>(location)->load(
          std::memory_order_acquire));
  auto* node = TracedNode::FromLocation(location);
  DCHECK(node->is_in_use());
  return MarkObject(object, *node, mark_mode);
}

// static
Tagged<Object> TracedHandles::MarkConservatively(
    Address* inner_location, Address* traced_node_block_base,
    MarkMode mark_mode) {
  // Compute the `TracedNode` address based on its inner pointer.
  const ptrdiff_t delta = reinterpret_cast<uintptr_t>(inner_location) -
                          reinterpret_cast<uintptr_t>(traced_node_block_base);
  const auto index = delta / sizeof(TracedNode);
  TracedNode& node =
      reinterpret_cast<TracedNode*>(traced_node_block_base)[index];
  if (!node.is_in_use()) return Smi::zero();
  return MarkObject(node.object(), node, mark_mode);
}

bool TracedHandles::IsValidInUseNode(Address* location) {
  TracedNode* node = TracedNode::FromLocation(location);
  // This method is called after mark bits have been cleared.
  DCHECK(!node->markbit());
  CHECK_IMPLIES(node->is_in_use(), node->raw_object() != kGlobalHandleZapValue);
  CHECK_IMPLIES(!node->is_in_use(),
                node->raw_object() == kGlobalHandleZapValue);
  return node->is_in_use();
}

bool TracedHandles::HasYoung() const { return !young_blocks_.empty(); }

}  // namespace v8::internal
