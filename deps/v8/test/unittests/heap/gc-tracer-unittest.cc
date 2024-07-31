// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/gc-tracer.h"

#include <cmath>
#include <limits>

#include "src/base/platform/platform.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/heap/gc-tracer-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8::internal {

using GCTracerTest = TestWithContext;

namespace {

void SampleAllocation(GCTracer* tracer, base::TimeTicks time,
                      size_t per_space_counter_bytes) {
  // Increment counters of all spaces.
  tracer->SampleAllocation(time, per_space_counter_bytes,
                           per_space_counter_bytes, per_space_counter_bytes);
}

enum class StartTracingMode {
  kAtomic,
  kIncremental,
  kIncrementalStart,
  kIncrementalEnterPause,
};

void StartTracing(GCTracer* tracer, GarbageCollector collector,
                  StartTracingMode mode,
                  base::Optional<base::TimeTicks> time = {}) {
  DCHECK_IMPLIES(mode != StartTracingMode::kAtomic,
                 !Heap::IsYoungGenerationCollector(collector));
  // Start the cycle for incremental marking.
  if (mode == StartTracingMode::kIncremental ||
      mode == StartTracingMode::kIncrementalStart) {
    tracer->StartCycle(collector, GarbageCollectionReason::kTesting,
                       "collector unittest",
                       GCTracer::MarkingType::kIncremental);
  }
  // If just that was requested, no more to be done.
  if (mode == StartTracingMode::kIncrementalStart) return;
  // Else, we enter the observable pause.
  tracer->StartObservablePause(time.value_or(base::TimeTicks::Now()));
  // Start an atomic GC cycle.
  if (mode == StartTracingMode::kAtomic) {
    tracer->StartCycle(collector, GarbageCollectionReason::kTesting,
                       "collector unittest", GCTracer::MarkingType::kAtomic);
  }
  // We enter the atomic pause.
  tracer->StartAtomicPause();
  // Update the current event for an incremental GC cycle.
  if (mode != StartTracingMode::kAtomic) {
    tracer->UpdateCurrentEvent(GarbageCollectionReason::kTesting,
                               "collector unittest");
  }
}

void StopTracing(GCTracer* tracer, GarbageCollector collector,
                 base::Optional<base::TimeTicks> time = {}) {
  tracer->StopAtomicPause();
  tracer->StopObservablePause(collector, time.value_or(base::TimeTicks::Now()));
  switch (collector) {
    case GarbageCollector::SCAVENGER:
      tracer->StopYoungCycleIfNeeded();
      break;
    case GarbageCollector::MINOR_MARK_SWEEPER:
      tracer->NotifyYoungSweepingCompleted();
      break;
    case GarbageCollector::MARK_COMPACTOR:
      tracer->NotifyFullSweepingCompleted();
      break;
  }
}

}  // namespace

TEST_F(GCTracerTest, AllocationThroughput) {
  if (v8_flags.stress_incremental_marking) return;
  // GCTracer::AllocationThroughputInBytesPerMillisecond ignores global memory.
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();

  const int time1 = 100;
  const size_t counter1 = 1000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time1),
                   counter1);
  const int time2 = 200;
  const size_t counter2 = 2000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time2),
                   counter2);
  // Will only consider the current sample.
  EXPECT_EQ(
      2 * (counter2 - counter1) / (time2 - time1),
      static_cast<size_t>(tracer->AllocationThroughputInBytesPerMillisecond(
          base::TimeDelta::FromMilliseconds(100))));
  const int time3 = 1000;
  const size_t counter3 = 30000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time3),
                   counter3);
  // Only consider last sample.
  EXPECT_EQ(
      2 * (counter3 - counter2) / (time3 - time2),
      static_cast<size_t>(tracer->AllocationThroughputInBytesPerMillisecond(
          base::TimeDelta::FromMilliseconds(800))));
  // Considers last 2 samples.
  EXPECT_EQ(
      2 * (counter3 - counter1) / (time3 - time1),
      static_cast<size_t>(tracer->AllocationThroughputInBytesPerMillisecond(
          base::TimeDelta::FromMilliseconds(801))));
}

