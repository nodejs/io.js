// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/scavenger.h"

#include <algorithm>
#include <atomic>
#include <optional>
#include <unordered_map>

#include "src/base/utils/random-number-generator.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles.h"
#include "src/heap/array-buffer-sweeper.h"
#include "src/heap/concurrent-marking.h"
#include "src/heap/conservative-stack-visitor-inl.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/heap-layout.h"
#include "src/heap/heap-visitor-inl.h"
#include "src/heap/heap.h"
#include "src/heap/large-page-metadata-inl.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/mark-compact.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk.h"
#include "src/heap/mutable-page-metadata-inl.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/page-metadata.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/remembered-set-inl.h"
#include "src/heap/scavenger-inl.h"
#include "src/heap/slot-set.h"
#include "src/heap/sweeper.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/objects-body-descriptors-inl.h"
#include "src/objects/objects.h"
#include "src/objects/slots.h"
#include "src/objects/transitions-inl.h"
#include "src/utils/utils-inl.h"

namespace v8 {
namespace internal {

class IterateAndScavengePromotedObjectsVisitor final
    : public HeapVisitor<IterateAndScavengePromotedObjectsVisitor> {
 public:
  IterateAndScavengePromotedObjectsVisitor(Scavenger* scavenger,
                                           bool record_slots)
      : HeapVisitor(scavenger->heap()->isolate()),
        scavenger_(scavenger),
        record_slots_(record_slots) {}

  V8_INLINE static constexpr bool ShouldUseUncheckedCast() { return true; }

  V8_INLINE static constexpr bool UsePrecomputedObjectSize() { return true; }

  V8_INLINE void VisitMapPointer(Tagged<HeapObject> host) final {
    if (!record_slots_) return;
    MapWord map_word = host->map_word(kRelaxedLoad);
    if (map_word.IsForwardingAddress()) {
      // Surviving new large objects and pinned objects have forwarding pointers
      // in the map word.
      DCHECK(MemoryChunk::FromHeapObject(host)->InNewLargeObjectSpace() ||
             v8_flags.scavenger_pinning_objects);
      return;
    }
    HandleSlot(host, HeapObjectSlot(host->map_slot()), map_word.ToMap());
  }

  V8_INLINE void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                               ObjectSlot end) final {
    VisitPointersImpl(host, start, end);
  }

  V8_INLINE void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                               MaybeObjectSlot end) final {
    VisitPointersImpl(host, start, end);
  }

  inline void VisitEphemeron(Tagged<HeapObject> obj, int entry, ObjectSlot key,
                             ObjectSlot value) override {
    DCHECK(HeapLayout::IsSelfForwarded(obj) || IsEphemeronHashTable(obj));
    VisitPointer(obj, value);

    if (HeapLayout::InYoungGeneration(*key)) {
      // We cannot check the map here, as it might be a large object.
      scavenger_->RememberPromotedEphemeron(
          UncheckedCast<EphemeronHashTable>(obj), entry);
    } else {
      VisitPointer(obj, key);
    }
  }

  void VisitExternalPointer(Tagged<HeapObject> host,
                            ExternalPointerSlot slot) override {
#ifdef V8_COMPRESS_POINTERS
    DCHECK(!slot.tag_range().IsEmpty());
    DCHECK(!IsSharedExternalPointerType(slot.tag_range()));
    // TODO(chromium:337580006): Remove when pointer compression always uses
    // EPT.
    if (!slot.HasExternalPointerHandle()) return;
    ExternalPointerHandle handle = slot.Relaxed_LoadHandle();
    Heap* heap = scavenger_->heap();
    ExternalPointerTable& table = heap->isolate()->external_pointer_table();

    // For survivor objects, the scavenger marks their EPT entries when they are
    // copied and then sweeps the young EPT space at the end of collection,
    // reclaiming unmarked EPT entries.  (Exception: if an incremental mark is
    // in progress, the scavenger neither marks nor sweeps, as it will be the
    // major GC's responsibility.)
    //
    // However when promoting, we just evacuate the entry from new to old space.
    // Usually the entry will be unmarked, unless an incremental mark is in
    // progress, or the slot was initialized since the last GC (external pointer
    // tags have the mark bit set), in which case it may be marked already.  In
    // any case, transfer the color from new to old EPT space.
    table.Evacuate(heap->young_external_pointer_space(),
                   heap->old_external_pointer_space(), handle, slot.address(),
                   ExternalPointerTable::EvacuateMarkMode::kTransferMark);
#endif  // V8_COMPRESS_POINTERS
  }

  // Special cases: Unreachable visitors for objects that are never found in the
  // young generation and thus cannot be found when iterating promoted objects.
  void VisitInstructionStreamPointer(Tagged<Code>,
                                     InstructionStreamSlot) final {
    UNREACHABLE();
  }
  void VisitCodeTarget(Tagged<InstructionStream>, RelocInfo*) final {
    UNREACHABLE();
  }
  void VisitEmbeddedPointer(Tagged<InstructionStream>, RelocInfo*) final {
    UNREACHABLE();
  }

 private:
  template <typename TSlot>
  V8_INLINE void VisitPointersImpl(Tagged<HeapObject> host, TSlot start,
                                   TSlot end) {
    using THeapObjectSlot = typename TSlot::THeapObjectSlot;
    // Treat weak references as strong.
    // TODO(marja): Proper weakness handling in the young generation.
    for (TSlot slot = start; slot < end; ++slot) {
      typename TSlot::TObject object = *slot;
      Tagged<HeapObject> heap_object;
      if (object.GetHeapObject(&heap_object)) {
        HandleSlot(host, THeapObjectSlot(slot), heap_object);
      }
    }
  }

