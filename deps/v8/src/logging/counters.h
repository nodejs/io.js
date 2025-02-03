// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LOGGING_COUNTERS_H_
#define V8_LOGGING_COUNTERS_H_

#include <memory>

#include "include/v8-callbacks.h"
#include "src/base/atomic-utils.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/platform/time.h"
#include "src/common/globals.h"
#include "src/logging/counters-definitions.h"
#include "src/logging/runtime-call-stats.h"
#include "src/objects/code-kind.h"
#include "src/objects/objects.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

// StatsCounters is an interface for plugging into external
// counters for monitoring.  Counters can be looked up and
// manipulated by name.

class Counters;
class Isolate;

class StatsTable {
 public:
  StatsTable(const StatsTable&) = delete;
  StatsTable& operator=(const StatsTable&) = delete;

  // Register an application-defined function for recording
  // subsequent counter statistics.
  void SetCounterFunction(CounterLookupCallback f);

  // Register an application-defined function to create histograms for
  // recording subsequent histogram samples.
  void SetCreateHistogramFunction(CreateHistogramCallback f) {
    create_histogram_function_ = f;
  }

  // Register an application-defined function to add a sample
  // to a histogram created with CreateHistogram function.
  void SetAddHistogramSampleFunction(AddHistogramSampleCallback f) {
    add_histogram_sample_function_ = f;
  }

  bool HasCounterFunction() const { return lookup_function_ != nullptr; }

  // Lookup the location of a counter by name.  If the lookup
  // is successful, returns a non-nullptr pointer for writing the
  // value of the counter.  Each thread calling this function
  // may receive a different location to store it's counter.
  // The return value must not be cached and re-used across
  // threads, although a single thread is free to cache it.
  int* FindLocation(const char* name) {
    if (!lookup_function_) return nullptr;
    return lookup_function_(name);
  }

  // Create a histogram by name. If the create is successful,
  // returns a non-nullptr pointer for use with AddHistogramSample
  // function. min and max define the expected minimum and maximum
  // sample values. buckets is the maximum number of buckets
  // that the samples will be grouped into.
  void* CreateHistogram(const char* name, int min, int max, size_t buckets) {
    if (!create_histogram_function_) return nullptr;
    return create_histogram_function_(name, min, max, buckets);
  }

  // Add a sample to a histogram created with the CreateHistogram
  // function.
  void AddHistogramSample(void* histogram, int sample) {
    if (!add_histogram_sample_function_) return;
    return add_histogram_sample_function_(histogram, sample);
  }

 private:
  friend class Counters;

  explicit StatsTable(Counters* counters);

  CounterLookupCallback lookup_function_;
  CreateHistogramCallback create_histogram_function_;
  AddHistogramSampleCallback add_histogram_sample_function_;
};

// StatsCounters are dynamically created values which can be tracked in the
// StatsTable. They are designed to be lightweight to create and easy to use.
//
// Internally, a counter represents a value in a row of a StatsTable.
// The row has a 32bit value for each process/thread in the table and also
// a name (stored in the table metadata). Since the storage location can be
// thread-specific, this class cannot be shared across threads.
// This class is thread-safe.
class StatsCounter {
 public:
  void Set(int value) { GetPtr()->store(value, std::memory_order_relaxed); }
  int Get() { return GetPtr()->load(); }

  void Increment(int value = 1) {
    GetPtr()->fetch_add(value, std::memory_order_relaxed);
  }

  void Decrement(int value = 1) {
    GetPtr()->fetch_sub(value, std::memory_order_relaxed);
  }

  // Returns true if this counter is enabled (a lookup function was provided and
  // it returned a non-null pointer).
  V8_EXPORT_PRIVATE bool Enabled();

  // Get the internal pointer to the counter. This is used
  // by the code generator to emit code that manipulates a
  // given counter without calling the runtime system.
  std::atomic<int>* GetInternalPointer() { return GetPtr(); }

