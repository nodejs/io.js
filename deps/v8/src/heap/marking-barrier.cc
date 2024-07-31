// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/marking-barrier.h"

#include <memory>

#include "src/base/logging.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-barrier-inl.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/minor-mark-sweep.h"
#include "src/heap/mutable-page.h"
#include "src/heap/safepoint.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-array-buffer.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

MarkingBarrier::MarkingBarrier(LocalHeap* local_heap)
    : heap_(local_heap->heap()),
      major_collector_(heap_->mark_compact_collector()),
      minor_collector_(heap_->minor_mark_sweep_collector()),
      incremental_marking_(heap_->incremental_marking()),
      marking_state_(isolate()),
      is_main_thread_barrier_(local_heap->is_main_thread()),
      uses_shared_heap_(isolate()->has_shared_space()),
      is_shared_space_isolate_(isolate()->is_shared_space_isolate()) {}

MarkingBarrier::~MarkingBarrier() { DCHECK(typed_slots_map_.empty()); }

void MarkingBarrier::Write(Tagged<HeapObject> host, IndirectPointerSlot slot) {
  DCHECK(IsCurrentMarkingBarrier(host));
  DCHECK(is_activated_ || shared_heap_worklist_.has_value());
  DCHECK(MemoryChunk::FromHeapObject(host)->IsMarking());

  // An indirect pointer slot can only contain a Smi if it is uninitialized (in
  // which case the vaue will be Smi::zero()). However, at this point the slot
  // must have been initialized because it was just written to.
  Tagged<HeapObject> value = HeapObject::cast(slot.load(isolate()));
  MarkValue(host, value);

  // We don't emit generational- and shared write barriers for indirect
  // pointers as objects referenced through them are currently never allocated
  // in the young generation or the shared (writable) heap. If this ever
  // changes (in which case, this DCHECK here would fail), we would need to
  // update the code for these barriers and make the remembered sets aware of
  // pointer table entries.
  DCHECK(!Heap::InYoungGeneration(value));
  DCHECK(!InWritableSharedSpace(value));

  // We never need to record indirect pointer slots (i.e. references through a
  // pointer table) since the referenced object owns its table entry and will
  // take care of updating the pointer to itself if it is relocated.
}

void MarkingBarrier::WriteWithoutHost(Tagged<HeapObject> value) {
  DCHECK(is_main_thread_barrier_);
  DCHECK(is_activated_);

  // Without a shared heap and on the shared space isolate (= main isolate) all
  // objects are considered local.
  if (V8_UNLIKELY(uses_shared_heap_) && !is_shared_space_isolate_) {
    // On client isolates (= worker isolates) shared values can be ignored.
    if (InWritableSharedSpace(value)) {
      return;
    }
  }
  if (InReadOnlySpace(value)) return;
  MarkValueLocal(value);
}

void MarkingBarrier::Write(Tagged<InstructionStream> host,
                           RelocInfo* reloc_info, Tagged<HeapObject> value) {
  DCHECK(IsCurrentMarkingBarrier(host));
  DCHECK(!InWritableSharedSpace(host));
  DCHECK(is_activated_ || shared_heap_worklist_.has_value());
  DCHECK(MemoryChunk::FromHeapObject(host)->IsMarking());

  MarkValue(host, value);

  if (is_compacting_) {
    DCHECK(is_major());
    if (is_main_thread_barrier_) {
      // An optimization to avoid allocating additional typed slots for the
      // main thread.
      major_collector_->RecordRelocSlot(host, reloc_info, value);
    } else {
      RecordRelocSlot(host, reloc_info, value);
    }
  }
}

void MarkingBarrier::Write(Tagged<JSArrayBuffer> host,
                           ArrayBufferExtension* extension) {
  DCHECK(IsCurrentMarkingBarrier(host));
  DCHECK(!InWritableSharedSpace(host));
  DCHECK(MemoryChunk::FromHeapObject(host)->IsMarking());

  if (is_minor()) {
    if (Heap::InYoungGeneration(host)) {
      extension->YoungMark();
    }
  } else {
    extension->Mark();
  }
}