  template <typename THeapObjectSlot>
  V8_INLINE void HandleSlot(Tagged<HeapObject> host, THeapObjectSlot slot,
                            Tagged<HeapObject> target) {
    static_assert(
        std::is_same_v<THeapObjectSlot, FullHeapObjectSlot> ||
            std::is_same_v<THeapObjectSlot, HeapObjectSlot>,
        "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
    scavenger_->SynchronizePageAccess(target);

    if (Heap::InFromPage(target)) {
      SlotCallbackResult result = scavenger_->ScavengeObject(slot, target);
      bool success = (*slot).GetHeapObject(&target);
      USE(success);
      DCHECK(success);

      if (result == KEEP_SLOT) {
        SLOW_DCHECK(IsHeapObject(target));
        MemoryChunk* chunk = MemoryChunk::FromHeapObject(host);
        MutablePageMetadata* page =
            MutablePageMetadata::cast(chunk->Metadata());

        // Sweeper is stopped during scavenge, so we can directly
        // insert into its remembered set here.
        RememberedSet<OLD_TO_NEW>::Insert<AccessMode::ATOMIC>(
            page, chunk->Offset(slot.address()));
      }
      DCHECK(!MarkCompactCollector::IsOnEvacuationCandidate(target));
    } else if (record_slots_ &&
               MarkCompactCollector::IsOnEvacuationCandidate(target)) {
      // We should never try to record off-heap slots.
      DCHECK((std::is_same<THeapObjectSlot, HeapObjectSlot>::value));
      // InstructionStream slots never appear in new space because
      // Code objects, the only object that can contain code pointers, are
      // always allocated in the old space.
      DCHECK_IMPLIES(V8_EXTERNAL_CODE_SPACE_BOOL,
                     !MemoryChunk::FromHeapObject(target)->IsFlagSet(
                         MemoryChunk::IS_EXECUTABLE));

      // We cannot call MarkCompactCollector::RecordSlot because that checks
      // that the host page is not in young generation, which does not hold
      // for pending large pages.
      MemoryChunk* chunk = MemoryChunk::FromHeapObject(host);
      MutablePageMetadata* page = MutablePageMetadata::cast(chunk->Metadata());
      RememberedSet<OLD_TO_OLD>::Insert<AccessMode::ATOMIC>(
          page, chunk->Offset(slot.address()));
    }

    if (HeapLayout::InWritableSharedSpace(target)) {
      MemoryChunk* chunk = MemoryChunk::FromHeapObject(host);
      MutablePageMetadata* page = MutablePageMetadata::cast(chunk->Metadata());
      RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(
          page, chunk->Offset(slot.address()));
    }
  }

  Scavenger* const scavenger_;
  const bool record_slots_;
};

namespace {

V8_INLINE bool IsUnscavengedHeapObject(Heap* heap, Tagged<Object> object) {
  return Heap::InFromPage(object) && !Cast<HeapObject>(object)
                                          ->map_word(kRelaxedLoad)
                                          .IsForwardingAddress();
}

// Same as IsUnscavengedHeapObject() above but specialized for HeapObjects.
V8_INLINE bool IsUnscavengedHeapObject(Heap* heap,
                                       Tagged<HeapObject> heap_object) {
  return Heap::InFromPage(heap_object) &&
         !heap_object->map_word(kRelaxedLoad).IsForwardingAddress();
}

bool IsUnscavengedHeapObjectSlot(Heap* heap, FullObjectSlot p) {
  return IsUnscavengedHeapObject(heap, *p);
}

}  // namespace

ScavengerCollector::JobTask::JobTask(
    ScavengerCollector* collector,
    std::vector<std::unique_ptr<Scavenger>>* scavengers,
    std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
        old_to_new_chunks,
    const Scavenger::CopiedList& copied_list,
    const Scavenger::PinnedList& pinned_list,
    const Scavenger::PromotedList& promoted_list)
    : collector_(collector),
      scavengers_(scavengers),
      old_to_new_chunks_(std::move(old_to_new_chunks)),
      remaining_memory_chunks_(old_to_new_chunks_.size()),
      generator_(old_to_new_chunks_.size()),
      copied_list_(copied_list),
      pinned_list_(pinned_list),
      promoted_list_(promoted_list),
      trace_id_(reinterpret_cast<uint64_t>(this) ^
                collector_->heap_->tracer()->CurrentEpoch(
                    GCTracer::Scope::SCAVENGER)) {}

void ScavengerCollector::JobTask::Run(JobDelegate* delegate) {
  DCHECK_LT(delegate->GetTaskId(), scavengers_->size());
  // In case multi-cage pointer compression mode is enabled ensure that
  // current thread's cage base values are properly initialized.
  PtrComprCageAccessScope ptr_compr_cage_access_scope(
      collector_->heap_->isolate());

  collector_->estimate_concurrency_.fetch_add(1, std::memory_order_relaxed);

  Scavenger* scavenger = (*scavengers_)[delegate->GetTaskId()].get();
  if (delegate->IsJoiningThread()) {
    TRACE_GC_WITH_FLOW(collector_->heap_->tracer(),
                       GCTracer::Scope::SCAVENGER_SCAVENGE_PARALLEL, trace_id_,
                       TRACE_EVENT_FLAG_FLOW_IN);
    ProcessItems(delegate, scavenger);
  } else {
    TRACE_GC_EPOCH_WITH_FLOW(
        collector_->heap_->tracer(),
        GCTracer::Scope::SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL,
        ThreadKind::kBackground, trace_id_, TRACE_EVENT_FLAG_FLOW_IN);
    ProcessItems(delegate, scavenger);
  }
}

size_t ScavengerCollector::JobTask::GetMaxConcurrency(
    size_t worker_count) const {
  // We need to account for local segments held by worker_count in addition to
  // GlobalPoolSize() of copied_list_, pinned_list_ and promoted_list_.
  size_t wanted_num_workers =
      std::max<size_t>(remaining_memory_chunks_.load(std::memory_order_relaxed),
                       worker_count + copied_list_.Size() +
                           pinned_list_.Size() + promoted_list_.Size());
  if (!collector_->heap_->ShouldUseBackgroundThreads() ||
      collector_->heap_->ShouldOptimizeForBattery()) {
    return std::min<size_t>(wanted_num_workers, 1);
  }
  return std::min<size_t>(scavengers_->size(), wanted_num_workers);
}

void ScavengerCollector::JobTask::ProcessItems(JobDelegate* delegate,
                                               Scavenger* scavenger) {
  double scavenging_time = 0.0;
  {
    TimedScope scope(&scavenging_time);
    scavenger->VisitPinnedObjects();
    ConcurrentScavengePages(scavenger);
    scavenger->Process(delegate);
  }
  if (V8_UNLIKELY(v8_flags.trace_parallel_scavenge)) {
    PrintIsolate(collector_->heap_->isolate(),
                 "scavenge[%p]: time=%.2f copied=%zu promoted=%zu\n",
                 static_cast<void*>(this), scavenging_time,
                 scavenger->bytes_copied(), scavenger->bytes_promoted());
  }
}

void ScavengerCollector::JobTask::ConcurrentScavengePages(
    Scavenger* scavenger) {
  while (remaining_memory_chunks_.load(std::memory_order_relaxed) > 0) {
    std::optional<size_t> index = generator_.GetNext();
    if (!index) {
      return;
    }
    for (size_t i = *index; i < old_to_new_chunks_.size(); ++i) {
      auto& work_item = old_to_new_chunks_[i];
      if (!work_item.first.TryAcquire()) {
        break;
      }
      scavenger->ScavengePage(work_item.second);
      if (remaining_memory_chunks_.fetch_sub(1, std::memory_order_relaxed) <=
          1) {
        return;
      }
    }
  }
}

ScavengerCollector::ScavengerCollector(Heap* heap)
    : isolate_(heap->isolate()), heap_(heap) {}

namespace {

// Helper class for updating weak global handles. There's no additional scavenge
// processing required here as this phase runs after actual scavenge.
class GlobalHandlesWeakRootsUpdatingVisitor final : public RootVisitor {
 public:
  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) final {
    UpdatePointer(p);
  }
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) final {
    for (FullObjectSlot p = start; p < end; ++p) {
      UpdatePointer(p);
    }
  }

 private:
  void UpdatePointer(FullObjectSlot p) {
    Tagged<Object> object = *p;
    DCHECK(!HasWeakHeapObjectTag(object));
    // The object may be in the old generation as global handles over
    // approximates the list of young nodes. This checks also bails out for
    // Smis.
    if (!HeapLayout::InYoungGeneration(object)) {
      return;
    }

    Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
    // TODO(chromium:1336158): Turn the following CHECKs into DCHECKs after
    // flushing out potential issues.
    CHECK(Heap::InFromPage(heap_object));
    MapWord first_word = heap_object->map_word(kRelaxedLoad);
    CHECK(first_word.IsForwardingAddress());
    Tagged<HeapObject> dest = first_word.ToForwardingAddress(heap_object);
    if (heap_object == dest) {
      DCHECK(Heap::IsLargeObject(heap_object) ||
             MemoryChunk::FromHeapObject(heap_object)->IsQuarantined());
      return;
    }
    UpdateHeapObjectReferenceSlot(FullHeapObjectSlot(p), dest);
    DCHECK_IMPLIES(HeapLayout::InYoungGeneration(dest), Heap::InToPage(dest));
  }
};

}  // namespace

