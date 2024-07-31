// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_METRICS_H_
#define V8_METRICS_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "v8-internal.h"      // NOLINT(build/include_directory)
#include "v8-local-handle.h"  // NOLINT(build/include_directory)
#include "v8config.h"         // NOLINT(build/include_directory)

namespace v8 {

class Context;
class Isolate;

namespace metrics {

struct GarbageCollectionPhases {
  int64_t total_wall_clock_duration_in_us = -1;
  int64_t compact_wall_clock_duration_in_us = -1;
  int64_t mark_wall_clock_duration_in_us = -1;
  int64_t sweep_wall_clock_duration_in_us = -1;
  int64_t weak_wall_clock_duration_in_us = -1;
};

struct GarbageCollectionSizes {
  int64_t bytes_before = -1;
  int64_t bytes_after = -1;
  int64_t bytes_freed = -1;
};

struct GarbageCollectionFullCycle {
  int reason = -1;
  GarbageCollectionPhases total;
  GarbageCollectionPhases total_cpp;
  GarbageCollectionPhases main_thread;
  GarbageCollectionPhases main_thread_cpp;
  GarbageCollectionPhases main_thread_atomic;
  GarbageCollectionPhases main_thread_atomic_cpp;
  GarbageCollectionPhases main_thread_incremental;
  GarbageCollectionPhases main_thread_incremental_cpp;
  GarbageCollectionSizes objects;
  GarbageCollectionSizes objects_cpp;
  GarbageCollectionSizes memory;
  GarbageCollectionSizes memory_cpp;
  double collection_rate_in_percent = -1.0;
  double collection_rate_cpp_in_percent = -1.0;
  double efficiency_in_bytes_per_us = -1.0;
  double efficiency_cpp_in_bytes_per_us = -1.0;
  double main_thread_efficiency_in_bytes_per_us = -1.0;
  double main_thread_efficiency_cpp_in_bytes_per_us = -1.0;
  int64_t incremental_marking_start_stop_wall_clock_duration_in_us = -1;
};

struct GarbageCollectionFullMainThreadIncrementalMark {
  int64_t wall_clock_duration_in_us = -1;
  int64_t cpp_wall_clock_duration_in_us = -1;
};

struct GarbageCollectionFullMainThreadIncrementalSweep {
  int64_t wall_clock_duration_in_us = -1;
  int64_t cpp_wall_clock_duration_in_us = -1;
};

template <typename EventType>
struct GarbageCollectionBatchedEvents {
  std::vector<EventType> events;
};

using GarbageCollectionFullMainThreadBatchedIncrementalMark =
    GarbageCollectionBatchedEvents<
        GarbageCollectionFullMainThreadIncrementalMark>;
using GarbageCollectionFullMainThreadBatchedIncrementalSweep =
    GarbageCollectionBatchedEvents<
        GarbageCollectionFullMainThreadIncrementalSweep>;

struct GarbageCollectionYoungCycle {
  int reason = -1;
  int64_t total_wall_clock_duration_in_us = -1;
  int64_t main_thread_wall_clock_duration_in_us = -1;
  double collection_rate_in_percent = -1.0;
  double efficiency_in_bytes_per_us = -1.0;
  double main_thread_efficiency_in_bytes_per_us = -1.0;
#if defined(CPPGC_YOUNG_GENERATION)
  GarbageCollectionPhases total_cpp;
  GarbageCollectionSizes objects_cpp;
  GarbageCollectionSizes memory_cpp;
  double collection_rate_cpp_in_percent = -1.0;
  double efficiency_cpp_in_bytes_per_us = -1.0;
  double main_thread_efficiency_cpp_in_bytes_per_us = -1.0;
#endif  // defined(CPPGC_YOUNG_GENERATION)
};

struct WasmModuleDecoded {
  WasmModuleDecoded() = default;
  WasmModuleDecoded(bool async, bool streamed, bool success,
                    size_t module_size_in_bytes, size_t function_count,
                    int64_t wall_clock_duration_in_us)
      : async(async),
        streamed(streamed),
        success(success),
        module_size_in_bytes(module_size_in_bytes),
        function_count(function_count),
        wall_clock_duration_in_us(wall_clock_duration_in_us) {}

  bool async = false;
  bool streamed = false;
  bool success = false;
  size_t module_size_in_bytes = 0;
  size_t function_count = 0;
  int64_t wall_clock_duration_in_us = -1;
};

struct WasmModuleCompiled {
  WasmModuleCompiled() = default;

  WasmModuleCompiled(bool async, bool streamed, bool cached, bool deserialized,
                     bool lazy, bool success, size_t code_size_in_bytes,
                     size_t liftoff_bailout_count,
                     int64_t wall_clock_duration_in_us)
      : async(async),
        streamed(streamed),
        cached(cached),
        deserialized(deserialized),
        lazy(lazy),
        success(success),
        code_size_in_bytes(code_size_in_bytes),
        liftoff_bailout_count(liftoff_bailout_count),
        wall_clock_duration_in_us(wall_clock_duration_in_us) {}