void MarkingBarrier::Write(Tagged<DescriptorArray> descriptor_array,
                           int number_of_own_descriptors) {
  DCHECK(IsCurrentMarkingBarrier(descriptor_array));
  DCHECK(IsReadOnlyHeapObject(descriptor_array->map()));
  DCHECK(MemoryChunk::FromHeapObject(descriptor_array)->IsMarking());

  // Only major GC uses custom liveness.
  if (is_minor() || IsStrongDescriptorArray(descriptor_array)) {
    MarkValueLocal(descriptor_array);
    return;
  }

  unsigned gc_epoch;
  MarkingWorklist::Local* worklist;
  if (V8_UNLIKELY(uses_shared_heap_) &&
      InWritableSharedSpace(descriptor_array) && !is_shared_space_isolate_) {
    gc_epoch = isolate()
                   ->shared_space_isolate()
                   ->heap()
                   ->mark_compact_collector()
                   ->epoch();
    DCHECK(shared_heap_worklist_.has_value());
    worklist = &*shared_heap_worklist_;
  } else {
    gc_epoch = major_collector_->epoch();
    worklist = current_worklist_.get();
  }

  // The DescriptorArray needs to be marked black here to ensure that slots
  // are recorded by the Scavenger in case the DescriptorArray is promoted
  // while incremental marking is running. This is needed as the regular
  // marking visitor does not re-process any already marked descriptors. If we
  // don't mark it black here, the Scavenger may promote a DescriptorArray and
  // any already marked descriptors will not have any slots recorded.
  marking_state_.TryMark(descriptor_array);

  // `TryUpdateIndicesToMark()` acts as a barrier that publishes the slots'
  // values corresponding to `number_of_own_descriptors`.
  if (DescriptorArrayMarkingState::TryUpdateIndicesToMark(
          gc_epoch, descriptor_array, number_of_own_descriptors)) {
    worklist->Push(descriptor_array);
  }
}

void MarkingBarrier::RecordRelocSlot(Tagged<InstructionStream> host,
                                     RelocInfo* rinfo,
                                     Tagged<HeapObject> target) {
  DCHECK(IsCurrentMarkingBarrier(host));
  if (!MarkCompactCollector::ShouldRecordRelocSlot(host, rinfo, target)) return;

  MarkCompactCollector::RecordRelocSlotInfo info =
      MarkCompactCollector::ProcessRelocInfo(host, rinfo, target);

  auto& typed_slots = typed_slots_map_[info.page_metadata];
  if (!typed_slots) {
    typed_slots.reset(new TypedSlots());
  }
  typed_slots->Insert(info.slot_type, info.offset);
}

