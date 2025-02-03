// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARK_COMPACT_H_
#define V8_HEAP_MARK_COMPACT_H_

#include <vector>

#include "include/v8-internal.h"
#include "src/common/globals.h"
#include "src/heap/marking-state.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/marking.h"
#include "src/heap/memory-measurement.h"
#include "src/heap/spaces.h"
#include "src/heap/sweeper.h"

namespace v8 {
namespace internal {

// Forward declarations.
class HeapObjectVisitor;
class LargeObjectSpace;
class LargePageMetadata;
class MainMarkingVisitor;
class MarkCompactCollector;
class RecordMigratedSlotVisitor;

class RootMarkingVisitor final : public RootVisitor {
 public:
  explicit RootMarkingVisitor(MarkCompactCollector* collector);
  ~RootMarkingVisitor();

  V8_INLINE void VisitRootPointer(Root root, const char* description,
                                  FullObjectSlot p) final;

  V8_INLINE void VisitRootPointers(Root root, const char* description,
                                   FullObjectSlot start,
                                   FullObjectSlot end) final;

  // Keep this synced with `RootsReferencesExtractor::VisitRunningCode()`.
  void VisitRunningCode(FullObjectSlot code_slot,
                        FullObjectSlot istream_or_smi_zero_slot) final;

  RootMarkingVisitor(const RootMarkingVisitor&) = delete;
  RootMarkingVisitor& operator=(const RootMarkingVisitor&) = delete;

 private:
  V8_INLINE void MarkObjectByPointer(Root root, FullObjectSlot p);

  MarkCompactCollector* const collector_;
};

// Collector for young and old generation.
class MarkCompactCollector final {
 public:
  class CustomRootBodyMarkingVisitor;
  class SharedHeapObjectVisitor;

  enum class StartCompactionMode {
    kIncremental,
    kAtomic,
  };

  enum class MarkingWorklistProcessingMode {
    kDefault,
    kTrackNewlyDiscoveredObjects
  };

  enum class CallOrigin {
    kIncrementalMarkingStep,
    kAtomicGC,
  };

  // Callback function for telling whether the object *p is an unmarked
  // heap object.
  static bool IsUnmarkedHeapObject(Heap* heap, FullObjectSlot p);
  static bool IsUnmarkedSharedHeapObject(Heap* heap, FullObjectSlot p);

  std::pair<size_t, size_t> ProcessMarkingWorklist(
      v8::base::TimeDelta max_duration, size_t max_bytes_to_process,
      MarkingWorklistProcessingMode mode);

  void TearDown();

  // Performs a global garbage collection.
  void CollectGarbage();

  void CollectEvacuationCandidates(PagedSpace* space);

  void AddEvacuationCandidate(PageMetadata* p);

  // Prepares for GC by resetting relocation info in old and map spaces and
  // choosing spaces to compact.
  void Prepare();

  // Stop concurrent marking (either by preempting it right away or waiting for
  // it to complete as requested by |stop_request|).
  void FinishConcurrentMarking();

  // Returns whether compaction is running.
  bool StartCompaction(StartCompactionMode mode);

  void StartMarking();

  static inline bool IsOnEvacuationCandidate(Tagged<MaybeObject> obj) {
    return MemoryChunk::FromAddress(obj.ptr())->IsEvacuationCandidate();
  }

  struct RecordRelocSlotInfo {
    MutablePageMetadata* page_metadata;
    SlotType slot_type;
    uint32_t offset;
  };

  static bool ShouldRecordRelocSlot(Tagged<InstructionStream> host,
                                    RelocInfo* rinfo,
                                    Tagged<HeapObject> target);
  static RecordRelocSlotInfo ProcessRelocInfo(Tagged<InstructionStream> host,
                                              RelocInfo* rinfo,
                                              Tagged<HeapObject> target);

  static void RecordRelocSlot(Tagged<InstructionStream> host, RelocInfo* rinfo,
                              Tagged<HeapObject> target);
  template <typename THeapObjectSlot>
  V8_INLINE static void RecordSlot(Tagged<HeapObject> object,
                                   THeapObjectSlot slot,
                                   Tagged<HeapObject> target);
  template <typename THeapObjectSlot>
  V8_INLINE static void RecordSlot(MemoryChunk* source_chunk,
                                   THeapObjectSlot slot,
                                   Tagged<HeapObject> target);

