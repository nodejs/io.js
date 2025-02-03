// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MINOR_MARK_SWEEP_H_
#define V8_HEAP_MINOR_MARK_SWEEP_H_

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/heap/heap.h"
#include "src/heap/index-generator.h"
#include "src/heap/marking-state.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/parallel-work-item.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/slot-set.h"
#include "src/heap/sweeper.h"
#include "src/heap/young-generation-marking-visitor.h"

namespace v8 {
namespace internal {

class MinorMarkSweepCollector;

using YoungGenerationMainMarkingVisitor = YoungGenerationMarkingVisitor<
    YoungGenerationMarkingVisitationMode::kParallel>;

class YoungGenerationRememberedSetsMarkingWorklist {
 private:
  class MarkingItem;

 public:
  class Local {
   public:
    explicit Local(YoungGenerationRememberedSetsMarkingWorklist* handler)
        : handler_(handler) {}

    template <typename Visitor>
    bool ProcessNextItem(Visitor* visitor) {
      return handler_->ProcessNextItem(visitor, index_);
    }

   private:
    YoungGenerationRememberedSetsMarkingWorklist* const handler_;
    std::optional<size_t> index_;
  };

  static std::vector<MarkingItem> CollectItems(Heap* heap);

  explicit YoungGenerationRememberedSetsMarkingWorklist(Heap* heap);
  ~YoungGenerationRememberedSetsMarkingWorklist();

  size_t RemainingRememberedSetsMarkingIteams() const {
    return remaining_remembered_sets_marking_items_.load(
        std::memory_order_relaxed);
  }

  void TearDown();

 private:
  class MarkingItem : public ParallelWorkItem {
   public:
    enum class SlotsType { kRegularSlots, kTypedSlots };

    MarkingItem(MutablePageMetadata* chunk, SlotsType slots_type,
                SlotSet* slot_set, SlotSet* background_slot_set)
        : chunk_(chunk),
          slots_type_(slots_type),
          slot_set_(slot_set),
          background_slot_set_(background_slot_set) {}
    MarkingItem(MutablePageMetadata* chunk, SlotsType slots_type,
                TypedSlotSet* typed_slot_set)
        : chunk_(chunk),
          slots_type_(slots_type),
          typed_slot_set_(typed_slot_set) {}
    ~MarkingItem() = default;

    template <typename Visitor>
    void Process(Visitor* visitor);
    void MergeAndDeleteRememberedSets();
    void DeleteRememberedSets();

    void DeleteSetsOnTearDown();

   private:
    inline Heap* heap() { return chunk_->heap(); }

    template <typename Visitor>
    void MarkUntypedPointers(Visitor* visitor);
    template <typename Visitor>
    void MarkTypedPointers(Visitor* visitor);
    template <typename Visitor, typename TSlot>
    V8_INLINE SlotCallbackResult CheckAndMarkObject(Visitor* visitor,
                                                    TSlot slot);

    MutablePageMetadata* const chunk_;
    const SlotsType slots_type_;
    union {
      SlotSet* slot_set_;
      TypedSlotSet* typed_slot_set_;
    };
    SlotSet* background_slot_set_ = nullptr;
  };

  template <typename Visitor>
  bool ProcessNextItem(Visitor* visitor, std::optional<size_t>& index);

  std::vector<MarkingItem> remembered_sets_marking_items_;
  std::atomic_size_t remaining_remembered_sets_marking_items_;
  IndexGenerator remembered_sets_marking_index_generator_;
};

class YoungGenerationRootMarkingVisitor final : public RootVisitor {
 public:
  explicit YoungGenerationRootMarkingVisitor(
      MinorMarkSweepCollector* collector);
  ~YoungGenerationRootMarkingVisitor();

  V8_INLINE void VisitRootPointer(Root root, const char* description,
                                  FullObjectSlot p) final;

  V8_INLINE void VisitRootPointers(Root root, const char* description,
                                   FullObjectSlot start,
                                   FullObjectSlot end) final;

  GarbageCollector collector() const override {
    return GarbageCollector::MINOR_MARK_SWEEPER;
  }