namespace {
void ActivateSpace(PagedSpace* space, MarkingMode marking_mode) {
  for (PageMetadata* p : *space) {
    p->SetOldGenerationPageFlags(marking_mode);
  }
}

void ActivateSpace(NewSpace* space, MarkingMode marking_mode) {
  for (PageMetadata* p : *space) {
    p->SetYoungGenerationPageFlags(marking_mode);
  }
}

void ActivateSpaces(Heap* heap, MarkingMode marking_mode) {
  ActivateSpace(heap->old_space(), marking_mode);
  for (LargePageMetadata* p : *heap->lo_space()) {
    p->SetOldGenerationPageFlags(marking_mode);
  }

  ActivateSpace(heap->new_space(), marking_mode);
  for (LargePageMetadata* p : *heap->new_lo_space()) {
    p->SetYoungGenerationPageFlags(marking_mode);
    DCHECK(p->Chunk()->IsLargePage());
  }

  {
    CodePageHeaderModificationScope rwx_write_scope(
        "Modification of InstructionStream page header flags requires write "
        "access");
    ActivateSpace(heap->code_space(), marking_mode);
    for (LargePageMetadata* p : *heap->code_lo_space()) {
      p->SetOldGenerationPageFlags(marking_mode);
    }
  }

  if (marking_mode == MarkingMode::kMajorMarking) {
    if (heap->shared_space()) {
      ActivateSpace(heap->shared_space(), MarkingMode::kMajorMarking);
    }
    if (heap->shared_lo_space()) {
      for (LargePageMetadata* p : *heap->shared_lo_space()) {
        p->SetOldGenerationPageFlags(MarkingMode::kMajorMarking);
      }
    }
  }

  ActivateSpace(heap->trusted_space(), marking_mode);
  for (LargePageMetadata* p : *heap->trusted_lo_space()) {
    p->SetOldGenerationPageFlags(marking_mode);
  }
}

void DeactivateSpace(PagedSpace* space) {
  for (PageMetadata* p : *space) {
    p->SetOldGenerationPageFlags(MarkingMode::kNoMarking);
  }
}

void DeactivateSpace(NewSpace* space) {
  for (PageMetadata* p : *space) {
    p->SetYoungGenerationPageFlags(MarkingMode::kNoMarking);
  }
}

void DeactivateSpaces(Heap* heap, MarkingMode marking_mode) {
  DeactivateSpace(heap->old_space());
  for (LargePageMetadata* p : *heap->lo_space()) {
    p->SetOldGenerationPageFlags(MarkingMode::kNoMarking);
  }

  DeactivateSpace(heap->new_space());
  for (LargePageMetadata* p : *heap->new_lo_space()) {
    p->SetYoungGenerationPageFlags(MarkingMode::kNoMarking);
    DCHECK(p->Chunk()->IsLargePage());
  }

  {
    CodePageHeaderModificationScope rwx_write_scope(
        "Modification of InstructionStream page header flags requires write "
        "access");
    DeactivateSpace(heap->code_space());
    for (LargePageMetadata* p : *heap->code_lo_space()) {
      p->SetOldGenerationPageFlags(MarkingMode::kNoMarking);
    }
  }

  if (marking_mode == MarkingMode::kMajorMarking) {
    if (heap->shared_space()) {
      DeactivateSpace(heap->shared_space());
    }
    if (heap->shared_lo_space()) {
      for (LargePageMetadata* p : *heap->shared_lo_space()) {
        p->SetOldGenerationPageFlags(MarkingMode::kNoMarking);
      }
    }
  }

  DeactivateSpace(heap->trusted_space());
  for (LargePageMetadata* p : *heap->trusted_lo_space()) {
    p->SetOldGenerationPageFlags(MarkingMode::kNoMarking);
  }
}
}  // namespace

// static
void MarkingBarrier::ActivateAll(Heap* heap, bool is_compacting) {
  ActivateSpaces(heap, MarkingMode::kMajorMarking);

  heap->safepoint()->IterateLocalHeaps([is_compacting](LocalHeap* local_heap) {
    local_heap->marking_barrier()->Activate(is_compacting,
                                            MarkingMode::kMajorMarking);
  });

  if (heap->isolate()->is_shared_space_isolate()) {
    heap->isolate()
        ->shared_space_isolate()
        ->global_safepoint()
        ->IterateClientIsolates([](Isolate* client) {
          // Force the RecordWrite builtin into the incremental marking code
          // path.
          client->heap()->SetIsMarkingFlag(true);
          client->heap()->safepoint()->IterateLocalHeaps(
              [](LocalHeap* local_heap) {
                local_heap->marking_barrier()->ActivateShared();
              });
        });
  }
}

// static
void MarkingBarrier::ActivateYoung(Heap* heap) {
  ActivateSpaces(heap, MarkingMode::kMinorMarking);

  heap->safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->marking_barrier()->Activate(false, MarkingMode::kMinorMarking);
  });
}

void MarkingBarrier::Activate(bool is_compacting, MarkingMode marking_mode) {
  DCHECK(!is_activated_);
  is_compacting_ = is_compacting;
  marking_mode_ = marking_mode;
  current_worklist_ = std::make_unique<MarkingWorklist::Local>(
      is_minor() ? *minor_collector_->marking_worklists()->shared()
                 : *major_collector_->marking_worklists()->shared());
  is_activated_ = true;
}