 private:
  friend class Counters;
  friend class CountersInitializer;
  friend class StatsCounterResetter;

  void Initialize(const char* name, Counters* counters) {
    DCHECK_NULL(counters_);
    DCHECK_NOT_NULL(counters);
    // Counter names always start with "c:V8.".
    DCHECK_EQ(0, memcmp(name, "c:V8.", 5));
    counters_ = counters;
    name_ = name;
  }

  V8_NOINLINE V8_EXPORT_PRIVATE std::atomic<int>* SetupPtrFromStatsTable();

  // Reset the cached internal pointer.
  void Reset() { ptr_.store(nullptr, std::memory_order_relaxed); }

  // Returns the cached address of this counter location.
  std::atomic<int>* GetPtr() {
    auto* ptr = ptr_.load(std::memory_order_acquire);
    if (V8_LIKELY(ptr)) return ptr;
    return SetupPtrFromStatsTable();
  }

  Counters* counters_ = nullptr;
  const char* name_ = nullptr;
  // A pointer to an atomic, set atomically in {GetPtr}.
  std::atomic<std::atomic<int>*> ptr_{nullptr};
};

// A Histogram represents a dynamically created histogram in the
// StatsTable.  Note: This class is thread safe.
class Histogram {
 public:
  // Add a single sample to this histogram.
  V8_EXPORT_PRIVATE void AddSample(int sample);

  // Returns true if this histogram is enabled.
  bool Enabled() { return histogram_ != nullptr; }

  const char* name() const { return name_; }

  int min() const { return min_; }
  int max() const { return max_; }
  int num_buckets() const { return num_buckets_; }

  // Asserts that |expected_counters| are the same as the Counters this
  // Histogram reports to.
  void AssertReportsToCounters(Counters* expected_counters) {
    DCHECK_EQ(counters_, expected_counters);
  }

 protected:
  Histogram() = default;
  Histogram(const Histogram&) = delete;
  Histogram& operator=(const Histogram&) = delete;

  void Initialize(const char* name, int min, int max, int num_buckets,
                  Counters* counters) {
    name_ = name;
    min_ = min;
    max_ = max;
    num_buckets_ = num_buckets;
    histogram_ = nullptr;
    counters_ = counters;
    DCHECK_NOT_NULL(counters_);
  }

  Counters* counters() const { return counters_; }

  // Reset the cached internal pointer to nullptr; the histogram will be
  // created lazily, the first time it is needed.
  void Reset() { histogram_ = nullptr; }

  // Lazily create the histogram, if it has not been created yet.
  void EnsureCreated(bool create_new = true) {
    if (create_new && histogram_.load(std::memory_order_acquire) == nullptr) {
      base::MutexGuard Guard(&mutex_);
      if (histogram_.load(std::memory_order_relaxed) == nullptr)
        histogram_.store(CreateHistogram(), std::memory_order_release);
    }
  }

 private:
  friend class Counters;
  friend class CountersInitializer;
  friend class HistogramResetter;

  V8_EXPORT_PRIVATE void* CreateHistogram() const;

  const char* name_;
  int min_;
  int max_;
  int num_buckets_;
  std::atomic<void*> histogram_;
  Counters* counters_;
  base::Mutex mutex_;
};

// Dummy classes for better visiting.

class PercentageHistogram : public Histogram {};
class LegacyMemoryHistogram : public Histogram {};

enum class TimedHistogramResolution { MILLISECOND, MICROSECOND };

// A thread safe histogram timer.
class TimedHistogram : public Histogram {
 public:
  // Records a TimeDelta::Max() result. Useful to record percentage of tasks
  // that never got to run in a given scenario. Log if isolate non-null.
  void RecordAbandon(base::ElapsedTimer* timer, Isolate* isolate);