// Remove this crashkey after chromium:1010312 is fixed.
class V8_NODISCARD ScopedFullHeapCrashKey {
 public:
  explicit ScopedFullHeapCrashKey(Isolate* isolate) : isolate_(isolate) {
    isolate_->AddCrashKey(v8::CrashKeyId::kDumpType, "heap");
  }
  ~ScopedFullHeapCrashKey() {
    isolate_->AddCrashKey(v8::CrashKeyId::kDumpType, "");
  }

 private:
  Isolate* isolate_ = nullptr;
};

namespace {

// A conservative stack scanning visitor implementation that:
// 1) Filters out non-young objects, and
// 2) Use the marking bitmap as a temporary object start bitmap.
class YoungGenerationConservativeStackVisitor
    : public ConservativeStackVisitorBase<
          YoungGenerationConservativeStackVisitor> {
 public:
  YoungGenerationConservativeStackVisitor(Isolate* isolate,
                                          RootVisitor* root_visitor)
      : ConservativeStackVisitorBase(isolate, root_visitor), isolate_(isolate) {
    DCHECK(v8_flags.scavenger_pinning_objects);
    DCHECK(!v8_flags.minor_ms);
    DCHECK(!v8_flags.sticky_mark_bits);
    DCHECK(std::all_of(
        isolate_->heap()->semi_space_new_space()->to_space().begin(),
        isolate_->heap()->semi_space_new_space()->to_space().end(),
        [](const PageMetadata* page) {
          return page->marking_bitmap()->IsClean();
        }));
    DCHECK(std::all_of(
        isolate_->heap()->semi_space_new_space()->from_space().begin(),
        isolate_->heap()->semi_space_new_space()->from_space().end(),
        [](const PageMetadata* page) {
          return page->marking_bitmap()->IsClean();
        }));
  }

  ~YoungGenerationConservativeStackVisitor() {
    DCHECK(std::all_of(
        isolate_->heap()->semi_space_new_space()->to_space().begin(),
        isolate_->heap()->semi_space_new_space()->to_space().end(),
        [](const PageMetadata* page) {
          return page->marking_bitmap()->IsClean();
        }));
    for (PageMetadata* page :
         isolate_->heap()->semi_space_new_space()->from_space()) {
      page->marking_bitmap()->Clear<AccessMode::NON_ATOMIC>();
    }
  }

 private:
  static constexpr bool kOnlyVisitMainV8Cage [[maybe_unused]] = true;

  static bool FilterPage(const MemoryChunk* chunk) {
    return chunk->IsFromPage();
  }

  static bool FilterLargeObject(Tagged<HeapObject> object, MapWord map_word) {
    DCHECK_EQ(map_word, object->map_word(kRelaxedLoad));
    return HeapLayout::IsSelfForwarded(object, map_word);
  }

  static bool FilterNormalObject(Tagged<HeapObject> object, MapWord map_word,
                                 MarkingBitmap* bitmap) {
    DCHECK_EQ(map_word, object->map_word(kRelaxedLoad));
    if (map_word.IsForwardingAddress()) {
      DCHECK(HeapLayout::IsSelfForwarded(object));
      DCHECK(
          MarkingBitmap::MarkBitFromAddress(bitmap, object->address()).Get());
      return false;
    }
    MarkingBitmap::MarkBitFromAddress(bitmap, object->address())
        .Set<AccessMode::NON_ATOMIC>();
    return true;
  }

  static void HandleObjectFound(Tagged<HeapObject> object, size_t object_size,
                                MarkingBitmap* bitmap) {
    DCHECK_EQ(object_size, object->Size());
    Address object_address = object->address();
    if (object_address + object_size <
        PageMetadata::FromHeapObject(object)->area_end()) {
      MarkingBitmap::MarkBitFromAddress(bitmap, object_address + object_size)
          .Set<AccessMode::NON_ATOMIC>();
    }
  }

  Isolate* const isolate_;

  friend class ConservativeStackVisitorBase<
      YoungGenerationConservativeStackVisitor>;
};

using PinnedObjects = std::vector<std::pair<Address, MapWord>>;

template <typename ConcreteVisitor>
class ObjectPinningVisitorBase : public RootVisitor {
 public:
  ObjectPinningVisitorBase(Scavenger& scavenger, PinnedObjects& pinned_objects)
      : RootVisitor(), scavenger_(scavenger), pinned_objects_(pinned_objects) {}

  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) final {
    static_cast<ConcreteVisitor*>(this)->HandlePointer(p);
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) final {
    for (FullObjectSlot p = start; p < end; ++p) {
      static_cast<ConcreteVisitor*>(this)->HandlePointer(p);
    }
  }

 protected:
  void HandleHeapObject(Tagged<HeapObject> object) {
    DCHECK(!HasWeakHeapObjectTag(object));
    DCHECK(!MapWord::IsPacked(object.ptr()));
    DCHECK(!HeapLayout::IsSelfForwarded(object));
    if (scavenger_.PromoteIfLargeObject(object)) {
      // Large objects are not moved and thus don't require pinning. Instead,
      // we scavenge large pages eagerly to keep them from being reclaimed (if
      // the page is only reachable from stack).
      return;
    }
    DCHECK(!MemoryChunk::FromHeapObject(object)->IsLargePage());
    DCHECK(HeapLayout::InYoungGeneration(object));
    DCHECK(Heap::InFromPage(object));
    Address object_address = object.address();
    MapWord map_word = object->map_word(kRelaxedLoad);
    DCHECK(!map_word.IsForwardingAddress());
    DCHECK(std::all_of(
        pinned_objects_.begin(), pinned_objects_.end(),
        [object_address](std::pair<Address, MapWord>& object_and_map) {
          return object_and_map.first != object_address;
        }));
    pinned_objects_.push_back({object_address, map_word});
    // Pin the object in place.
    object->set_map_word_forwarded(object, kRelaxedStore);
    DCHECK(object->map_word(kRelaxedLoad).IsForwardingAddress());
    DCHECK(HeapLayout::IsSelfForwarded(object));
    MemoryChunk::FromHeapObject(object)->SetFlagNonExecutable(
        MemoryChunk::IS_QUARANTINED);
    scavenger_.PushPinnedObject(object, map_word.ToMap());
  }

 private:
  Scavenger& scavenger_;
  PinnedObjects& pinned_objects_;
};

class ConservativeObjectPinningVisitor final
    : public ObjectPinningVisitorBase<ConservativeObjectPinningVisitor> {
 public:
  ConservativeObjectPinningVisitor(Scavenger& scavenger,
                                   PinnedObjects& pinned_objects)
      : ObjectPinningVisitorBase<ConservativeObjectPinningVisitor>(
            scavenger, pinned_objects) {}

 private:
  void HandlePointer(FullObjectSlot p) {
    HandleHeapObject(Cast<HeapObject>(*p));
  }

  friend class ObjectPinningVisitorBase<ConservativeObjectPinningVisitor>;
};

