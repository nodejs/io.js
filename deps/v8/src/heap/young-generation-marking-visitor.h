// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_YOUNG_GENERATION_MARKING_VISITOR_H_
#define V8_HEAP_YOUNG_GENERATION_MARKING_VISITOR_H_

#include <type_traits>

#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/heap-visitor.h"
#include "src/heap/heap.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/pretenuring-handler.h"

namespace v8 {
namespace internal {

enum class YoungGenerationMarkingVisitationMode { kParallel, kConcurrent };

template <YoungGenerationMarkingVisitationMode marking_mode>
class YoungGenerationMarkingVisitor final
    : public NewSpaceVisitor<YoungGenerationMarkingVisitor<marking_mode>> {
 public:
  using Base = NewSpaceVisitor<YoungGenerationMarkingVisitor<marking_mode>>;

  enum class ObjectVisitationMode {
    kVisitDirectly,
    kPushToWorklist,
  };

  enum class SlotTreatmentMode {
    kReadOnly,
    kReadWrite,
  };

  YoungGenerationMarkingVisitor(
      Heap* heap,
      PretenuringHandler::PretenuringFeedbackMap* local_pretenuring_feedback);

  ~YoungGenerationMarkingVisitor() override;

  YoungGenerationMarkingVisitor(const YoungGenerationMarkingVisitor&) = delete;
  YoungGenerationMarkingVisitor& operator=(
      const YoungGenerationMarkingVisitor&) = delete;

  static constexpr bool EnableConcurrentVisitation() {
    return marking_mode == YoungGenerationMarkingVisitationMode::kConcurrent;
  }

  V8_INLINE void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                               ObjectSlot end) final {
    VisitPointersImpl(host, start, end);
  }
  V8_INLINE void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                               MaybeObjectSlot end) final {
    VisitPointersImpl(host, start, end);
  }
  V8_INLINE void VisitPointer(Tagged<HeapObject> host, ObjectSlot p) final {
    VisitPointersImpl(host, p, p + 1);
  }
  V8_INLINE void VisitPointer(Tagged<HeapObject> host,
                              MaybeObjectSlot p) final {
    VisitPointersImpl(host, p, p + 1);
  }

  // Visitation specializations used for unified heap young gen marking.
  V8_INLINE size_t VisitJSArrayBuffer(Tagged<Map> map,
                                      Tagged<JSArrayBuffer> object,
                                      MaybeObjectSize);
  // Visitation specializations used for collecting pretenuring feedback.
  template <typename T, typename TBodyDescriptor = typename T::BodyDescriptor>
  V8_INLINE size_t VisitJSObjectSubclass(Tagged<Map> map, Tagged<T> object,
                                         MaybeObjectSize);

  V8_INLINE size_t VisitEphemeronHashTable(Tagged<Map> map,
                                           Tagged<EphemeronHashTable> table,
                                           MaybeObjectSize);

#ifdef V8_COMPRESS_POINTERS
  V8_INLINE void VisitExternalPointer(Tagged<HeapObject> host,
                                      ExternalPointerSlot slot) final;
#endif  // V8_COMPRESS_POINTERS
  V8_INLINE void VisitCppHeapPointer(Tagged<HeapObject> host,
                                     CppHeapPointerSlot slot) override;

  template <ObjectVisitationMode visitation_mode,
            SlotTreatmentMode slot_treatment_mode, typename TSlot>
  V8_INLINE bool VisitObjectViaSlot(TSlot slot);

  template <typename TSlot>
  V8_INLINE bool VisitObjectViaSlotInRememberedSet(TSlot slot);

  MarkingWorklists::Local& marking_worklists_local() {
    return marking_worklists_local_;
  }

  V8_INLINE void IncrementLiveBytesCached(MutablePageMetadata* chunk,
                                          intptr_t by);

  void PublishWorklists() {
    marking_worklists_local_.Publish();
    ephemeron_table_list_local_.Publish();
  }

  V8_INLINE static constexpr bool CanEncounterFillerOrFreeSpace() {
    return false;
  }

 private:
  bool TryMark(Tagged<HeapObject> obj) {
    return MarkBit::From(obj).Set<AccessMode::ATOMIC>();
  }

  template <typename TSlot>
  V8_INLINE void VisitPointersImpl(Tagged<HeapObject> host, TSlot start,
                                   TSlot end);

#ifdef V8_MINORMS_STRING_SHORTCUTTING
  V8_INLINE bool ShortCutStrings(HeapObjectSlot slot,
                                 Tagged<HeapObject>* heap_object);
#endif  // V8_MINORMS_STRING_SHORTCUTTING

  static constexpr size_t kNumEntries = 128;
  static constexpr size_t kEntriesMask = kNumEntries - 1;
  // Fixed-size hashmap that caches live bytes. Hashmap entries are evicted to
  // the global counters on collision.
  std::array<std::pair<MutablePageMetadata*, size_t>, kNumEntries>
      live_bytes_data_;

  Isolate* const isolate_;
  MarkingWorklists::Local marking_worklists_local_;
  EphemeronRememberedSet::TableList::Local ephemeron_table_list_local_;
  PretenuringHandler* const pretenuring_handler_;
  PretenuringHandler::PretenuringFeedbackMap* const local_pretenuring_feedback_;
  const bool shortcut_strings_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_YOUNG_GENERATION_MARKING_VISITOR_H_