  bool is_compacting() const { return compacting_; }

  V8_INLINE void AddTransitionArray(Tagged<TransitionArray> array);

  void RecordStrongDescriptorArraysForWeakening(
      GlobalHandleVector<DescriptorArray> strong_descriptor_arrays);

#ifdef DEBUG
  // Checks whether performing mark-compact collection.
  bool in_use() { return state_ > PREPARE_GC; }
  bool are_map_pointers_encoded() { return state_ == UPDATE_POINTERS; }
#endif

  void VerifyMarking();
#ifdef VERIFY_HEAP
  void VerifyMarkbitsAreClean();
  void VerifyMarkbitsAreClean(PagedSpaceBase* space);
  void VerifyMarkbitsAreClean(NewSpace* space);
  void VerifyMarkbitsAreClean(LargeObjectSpace* space);
#endif

  unsigned epoch() const { return epoch_; }

  base::EnumSet<CodeFlushMode> code_flush_mode() const {
    return code_flush_mode_;
  }

  MarkingWorklists* marking_worklists() { return &marking_worklists_; }

  MarkingWorklists::Local* local_marking_worklists() const {
    return local_marking_worklists_.get();
  }

  WeakObjects* weak_objects() { return &weak_objects_; }
  WeakObjects::Local* local_weak_objects() { return local_weak_objects_.get(); }

  void AddNewlyDiscovered(Tagged<HeapObject> object) {
    if (ephemeron_marking_.newly_discovered_overflowed) return;

    if (ephemeron_marking_.newly_discovered.size() <
        ephemeron_marking_.newly_discovered_limit) {
      ephemeron_marking_.newly_discovered.push_back(object);
    } else {
      ephemeron_marking_.newly_discovered_overflowed = true;
    }
  }

  void ResetNewlyDiscovered() {
    ephemeron_marking_.newly_discovered_overflowed = false;
    ephemeron_marking_.newly_discovered.clear();
  }

  bool UseBackgroundThreadsInCycle() const {
    return use_background_threads_in_cycle_;
  }

  void MaybeEnableBackgroundThreadsInCycle(CallOrigin origin);

  Heap* heap() { return heap_; }

  explicit MarkCompactCollector(Heap* heap);
  ~MarkCompactCollector();

 private:
  using ResizeNewSpaceMode = Heap::ResizeNewSpaceMode;

  void ComputeEvacuationHeuristics(size_t area_size,
                                   int* target_fragmentation_percent,
                                   size_t* max_evacuated_bytes);

  void RecordObjectStats();

  // Finishes GC, performs heap verification if enabled.
  void Finish();

  // Free unmarked ArrayBufferExtensions.
  void SweepArrayBufferExtensions();

  void MarkLiveObjects();

  // Marks the object and adds it to the worklist.
  V8_INLINE void MarkObject(Tagged<HeapObject> host, Tagged<HeapObject> obj,
                            MarkingHelper::WorklistTarget target_worklist);

  // Marks the root object and adds it to the worklist.
  V8_INLINE void MarkRootObject(Root root, Tagged<HeapObject> obj,
                                MarkingHelper::WorklistTarget target_worklist);

  // Mark the heap roots and all objects reachable from them.
  void MarkRoots(RootVisitor* root_visitor);

  // Mark the stack roots and all objects reachable from them.
  void MarkRootsFromConservativeStack(RootVisitor* root_visitor);

  // Mark all objects that are directly referenced from one of the clients
  // heaps.
  void MarkObjectsFromClientHeaps();
  void MarkObjectsFromClientHeap(Isolate* client);

  // Updates pointers to shared objects from client heaps.
  void UpdatePointersInClientHeaps();
  void UpdatePointersInClientHeap(Isolate* client);

  // Update pointers in sandbox-related pointer tables.
  void UpdatePointersInPointerTables();

  // Marks object reachable from harmony weak maps and wrapper tracing.
  void MarkTransitiveClosure();
  void VerifyEphemeronMarking();

  // If the call-site of the top optimized code was not prepared for
  // deoptimization, then treat embedded pointers in the code as strong as
  // otherwise they can die and try to deoptimize the underlying code.
  void ProcessTopOptimizedFrame(ObjectVisitor* visitor, Isolate* isolate);