  // Add a single sample to this histogram.
  V8_EXPORT_PRIVATE void AddTimedSample(base::TimeDelta sample);

#ifdef DEBUG
  // Ensures that we don't have nested timers for TimedHistogram per thread, use
  // NestedTimedHistogram which correctly pause and resume timers.
  // This method assumes that each timer is alternating between stopped and
  // started on a single thread. Multiple timers can be active on different
  // threads.
  bool ToggleRunningState(bool expected_is_running) const;
#endif  // DEBUG

 protected:
  void Stop(base::ElapsedTimer* timer);
  void LogStart(Isolate* isolate);
  void LogEnd(Isolate* isolate);

  friend class Counters;
  friend class CountersInitializer;

  TimedHistogramResolution resolution_;

  TimedHistogram() = default;
  TimedHistogram(const TimedHistogram&) = delete;
  TimedHistogram& operator=(const TimedHistogram&) = delete;

  void Initialize(const char* name, int min, int max,
                  TimedHistogramResolution resolution, int num_buckets,
                  Counters* counters) {
    Histogram::Initialize(name, min, max, num_buckets, counters);
    resolution_ = resolution;
  }
};

class NestedTimedHistogramScope;
class PauseNestedTimedHistogramScope;

// For use with the NestedTimedHistogramScope. 'Nested' here means that scopes
// may have nested lifetimes while still correctly accounting for time, e.g.:
//
// void f() {
//   NestedTimedHistogramScope timer(...);
//   ...
//   f();  // Recursive call.
// }
class NestedTimedHistogram : public TimedHistogram {
 public:
  // Note: public for testing purposes only.
  NestedTimedHistogram(const char* name, int min, int max,
                       TimedHistogramResolution resolution, int num_buckets,
                       Counters* counters)
      : NestedTimedHistogram() {
    Initialize(name, min, max, resolution, num_buckets, counters);
  }

 private:
  friend class Counters;
  friend class NestedTimedHistogramScope;
  friend class PauseNestedTimedHistogramScope;

  inline NestedTimedHistogramScope* Enter(NestedTimedHistogramScope* next) {
    NestedTimedHistogramScope* previous = current_;
    current_ = next;
    return previous;
  }

  inline void Leave(NestedTimedHistogramScope* previous) {
    current_ = previous;
  }

  NestedTimedHistogramScope* current_ = nullptr;

  NestedTimedHistogram() = default;
  NestedTimedHistogram(const NestedTimedHistogram&) = delete;
  NestedTimedHistogram& operator=(const NestedTimedHistogram&) = delete;
};

// A histogram timer that can aggregate events within a larger scope.
//
// Intended use of this timer is to have an outer (aggregating) and an inner
// (to be aggregated) scope, where the inner scope measure the time of events,
// and all those inner scope measurements will be summed up by the outer scope.
// An example use might be to aggregate the time spent in lazy compilation
// while running a script.
//
// Helpers:
// - AggregatingHistogramTimerScope, the "outer" scope within which
//     times will be summed up.
// - AggregatedHistogramTimerScope, the "inner" scope which defines the
//     events to be timed.
class AggregatableHistogramTimer : public Histogram {
 public:
  // Start/stop the "outer" scope.
  void Start() { time_ = base::TimeDelta(); }
  void Stop() {
    if (time_ != base::TimeDelta()) {
      // Only add non-zero samples, since zero samples represent situations
      // where there were no aggregated samples added.
      AddSample(static_cast<int>(time_.InMicroseconds()));
    }
  }

  // Add a time value ("inner" scope).
  void Add(base::TimeDelta other) { time_ += other; }

 private:
  friend class Counters;

  AggregatableHistogramTimer() = default;
  AggregatableHistogramTimer(const AggregatableHistogramTimer&) = delete;
  AggregatableHistogramTimer& operator=(const AggregatableHistogramTimer&) =
      delete;

  base::TimeDelta time_;
};