TEST_F(GCTracerTest, PerGenerationAllocationThroughput) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();
  tracer->allocation_time_ = base::TimeTicks();

  const int time1 = 100;
  const size_t counter1 = 1000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time1),
                   counter1);
  const int time2 = 200;
  const size_t counter2 = 2000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time2),
                   counter2);
  const size_t expected_throughput1 = (counter2 - counter1) / (200 - 100);
  EXPECT_EQ(expected_throughput1,
            static_cast<size_t>(
                tracer->NewSpaceAllocationThroughputInBytesPerMillisecond()));
  EXPECT_EQ(
      expected_throughput1,
      static_cast<size_t>(
          tracer->OldGenerationAllocationThroughputInBytesPerMillisecond()));
  EXPECT_EQ(expected_throughput1,
            static_cast<size_t>(
                tracer->EmbedderAllocationThroughputInBytesPerMillisecond()));
  const int time3 = 1000;
  const size_t counter3 = 30000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time3),
                   counter3);
  const size_t expected_throughput2 = counter3 / time3;
  EXPECT_EQ(expected_throughput2,
            static_cast<size_t>(
                tracer->NewSpaceAllocationThroughputInBytesPerMillisecond()));
  EXPECT_EQ(
      expected_throughput2,
      static_cast<size_t>(
          tracer->OldGenerationAllocationThroughputInBytesPerMillisecond()));
  EXPECT_EQ(expected_throughput2,
            static_cast<size_t>(
                tracer->EmbedderAllocationThroughputInBytesPerMillisecond()));
}

TEST_F(GCTracerTest, PerGenerationAllocationThroughputWithProvidedTime) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();

  const int time1 = 100;
  const size_t counter1 = 1000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time1),
                   counter1);
  const int time2 = 200;
  const size_t counter2 = 2000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time2),
                   counter2);
  const size_t expected_throughput1 = (counter2 - counter1) / (time2 - time1);
  EXPECT_EQ(expected_throughput1,
            static_cast<size_t>(
                tracer->NewSpaceAllocationThroughputInBytesPerMillisecond(
                    base::TimeDelta::FromMilliseconds(100))));
  EXPECT_EQ(expected_throughput1,
            static_cast<size_t>(
                tracer->OldGenerationAllocationThroughputInBytesPerMillisecond(
                    base::TimeDelta::FromMilliseconds(100))));
  const int time3 = 1000;
  const size_t counter3 = 30000;
  SampleAllocation(tracer, base::TimeTicks::FromMsTicksForTesting(time3),
                   counter3);
  const size_t expected_throughput2 = (counter3 - counter2) / (time3 - time2);
  // Only consider last sample.
  EXPECT_EQ(expected_throughput2,
            static_cast<size_t>(
                tracer->NewSpaceAllocationThroughputInBytesPerMillisecond(
                    base::TimeDelta::FromMilliseconds(800))));
  EXPECT_EQ(expected_throughput2,
            static_cast<size_t>(
                tracer->OldGenerationAllocationThroughputInBytesPerMillisecond(
                    base::TimeDelta::FromMilliseconds(800))));
  const size_t expected_throughput3 = (counter3 - counter1) / (time3 - time1);
  // Consider last two samples.
  EXPECT_EQ(expected_throughput3,
            static_cast<size_t>(
                tracer->NewSpaceAllocationThroughputInBytesPerMillisecond(
                    base::TimeDelta::FromMilliseconds(801))));
  EXPECT_EQ(expected_throughput3,
            static_cast<size_t>(
                tracer->OldGenerationAllocationThroughputInBytesPerMillisecond(
                    base::TimeDelta::FromMilliseconds(801))));
}

TEST_F(GCTracerTest, RegularScope) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();

  EXPECT_EQ(base::TimeDelta(),
            tracer->current_.scopes[GCTracer::Scope::MC_MARK]);
  // Sample not added because the cycle has not started.
  tracer->AddScopeSample(GCTracer::Scope::MC_MARK,
                         base::TimeDelta::FromMilliseconds(10));
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kAtomic);
  tracer->AddScopeSample(GCTracer::Scope::MC_MARK,
                         base::TimeDelta::FromMilliseconds(100));
  StopTracing(tracer, GarbageCollector::MARK_COMPACTOR);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(100),
            tracer->current_.scopes[GCTracer::Scope::MC_MARK]);
}

TEST_F(GCTracerTest, IncrementalScope) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();

  EXPECT_EQ(base::TimeDelta(),
            tracer->current_.scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]);
  // Sample is added because its ScopeId is listed as incremental sample.
  tracer->AddScopeSample(GCTracer::Scope::MC_INCREMENTAL_FINALIZE,
                         base::TimeDelta::FromMilliseconds(100));
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncremental);
  tracer->AddScopeSample(GCTracer::Scope::MC_INCREMENTAL_FINALIZE,
                         base::TimeDelta::FromMilliseconds(100));
  StopTracing(tracer, GarbageCollector::MARK_COMPACTOR);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(200),
            tracer->current_.scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]);
}

