// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_BARRIER_H_
#define V8_HEAP_MARKING_BARRIER_H_

#include <optional>

#include "include/v8-internal.h"
#include "src/base/hashing.h"
#include "src/common/globals.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/mutable-page-metadata.h"

namespace v8 {
namespace internal {

class Heap;
class IncrementalMarking;
class LocalHeap;
class PagedSpace;
class NewSpace;

class MarkingBarrier {
 public:
  explicit MarkingBarrier(LocalHeap*);
  ~MarkingBarrier();

  void Activate(bool is_compacting, MarkingMode marking_mode);
  void Deactivate();
  void PublishIfNeeded();

  void ActivateShared();
  void DeactivateShared();
  void PublishSharedIfNeeded();

  static void ActivateAll(Heap* heap, bool is_compacting);
  static void DeactivateAll(Heap* heap);
  V8_EXPORT_PRIVATE static void PublishAll(Heap* heap);

  static void ActivateYoung(Heap* heap);
  static void DeactivateYoung(Heap* heap);
  V8_EXPORT_PRIVATE static void PublishYoung(Heap* heap);

  template <typename TSlot>
  void Write(Tagged<HeapObject> host, TSlot slot, Tagged<HeapObject> value);
  void Write(Tagged<HeapObject> host, IndirectPointerSlot slot);
  void Write(Tagged<InstructionStream> host, RelocInfo*,
             Tagged<HeapObject> value);
  void Write(Tagged<JSArrayBuffer> host, ArrayBufferExtension*);
  void Write(Tagged<DescriptorArray>, int number_of_own_descriptors);
  // Only usable when there's no valid JS host object for this write, e.g., when
  // value is held alive from a global handle.
  void WriteWithoutHost(Tagged<HeapObject> value);

  inline void MarkValue(Tagged<HeapObject> host, Tagged<HeapObject> value);

  bool is_minor() const { return marking_mode_ == MarkingMode::kMinorMarking; }

  bool is_not_major() const {
    switch (marking_mode_) {
      case MarkingMode::kMajorMarking:
        return false;
      case MarkingMode::kNoMarking:
      case MarkingMode::kMinorMarking:
        return true;
    }
  }

  Heap* heap() const { return heap_; }

#if DEBUG
  void AssertMarkingIsActivated() const;
  void AssertSharedMarkingIsActivated() const;
  bool IsMarked(const Tagged<HeapObject> value) const;
#endif  // DEBUG

 private:
  inline void MarkValueShared(Tagged<HeapObject> value);
  inline void MarkValueLocal(Tagged<HeapObject> value);

  void RecordRelocSlot(Tagged<InstructionStream> host, RelocInfo* rinfo,
                       Tagged<HeapObject> target);

  bool IsCurrentMarkingBarrier(Tagged<HeapObject> verification_candidate);

  template <typename TSlot>
  inline void MarkRange(Tagged<HeapObject> value, TSlot start, TSlot end);

  inline bool IsCompacting(Tagged<HeapObject> object) const;

  bool is_major() const { return marking_mode_ == MarkingMode::kMajorMarking; }

  Isolate* isolate() const;

  Heap* heap_;
  MarkCompactCollector* major_collector_;
  MinorMarkSweepCollector* minor_collector_;
  IncrementalMarking* incremental_marking_;
  std::unique_ptr<MarkingWorklists::Local> current_worklists_;
  std::optional<MarkingWorklists::Local> shared_heap_worklists_;
  MarkingState marking_state_;
  std::unordered_map<MutablePageMetadata*, std::unique_ptr<TypedSlots>,
                     base::hash<MutablePageMetadata*>>
      typed_slots_map_;
  bool is_compacting_ = false;
  bool is_activated_ = false;
  const bool is_main_thread_barrier_;
  const bool uses_shared_heap_;
  const bool is_shared_space_isolate_;
  MarkingMode marking_mode_ = MarkingMode::kNoMarking;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_BARRIER_H_