class PreciseObjectPinningVisitor final
    : public ObjectPinningVisitorBase<PreciseObjectPinningVisitor> {
 public:
  PreciseObjectPinningVisitor(Scavenger& scavenger,
                              PinnedObjects& pinned_objects)
      : ObjectPinningVisitorBase<PreciseObjectPinningVisitor>(scavenger,
                                                              pinned_objects) {}

 private:
  void HandlePointer(FullObjectSlot p) {
    Tagged<Object> object = *p;
    if (!object.IsHeapObject()) {
      return;
    }
    Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
    if (!MemoryChunk::FromHeapObject(heap_object)->IsFromPage()) {
      return;
    }
    if (HeapLayout::IsSelfForwarded(heap_object)) {
      return;
    }
    HandleHeapObject(heap_object);
  }

  friend class ObjectPinningVisitorBase<PreciseObjectPinningVisitor>;
};

// A visitor for treating precise references conservatively (by passing them to
// the conservative stack visitor). This visitor is used for streesing object
// pinning in Scavenger.
class TreatConservativelyVisitor final : public RootVisitor {
 public:
  TreatConservativelyVisitor(YoungGenerationConservativeStackVisitor* v,
                             Heap* heap)
      : RootVisitor(),
        stack_visitor_(v),
        rng_(heap->isolate()->fuzzer_rng()),
        stressing_threshold_(v8_flags.stress_scavenger_pinning_objects_random
                                 ? rng_->NextDouble()
                                 : 0) {}

  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) final {
    HandlePointer(p);
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) final {
    for (FullObjectSlot p = start; p < end; ++p) {
      HandlePointer(p);
    }
  }

 private:
  void HandlePointer(FullObjectSlot p) {
    if (rng_->NextDouble() < stressing_threshold_) {
      return;
    }
    Tagged<Object> object = *p;
    stack_visitor_->VisitPointer(reinterpret_cast<void*>(object.ptr()));
  }

  YoungGenerationConservativeStackVisitor* const stack_visitor_;
  base::RandomNumberGenerator* const rng_;
  double stressing_threshold_;
};
template <typename FreeSpaceHandler>
size_t SweepQuarantinedPage(
    FreeSpaceHandler& free_space_handler, MemoryChunk* chunk,
    std::vector<std::pair<Address, size_t>>& pinned_objects_and_sizes) {
  MemoryChunkMetadata* metadata = chunk->Metadata();
  Address start = metadata->area_start();
  std::sort(pinned_objects_and_sizes.begin(), pinned_objects_and_sizes.end());
  size_t quarantined_objects_size = 0;
  for (const auto& [object, size] : pinned_objects_and_sizes) {
    DCHECK_LE(start, object);
    if (start != object) {
      free_space_handler(start, object - start);
    }
    quarantined_objects_size += size;
    start = object + size;
  }
  Address end = metadata->area_end();
  if (start != end) {
    free_space_handler(start, end - start);
  }
  DCHECK(
      static_cast<MutablePageMetadata*>(metadata)->marking_bitmap()->IsClean());
  DCHECK_LT(0, quarantined_objects_size);
  return quarantined_objects_size;
}

void RestoreAndQuarantinePinnedObjects(SemiSpaceNewSpace& new_space,
                                       const PinnedObjects& pinned_objects) {
  std::unordered_map<MemoryChunk*, std::vector<std::pair<Address, size_t>>,
                     base::hash<const MemoryChunk*>>
      pages_with_pinned_objects;
  // Restore the maps of quarantined objects. We use the iteration over
  // quarantined objects to split them based on pages. This will be used below
  // for sweeping the quarantined pages (since there are no markbits).
  for (const auto& [object_address, map_word] : pinned_objects) {
    DCHECK(!map_word.IsForwardingAddress());
    Tagged<HeapObject> object = HeapObject::FromAddress(object_address);
    DCHECK(HeapLayout::IsSelfForwarded(object));
    object->set_map_word(map_word.ToMap(), kRelaxedStore);
    const size_t object_size = object->SizeFromMap(map_word.ToMap());
    pages_with_pinned_objects[MemoryChunk::FromHeapObject(
                                  Cast<HeapObject>(object))]
        .emplace_back(object_address, object_size);
  }
  DCHECK_EQ(0, new_space.QuarantinedPageCount());
  // Sweep quarantined pages to make them iterable.
  Heap* const heap = new_space.heap();
  auto create_filler = [heap](Address address, size_t size) {
    if (heap::ShouldZapGarbage()) {
      heap::ZapBlock(address, size, heap::ZapValue());
    }
    heap->CreateFillerObjectAt(address, static_cast<int>(size));
  };
  auto create_filler_and_add_to_freelist = [heap, create_filler](
                                               Address address, size_t size) {
    create_filler(address, size);
    PageMetadata* page = PageMetadata::FromAddress(address);
    DCHECK_EQ(OLD_SPACE, page->owner()->identity());
    DCHECK(page->SweepingDone());
    OldSpace* const old_space = heap->old_space();
    FreeList* const free_list = old_space->free_list();
    const size_t wasted = free_list->Free(
        WritableFreeSpace::ForNonExecutableMemory(address, size),
        kLinkCategory);
    old_space->DecreaseAllocatedBytes(size, page);
    free_list->increase_wasted_bytes(wasted);
  };
  size_t quarantined_objects_size = 0;
  for (auto it : pages_with_pinned_objects) {
    MemoryChunk* chunk = it.first;
    std::vector<std::pair<Address, size_t>>& pinned_objects_and_sizes =
        it.second;
    DCHECK(chunk->IsFromPage());
    if (new_space.ShouldPageBePromoted(chunk->address())) {
      new_space.PromotePageToOldSpace(
          static_cast<PageMetadata*>(chunk->Metadata()));
      DCHECK(!chunk->InYoungGeneration());
      SweepQuarantinedPage(create_filler_and_add_to_freelist, chunk,
                           pinned_objects_and_sizes);
    } else {
      new_space.MoveQuarantinedPage(chunk);
      DCHECK(!chunk->IsFromPage());
      DCHECK(chunk->IsToPage());
      quarantined_objects_size +=
          SweepQuarantinedPage(create_filler, chunk, pinned_objects_and_sizes);
    }
  }
  new_space.SetQuarantinedSize(quarantined_objects_size);
}

void IterateRootsForPrecisePinning(Heap* heap, RootVisitor* visitor) {
  heap->IterateStackRoots(visitor);
  heap->isolate()->handle_scope_implementer()->Iterate(visitor);
}

}  // namespace

