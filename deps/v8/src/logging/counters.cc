// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logging/counters.h"

#include "src/base/atomic-utils.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/base/platform/time.h"
#include "src/builtins/builtins-definitions.h"
#include "src/execution/isolate.h"
#include "src/execution/thread-id.h"
#include "src/logging/log-inl.h"
#include "src/logging/log.h"

namespace v8 {
namespace internal {

StatsTable::StatsTable(Counters* counters)
    : lookup_function_(nullptr),
      create_histogram_function_(nullptr),
      add_histogram_sample_function_(nullptr) {}

void StatsTable::SetCounterFunction(CounterLookupCallback f) {
  lookup_function_ = f;
}

namespace {
std::atomic<int> unused_counter_dump{0};
}

bool StatsCounter::Enabled() { return GetPtr() != &unused_counter_dump; }

std::atomic<int>* StatsCounter::SetupPtrFromStatsTable() {
  // {Init} must have been called.
  DCHECK_NOT_NULL(counters_);
  DCHECK_NOT_NULL(name_);
  int* location = counters_->FindLocation(name_);
  std::atomic<int>* ptr =
      location ? base::AsAtomicPtr(location) : &unused_counter_dump;
#ifdef DEBUG
  std::atomic<int>* old_ptr = ptr_.exchange(ptr, std::memory_order_release);
  DCHECK_IMPLIES(old_ptr, old_ptr == ptr);
#else
  ptr_.store(ptr, std::memory_order_release);
#endif
  return ptr;
}

void Histogram::AddSample(int sample) {
  if (Enabled()) {
    counters_->AddHistogramSample(histogram_, sample);
  }
}

void* Histogram::CreateHistogram() const {
  return counters_->CreateHistogram(name_, min_, max_, num_buckets_);
}

void TimedHistogram::Stop(base::ElapsedTimer* timer) {
  DCHECK(Enabled());
  AddTimedSample(timer->Elapsed());
  timer->Stop();
}

void TimedHistogram::AddTimedSample(base::TimeDelta sample) {
  if (Enabled()) {
    int64_t sample_int = resolution_ == TimedHistogramResolution::MICROSECOND
                             ? sample.InMicroseconds()
                             : sample.InMilliseconds();
    AddSample(static_cast<int>(sample_int));
  }
}

void TimedHistogram::RecordAbandon(base::ElapsedTimer* timer,
                                   Isolate* isolate) {
  if (Enabled()) {
    DCHECK(timer->IsStarted());
    timer->Stop();
    int64_t sample = resolution_ == TimedHistogramResolution::MICROSECOND
                         ? base::TimeDelta::Max().InMicroseconds()
                         : base::TimeDelta::Max().InMilliseconds();
    AddSample(static_cast<int>(sample));
  }
  if (isolate != nullptr) {
    V8FileLogger::CallEventLogger(isolate, name(), v8::LogEventStatus::kEnd,
                                  true);
  }
}

#ifdef DEBUG
bool TimedHistogram::ToggleRunningState(bool expect_to_run) const {
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
  static thread_local std::unordered_map<const TimedHistogram*, bool>
      active_timer;
#if __clang__
#pragma clang diagnostic pop
#endif
  bool is_running = active_timer[this];
  DCHECK_NE(is_running, expect_to_run);
  active_timer[this] = !is_running;
  return true;
}
#endif

namespace {
static constexpr int DefaultTimedHistogramNumBuckets = 50;
}

void CountersInitializer::Visit(Histogram* histogram, const char* caption,
                                int min, int max, int num_buckets) {
  histogram->Initialize(caption, min, max, num_buckets, counters());
}

void CountersInitializer::Visit(PercentageHistogram* histogram,
                                const char* caption) {
  histogram->Initialize(caption, 0, 101, 100, counters());
}

void CountersInitializer::Visit(LegacyMemoryHistogram* histogram,
                                const char* caption) {
  // Exponential histogram assigns bucket limits to points
  // p[1], p[2], ... p[n] such that p[i+1] / p[i] = constant.
  // The constant factor is equal to the n-th root of (high / low),
  // where the n is the number of buckets, the low is the lower limit,
  // the high is the upper limit.
  // For n = 50, low = 1000, high = 500000: the factor = 1.13.
  histogram->Initialize(caption, 1000, 500000, 50, counters());
}

void CountersInitializer::Visit(TimedHistogram* histogram, const char* caption,
                                int max, TimedHistogramResolution res) {
  histogram->Initialize(caption, 0, max, res, DefaultTimedHistogramNumBuckets,
                        counters());
}

void CountersInitializer::Visit(NestedTimedHistogram* histogram,
                                const char* caption, int max,
                                TimedHistogramResolution res) {
  histogram->Initialize(caption, 0, max, res, DefaultTimedHistogramNumBuckets,
                        counters());
}

void CountersInitializer::Visit(AggregatableHistogramTimer* histogram,
                                const char* caption) {
  histogram->Initialize(caption, 0, 10000000, DefaultTimedHistogramNumBuckets,
                        counters());
}

void CountersInitializer::Visit(StatsCounter* counter, const char* caption) {
  counter->Initialize(caption, counters());
}

Counters::Counters(Isolate* isolate)
    :
#ifdef V8_RUNTIME_CALL_STATS
      runtime_call_stats_(RuntimeCallStats::kMainIsolateThread),
      worker_thread_runtime_call_stats_(),
#endif
      isolate_(isolate),
      stats_table_(this) {
  CountersInitializer init(this);
  init.Start();
}

void StatsCounterResetter::VisitStatsCounter(StatsCounter* counter,
                                             const char* caption) {
  counter->Reset();
}

void Counters::ResetCounterFunction(CounterLookupCallback f) {
  stats_table_.SetCounterFunction(f);
  StatsCounterResetter resetter(this);
  resetter.Start();
}

void HistogramResetter::VisitHistogram(Histogram* histogram,
                                       const char* caption) {
  histogram->Reset();
}

void Counters::ResetCreateHistogramFunction(CreateHistogramCallback f) {
  stats_table_.SetCreateHistogramFunction(f);
  HistogramResetter resetter(this);
  resetter.Start();
}

void CountersVisitor::Start() {
  VisitStatsCounters();
  VisitHistograms();
}

void CountersVisitor::VisitHistograms() {
#define HR(name, caption, min, max, num_buckets) \
  Visit(&counters()->name##_, #caption, min, max, num_buckets);
  HISTOGRAM_RANGE_LIST(HR)
#undef HR

#define HR(name, caption) Visit(&counters()->name##_, #caption);
  HISTOGRAM_PERCENTAGE_LIST(HR)
#undef HR

#define HR(name, caption) Visit(&counters()->name##_, #caption);
  HISTOGRAM_LEGACY_MEMORY_LIST(HR)
#undef HR

#define HT(name, caption, max, res) \
  Visit(&counters()->name##_, #caption, max, TimedHistogramResolution::res);
  NESTED_TIMED_HISTOGRAM_LIST(HT)
#undef HT

#define HT(name, caption, max, res) \
  Visit(&counters()->name##_, #caption, max, TimedHistogramResolution::res);
  NESTED_TIMED_HISTOGRAM_LIST_SLOW(HT)
#undef HT

#define HT(name, caption, max, res) \
  Visit(&counters()->name##_, #caption, max, TimedHistogramResolution::res);
  TIMED_HISTOGRAM_LIST(HT)
#undef HT

#define AHT(name, caption) Visit(&counters()->name##_, #caption);
  AGGREGATABLE_HISTOGRAM_TIMER_LIST(AHT)
#undef AHT
}

void CountersVisitor::VisitStatsCounters() {
#define SC(name, caption) Visit(&counters()->name##_, "c:" #caption);
  STATS_COUNTER_LIST(SC)
  STATS_COUNTER_NATIVE_CODE_LIST(SC)
#undef SC
}

void CountersVisitor::Visit(Histogram* histogram, const char* caption, int min,
                            int max, int num_buckets) {
  VisitHistogram(histogram, caption);
}
void CountersVisitor::Visit(TimedHistogram* histogram, const char* caption,
                            int max, TimedHistogramResolution res) {
  VisitHistogram(histogram, caption);
}
void CountersVisitor::Visit(NestedTimedHistogram* histogram,
                            const char* caption, int max,
                            TimedHistogramResolution res) {
  VisitHistogram(histogram, caption);
}

void CountersVisitor::Visit(AggregatableHistogramTimer* histogram,
                            const char* caption) {
  VisitHistogram(histogram, caption);
}

void CountersVisitor::Visit(PercentageHistogram* histogram,
                            const char* caption) {
  VisitHistogram(histogram, caption);
}

void CountersVisitor::Visit(LegacyMemoryHistogram* histogram,
                            const char* caption) {
  VisitHistogram(histogram, caption);
}

void CountersVisitor::Visit(StatsCounter* counter, const char* caption) {
  VisitStatsCounter(counter, caption);
}

}  // namespace internal
}  // namespace v8