TEST_F(GCTracerTest, IncrementalMarkingDetails) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();

  // Round 1.
  tracer->AddScopeSample(GCTracer::Scope::MC_INCREMENTAL_FINALIZE,
                         base::TimeDelta::FromMilliseconds(50));
  // Scavenger has no impact on incremental marking details.
  StartTracing(tracer, GarbageCollector::SCAVENGER, StartTracingMode::kAtomic);
  StopTracing(tracer, GarbageCollector::SCAVENGER);
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncremental);
  tracer->AddScopeSample(GCTracer::Scope::MC_INCREMENTAL_FINALIZE,
                         base::TimeDelta::FromMilliseconds(100));
  StopTracing(tracer, GarbageCollector::MARK_COMPACTOR);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(100),
            tracer->current_
                .incremental_scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]
                .longest_step);
  EXPECT_EQ(2, tracer->current_
                   .incremental_scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]
                   .steps);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(150),
            tracer->current_
                .incremental_scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]
                .duration);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(150),
            tracer->current_.scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]);

  // Round 2. Numbers should be reset.
  tracer->AddScopeSample(GCTracer::Scope::MC_INCREMENTAL_FINALIZE,
                         base::TimeDelta::FromMilliseconds(13));
  tracer->AddScopeSample(GCTracer::Scope::MC_INCREMENTAL_FINALIZE,
                         base::TimeDelta::FromMilliseconds(15));
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncremental);
  tracer->AddScopeSample(GCTracer::Scope::MC_INCREMENTAL_FINALIZE,
                         base::TimeDelta::FromMilliseconds(122));
  StopTracing(tracer, GarbageCollector::MARK_COMPACTOR);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(122),
            tracer->current_
                .incremental_scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]
                .longest_step);
  EXPECT_EQ(3, tracer->current_
                   .incremental_scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]
                   .steps);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(150),
            tracer->current_
                .incremental_scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]
                .duration);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(150),
            tracer->current_.scopes[GCTracer::Scope::MC_INCREMENTAL_FINALIZE]);
}

TEST_F(GCTracerTest, IncrementalMarkingSpeed) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();
  tracer->previous_mark_compact_end_time_ = base::TimeTicks();

  // Round 1.
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncrementalStart,
               base::TimeTicks::FromMsTicksForTesting(0));
  // 1000000 bytes in 100ms.
  tracer->AddIncrementalMarkingStep(100, 1000000);
  EXPECT_EQ(1000000 / 100,
            tracer->IncrementalMarkingSpeedInBytesPerMillisecond());
  // 1000000 bytes in 100ms.
  tracer->AddIncrementalMarkingStep(100, 1000000);
  EXPECT_EQ(1000000 / 100,
            tracer->IncrementalMarkingSpeedInBytesPerMillisecond());
  // Scavenger has no impact on incremental marking details.
  StartTracing(tracer, GarbageCollector::SCAVENGER, StartTracingMode::kAtomic);
  StopTracing(tracer, GarbageCollector::SCAVENGER);
  // 1000000 bytes in 100ms.
  tracer->AddIncrementalMarkingStep(100, 1000000);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(300),
            tracer->current_.incremental_marking_duration);
  EXPECT_EQ(3000000u, tracer->current_.incremental_marking_bytes);
  EXPECT_EQ(1000000 / 100,
            tracer->IncrementalMarkingSpeedInBytesPerMillisecond());
  // 1000000 bytes in 100ms.
  tracer->AddIncrementalMarkingStep(100, 1000000);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(400),
            tracer->current_.incremental_marking_duration);
  EXPECT_EQ(4000000u, tracer->current_.incremental_marking_bytes);
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncrementalEnterPause,
               base::TimeTicks::FromMsTicksForTesting(500));
  StopTracing(tracer, GarbageCollector::MARK_COMPACTOR,
              base::TimeTicks::FromMsTicksForTesting(600));
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(400),
            tracer->current_.incremental_marking_duration);
  EXPECT_EQ(4000000u, tracer->current_.incremental_marking_bytes);
  EXPECT_EQ(1000000 / 100,
            tracer->IncrementalMarkingSpeedInBytesPerMillisecond());

  // Round 2.
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncrementalStart,
               base::TimeTicks::FromMsTicksForTesting(700));
  tracer->AddIncrementalMarkingStep(2000, 1000);
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncrementalEnterPause,
               base::TimeTicks::FromMsTicksForTesting(3000));
  StopTracing(tracer, GarbageCollector::MARK_COMPACTOR,
              base::TimeTicks::FromMsTicksForTesting(3100));
  EXPECT_DOUBLE_EQ((4000000.0 / 400 + 1000.0 / 2000) / 2,
                   static_cast<double>(
                       tracer->IncrementalMarkingSpeedInBytesPerMillisecond()));
}