// A helper class for use with AggregatableHistogramTimer. This is the
// // outer-most timer scope used with an AggregatableHistogramTimer. It will
// // aggregate the information from the inner AggregatedHistogramTimerScope.
class V8_NODISCARD AggregatingHistogramTimerScope {
 public:
  explicit AggregatingHistogramTimerScope(AggregatableHistogramTimer* histogram)
      : histogram_(histogram) {
    histogram_->Start();
  }
  ~AggregatingHistogramTimerScope() { histogram_->Stop(); }

 private:
  AggregatableHistogramTimer* histogram_;
};

// A helper class for use with AggregatableHistogramTimer, the "inner" scope
// // which defines the events to be timed.
class V8_NODISCARD AggregatedHistogramTimerScope {
 public:
  explicit AggregatedHistogramTimerScope(AggregatableHistogramTimer* histogram)
      : histogram_(histogram) {
    timer_.Start();
  }
  ~AggregatedHistogramTimerScope() { histogram_->Add(timer_.Elapsed()); }

 private:
  base::ElapsedTimer timer_;
  AggregatableHistogramTimer* histogram_;
};

// AggretatedMemoryHistogram collects (time, value) sample pairs and turns
// them into time-uniform samples for the backing historgram, such that the
// backing histogram receives one sample every T ms, where the T is controlled
// by the v8_flags.histogram_interval.
//
// More formally: let F be a real-valued function that maps time to sample
// values. We define F as a linear interpolation between adjacent samples. For
// each time interval [x; x + T) the backing histogram gets one sample value
// that is the average of F(t) in the interval.
template <typename Histogram>
class AggregatedMemoryHistogram {
 public:
  // Note: public for testing purposes only.
  explicit AggregatedMemoryHistogram(Histogram* backing_histogram)
      : AggregatedMemoryHistogram() {
    backing_histogram_ = backing_histogram;
  }

  // Invariants that hold before and after AddSample if
  // is_initialized_ is true:
  //
  // 1) For we processed samples that came in before start_ms_ and sent the
  // corresponding aggregated samples to backing histogram.
  // 2) (last_ms_, last_value_) is the last received sample.
  // 3) last_ms_ < start_ms_ + v8_flags.histogram_interval.
  // 4) aggregate_value_ is the average of the function that is constructed by
  // linearly interpolating samples received between start_ms_ and last_ms_.
  void AddSample(double current_ms, double current_value);

 private:
  friend class Counters;

  AggregatedMemoryHistogram()
      : is_initialized_(false),
        start_ms_(0.0),
        last_ms_(0.0),
        aggregate_value_(0.0),
        last_value_(0.0),
        backing_histogram_(nullptr) {}
  double Aggregate(double current_ms, double current_value);

  bool is_initialized_;
  double start_ms_;
  double last_ms_;
  double aggregate_value_;
  double last_value_;
  Histogram* backing_histogram_;
};

template <typename Histogram>
void AggregatedMemoryHistogram<Histogram>::AddSample(double current_ms,
                                                     double current_value) {
  if (!is_initialized_) {
    aggregate_value_ = current_value;
    start_ms_ = current_ms;
    last_value_ = current_value;
    last_ms_ = current_ms;
    is_initialized_ = true;
  } else {
    const double kEpsilon = 1e-6;
    const int kMaxSamples = 1000;
    if (current_ms < last_ms_ + kEpsilon) {
      // Two samples have the same time, remember the last one.
      last_value_ = current_value;
    } else {
      double sample_interval_ms = v8_flags.histogram_interval;
      double end_ms = start_ms_ + sample_interval_ms;
      if (end_ms <= current_ms + kEpsilon) {
        // Linearly interpolate between the last_ms_ and the current_ms.
        double slope = (current_value - last_value_) / (current_ms - last_ms_);
        int i;
        // Send aggregated samples to the backing histogram from the start_ms
        // to the current_ms.
        for (i = 0; i < kMaxSamples && end_ms <= current_ms + kEpsilon; i++) {
          double end_value = last_value_ + (end_ms - last_ms_) * slope;
          double sample_value;
          if (i == 0) {
            // Take aggregate_value_ into account.
            sample_value = Aggregate(end_ms, end_value);
          } else {
            // There is no aggregate_value_ for i > 0.
            sample_value = (last_value_ + end_value) / 2;
          }
          backing_histogram_->AddSample(static_cast<int>(sample_value + 0.5));
          last_value_ = end_value;
          last_ms_ = end_ms;
          end_ms += sample_interval_ms;
        }
        if (i == kMaxSamples) {
          // We hit the sample limit, ignore the remaining samples.
          aggregate_value_ = current_value;
          start_ms_ = current_ms;
        } else {
          aggregate_value_ = last_value_;
          start_ms_ = last_ms_;
        }
      }
      aggregate_value_ = current_ms > start_ms_ + kEpsilon
                             ? Aggregate(current_ms, current_value)
                             : aggregate_value_;
      last_value_ = current_value;
      last_ms_ = current_ms;
    }
  }
}