void ScavengerCollector::CollectGarbage() {
  ScopedFullHeapCrashKey collect_full_heap_dump_if_crash(isolate_);

  SemiSpaceNewSpace* new_space = SemiSpaceNewSpace::From(heap_->new_space());
  new_space->GarbageCollectionPrologue();
  new_space->EvacuatePrologue();

  // We also flip the young generation large object space. All large objects
  // will be in the from space.
  heap_->new_lo_space()->Flip();
  heap_->new_lo_space()->ResetPendingObject();

  DCHECK(!heap_->allocator()->new_space_allocator()->IsLabValid());

  DCHECK(surviving_new_large_objects_.empty());

  Scavenger::EmptyChunksList empty_chunks;
  Scavenger::CopiedList copied_list;
  Scavenger::PinnedList pinned_list;
  Scavenger::PromotedList promoted_list;
  EphemeronRememberedSet::TableList ephemeron_table_list;

  PinnedObjects pinned_objects;

  const int num_scavenge_tasks = NumberOfScavengeTasks();
  std::vector<std::unique_ptr<Scavenger>> scavengers;
  {
    const bool is_logging = isolate_->log_object_relocation();
    for (int i = 0; i < num_scavenge_tasks; ++i) {
      scavengers.emplace_back(
          new Scavenger(this, heap_, is_logging, &empty_chunks, &copied_list,
                        &pinned_list, &promoted_list, &ephemeron_table_list));
    }
    Scavenger& main_thread_scavenger = *scavengers[kMainThreadId].get();

    {
      // Identify weak unmodified handles. Requires an unmodified graph.
      TRACE_GC(
          heap_->tracer(),
          GCTracer::Scope::SCAVENGER_SCAVENGE_WEAK_GLOBAL_HANDLES_IDENTIFY);
      isolate_->traced_handles()->ComputeWeaknessForYoungObjects();
    }

    std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
        old_to_new_chunks;
    {
      // Copy roots.
      TRACE_GC(heap_->tracer(), GCTracer::Scope::SCAVENGER_SCAVENGE_ROOTS);

      // We must collect old-to-new pages before starting Scavenge because pages
      // could be removed from the old generation for allocation which hides
      // them from the iteration.
      OldGenerationMemoryChunkIterator::ForAll(
          heap_, [&old_to_new_chunks](MutablePageMetadata* chunk) {
            if (chunk->slot_set<OLD_TO_NEW>() ||
                chunk->typed_slot_set<OLD_TO_NEW>() ||
                chunk->slot_set<OLD_TO_NEW_BACKGROUND>()) {
              old_to_new_chunks.emplace_back(ParallelWorkItem{}, chunk);
            }
          });

      if (v8_flags.scavenger_pinning_objects &&
          heap_->IsGCWithMainThreadStack()) {
        // Pinning objects must be the first step and must happen before
        // scavenging any objects. Specifically we must all pin all objects
        // before visiting other pinned objects. If we scavenge some object X
        // and move it before all stack-reachable objects are pinned, and we
        // later find that we need to pin X, it will be too late to undo the
        // moving.
        TRACE_GC(heap_->tracer(),
                 GCTracer::Scope::SCAVENGER_SCAVENGE_PIN_OBJECTS);
        ConservativeObjectPinningVisitor conservative_pinning_visitor(
            main_thread_scavenger, pinned_objects);
        // Scavenger reuses the page's marking bitmap as a temporary object
        // start bitmap. Stack scanning will incrementally build the map as it
        // searches through pages.
        YoungGenerationConservativeStackVisitor stack_visitor(
            isolate_, &conservative_pinning_visitor);
        // Marker was already set by Heap::CollectGarbage.
        heap_->stack().IteratePointersUntilMarker(&stack_visitor);
        if (V8_UNLIKELY(v8_flags.stress_scavenger_pinning_objects)) {
          TreatConservativelyVisitor handles_visitor(&stack_visitor, heap_);
          IterateRootsForPrecisePinning(heap_, &handles_visitor);
        }
      }
      if (v8_flags.scavenger_precise_pinning_objects) {
        PreciseObjectPinningVisitor precise_pinning_visitor(
            main_thread_scavenger, pinned_objects);
        ClearStaleLeftTrimmedPointerVisitor left_trim_visitor(
            heap_, &precise_pinning_visitor);
        IterateRootsForPrecisePinning(heap_, &left_trim_visitor);
      }

      // Scavenger treats all weak roots except for global handles as strong.
      // That is why we don't set skip_weak = true here and instead visit
      // global handles separately.
      base::EnumSet<SkipRoot> options(
          {SkipRoot::kExternalStringTable, SkipRoot::kGlobalHandles,
           SkipRoot::kTracedHandles, SkipRoot::kOldGeneration,
           SkipRoot::kConservativeStack, SkipRoot::kReadOnlyBuiltins});
      if (v8_flags.scavenger_precise_pinning_objects) {
        options.Add({SkipRoot::kMainThreadHandles, SkipRoot::kStack});
      }
      RootScavengeVisitor root_scavenge_visitor(main_thread_scavenger);

      heap_->IterateRoots(&root_scavenge_visitor, options);
      isolate_->global_handles()->IterateYoungStrongAndDependentRoots(
          &root_scavenge_visitor);
      isolate_->traced_handles()->IterateYoungRoots(&root_scavenge_visitor);
    }
    {
      // Parallel phase scavenging all copied and promoted objects.
      TRACE_GC_ARG1(
          heap_->tracer(), GCTracer::Scope::SCAVENGER_SCAVENGE_PARALLEL_PHASE,
          "UseBackgroundThreads", heap_->ShouldUseBackgroundThreads());

      auto job = std::make_unique<JobTask>(
          this, &scavengers, std::move(old_to_new_chunks), copied_list,
          pinned_list, promoted_list);
      TRACE_GC_NOTE_WITH_FLOW("Parallel scavenge started", job->trace_id(),
                              TRACE_EVENT_FLAG_FLOW_OUT);
      V8::GetCurrentPlatform()
          ->CreateJob(v8::TaskPriority::kUserBlocking, std::move(job))
          ->Join();
      DCHECK(copied_list.IsEmpty());
      DCHECK(pinned_list.IsEmpty());
      DCHECK(promoted_list.IsEmpty());
    }

    {
      // Scavenge weak global handles.
      TRACE_GC(heap_->tracer(),
               GCTracer::Scope::SCAVENGER_SCAVENGE_WEAK_GLOBAL_HANDLES_PROCESS);
      GlobalHandlesWeakRootsUpdatingVisitor visitor;
      isolate_->global_handles()->ProcessWeakYoungObjects(
          &visitor, &IsUnscavengedHeapObjectSlot);
      isolate_->traced_handles()->ProcessYoungObjects(
          &visitor, &IsUnscavengedHeapObjectSlot);
    }

    {
      // Finalize parallel scavenging.
      TRACE_GC(heap_->tracer(), GCTracer::Scope::SCAVENGER_SCAVENGE_FINALIZE);

      DCHECK(surviving_new_large_objects_.empty());

      for (auto& scavenger : scavengers) {
        scavenger->Finalize();
      }
      scavengers.clear();

#ifdef V8_COMPRESS_POINTERS
      // Sweep the external pointer table, unless an incremental mark is in
      // progress, in which case leave sweeping to the end of the
      // already-scheduled major GC cycle.  (If we swept here we'd clear EPT
      // marks that the major marker was using, which would be an error.)
      DCHECK(heap_->concurrent_marking()->IsStopped());
      if (!heap_->incremental_marking()->IsMajorMarking()) {
        heap_->isolate()->external_pointer_table().Sweep(
            heap_->young_external_pointer_space(),
            heap_->isolate()->counters());
      }
#endif  // V8_COMPRESS_POINTERS

      HandleSurvivingNewLargeObjects();

      heap_->tracer()->SampleConcurrencyEsimate(
          FetchAndResetConcurrencyEstimate());
    }
  }

  {
    // Update references into new space
    TRACE_GC(heap_->tracer(), GCTracer::Scope::SCAVENGER_SCAVENGE_UPDATE_REFS);
    heap_->UpdateYoungReferencesInExternalStringTable(
        &Heap::UpdateYoungReferenceInExternalStringTableEntry);

    heap_->incremental_marking()->UpdateMarkingWorklistAfterScavenge();
    heap_->incremental_marking()->UpdateExternalPointerTableAfterScavenge();

    if (V8_UNLIKELY(v8_flags.always_use_string_forwarding_table)) {
      isolate_->string_forwarding_table()->UpdateAfterYoungEvacuation();
    }
  }

  if (v8_flags.concurrent_marking) {
    // Ensure that concurrent marker does not track pages that are
    // going to be unmapped.
    for (PageMetadata* p :
         PageRange(new_space->from_space().first_page(), nullptr)) {
      heap_->concurrent_marking()->ClearMemoryChunkData(p);
    }
  }

  ProcessWeakReferences(&ephemeron_table_list);

  RestoreAndQuarantinePinnedObjects(*new_space, pinned_objects);

  // Need to free new space LAB that was allocated during scavenge.
  heap_->allocator()->new_space_allocator()->FreeLinearAllocationArea();

  // Since we promote all surviving large objects immediately, all remaining
  // large objects must be dead.
  // TODO(hpayer): Don't free all as soon as we have an intermediate generation.
  heap_->new_lo_space()->FreeDeadObjects(
      [](Tagged<HeapObject>) { return true; });

  new_space->GarbageCollectionEpilogue();

  {
    TRACE_GC(heap_->tracer(), GCTracer::Scope::SCAVENGER_FREE_REMEMBERED_SET);
    Scavenger::EmptyChunksList::Local empty_chunks_local(empty_chunks);
    MutablePageMetadata* chunk;
    while (empty_chunks_local.Pop(&chunk)) {
      RememberedSet<OLD_TO_NEW>::CheckPossiblyEmptyBuckets(chunk);
      RememberedSet<OLD_TO_NEW_BACKGROUND>::CheckPossiblyEmptyBuckets(chunk);
    }

#ifdef DEBUG
    OldGenerationMemoryChunkIterator::ForAll(
        heap_, [](MutablePageMetadata* chunk) {
          if (chunk->slot_set<OLD_TO_NEW>() ||
              chunk->typed_slot_set<OLD_TO_NEW>() ||
              chunk->slot_set<OLD_TO_NEW_BACKGROUND>()) {
            DCHECK(chunk->possibly_empty_buckets()->IsEmpty());
          }
        });
#endif
  }

  SweepArrayBufferExtensions();

  isolate_->global_handles()->UpdateListOfYoungNodes();
  isolate_->traced_handles()->UpdateListOfYoungNodes();

  // Update how much has survived scavenge.
  heap_->IncrementYoungSurvivorsCounter(heap_->SurvivedYoungObjectSize());

  const auto resize_mode = heap_->ShouldResizeNewSpace();
  switch (resize_mode) {
    case Heap::ResizeNewSpaceMode::kShrink:
      heap_->ReduceNewSpaceSize();
      break;
    case Heap::ResizeNewSpaceMode::kGrow:
      heap_->ExpandNewSpaceSize();
      break;
    case Heap::ResizeNewSpaceMode::kNone:
      break;
  }
}

