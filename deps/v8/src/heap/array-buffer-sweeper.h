// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ARRAY_BUFFER_SWEEPER_H_
#define V8_HEAP_ARRAY_BUFFER_SWEEPER_H_

#include <memory>

#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/heap/sweeper.h"
#include "src/objects/js-array-buffer.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class ArrayBufferExtension;
class Heap;

// Singly linked-list of ArrayBufferExtensions that stores head and tail of the
// list to allow for concatenation of lists.
struct ArrayBufferList final {
  bool IsEmpty() const;
  size_t ApproximateBytes() const { return bytes_; }
  size_t BytesSlow() const;

  void Append(ArrayBufferExtension* extension);
  void Append(ArrayBufferList* list);

  V8_EXPORT_PRIVATE bool ContainsSlow(ArrayBufferExtension* extension) const;

 private:
  ArrayBufferExtension* head_ = nullptr;
  ArrayBufferExtension* tail_ = nullptr;
  // Bytes are approximate as they may be subtracted eagerly, while the
  // `ArrayBufferExtension` is still in the list. The extension will only be
  // dropped on next sweep.
  size_t bytes_ = 0;

  friend class ArrayBufferSweeper;
};

// The ArrayBufferSweeper iterates and deletes ArrayBufferExtensions
// concurrently to the application.
class ArrayBufferSweeper final {
 public:
  enum class SweepingType { kYoung, kFull };
  enum class TreatAllYoungAsPromoted { kNo, kYes };

  explicit ArrayBufferSweeper(Heap* heap);
  ~ArrayBufferSweeper();

  void RequestSweep(SweepingType sweeping_type,
                    TreatAllYoungAsPromoted treat_all_young_as_promoted);
  void EnsureFinished();

  // Track the given ArrayBufferExtension for the given JSArrayBuffer.
  void Append(Tagged<JSArrayBuffer> object, ArrayBufferExtension* extension);

  // Detaches an ArrayBufferExtension from a JSArrayBuffer.
  void Detach(Tagged<JSArrayBuffer> object, ArrayBufferExtension* extension);

  const ArrayBufferList& young() const { return young_; }
  const ArrayBufferList& old() const { return old_; }

  // Bytes accounted in the young generation. Rebuilt during sweeping.
  size_t YoungBytes() const { return young().ApproximateBytes(); }
  // Bytes accounted in the old generation. Rebuilt during sweeping.
  size_t OldBytes() const { return old().ApproximateBytes(); }

  bool sweeping_in_progress() const { return job_.get(); }

  uint64_t GetTraceIdForFlowEvent(GCTracer::Scope::ScopeId scope_id) const;

 private:
  struct SweepingJob;

  enum class SweepingState { kInProgress, kDone };

  // Finishes sweeping if it is already done.
  void FinishIfDone();

  // Increments external memory counters outside of ArrayBufferSweeper.
  // Increment may trigger GC.
  void IncrementExternalMemoryCounters(size_t bytes);
  void DecrementExternalMemoryCounters(size_t bytes);

  void Prepare(SweepingType type,
               TreatAllYoungAsPromoted treat_all_young_as_promoted,
               uint64_t trace_id);
  void Finalize();

  void ReleaseAll(ArrayBufferList* extension);

  void DoSweep(SweepingType type, ThreadKind thread_kind, uint64_t trace_id);

  Heap* const heap_;
  std::unique_ptr<SweepingJob> job_;
  base::Mutex sweeping_mutex_;
  base::ConditionVariable job_finished_;
  ArrayBufferList young_;
  ArrayBufferList old_;
  Sweeper::LocalSweeper local_sweeper_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ARRAY_BUFFER_SWEEPER_H_
