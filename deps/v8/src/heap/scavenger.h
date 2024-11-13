// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SCAVENGER_H_
#define V8_HEAP_SCAVENGER_H_

#include "src/base/platform/condition-variable.h"
#include "src/heap/base/worklist.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/evacuation-allocator.h"
#include "src/heap/index-generator.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/objects-visiting.h"
#include "src/heap/parallel-work-item.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/slot-set.h"

namespace v8 {
namespace internal {

class RootScavengeVisitor;
class Scavenger;
class ScavengeVisitor;

enum class CopyAndForwardResult {
  SUCCESS_YOUNG_GENERATION,
  SUCCESS_OLD_GENERATION,
  FAILURE
};

using ObjectAndSize = std::pair<Tagged<HeapObject>, int>;
using SurvivingNewLargeObjectsMap =
    std::unordered_map<Tagged<HeapObject>, Tagged<Map>, Object::Hasher>;
using SurvivingNewLargeObjectMapEntry =
    std::pair<Tagged<HeapObject>, Tagged<Map>>;

class ScavengerCollector;

class Scavenger {
 public:
  struct PromotionListEntry {
    Tagged<HeapObject> heap_object;
    Tagged<Map> map;
    int size;
  };

  class PromotionList {
   public:
    static constexpr size_t kRegularObjectPromotionListSegmentSize = 256;
    static constexpr size_t kLargeObjectPromotionListSegmentSize = 4;

    using RegularObjectPromotionList =
        ::heap::base::Worklist<ObjectAndSize,
                               kRegularObjectPromotionListSegmentSize>;
    using LargeObjectPromotionList =
        ::heap::base::Worklist<PromotionListEntry,
                               kLargeObjectPromotionListSegmentSize>;

    class Local {
     public:
      explicit Local(PromotionList* promotion_list);

      inline void PushRegularObject(Tagged<HeapObject> object, int size);
      inline void PushLargeObject(Tagged<HeapObject> object, Tagged<Map> map,
                                  int size);
      inline size_t LocalPushSegmentSize() const;
      inline bool Pop(struct PromotionListEntry* entry);
      inline bool IsGlobalPoolEmpty() const;
      inline bool ShouldEagerlyProcessPromotionList() const;
      inline void Publish();

     private:
      RegularObjectPromotionList::Local regular_object_promotion_list_local_;
      LargeObjectPromotionList::Local large_object_promotion_list_local_;
    };

    inline bool IsEmpty() const;
    inline size_t Size() const;

   private:
    RegularObjectPromotionList regular_object_promotion_list_;
    LargeObjectPromotionList large_object_promotion_list_;
  };

  static const int kCopiedListSegmentSize = 256;

  using CopiedList =
      ::heap::base::Worklist<ObjectAndSize, kCopiedListSegmentSize>;
  using EmptyChunksList = ::heap::base::Worklist<MutablePageMetadata*, 64>;

  Scavenger(ScavengerCollector* collector, Heap* heap, bool is_logging,
            EmptyChunksList* empty_chunks, CopiedList* copied_list,
            PromotionList* promotion_list,
            EphemeronRememberedSet::TableList* ephemeron_table_list,
            int task_id);

  // Entry point for scavenging an old generation page. For scavenging single
  // objects see RootScavengingVisitor and ScavengeVisitor below.
  void ScavengePage(MutablePageMetadata* page);

  // Processes remaining work (=objects) after single objects have been
  // manually scavenged using ScavengeObject or CheckAndScavengeObject.
  void Process(JobDelegate* delegate = nullptr);

  // Finalize the Scavenger. Needs to be called from the main thread.
  void Finalize();
  void Publish();

  void AddEphemeronHashTable(Tagged<EphemeronHashTable> table);

  size_t bytes_copied() const { return copied_size_; }
  size_t bytes_promoted() const { return promoted_size_; }

 private:
  enum PromotionHeapChoice { kPromoteIntoLocalHeap, kPromoteIntoSharedHeap };

