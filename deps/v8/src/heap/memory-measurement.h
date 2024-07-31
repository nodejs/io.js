// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_MEASUREMENT_H_
#define V8_HEAP_MEMORY_MEASUREMENT_H_

#include <list>
#include <unordered_map>

#include "include/v8-statistics.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/utils/random-number-generator.h"
#include "src/common/globals.h"
#include "src/objects/contexts.h"
#include "src/objects/map.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class Heap;
class NativeContextStats;

class MemoryMeasurement {
 public:
  explicit MemoryMeasurement(Isolate* isolate);

  bool EnqueueRequest(std::unique_ptr<v8::MeasureMemoryDelegate> delegate,
                      v8::MeasureMemoryExecution execution,
                      const std::vector<Handle<NativeContext>> contexts);
  std::vector<Address> StartProcessing();
  void FinishProcessing(const NativeContextStats& stats);

  static std::unique_ptr<v8::MeasureMemoryDelegate> DefaultDelegate(
      Isolate* isolate, Handle<NativeContext> context,
      Handle<JSPromise> promise, v8::MeasureMemoryMode mode);

 private:
  static const int kGCTaskDelayInSeconds = 10;
  struct Request {
    std::unique_ptr<v8::MeasureMemoryDelegate> delegate;
    Handle<WeakFixedArray> contexts;
    std::vector<size_t> sizes;
    size_t shared;
    size_t wasm_code;
    size_t wasm_metadata;
    base::ElapsedTimer timer;
  };
  void ScheduleReportingTask();
  void ReportResults();
  void ScheduleGCTask(v8::MeasureMemoryExecution execution);
  bool IsGCTaskPending(v8::MeasureMemoryExecution execution);
  void SetGCTaskPending(v8::MeasureMemoryExecution execution);
  void SetGCTaskDone(v8::MeasureMemoryExecution execution);
  int NextGCTaskDelayInSeconds();

  std::list<Request> received_;
  std::list<Request> processing_;
  std::list<Request> done_;
  Isolate* isolate_;
  std::shared_ptr<v8::TaskRunner> task_runner_;
  bool reporting_task_pending_ = false;
  bool delayed_gc_task_pending_ = false;
  bool eager_gc_task_pending_ = false;
  base::RandomNumberGenerator random_number_generator_;
};

// Infers the native context for some of the heap objects.
class V8_EXPORT_PRIVATE NativeContextInferrer {
 public:
  // The native_context parameter is both the input and output parameter.
  // It should be initialized to the context that will be used for the object
  // if the inference is not successful. The function performs more work if the
  // context is the shared context.
  V8_INLINE bool Infer(PtrComprCageBase cage_base, Tagged<Map> map,
                       Tagged<HeapObject> object, Address* native_context);

};

// Maintains mapping from native contexts to their sizes.
class V8_EXPORT_PRIVATE NativeContextStats {
 public:
  V8_INLINE void IncrementSize(Address context, Tagged<Map> map,
                               Tagged<HeapObject> object, size_t size);

  size_t Get(Address context) const {
    const auto it = size_by_context_.find(context);
    if (it == size_by_context_.end()) return 0;
    return it->second;
  }
  void Clear();
  void Merge(const NativeContextStats& other);

  bool Empty() const { return size_by_context_.empty(); }

 private:
  V8_INLINE bool HasExternalBytes(Tagged<Map> map);
  void IncrementExternalSize(Address context, Tagged<Map> map,
                             Tagged<HeapObject> object);
  std::unordered_map<Address, size_t> size_by_context_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_MEASUREMENT_H_