TEST_F(GCTracerTest, MutatorUtilization) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();
  tracer->previous_mark_compact_end_time_ = base::TimeTicks();

  // Mark-compact #1 ended at 200ms and took 100ms.
  tracer->RecordMutatorUtilization(base::TimeTicks::FromMsTicksForTesting(200),
                                   base::TimeDelta::FromMilliseconds(100));
  // Average mark-compact time = 100ms.
  // Average mutator time = 100ms.
  EXPECT_DOUBLE_EQ(0.5, tracer->CurrentMarkCompactMutatorUtilization());
  EXPECT_DOUBLE_EQ(0.5, tracer->AverageMarkCompactMutatorUtilization());

  // Mark-compact #2 ended at 400ms and took 100ms.
  tracer->RecordMutatorUtilization(base::TimeTicks::FromMsTicksForTesting(400),
                                   base::TimeDelta::FromMilliseconds(100));
  // Average mark-compact time = 100ms * 0.5 + 100ms * 0.5.
  // Average mutator time = 100ms * 0.5 + 100ms * 0.5.
  EXPECT_DOUBLE_EQ(0.5, tracer->CurrentMarkCompactMutatorUtilization());
  EXPECT_DOUBLE_EQ(0.5, tracer->AverageMarkCompactMutatorUtilization());

  // Mark-compact #3 ended at 600ms and took 200ms.
  tracer->RecordMutatorUtilization(base::TimeTicks::FromMsTicksForTesting(600),
                                   base::TimeDelta::FromMilliseconds(200));
  // Average mark-compact time = 100ms * 0.5 + 200ms * 0.5.
  // Average mutator time = 100ms * 0.5 + 0ms * 0.5.
  EXPECT_DOUBLE_EQ(0.0, tracer->CurrentMarkCompactMutatorUtilization());
  EXPECT_DOUBLE_EQ(50.0 / 200.0,
                   tracer->AverageMarkCompactMutatorUtilization());

  // Mark-compact #4 ended at 800ms and took 0ms.
  tracer->RecordMutatorUtilization(base::TimeTicks::FromMsTicksForTesting(800),
                                   base::TimeDelta());
  // Average mark-compact time = 150ms * 0.5 + 0ms * 0.5.
  // Average mutator time = 50ms * 0.5 + 200ms * 0.5.
  EXPECT_DOUBLE_EQ(1.0, tracer->CurrentMarkCompactMutatorUtilization());
  EXPECT_DOUBLE_EQ(125.0 / 200.0,
                   tracer->AverageMarkCompactMutatorUtilization());
}

TEST_F(GCTracerTest, BackgroundScavengerScope) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();
  StartTracing(tracer, GarbageCollector::SCAVENGER, StartTracingMode::kAtomic);
  tracer->AddScopeSample(
      GCTracer::Scope::SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL,
      base::TimeDelta::FromMilliseconds(10));
  tracer->AddScopeSample(
      GCTracer::Scope::SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL,
      base::TimeDelta::FromMilliseconds(1));
  StopTracing(tracer, GarbageCollector::SCAVENGER);
  EXPECT_EQ(
      base::TimeDelta::FromMilliseconds(11),
      tracer->current_
          .scopes[GCTracer::Scope::SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL]);
}

TEST_F(GCTracerTest, BackgroundMinorMSScope) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();
  StartTracing(tracer, GarbageCollector::MINOR_MARK_SWEEPER,
               StartTracingMode::kAtomic);
  tracer->AddScopeSample(GCTracer::Scope::MINOR_MS_BACKGROUND_MARKING,
                         base::TimeDelta::FromMilliseconds(10));
  tracer->AddScopeSample(GCTracer::Scope::MINOR_MS_BACKGROUND_MARKING,
                         base::TimeDelta::FromMilliseconds(1));
  StopTracing(tracer, GarbageCollector::MINOR_MARK_SWEEPER);
  EXPECT_EQ(
      base::TimeDelta::FromMilliseconds(11),
      tracer->current_.scopes[GCTracer::Scope::MINOR_MS_BACKGROUND_MARKING]);
}