void ScavengerCollector::SweepArrayBufferExtensions() {
  DCHECK_EQ(0, heap_->new_lo_space()->Size());
  heap_->array_buffer_sweeper()->RequestSweep(
      ArrayBufferSweeper::SweepingType::kYoung,
      (heap_->new_space()->SizeOfObjects() == 0)
          ? ArrayBufferSweeper::TreatAllYoungAsPromoted::kYes
          : ArrayBufferSweeper::TreatAllYoungAsPromoted::kNo);
}

void ScavengerCollector::HandleSurvivingNewLargeObjects() {
  const bool is_compacting = heap_->incremental_marking()->IsCompacting();
  MarkingState* marking_state = heap_->marking_state();

  for (SurvivingNewLargeObjectMapEntry update_info :
       surviving_new_large_objects_) {
    Tagged<HeapObject> object = update_info.first;
    Tagged<Map> map = update_info.second;
    // Order is important here. We have to re-install the map to have access
    // to meta-data like size during page promotion.
    object->set_map_word(map, kRelaxedStore);

    if (is_compacting && marking_state->IsMarked(object) &&
        MarkCompactCollector::IsOnEvacuationCandidate(map)) {
      MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
      MutablePageMetadata* page = MutablePageMetadata::cast(chunk->Metadata());
      RememberedSet<OLD_TO_OLD>::Insert<AccessMode::ATOMIC>(
          page, chunk->Offset(object->map_slot().address()));
    }
    LargePageMetadata* page = LargePageMetadata::FromHeapObject(object);
    heap_->lo_space()->PromoteNewLargeObject(page);
  }
  surviving_new_large_objects_.clear();
  heap_->new_lo_space()->set_objects_size(0);
}

void ScavengerCollector::MergeSurvivingNewLargeObjects(
    const SurvivingNewLargeObjectsMap& objects) {
  for (SurvivingNewLargeObjectMapEntry object : objects) {
    bool success = surviving_new_large_objects_.insert(object).second;
    USE(success);
    DCHECK(success);
  }
}

int ScavengerCollector::NumberOfScavengeTasks() {
  if (!v8_flags.parallel_scavenge) {
    return 1;
  }
  const int num_scavenge_tasks =
      static_cast<int>(
          SemiSpaceNewSpace::From(heap_->new_space())->TotalCapacity()) /
          MB +
      1;
  static int num_cores = V8::GetCurrentPlatform()->NumberOfWorkerThreads() + 1;
  int tasks = std::max(
      1, std::min({num_scavenge_tasks, kMaxScavengerTasks, num_cores}));
  if (!heap_->CanPromoteYoungAndExpandOldGeneration(
          static_cast<size_t>(tasks * PageMetadata::kPageSize))) {
    // Optimize for memory usage near the heap limit.
    tasks = 1;
  }
  return tasks;
}

Scavenger::Scavenger(ScavengerCollector* collector, Heap* heap, bool is_logging,
                     EmptyChunksList* empty_chunks, CopiedList* copied_list,
                     PinnedList* pinned_list, PromotedList* promoted_list,
                     EphemeronRememberedSet::TableList* ephemeron_table_list)
    : collector_(collector),
      heap_(heap),
      local_empty_chunks_(*empty_chunks),
      local_copied_list_(*copied_list),
      local_pinned_list_(*pinned_list),
      local_promoted_list_(*promoted_list),
      local_ephemeron_table_list_(*ephemeron_table_list),
      local_pretenuring_feedback_(PretenuringHandler::kInitialFeedbackCapacity),
      allocator_(heap, CompactionSpaceKind::kCompactionSpaceForScavenge),
      is_logging_(is_logging),
      is_incremental_marking_(heap->incremental_marking()->IsMarking()),
      is_compacting_(heap->incremental_marking()->IsCompacting()),
      shared_string_table_(v8_flags.shared_string_table &&
                           heap->isolate()->has_shared_space()),
      mark_shared_heap_(heap->isolate()->is_shared_space_isolate()),
      shortcut_strings_(
          heap->CanShortcutStringsDuringGC(GarbageCollector::SCAVENGER)) {
  DCHECK_IMPLIES(is_incremental_marking_,
                 heap->incremental_marking()->IsMajorMarking());
}