  // Number of objects to process before interrupting for potentially waking
  // up other tasks.
  static const int kInterruptThreshold = 128;

  inline Heap* heap() { return heap_; }

  inline void PageMemoryFence(Tagged<MaybeObject> object);

  void AddPageToSweeperIfNecessary(MutablePageMetadata* page);

  // Potentially scavenges an object referenced from |slot| if it is
  // indeed a HeapObject and resides in from space.
  template <typename TSlot>
  inline SlotCallbackResult CheckAndScavengeObject(Heap* heap, TSlot slot);

  template <typename TSlot>
  inline void CheckOldToNewSlotForSharedUntyped(MemoryChunk* chunk,
                                                MutablePageMetadata* page,
                                                TSlot slot);
  inline void CheckOldToNewSlotForSharedTyped(MemoryChunk* chunk,
                                              MutablePageMetadata* page,
                                              SlotType slot_type,
                                              Address slot_address,
                                              Tagged<MaybeObject> new_target);

  // Scavenges an object |object| referenced from slot |p|. |object| is required
  // to be in from space.
  template <typename THeapObjectSlot>
  inline SlotCallbackResult ScavengeObject(THeapObjectSlot p,
                                           Tagged<HeapObject> object);

  // Copies |source| to |target| and sets the forwarding pointer in |source|.
  V8_INLINE bool MigrateObject(Tagged<Map> map, Tagged<HeapObject> source,
                               Tagged<HeapObject> target, int size,
                               PromotionHeapChoice promotion_heap_choice);

  V8_INLINE SlotCallbackResult
  RememberedSetEntryNeeded(CopyAndForwardResult result);

  template <typename THeapObjectSlot>
  V8_INLINE CopyAndForwardResult SemiSpaceCopyObject(
      Tagged<Map> map, THeapObjectSlot slot, Tagged<HeapObject> object,
      int object_size, ObjectFields object_fields);

  template <typename THeapObjectSlot,
            PromotionHeapChoice promotion_heap_choice = kPromoteIntoLocalHeap>
  V8_INLINE CopyAndForwardResult PromoteObject(Tagged<Map> map,
                                               THeapObjectSlot slot,
                                               Tagged<HeapObject> object,
                                               int object_size,
                                               ObjectFields object_fields);

  template <typename THeapObjectSlot>
  V8_INLINE SlotCallbackResult EvacuateObject(THeapObjectSlot slot,
                                              Tagged<Map> map,
                                              Tagged<HeapObject> source);

  V8_INLINE bool HandleLargeObject(Tagged<Map> map, Tagged<HeapObject> object,
                                   int object_size, ObjectFields object_fields);

  // Different cases for object evacuation.
  template <typename THeapObjectSlot,
            PromotionHeapChoice promotion_heap_choice = kPromoteIntoLocalHeap>
  V8_INLINE SlotCallbackResult EvacuateObjectDefault(
      Tagged<Map> map, THeapObjectSlot slot, Tagged<HeapObject> object,
      int object_size, ObjectFields object_fields);

  template <typename THeapObjectSlot>
  inline SlotCallbackResult EvacuateThinString(Tagged<Map> map,
                                               THeapObjectSlot slot,
                                               Tagged<ThinString> object,
                                               int object_size);

  template <typename THeapObjectSlot>
  inline SlotCallbackResult EvacuateShortcutCandidate(Tagged<Map> map,
                                                      THeapObjectSlot slot,
                                                      Tagged<ConsString> object,
                                                      int object_size);

  template <typename THeapObjectSlot>
  inline SlotCallbackResult EvacuateInPlaceInternalizableString(
      Tagged<Map> map, THeapObjectSlot slot, Tagged<String> string,
      int object_size, ObjectFields object_fields);

  void IterateAndScavengePromotedObject(Tagged<HeapObject> target,
                                        Tagged<Map> map, int size);
  void RememberPromotedEphemeron(Tagged<EphemeronHashTable> table, int index);