void MarkingBarrier::ActivateShared() {
  DCHECK(!shared_heap_worklist_.has_value());
  Isolate* shared_isolate = isolate()->shared_space_isolate();
  shared_heap_worklist_.emplace(*shared_isolate->heap()
                                     ->mark_compact_collector()
                                     ->marking_worklists()
                                     ->shared());
}

// static
void MarkingBarrier::DeactivateAll(Heap* heap) {
  DeactivateSpaces(heap, MarkingMode::kMajorMarking);

  heap->safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->marking_barrier()->Deactivate();
  });

  if (heap->isolate()->is_shared_space_isolate()) {
    heap->isolate()
        ->shared_space_isolate()
        ->global_safepoint()
        ->IterateClientIsolates([](Isolate* client) {
          // We can't just simply disable the marking barrier for all clients. A
          // client may still need it to be set for incremental marking in the
          // local heap.
          const bool is_marking =
              client->heap()->incremental_marking()->IsMarking();
          client->heap()->SetIsMarkingFlag(is_marking);
          client->heap()->safepoint()->IterateLocalHeaps(
              [](LocalHeap* local_heap) {
                local_heap->marking_barrier()->DeactivateShared();
              });
        });
  }
}

// static
void MarkingBarrier::DeactivateYoung(Heap* heap) {
  DeactivateSpaces(heap, MarkingMode::kMinorMarking);

  heap->safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->marking_barrier()->Deactivate();
  });
}

void MarkingBarrier::Deactivate() {
  DCHECK(is_activated_);
  is_activated_ = false;
  is_compacting_ = false;
  marking_mode_ = MarkingMode::kNoMarking;
  DCHECK(typed_slots_map_.empty());
  DCHECK(current_worklist_->IsLocalEmpty());
  current_worklist_.reset();
}

void MarkingBarrier::DeactivateShared() {
  DCHECK(shared_heap_worklist_->IsLocalAndGlobalEmpty());
  shared_heap_worklist_.reset();
}

// static
void MarkingBarrier::PublishAll(Heap* heap) {
  heap->safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->marking_barrier()->PublishIfNeeded();
  });

  if (heap->isolate()->is_shared_space_isolate()) {
    heap->isolate()
        ->shared_space_isolate()
        ->global_safepoint()
        ->IterateClientIsolates([](Isolate* client) {
          client->heap()->safepoint()->IterateLocalHeaps(
              [](LocalHeap* local_heap) {
                local_heap->marking_barrier()->PublishSharedIfNeeded();
              });
        });
  }
}

// static
void MarkingBarrier::PublishYoung(Heap* heap) {
  heap->safepoint()->IterateLocalHeaps([](LocalHeap* local_heap) {
    local_heap->marking_barrier()->PublishIfNeeded();
  });
}

void MarkingBarrier::PublishIfNeeded() {
  if (is_activated_) {
    current_worklist_->Publish();
    for (auto& it : typed_slots_map_) {
      MutablePageMetadata* memory_chunk = it.first;
      // Access to TypeSlots need to be protected, since LocalHeaps might
      // publish code in the background thread.
      base::MutexGuard guard(memory_chunk->mutex());
      std::unique_ptr<TypedSlots>& typed_slots = it.second;
      RememberedSet<OLD_TO_OLD>::MergeTyped(memory_chunk,
                                            std::move(typed_slots));
    }
    typed_slots_map_.clear();
  }
}

void MarkingBarrier::PublishSharedIfNeeded() {
  if (shared_heap_worklist_) {
    shared_heap_worklist_->Publish();
  }
}

bool MarkingBarrier::IsCurrentMarkingBarrier(
    Tagged<HeapObject> verification_candidate) {
  return WriteBarrier::CurrentMarkingBarrier(verification_candidate) == this;
}

Isolate* MarkingBarrier::isolate() const { return heap_->isolate(); }

#if DEBUG
void MarkingBarrier::AssertMarkingIsActivated() const { DCHECK(is_activated_); }

void MarkingBarrier::AssertSharedMarkingIsActivated() const {
  DCHECK(shared_heap_worklist_.has_value());
}
#endif  // DEBUG

}  // namespace internal
}  // namespace v8