void Scavenger::IterateAndScavengePromotedObject(Tagged<HeapObject> target,
                                                 Tagged<Map> map, int size) {
  // We are not collecting slots on new space objects during mutation thus we
  // have to scan for pointers to evacuation candidates when we promote
  // objects. But we should not record any slots in non-black objects. Grey
  // object's slots would be rescanned. White object might not survive until
  // the end of collection it would be a violation of the invariant to record
  // its slots.
  const bool record_slots =
      is_compacting_ && heap()->marking_state()->IsMarked(target);
  DCHECK_IMPLIES(v8_flags.separate_gc_phases, !record_slots);

  IterateAndScavengePromotedObjectsVisitor visitor(this, record_slots);

  // Iterate all outgoing pointers including map word.
  visitor.Visit(map, target, size);

  if (IsJSArrayBufferMap(map)) {
    DCHECK(!MemoryChunk::FromHeapObject(target)->IsLargePage());
    GCSafeCast<JSArrayBuffer>(target, heap_)->YoungMarkExtensionPromoted();
  }
}

void Scavenger::RememberPromotedEphemeron(Tagged<EphemeronHashTable> table,
                                          int index) {
  auto indices = local_ephemeron_remembered_set_.insert(
      {table, std::unordered_set<int>()});
  indices.first->second.insert(index);
}

void Scavenger::ScavengePage(MutablePageMetadata* page) {
  const bool record_old_to_shared_slots = heap_->isolate()->has_shared_space();

  MemoryChunk* chunk = page->Chunk();

  if (page->slot_set<OLD_TO_NEW, AccessMode::ATOMIC>() != nullptr) {
    RememberedSet<OLD_TO_NEW>::IterateAndTrackEmptyBuckets(
        page,
        [this, chunk, page, record_old_to_shared_slots](MaybeObjectSlot slot) {
          SlotCallbackResult result = CheckAndScavengeObject(heap_, slot);
          // A new space string might have been promoted into the shared heap
          // during GC.
          if (result == REMOVE_SLOT && record_old_to_shared_slots) {
            CheckOldToNewSlotForSharedUntyped(chunk, page, slot);
          }
          return result;
        },
        &local_empty_chunks_);
  }

  if (chunk->executable()) {
    std::vector<std::tuple<Tagged<HeapObject>, SlotType, Address>> slot_updates;

    // The code running write access to executable memory poses CFI attack
    // surface and needs to be kept to a minimum. So we do the the iteration in
    // two rounds. First we iterate the slots and scavenge objects and in the
    // second round with write access, we only perform the pointer updates.
    const auto typed_slot_count = RememberedSet<OLD_TO_NEW>::IterateTyped(
        page, [this, chunk, page, record_old_to_shared_slots, &slot_updates](
                  SlotType slot_type, Address slot_address) {
          Tagged<HeapObject> old_target =
              UpdateTypedSlotHelper::GetTargetObject(heap_, slot_type,
                                                     slot_address);
          Tagged<HeapObject> new_target = old_target;
          FullMaybeObjectSlot slot(&new_target);
          SlotCallbackResult result = CheckAndScavengeObject(heap(), slot);
          if (result == REMOVE_SLOT && record_old_to_shared_slots) {
            CheckOldToNewSlotForSharedTyped(chunk, page, slot_type,
                                            slot_address, *slot);
          }
          if (new_target != old_target) {
            slot_updates.emplace_back(new_target, slot_type, slot_address);
          }
          return result;
        });
    // Typed slots only exist in code objects. Since code is never young, it is
    // safe to release an empty typed slot set as no other scavenge thread will
    // attempt to promote to the page and write to the slot set.
    if (typed_slot_count == 0) {
      page->ReleaseTypedSlotSet(OLD_TO_NEW);
    }

    WritableJitPage jit_page = ThreadIsolation::LookupWritableJitPage(
        page->area_start(), page->area_size());
    for (auto& slot_update : slot_updates) {
      Tagged<HeapObject> new_target = std::get<0>(slot_update);
      SlotType slot_type = std::get<1>(slot_update);
      Address slot_address = std::get<2>(slot_update);

      WritableJitAllocation jit_allocation =
          jit_page.LookupAllocationContaining(slot_address);
      UpdateTypedSlotHelper::UpdateTypedSlot(
          jit_allocation, heap_, slot_type, slot_address,
          [new_target](FullMaybeObjectSlot slot) {
            slot.store(new_target);
            return KEEP_SLOT;
          });
    }
  } else {
    DCHECK_NULL(page->typed_slot_set<OLD_TO_NEW>());
  }

  if (page->slot_set<OLD_TO_NEW_BACKGROUND, AccessMode::ATOMIC>() != nullptr) {
    RememberedSet<OLD_TO_NEW_BACKGROUND>::IterateAndTrackEmptyBuckets(
        page,
        [this, chunk, page, record_old_to_shared_slots](MaybeObjectSlot slot) {
          SlotCallbackResult result = CheckAndScavengeObject(heap_, slot);
          // A new space string might have been promoted into the shared heap
          // during GC.
          if (result == REMOVE_SLOT && record_old_to_shared_slots) {
            CheckOldToNewSlotForSharedUntyped(chunk, page, slot);
          }
          return result;
        },
        &local_empty_chunks_);
  }
}

void Scavenger::Process(JobDelegate* delegate) {
  ScavengeVisitor scavenge_visitor(this);

  bool done;
  size_t objects = 0;
  do {
    done = true;
    Tagged<HeapObject> object;
    while (!ShouldEagerlyProcessPromotedList() &&
           local_copied_list_.Pop(&object)) {
      scavenge_visitor.Visit(object);
      done = false;
      if (delegate && ((++objects % kInterruptThreshold) == 0)) {
        if (!local_copied_list_.IsLocalEmpty()) {
          delegate->NotifyConcurrencyIncrease();
        }
      }
    }

    struct PromotedListEntry entry;
    while (local_promoted_list_.Pop(&entry)) {
      Tagged<HeapObject> target = entry.heap_object;
      IterateAndScavengePromotedObject(target, entry.map, entry.size);
      done = false;
      if (delegate && ((++objects % kInterruptThreshold) == 0)) {
        if (!local_promoted_list_.IsGlobalEmpty()) {
          delegate->NotifyConcurrencyIncrease();
        }
      }
    }
  } while (!done);
}

void ScavengerCollector::ProcessWeakReferences(
    EphemeronRememberedSet::TableList* ephemeron_table_list) {
  ClearYoungEphemerons(ephemeron_table_list);
  ClearOldEphemerons();
}