  YoungGenerationRootMarkingVisitor(const YoungGenerationRootMarkingVisitor&) =
      delete;
  YoungGenerationRootMarkingVisitor& operator=(
      const YoungGenerationRootMarkingVisitor&) = delete;

 private:
  template <typename TSlot>
  void VisitPointersImpl(Root root, TSlot start, TSlot end);

  YoungGenerationMainMarkingVisitor* const main_marking_visitor_;
};

// Collector for young-generation only.
class MinorMarkSweepCollector final {
 public:
  static constexpr size_t kMaxParallelTasks = 8;

  explicit MinorMarkSweepCollector(Heap* heap);
  ~MinorMarkSweepCollector();

  void TearDown();
  void CollectGarbage();
  void StartMarking(bool force_use_background_threads);

  void RequestGC();

  EphemeronRememberedSet::TableList* ephemeron_table_list() const {
    return ephemeron_table_list_.get();
  }

  MarkingWorklists* marking_worklists() { return marking_worklists_.get(); }

  MarkingWorklists::Local* local_marking_worklists() {
    return &main_marking_visitor_->marking_worklists_local();
  }

  YoungGenerationRememberedSetsMarkingWorklist*
  remembered_sets_marking_handler() {
    DCHECK_NOT_NULL(remembered_sets_marking_handler_);
    return remembered_sets_marking_handler_.get();
  }

  YoungGenerationMainMarkingVisitor* main_marking_visitor() {
    return main_marking_visitor_.get();
  }

  bool is_in_atomic_pause() const {
    return is_in_atomic_pause_.load(std::memory_order_relaxed);
  }

  bool gc_finalization_requsted() const {
    return gc_finalization_requested_.load(std::memory_order_relaxed);
  }

  bool UseBackgroundThreadsInCycle() const {
    return use_background_threads_in_cycle_.value();
  }

  void DrainMarkingWorklistForTesting() { DrainMarkingWorklist(); }

 private:
  using ResizeNewSpaceMode = Heap::ResizeNewSpaceMode;

  class RootMarkingVisitor;

  Sweeper* sweeper() { return sweeper_; }

  void MarkLiveObjects();
  void MarkRoots(YoungGenerationRootMarkingVisitor& root_visitor,
                 bool was_marked_incrementally);
  V8_EXPORT_PRIVATE void DrainMarkingWorklist();
  void MarkRootsFromTracedHandles(
      YoungGenerationRootMarkingVisitor& root_visitor);
  void MarkRootsFromConservativeStack(
      YoungGenerationRootMarkingVisitor& root_visitor);
  void EvacuateExternalPointerReferences(MutablePageMetadata* p);

  void TraceFragmentation();
  void ClearNonLiveReferences();
  void FinishConcurrentMarking();
  // Perform Wrapper Tracing if in use.
  void PerformWrapperTracing();

  void Sweep();
  // 'StartSweepNewSpace' and 'SweepNewLargeSpace' return true if any pages were
  // promoted.
  bool StartSweepNewSpace();
  void StartSweepNewSpaceWithStickyBits();
  bool SweepNewLargeSpace();

  void Finish();

  Heap* const heap_;

  std::unique_ptr<MarkingWorklists> marking_worklists_;

  std::unique_ptr<EphemeronRememberedSet::TableList> ephemeron_table_list_;
  std::unique_ptr<YoungGenerationMainMarkingVisitor> main_marking_visitor_;

  MarkingState* const marking_state_;
  NonAtomicMarkingState* const non_atomic_marking_state_;
  Sweeper* const sweeper_;

  std::unique_ptr<PretenuringHandler::PretenuringFeedbackMap>
      pretenuring_feedback_;

  std::unique_ptr<YoungGenerationRememberedSetsMarkingWorklist>
      remembered_sets_marking_handler_;

  ResizeNewSpaceMode resize_new_space_ = ResizeNewSpaceMode::kNone;

  std::optional<bool> use_background_threads_in_cycle_;

  std::atomic<bool> is_in_atomic_pause_{false};
  std::atomic<bool> gc_finalization_requested_{false};

  friend class IncrementalMarking;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MINOR_MARK_SWEEP_H_