template <typename Histogram>
double AggregatedMemoryHistogram<Histogram>::Aggregate(double current_ms,
                                                       double current_value) {
  double interval_ms = current_ms - start_ms_;
  double value = (current_value + last_value_) / 2;
  // The aggregate_value_ is the average for [start_ms_; last_ms_].
  // The value is the average for [last_ms_; current_ms].
  // Return the weighted average of the aggregate_value_ and the value.
  return aggregate_value_ * ((last_ms_ - start_ms_) / interval_ms) +
         value * ((current_ms - last_ms_) / interval_ms);
}

// This file contains all the v8 counters that are in use.
class Counters : public std::enable_shared_from_this<Counters> {
 public:
  explicit Counters(Isolate* isolate);

  // Register an application-defined function for recording
  // subsequent counter statistics. Note: Must be called on the main
  // thread.
  void ResetCounterFunction(CounterLookupCallback f);

  // Register an application-defined function to create histograms for
  // recording subsequent histogram samples. Note: Must be called on
  // the main thread.
  void ResetCreateHistogramFunction(CreateHistogramCallback f);

  // Register an application-defined function to add a sample
  // to a histogram. Will be used in all subsequent sample additions.
  // Note: Must be called on the main thread.
  void SetAddHistogramSampleFunction(AddHistogramSampleCallback f) {
    stats_table_.SetAddHistogramSampleFunction(f);
  }

#define HR(name, caption, min, max, num_buckets) \
  Histogram* name() {                            \
    name##_.EnsureCreated();                     \
    return &name##_;                             \
  }
  HISTOGRAM_RANGE_LIST(HR)
#undef HR

#if V8_ENABLE_DRUMBRAKE
#define HR(name, caption, min, max, num_buckets)     \
  Histogram* name() {                                \
    name##_.EnsureCreated(v8_flags.slow_histograms); \
    return &name##_;                                 \
  }
  HISTOGRAM_RANGE_LIST_SLOW(HR)
#undef HR
#endif  // V8_ENABLE_DRUMBRAKE

#define HT(name, caption, max, res) \
  NestedTimedHistogram* name() {    \
    name##_.EnsureCreated();        \
    return &name##_;                \
  }
  NESTED_TIMED_HISTOGRAM_LIST(HT)
#undef HT

#define HT(name, caption, max, res)                  \
  NestedTimedHistogram* name() {                     \
    name##_.EnsureCreated(v8_flags.slow_histograms); \
    return &name##_;                                 \
  }
  NESTED_TIMED_HISTOGRAM_LIST_SLOW(HT)
#undef HT

#define HT(name, caption, max, res) \
  TimedHistogram* name() {          \
    name##_.EnsureCreated();        \
    return &name##_;                \
  }
  TIMED_HISTOGRAM_LIST(HT)
#undef HT

#define AHT(name, caption)             \
  AggregatableHistogramTimer* name() { \
    name##_.EnsureCreated();           \
    return &name##_;                   \
  }
  AGGREGATABLE_HISTOGRAM_TIMER_LIST(AHT)
#undef AHT

#define HP(name, caption)       \
  PercentageHistogram* name() { \
    name##_.EnsureCreated();    \
    return &name##_;            \
  }
  HISTOGRAM_PERCENTAGE_LIST(HP)
#undef HP

#define HM(name, caption)         \
  LegacyMemoryHistogram* name() { \
    name##_.EnsureCreated();      \
    return &name##_;              \
  }
  HISTOGRAM_LEGACY_MEMORY_LIST(HM)
#undef HM

#define SC(name, caption) \
  StatsCounter* name() { return &name##_; }
  STATS_COUNTER_LIST(SC)
  STATS_COUNTER_NATIVE_CODE_LIST(SC)
#undef SC

  // clang-format off
  enum Id {
#define RATE_ID(name, caption, max, res) k_##name,
    NESTED_TIMED_HISTOGRAM_LIST(RATE_ID)
    NESTED_TIMED_HISTOGRAM_LIST_SLOW(RATE_ID)
    TIMED_HISTOGRAM_LIST(RATE_ID)
#undef RATE_ID
#define AGGREGATABLE_ID(name, caption) k_##name,
    AGGREGATABLE_HISTOGRAM_TIMER_LIST(AGGREGATABLE_ID)
#undef AGGREGATABLE_ID
#define PERCENTAGE_ID(name, caption) k_##name,
    HISTOGRAM_PERCENTAGE_LIST(PERCENTAGE_ID)
#undef PERCENTAGE_ID
#define MEMORY_ID(name, caption) k_##name,
    HISTOGRAM_LEGACY_MEMORY_LIST(MEMORY_ID)
#undef MEMORY_ID
#define COUNTER_ID(name, caption) k_##name,
    STATS_COUNTER_LIST(COUNTER_ID)
    STATS_COUNTER_NATIVE_CODE_LIST(COUNTER_ID)
#undef COUNTER_ID
#define COUNTER_ID(name) kCountOf##name, kSizeOf##name,
    INSTANCE_TYPE_LIST(COUNTER_ID)
#undef COUNTER_ID
#define COUNTER_ID(name) kCountOfCODE_TYPE_##name, \
    kSizeOfCODE_TYPE_##name,
    CODE_KIND_LIST(COUNTER_ID)
#undef COUNTER_ID
    stats_counter_count
  };
  // clang-format on

#ifdef V8_RUNTIME_CALL_STATS
  RuntimeCallStats* runtime_call_stats() { return &runtime_call_stats_; }

  WorkerThreadRuntimeCallStats* worker_thread_runtime_call_stats() {
    return &worker_thread_runtime_call_stats_;
  }
#else   // V8_RUNTIME_CALL_STATS
  RuntimeCallStats* runtime_call_stats() { return nullptr; }

  WorkerThreadRuntimeCallStats* worker_thread_runtime_call_stats() {
    return nullptr;
  }
#endif  // V8_RUNTIME_CALL_STATS

 private:
  friend class CountersVisitor;
  friend class Histogram;
  friend class NestedTimedHistogramScope;
  friend class StatsCounter;
  friend class StatsTable;

  int* FindLocation(const char* name) {
    return stats_table_.FindLocation(name);
  }

  void* CreateHistogram(const char* name, int min, int max, size_t buckets) {
    return stats_table_.CreateHistogram(name, min, max, buckets);
  }

  void AddHistogramSample(void* histogram, int sample) {
    stats_table_.AddHistogramSample(histogram, sample);
  }

  Isolate* isolate() { return isolate_; }

#define HR(name, caption, min, max, num_buckets) Histogram name##_;
  HISTOGRAM_RANGE_LIST(HR)
#if V8_ENABLE_DRUMBRAKE
  HISTOGRAM_RANGE_LIST_SLOW(HR)
#endif  // V8_ENABLE_DRUMBRAKE
#undef HR

#define HT(name, caption, max, res) NestedTimedHistogram name##_;
  NESTED_TIMED_HISTOGRAM_LIST(HT)
  NESTED_TIMED_HISTOGRAM_LIST_SLOW(HT)
#undef HT

#define HT(name, caption, max, res) TimedHistogram name##_;
  TIMED_HISTOGRAM_LIST(HT)
#undef HT

#define AHT(name, caption) AggregatableHistogramTimer name##_;
  AGGREGATABLE_HISTOGRAM_TIMER_LIST(AHT)
#undef AHT

#define HP(name, caption) PercentageHistogram name##_;
  HISTOGRAM_PERCENTAGE_LIST(HP)
#undef HP

#define HM(name, caption) LegacyMemoryHistogram name##_;
  HISTOGRAM_LEGACY_MEMORY_LIST(HM)
#undef HM

#define SC(name, caption) StatsCounter name##_;
  STATS_COUNTER_LIST(SC)
  STATS_COUNTER_NATIVE_CODE_LIST(SC)
#undef SC

#ifdef V8_RUNTIME_CALL_STATS
  RuntimeCallStats runtime_call_stats_;
  WorkerThreadRuntimeCallStats worker_thread_runtime_call_stats_;
#endif
  Isolate* isolate_;
  StatsTable stats_table_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Counters);
};