  // Implements ephemeron semantics: Marks value if key is already reachable.
  // Returns true if value was actually marked.
  bool ProcessEphemeron(Tagged<HeapObject> key, Tagged<HeapObject> value);

  // Marks the transitive closure by draining the marking worklist iteratively,
  // applying ephemerons semantics and invoking embedder tracing until a
  // fixpoint is reached. Returns false if too many iterations have been tried
  // and the linear approach should be used.
  bool MarkTransitiveClosureUntilFixpoint();

  // Marks the transitive closure applying ephemeron semantics and invoking
  // embedder tracing with a linear algorithm for ephemerons. Only used if
  // fixpoint iteration doesn't finish within a few iterations.
  void MarkTransitiveClosureLinear();

  // Drains ephemeron and marking worklists. Single iteration of the
  // fixpoint iteration.
  bool ProcessEphemerons();

  // Perform Wrapper Tracing if in use.
  void PerformWrapperTracing();

  // Retain dying maps for `v8_flags.retain_maps_for_n_gc` garbage collections
  // to increase chances of reusing of map transition tree in future.
  void RetainMaps();

  // Clear non-live references in weak cells, transition and descriptor arrays,
  // and deoptimize dependent code of non-live maps.
  void ClearNonLiveReferences();
  void MarkDependentCodeForDeoptimization();

  // Special handling for clearing map slots.
  // Returns true if the slot was cleared.
  bool SpecialClearMapSlot(Tagged<HeapObject> host, Tagged<Map> dead_target,
                           HeapObjectSlot slot);

  // Checks if the given weak cell is a simple transition from the parent map
  // of the given dead target. If so it clears the transition and trims
  // the descriptor array of the parent if needed.
  void ClearPotentialSimpleMapTransition(Tagged<Map> dead_target);
  void ClearPotentialSimpleMapTransition(Tagged<Map> map,
                                         Tagged<Map> dead_target);

  // Flushes a weakly held bytecode array from a shared function info.
  void FlushBytecodeFromSFI(Tagged<SharedFunctionInfo> shared_info);

  // Clears bytecode arrays / baseline code that have not been executed for
  // multiple collections.
  void ProcessOldCodeCandidates();

  bool ProcessOldBytecodeSFI(Tagged<SharedFunctionInfo> flushing_candidate);
  bool ProcessOldBaselineSFI(Tagged<SharedFunctionInfo> flushing_candidate);
  void FlushSFI(Tagged<SharedFunctionInfo> sfi,
                bool bytecode_already_decompiled);

  void ProcessFlushedBaselineCandidates();

  // Resets any JSFunctions which have had their bytecode flushed.
  void ClearFlushedJsFunctions();

  // Compact every array in the global list of transition arrays and
  // trim the corresponding descriptor array if a transition target is non-live.
  void ClearFullMapTransitions();
  void TrimDescriptorArray(Tagged<Map> map,
                           Tagged<DescriptorArray> descriptors);
  void TrimEnumCache(Tagged<Map> map, Tagged<DescriptorArray> descriptors);
  bool CompactTransitionArray(Tagged<Map> map,
                              Tagged<TransitionArray> transitions,
                              Tagged<DescriptorArray> descriptors);
  bool TransitionArrayNeedsCompaction(Tagged<TransitionArray> transitions,
                                      int num_transitions);
  void WeakenStrongDescriptorArrays();

  // After all reachable objects have been marked those weak map entries
  // with an unreachable key are removed from all encountered weak maps.
  // The linked list of all encountered weak maps is destroyed.
  void ClearWeakCollections();

  // Goes through the list of encountered trivial weak references and clears
  // those with dead values. This is performed in a parallel job. In short, a
  // weak reference is considered trivial if its value does not require special
  // weakness clearing.
  void ClearTrivialWeakReferences();
  class ClearTrivialWeakRefJobItem;

  // Goes through the list of encountered non-trivial weak references and
  // filters out those whose values are still alive. This is performed in a
  // parallel job.
  void FilterNonTrivialWeakReferences();
  class FilterNonTrivialWeakRefJobItem;

  // Goes through the list of encountered non-trivial weak references with
  // dead values. If the value is a dead map and the parent map transitions to
  // the dead map via weak cell, then this function also clears the map
  // transition.
  void ClearNonTrivialWeakReferences();

  // Goes through the list of encountered JSWeakRefs and WeakCells and clears
  // those with dead values.
  void ClearJSWeakRefs();