TEST_F(GCTracerTest, BackgroundMajorMCScope) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncrementalStart);
  tracer->AddScopeSample(GCTracer::Scope::MC_BACKGROUND_MARKING,
                         base::TimeDelta::FromMilliseconds(100));
  tracer->AddScopeSample(GCTracer::Scope::MC_BACKGROUND_SWEEPING,
                         base::TimeDelta::FromMilliseconds(200));
  tracer->AddScopeSample(GCTracer::Scope::MC_BACKGROUND_MARKING,
                         base::TimeDelta::FromMilliseconds(10));
  // Scavenger should not affect the major mark-compact scopes.
  StartTracing(tracer, GarbageCollector::SCAVENGER, StartTracingMode::kAtomic);
  StopTracing(tracer, GarbageCollector::SCAVENGER);
  tracer->AddScopeSample(GCTracer::Scope::MC_BACKGROUND_SWEEPING,
                         base::TimeDelta::FromMilliseconds(20));
  tracer->AddScopeSample(GCTracer::Scope::MC_BACKGROUND_MARKING,
                         base::TimeDelta::FromMilliseconds(1));
  tracer->AddScopeSample(GCTracer::Scope::MC_BACKGROUND_SWEEPING,
                         base::TimeDelta::FromMilliseconds(2));
  StartTracing(tracer, GarbageCollector::MARK_COMPACTOR,
               StartTracingMode::kIncrementalEnterPause);
  tracer->AddScopeSample(GCTracer::Scope::MC_BACKGROUND_EVACUATE_COPY,
                         base::TimeDelta::FromMilliseconds(30));
  tracer->AddScopeSample(GCTracer::Scope::MC_BACKGROUND_EVACUATE_COPY,
                         base::TimeDelta::FromMilliseconds(3));
  tracer->AddScopeSample(
      GCTracer::Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS,
      base::TimeDelta::FromMilliseconds(40));
  tracer->AddScopeSample(
      GCTracer::Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS,
      base::TimeDelta::FromMilliseconds(4));
  StopTracing(tracer, GarbageCollector::MARK_COMPACTOR);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(111),
            tracer->current_.scopes[GCTracer::Scope::MC_BACKGROUND_MARKING]);
  EXPECT_EQ(base::TimeDelta::FromMilliseconds(222),
            tracer->current_.scopes[GCTracer::Scope::MC_BACKGROUND_SWEEPING]);
  EXPECT_EQ(
      base::TimeDelta::FromMilliseconds(33),
      tracer->current_.scopes[GCTracer::Scope::MC_BACKGROUND_EVACUATE_COPY]);
  EXPECT_EQ(
      base::TimeDelta::FromMilliseconds(44),
      tracer->current_
          .scopes[GCTracer::Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS]);
}

class ThreadWithBackgroundScope final : public base::Thread {
 public:
  explicit ThreadWithBackgroundScope(GCTracer* tracer)
      : Thread(Options("ThreadWithBackgroundScope")), tracer_(tracer) {}
  void Run() override {
    GCTracer::Scope scope(tracer_, GCTracer::Scope::MC_BACKGROUND_MARKING,
                          ThreadKind::kBackground);
  }

 private:
  GCTracer* tracer_;
};

TEST_F(GCTracerTest, MultithreadedBackgroundScope) {
  if (v8_flags.stress_incremental_marking) return;
  GCTracer* tracer = i_isolate()->heap()->tracer();
  ThreadWithBackgroundScope thread1(tracer);
  ThreadWithBackgroundScope thread2(tracer);
  tracer->ResetForTesting();
  CHECK(thread1.Start());
  CHECK(thread2.Start());
  tracer->FetchBackgroundCounters();

  thread1.Join();
  thread2.Join();
  tracer->FetchBackgroundCounters();

  EXPECT_LE(base::TimeDelta(),
            tracer->current_.scopes[GCTracer::Scope::MC_BACKGROUND_MARKING]);
}