class CountersVisitor {
 public:
  explicit CountersVisitor(Counters* counters) : counters_(counters) {}

  void Start();
  Counters* counters() { return counters_; }

 protected:
  virtual void VisitHistograms();
  virtual void VisitStatsCounters();

  virtual void VisitHistogram(Histogram* histogram, const char* caption) {}
  virtual void VisitStatsCounter(StatsCounter* counter, const char* caption) {}

  virtual void Visit(Histogram* histogram, const char* caption, int min,
                     int max, int num_buckets);
  virtual void Visit(TimedHistogram* histogram, const char* caption, int max,
                     TimedHistogramResolution res);
  virtual void Visit(NestedTimedHistogram* histogram, const char* caption,
                     int max, TimedHistogramResolution res);
  virtual void Visit(AggregatableHistogramTimer* histogram,
                     const char* caption);
  virtual void Visit(PercentageHistogram* histogram, const char* caption);
  virtual void Visit(LegacyMemoryHistogram* histogram, const char* caption);
  virtual void Visit(StatsCounter* counter, const char* caption);

 private:
  Counters* counters_;
};

class CountersInitializer : public CountersVisitor {
 public:
  using CountersVisitor::CountersVisitor;

 protected:
  void Visit(Histogram* histogram, const char* caption, int min, int max,
             int num_buckets) final;
  void Visit(TimedHistogram* histogram, const char* caption, int max,
             TimedHistogramResolution res) final;
  void Visit(NestedTimedHistogram* histogram, const char* caption, int max,
             TimedHistogramResolution res) final;
  void Visit(AggregatableHistogramTimer* histogram, const char* caption) final;
  void Visit(PercentageHistogram* histogram, const char* caption) final;
  void Visit(LegacyMemoryHistogram* histogram, const char* caption) final;
  void Visit(StatsCounter* counter, const char* caption) final;
};

class StatsCounterResetter : public CountersVisitor {
 public:
  using CountersVisitor::CountersVisitor;

 protected:
  void VisitHistograms() final {}
  void VisitStatsCounter(StatsCounter* counter, const char* caption) final;
};

class HistogramResetter : public CountersVisitor {
 public:
  using CountersVisitor::CountersVisitor;

 protected:
  void VisitStatsCounters() final {}
  void VisitHistogram(Histogram* histogram, const char* caption) final;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_LOGGING_COUNTERS_H_