  // Starts sweeping of spaces by contributing on the main thread and setting
  // up other pages for sweeping. Does not start sweeper tasks.
  void Sweep();
  void StartSweepSpace(PagedSpace* space);

  void EvacuatePrologue();
  void EvacuateEpilogue();
  void Evacuate();
  void EvacuatePagesInParallel();
  void UpdatePointersAfterEvacuation();

  void ReleaseEvacuationCandidates();
  // Returns number of aborted pages.
  size_t PostProcessAbortedEvacuationCandidates();
  void ReportAbortedEvacuationCandidateDueToOOM(Address failed_start,
                                                PageMetadata* page);
  void ReportAbortedEvacuationCandidateDueToFlags(Address failed_start,
                                                  PageMetadata* page);

  static const int kEphemeronChunkSize = 8 * KB;

  int NumberOfParallelEphemeronVisitingTasks(size_t elements);

  void RightTrimDescriptorArray(Tagged<DescriptorArray> array,
                                int descriptors_to_trim);

  void StartSweepNewSpace();
  void SweepLargeSpace(LargeObjectSpace* space);

  Heap* const heap_;

  base::Mutex mutex_;
  base::Semaphore page_parallel_job_semaphore_{0};

#ifdef DEBUG
  enum CollectorState{IDLE,
                      PREPARE_GC,
                      MARK_LIVE_OBJECTS,
                      SWEEP_SPACES,
                      ENCODE_FORWARDING_ADDRESSES,
                      UPDATE_POINTERS,
                      RELOCATE_OBJECTS};

  // The current stage of the collector.
  CollectorState state_;
#endif

  const bool uses_shared_heap_;
  const bool is_shared_space_isolate_;

  // True if we are collecting slots to perform evacuation from evacuation
  // candidates.
  bool compacting_ = false;
  bool black_allocation_ = false;
  bool have_code_to_deoptimize_ = false;
  bool parallel_marking_ = false;

  MarkingWorklists marking_worklists_;
  std::unique_ptr<MarkingWorklists::Local> local_marking_worklists_;

  WeakObjects weak_objects_;
  EphemeronMarking ephemeron_marking_;

  std::unique_ptr<MainMarkingVisitor> marking_visitor_;
  std::unique_ptr<WeakObjects::Local> local_weak_objects_;
  NativeContextInferrer native_context_inferrer_;
  NativeContextStats native_context_stats_;

  std::vector<GlobalHandleVector<DescriptorArray>> strong_descriptor_arrays_;
  base::Mutex strong_descriptor_arrays_mutex_;

  // Candidates for pages that should be evacuated.
  std::vector<PageMetadata*> evacuation_candidates_;
  // Pages that are actually processed during evacuation.
  std::vector<PageMetadata*> old_space_evacuation_pages_;
  std::vector<PageMetadata*> new_space_evacuation_pages_;
  std::vector<std::pair<Address, PageMetadata*>>
      aborted_evacuation_candidates_due_to_oom_;
  std::vector<std::pair<Address, PageMetadata*>>
      aborted_evacuation_candidates_due_to_flags_;
  std::vector<LargePageMetadata*> promoted_large_pages_;

  MarkingState* const marking_state_;
  NonAtomicMarkingState* const non_atomic_marking_state_;
  Sweeper* const sweeper_;

  // Counts the number of major mark-compact collections. The counter is
  // incremented right after marking. This is used for:
  // - marking descriptor arrays. See NumberOfMarkedDescriptors. Only the lower
  //   two bits are used, so it is okay if this counter overflows and wraps
  //   around.
  unsigned epoch_ = 0;

  ResizeNewSpaceMode resize_new_space_ = ResizeNewSpaceMode::kNone;

  // Bytecode flushing is disabled when the code coverage mode is changed. Since
  // that can happen while a GC is happening and we need the
  // code_flush_mode_ to remain the same through out a GC, we record this at
  // the start of each GC.
  base::EnumSet<CodeFlushMode> code_flush_mode_;

  std::vector<PageMetadata*> empty_new_space_pages_to_be_swept_;

  bool use_background_threads_in_cycle_ = false;

  friend class Evacuator;
  friend class RecordMigratedSlotVisitor;
  friend class RootMarkingVisitor;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARK_COMPACT_H_