  ScavengerCollector* const collector_;
  Heap* const heap_;
  EmptyChunksList::Local empty_chunks_local_;
  PromotionList::Local promotion_list_local_;
  CopiedList::Local copied_list_local_;
  EphemeronRememberedSet::TableList::Local ephemeron_table_list_local_;
  PretenuringHandler* const pretenuring_handler_;
  PretenuringHandler::PretenuringFeedbackMap local_pretenuring_feedback_;
  size_t copied_size_{0};
  size_t promoted_size_{0};
  EvacuationAllocator allocator_;
  SurvivingNewLargeObjectsMap surviving_new_large_objects_;

  EphemeronRememberedSet::TableMap ephemeron_remembered_set_;
  const bool is_logging_;
  const bool is_incremental_marking_;
  const bool is_compacting_;
  const bool shared_string_table_;
  const bool mark_shared_heap_;
  const bool shortcut_strings_;

  friend class IterateAndScavengePromotedObjectsVisitor;
  friend class RootScavengeVisitor;
  friend class ScavengeVisitor;
};

// Helper class for turning the scavenger into an object visitor that is also
// filtering out non-HeapObjects and objects which do not reside in new space.
class RootScavengeVisitor final : public RootVisitor {
 public:
  explicit RootScavengeVisitor(Scavenger* scavenger);

  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) final;
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) final;

 private:
  void ScavengePointer(FullObjectSlot p);

  Scavenger* const scavenger_;
};

class ScavengerCollector {
 public:
  static const int kMaxScavengerTasks = 8;
  static const int kMainThreadId = 0;

  explicit ScavengerCollector(Heap* heap);

  void CollectGarbage();

 private:
  class JobTask : public v8::JobTask {
   public:
    explicit JobTask(
        ScavengerCollector* outer,
        std::vector<std::unique_ptr<Scavenger>>* scavengers,
        std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
            memory_chunks,
        Scavenger::CopiedList* copied_list,
        Scavenger::PromotionList* promotion_list);

    void Run(JobDelegate* delegate) override;
    size_t GetMaxConcurrency(size_t worker_count) const override;

    uint64_t trace_id() const { return trace_id_; }

   private:
    void ProcessItems(JobDelegate* delegate, Scavenger* scavenger);
    void ConcurrentScavengePages(Scavenger* scavenger);

    ScavengerCollector* outer_;

    std::vector<std::unique_ptr<Scavenger>>* scavengers_;
    std::vector<std::pair<ParallelWorkItem, MutablePageMetadata*>>
        memory_chunks_;
    std::atomic<size_t> remaining_memory_chunks_{0};
    IndexGenerator generator_;

    Scavenger::CopiedList* copied_list_;
    Scavenger::PromotionList* promotion_list_;

    const uint64_t trace_id_;
  };

  void MergeSurvivingNewLargeObjects(
      const SurvivingNewLargeObjectsMap& objects);

  int NumberOfScavengeTasks();

  void ProcessWeakReferences(
      EphemeronRememberedSet::TableList* ephemeron_table_list);
  void ClearYoungEphemerons(
      EphemeronRememberedSet::TableList* ephemeron_table_list);
  void ClearOldEphemerons();
  void HandleSurvivingNewLargeObjects();

  void SweepArrayBufferExtensions();

  void IterateStackAndScavenge(
      RootScavengeVisitor* root_scavenge_visitor,
      std::vector<std::unique_ptr<Scavenger>>* scavengers, int main_thread_id);

  size_t FetchAndResetConcurrencyEstimate() {
    const size_t estimate =
        estimate_concurrency_.exchange(0, std::memory_order_relaxed);
    return estimate == 0 ? 1 : estimate;
  }

  Isolate* const isolate_;
  Heap* const heap_;
  SurvivingNewLargeObjectsMap surviving_new_large_objects_;
  std::atomic<size_t> estimate_concurrency_{0};

  friend class Scavenger;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SCAVENGER_H_