class GcHistogram {
 public:
  static void* CreateHistogram(const char* name, int min, int max,
                               size_t buckets) {
    histograms_[name] = std::unique_ptr<GcHistogram>(new GcHistogram());
    return histograms_[name].get();
  }

  static void AddHistogramSample(void* histogram, int sample) {
    if (histograms_.empty()) return;
    static_cast<GcHistogram*>(histogram)->samples_.push_back(sample);
  }

  static GcHistogram* Get(const char* name) { return histograms_[name].get(); }

  static void CleanUp() { histograms_.clear(); }

  int Total() const {
    int result = 0;
    for (int i : samples_) {
      result += i;
    }
    return result;
  }

  int Count() const { return static_cast<int>(samples_.size()); }

 private:
  std::vector<int> samples_;
  static std::map<std::string, std::unique_ptr<GcHistogram>> histograms_;
};

std::map<std::string, std::unique_ptr<GcHistogram>> GcHistogram::histograms_ =
    std::map<std::string, std::unique_ptr<GcHistogram>>();

TEST_F(GCTracerTest, RecordMarkCompactHistograms) {
  if (v8_flags.stress_incremental_marking) return;
  isolate()->SetCreateHistogramFunction(&GcHistogram::CreateHistogram);
  isolate()->SetAddHistogramSampleFunction(&GcHistogram::AddHistogramSample);
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();
  tracer->current_.scopes[GCTracer::Scope::MC_CLEAR] =
      base::TimeDelta::FromMilliseconds(1);
  tracer->current_.scopes[GCTracer::Scope::MC_EPILOGUE] =
      base::TimeDelta::FromMilliseconds(2);
  tracer->current_.scopes[GCTracer::Scope::MC_EVACUATE] =
      base::TimeDelta::FromMilliseconds(3);
  tracer->current_.scopes[GCTracer::Scope::MC_FINISH] =
      base::TimeDelta::FromMilliseconds(4);
  tracer->current_.scopes[GCTracer::Scope::MC_MARK] =
      base::TimeDelta::FromMilliseconds(5);
  tracer->current_.scopes[GCTracer::Scope::MC_PROLOGUE] =
      base::TimeDelta::FromMilliseconds(6);
  tracer->current_.scopes[GCTracer::Scope::MC_SWEEP] =
      base::TimeDelta::FromMilliseconds(7);
  tracer->RecordGCPhasesHistograms(
      GCTracer::RecordGCPhasesInfo::Mode::Finalize);
  EXPECT_EQ(1, GcHistogram::Get("V8.GCFinalizeMC.Clear")->Total());
  EXPECT_EQ(2, GcHistogram::Get("V8.GCFinalizeMC.Epilogue")->Total());
  EXPECT_EQ(3, GcHistogram::Get("V8.GCFinalizeMC.Evacuate")->Total());
  EXPECT_EQ(4, GcHistogram::Get("V8.GCFinalizeMC.Finish")->Total());
  EXPECT_EQ(5, GcHistogram::Get("V8.GCFinalizeMC.Mark")->Total());
  EXPECT_EQ(6, GcHistogram::Get("V8.GCFinalizeMC.Prologue")->Total());
  EXPECT_EQ(7, GcHistogram::Get("V8.GCFinalizeMC.Sweep")->Total());
  GcHistogram::CleanUp();
}

TEST_F(GCTracerTest, RecordScavengerHistograms) {
  if (v8_flags.stress_incremental_marking) return;
  isolate()->SetCreateHistogramFunction(&GcHistogram::CreateHistogram);
  isolate()->SetAddHistogramSampleFunction(&GcHistogram::AddHistogramSample);
  GCTracer* tracer = i_isolate()->heap()->tracer();
  tracer->ResetForTesting();
  tracer->current_.scopes[GCTracer::Scope::SCAVENGER_SCAVENGE_ROOTS] =
      base::TimeDelta::FromMilliseconds(1);
  tracer->current_.scopes[GCTracer::Scope::SCAVENGER_SCAVENGE_PARALLEL] =
      base::TimeDelta::FromMilliseconds(2);
  tracer->RecordGCPhasesHistograms(
      GCTracer::RecordGCPhasesInfo::Mode::Scavenger);
  EXPECT_EQ(1, GcHistogram::Get("V8.GCScavenger.ScavengeRoots")->Total());
  EXPECT_EQ(2, GcHistogram::Get("V8.GCScavenger.ScavengeMain")->Total());
  GcHistogram::CleanUp();
}

}  // namespace v8::internal