  bool async = false;
  bool streamed = false;
  bool cached = false;
  bool deserialized = false;
  bool lazy = false;
  bool success = false;
  size_t code_size_in_bytes = 0;
  size_t liftoff_bailout_count = 0;
  int64_t wall_clock_duration_in_us = -1;
};

struct WasmModuleInstantiated {
  bool async = false;
  bool success = false;
  size_t imported_function_count = 0;
  int64_t wall_clock_duration_in_us = -1;
};

struct WasmModulesPerIsolate {
  size_t count = 0;
};

/**
 * This class serves as a base class for recording event-based metrics in V8.
 * There a two kinds of metrics, those which are expected to be thread-safe and
 * whose implementation is required to fulfill this requirement and those whose
 * implementation does not have that requirement and only needs to be
 * executable on the main thread. If such an event is triggered from a
 * background thread, it will be delayed and executed by the foreground task
 * runner.
 *
 * The embedder is expected to call v8::Isolate::SetMetricsRecorder()
 * providing its implementation and have the virtual methods overwritten
 * for the events it cares about.
 */
class V8_EXPORT Recorder {
 public:
  // A unique identifier for a context in this Isolate.
  // It is guaranteed to not be reused throughout the lifetime of the Isolate.
  class ContextId {
   public:
    ContextId() : id_(kEmptyId) {}

    bool IsEmpty() const { return id_ == kEmptyId; }
    static const ContextId Empty() { return ContextId{kEmptyId}; }

    bool operator==(const ContextId& other) const { return id_ == other.id_; }
    bool operator!=(const ContextId& other) const { return id_ != other.id_; }

   private:
    friend class ::v8::Context;
    friend class ::v8::internal::Isolate;

    explicit ContextId(uintptr_t id) : id_(id) {}

    static constexpr uintptr_t kEmptyId = 0;
    uintptr_t id_;
  };

  virtual ~Recorder() = default;

  // Main thread events. Those are only triggered on the main thread, and hence
  // can access the context.
#define ADD_MAIN_THREAD_EVENT(E) \
  virtual void AddMainThreadEvent(const E&, ContextId) {}
  ADD_MAIN_THREAD_EVENT(GarbageCollectionFullCycle)
  ADD_MAIN_THREAD_EVENT(GarbageCollectionFullMainThreadIncrementalMark)
  ADD_MAIN_THREAD_EVENT(GarbageCollectionFullMainThreadBatchedIncrementalMark)
  ADD_MAIN_THREAD_EVENT(GarbageCollectionFullMainThreadIncrementalSweep)
  ADD_MAIN_THREAD_EVENT(GarbageCollectionFullMainThreadBatchedIncrementalSweep)
  ADD_MAIN_THREAD_EVENT(GarbageCollectionYoungCycle)
  ADD_MAIN_THREAD_EVENT(WasmModuleDecoded)
  ADD_MAIN_THREAD_EVENT(WasmModuleCompiled)
  ADD_MAIN_THREAD_EVENT(WasmModuleInstantiated)
#undef ADD_MAIN_THREAD_EVENT

  // Thread-safe events are not allowed to access the context and therefore do
  // not carry a context ID with them. These IDs can be generated using
  // Recorder::GetContextId() and the ID will be valid throughout the lifetime
  // of the isolate. It is not guaranteed that the ID will still resolve to
  // a valid context using Recorder::GetContext() at the time the metric is
  // recorded. In this case, an empty handle will be returned.
#define ADD_THREAD_SAFE_EVENT(E) \
  virtual void AddThreadSafeEvent(const E&) {}
  ADD_THREAD_SAFE_EVENT(WasmModulesPerIsolate)
#undef ADD_THREAD_SAFE_EVENT

  virtual void NotifyIsolateDisposal() {}

  // Return the context with the given id or an empty handle if the context
  // was already garbage collected.
  static MaybeLocal<Context> GetContext(Isolate* isolate, ContextId id);
  // Return the unique id corresponding to the given context.
  static ContextId GetContextId(Local<Context> context);
};

/**
 * Experimental API intended for the LongTasks UKM (crbug.com/1173527).
 * The Reset() method should be called at the start of a potential
 * long task. The Get() method returns durations of V8 work that
 * happened during the task.
 *
 * This API is experimental and may be removed/changed in the future.
 */
struct V8_EXPORT LongTaskStats {
  /**
   * Resets durations of V8 work for the new task.
   */
  V8_INLINE static void Reset(Isolate* isolate) {
    v8::internal::Internals::IncrementLongTasksStatsCounter(isolate);
  }

  /**
   * Returns durations of V8 work that happened since the last Reset().
   */
  static LongTaskStats Get(Isolate* isolate);

  int64_t gc_full_atomic_wall_clock_duration_us = 0;
  int64_t gc_full_incremental_wall_clock_duration_us = 0;
  int64_t gc_young_wall_clock_duration_us = 0;
  // Only collected with --slow-histograms
  int64_t v8_execute_us = 0;
};

}  // namespace metrics
}  // namespace v8

#endif  // V8_METRICS_H_