// Clear ephemeron entries from EphemeronHashTables in new-space whenever the
// entry has a dead new-space key.
void ScavengerCollector::ClearYoungEphemerons(
    EphemeronRememberedSet::TableList* ephemeron_table_list) {
  ephemeron_table_list->Iterate([this](Tagged<EphemeronHashTable> table) {
    for (InternalIndex i : table->IterateEntries()) {
      // Keys in EphemeronHashTables must be heap objects.
      HeapObjectSlot key_slot(
          table->RawFieldOfElementAt(EphemeronHashTable::EntryToIndex(i)));
      Tagged<HeapObject> key = key_slot.ToHeapObject();
      if (IsUnscavengedHeapObject(heap_, key)) {
        table->RemoveEntry(i);
      } else {
        Tagged<HeapObject> forwarded = ForwardingAddress(key);
        key_slot.StoreHeapObject(forwarded);
      }
    }
  });
  ephemeron_table_list->Clear();
}

// Clear ephemeron entries from EphemeronHashTables in old-space whenever the
// entry has a dead new-space key.
void ScavengerCollector::ClearOldEphemerons() {
  auto* table_map = heap_->ephemeron_remembered_set_->tables();
  for (auto it = table_map->begin(); it != table_map->end();) {
    Tagged<EphemeronHashTable> table = it->first;
    auto& indices = it->second;
    for (auto iti = indices.begin(); iti != indices.end();) {
      // Keys in EphemeronHashTables must be heap objects.
      HeapObjectSlot key_slot(table->RawFieldOfElementAt(
          EphemeronHashTable::EntryToIndex(InternalIndex(*iti))));
      Tagged<HeapObject> key = key_slot.ToHeapObject();
      if (IsUnscavengedHeapObject(heap_, key)) {
        table->RemoveEntry(InternalIndex(*iti));
        iti = indices.erase(iti);
      } else {
        Tagged<HeapObject> forwarded = ForwardingAddress(key);
        key_slot.StoreHeapObject(forwarded);
        if (!HeapLayout::InYoungGeneration(forwarded)) {
          iti = indices.erase(iti);
        } else {
          ++iti;
        }
      }
    }

    if (indices.empty()) {
      it = table_map->erase(it);
    } else {
      ++it;
    }
  }
}

void Scavenger::Finalize() {
  heap()->pretenuring_handler()->MergeAllocationSitePretenuringFeedback(
      local_pretenuring_feedback_);
  for (const auto& it : local_ephemeron_remembered_set_) {
    // The ephemeron objects in the remembered set should be either large
    // objects, promoted to old space, or pinned objects on quarantined pages
    // that will be promoted.
    DCHECK_IMPLIES(
        !MemoryChunk::FromHeapObject(it.first)->IsLargePage(),
        !HeapLayout::InYoungGeneration(it.first) ||
            (HeapLayout::IsSelfForwarded(it.first) &&
             MemoryChunk::FromHeapObject(it.first)->IsQuarantined() &&
             heap()->semi_space_new_space()->ShouldPageBePromoted(
                 it.first->address())));
    heap()->ephemeron_remembered_set()->RecordEphemeronKeyWrites(
        it.first, std::move(it.second));
  }
  heap()->IncrementNewSpaceSurvivingObjectSize(copied_size_);
  heap()->IncrementPromotedObjectsSize(promoted_size_);
  collector_->MergeSurvivingNewLargeObjects(local_surviving_new_large_objects_);
  allocator_.Finalize();
  local_empty_chunks_.Publish();
  local_ephemeron_table_list_.Publish();
}

void Scavenger::Publish() {
  local_copied_list_.Publish();
  local_pinned_list_.Publish();
  local_promoted_list_.Publish();
}

void Scavenger::AddEphemeronHashTable(Tagged<EphemeronHashTable> table) {
  local_ephemeron_table_list_.Push(table);
}

template <typename TSlot>
void Scavenger::CheckOldToNewSlotForSharedUntyped(MemoryChunk* chunk,
                                                  MutablePageMetadata* page,
                                                  TSlot slot) {
  Tagged<MaybeObject> object = *slot;
  Tagged<HeapObject> heap_object;

  if (object.GetHeapObject(&heap_object) &&
      HeapLayout::InWritableSharedSpace(heap_object)) {
    RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(
        page, chunk->Offset(slot.address()));
  }
}

void Scavenger::CheckOldToNewSlotForSharedTyped(
    MemoryChunk* chunk, MutablePageMetadata* page, SlotType slot_type,
    Address slot_address, Tagged<MaybeObject> new_target) {
  Tagged<HeapObject> heap_object;

  if (new_target.GetHeapObject(&heap_object) &&
      HeapLayout::InWritableSharedSpace(heap_object)) {
    const uintptr_t offset = chunk->Offset(slot_address);
    DCHECK_LT(offset, static_cast<uintptr_t>(TypedSlotSet::kMaxOffset));

    base::SpinningMutexGuard guard(page->mutex());
    RememberedSet<OLD_TO_SHARED>::InsertTyped(page, slot_type,
                                              static_cast<uint32_t>(offset));
  }
}

bool Scavenger::PromoteIfLargeObject(Tagged<HeapObject> object) {
  Tagged<Map> map = object->map();
  return HandleLargeObject(map, object, object->SizeFromMap(map),
                           Map::ObjectFieldsFrom(map->visitor_id()));
}

void Scavenger::PushPinnedObject(Tagged<HeapObject> object, Tagged<Map> map) {
  DCHECK(HeapLayout::IsSelfForwarded(object));
  int object_size = object->SizeFromMap(map);
  if (heap_->semi_space_new_space()->ShouldPageBePromoted(object->address())) {
    local_promoted_list_.Push({object, map, object_size});
    promoted_size_ += object_size;
  } else {
    local_pinned_list_.Push(ObjectAndMap(object, map));
    copied_size_ += object_size;
  }
}

void Scavenger::VisitPinnedObjects() {
  ScavengeVisitor scavenge_visitor(this);

  ObjectAndMap object_and_map;
  while (local_pinned_list_.Pop(&object_and_map)) {
    DCHECK(HeapLayout::IsSelfForwarded(object_and_map.first));
    scavenge_visitor.Visit(object_and_map.second, object_and_map.first);
  }
}

void RootScavengeVisitor::VisitRootPointer(Root root, const char* description,
                                           FullObjectSlot p) {
  DCHECK(!HasWeakHeapObjectTag(*p));
  DCHECK(!MapWord::IsPacked((*p).ptr()));
  ScavengePointer(p);
}

void RootScavengeVisitor::VisitRootPointers(Root root, const char* description,
                                            FullObjectSlot start,
                                            FullObjectSlot end) {
  // Copy all HeapObject pointers in [start, end)
  for (FullObjectSlot p = start; p < end; ++p) {
    ScavengePointer(p);
  }
}

void RootScavengeVisitor::ScavengePointer(FullObjectSlot p) {
  Tagged<Object> object = *p;
  DCHECK(!HasWeakHeapObjectTag(object));
  DCHECK(!MapWord::IsPacked(object.ptr()));
  if (HeapLayout::InYoungGeneration(object)) {
    scavenger_.ScavengeObject(FullHeapObjectSlot(p), Cast<HeapObject>(object));
  }
}

RootScavengeVisitor::RootScavengeVisitor(Scavenger& scavenger)
    : scavenger_(scavenger) {}

RootScavengeVisitor::~RootScavengeVisitor() { scavenger_.Publish(); }

ScavengeVisitor::ScavengeVisitor(Scavenger* scavenger)
    : NewSpaceVisitor<ScavengeVisitor>(scavenger->heap()->isolate()),
      scavenger_(scavenger) {}

}  // namespace internal
}  // namespace v8
