// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/metric-recorder.h"

#include "src/heap/cppgc/stats-collector.h"
#include "test/unittests/heap/cppgc/tests.h"

namespace cppgc {
namespace internal {

namespace {
class MetricRecorderImpl final : public MetricRecorder {
 public:
  void AddMainThreadEvent(const GCCycle& event) final {
    GCCycle_event = event;
    GCCycle_callcount++;
  }
  void AddMainThreadEvent(const MainThreadIncrementalMark& event) final {
    MainThreadIncrementalMark_event = event;
    MainThreadIncrementalMark_callcount++;
  }
  void AddMainThreadEvent(const MainThreadIncrementalSweep& event) final {
    MainThreadIncrementalSweep_event = event;
    MainThreadIncrementalSweep_callcount++;
  }

  static size_t GCCycle_callcount;
  static GCCycle GCCycle_event;
  static size_t MainThreadIncrementalMark_callcount;
  static MainThreadIncrementalMark MainThreadIncrementalMark_event;
  static size_t MainThreadIncrementalSweep_callcount;
  static MainThreadIncrementalSweep MainThreadIncrementalSweep_event;
};

// static
size_t MetricRecorderImpl::GCCycle_callcount = 0u;
MetricRecorderImpl::GCCycle MetricRecorderImpl::GCCycle_event;
size_t MetricRecorderImpl::MainThreadIncrementalMark_callcount = 0u;
MetricRecorderImpl::MainThreadIncrementalMark
    MetricRecorderImpl::MainThreadIncrementalMark_event;
size_t MetricRecorderImpl::MainThreadIncrementalSweep_callcount = 0u;
MetricRecorderImpl::MainThreadIncrementalSweep
    MetricRecorderImpl::MainThreadIncrementalSweep_event;

class MetricRecorderTest : public testing::TestWithHeap {
 public:
  MetricRecorderTest() : stats(Heap::From(GetHeap())->stats_collector()) {
    stats->SetMetricRecorder(std::make_unique<MetricRecorderImpl>());
  }

  void StartGC() {
    stats->NotifyMarkingStarted(CollectionType::kMajor,
                                GCConfig::MarkingType::kIncremental,
                                GCConfig::IsForcedGC::kNotForced);
  }
  void EndGC(size_t marked_bytes) {
    stats->NotifyMarkingCompleted(marked_bytes);
    stats->NotifySweepingCompleted(GCConfig::SweepingType::kIncremental);
  }

  StatsCollector* stats;
};
}  // namespace

TEST_F(MetricRecorderTest, IncrementalScopesReportedImmediately) {
  MetricRecorderImpl::GCCycle_callcount = 0u;
  MetricRecorderImpl::MainThreadIncrementalMark_callcount = 0u;
  MetricRecorderImpl::MainThreadIncrementalSweep_callcount = 0u;
  StartGC();
  {
    EXPECT_EQ(0u, MetricRecorderImpl::MainThreadIncrementalMark_callcount);
    {
      StatsCollector::EnabledScope scope(
          Heap::From(GetHeap())->stats_collector(),
          StatsCollector::kIncrementalMark);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EXPECT_EQ(1u, MetricRecorderImpl::MainThreadIncrementalMark_callcount);
    EXPECT_LT(0u,
              MetricRecorderImpl::MainThreadIncrementalMark_event.duration_us);
  }
  {
    EXPECT_EQ(0u, MetricRecorderImpl::MainThreadIncrementalSweep_callcount);
    {
      StatsCollector::EnabledScope scope(
          Heap::From(GetHeap())->stats_collector(),
          StatsCollector::kIncrementalSweep);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EXPECT_EQ(1u, MetricRecorderImpl::MainThreadIncrementalSweep_callcount);
    EXPECT_LT(0u,
              MetricRecorderImpl::MainThreadIncrementalSweep_event.duration_us);
  }
  EXPECT_EQ(0u, MetricRecorderImpl::GCCycle_callcount);
  EndGC(0);
}

TEST_F(MetricRecorderTest, NonIncrementalScopesNotReportedImmediately) {
  MetricRecorderImpl::GCCycle_callcount = 0u;
  MetricRecorderImpl::MainThreadIncrementalMark_callcount = 0u;
  MetricRecorderImpl::MainThreadIncrementalSweep_callcount = 0u;
  StartGC();
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kAtomicMark);
  }
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kAtomicWeak);
  }
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kAtomicCompact);
  }
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kAtomicSweep);
  }
  {
    StatsCollector::EnabledConcurrentScope scope(
        Heap::From(GetHeap())->stats_collector(),
        StatsCollector::kConcurrentMark);
  }
  {
    StatsCollector::EnabledConcurrentScope scope(
        Heap::From(GetHeap())->stats_collector(),
        StatsCollector::kConcurrentSweep);
  }
  EXPECT_EQ(0u, MetricRecorderImpl::MainThreadIncrementalMark_callcount);
  EXPECT_EQ(0u, MetricRecorderImpl::MainThreadIncrementalSweep_callcount);
  EXPECT_EQ(0u, MetricRecorderImpl::GCCycle_callcount);
  EndGC(0);
}

TEST_F(MetricRecorderTest, CycleEndMetricsReportedOnGcEnd) {
  MetricRecorderImpl::GCCycle_callcount = 0u;
  MetricRecorderImpl::MainThreadIncrementalMark_callcount = 0u;
  MetricRecorderImpl::MainThreadIncrementalSweep_callcount = 0u;
  StartGC();
  EndGC(0);
  EXPECT_EQ(0u, MetricRecorderImpl::MainThreadIncrementalMark_callcount);
  EXPECT_EQ(0u, MetricRecorderImpl::MainThreadIncrementalSweep_callcount);
  EXPECT_EQ(1u, MetricRecorderImpl::GCCycle_callcount);
}

TEST_F(MetricRecorderTest, CycleEndHistogramReportsCorrectValues) {
  StartGC();
  {
    // Warmup scope to make sure everything is loaded in memory and reduce noise
    // in timing measurements.
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kIncrementalMark);
  }
  EndGC(1000);
  StartGC();
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kIncrementalMark);
    scope.DecreaseStartTimeForTesting(
        v8::base::TimeDelta::FromMilliseconds(10));
  }
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kIncrementalSweep);
    scope.DecreaseStartTimeForTesting(
        v8::base::TimeDelta::FromMilliseconds(20));
  }
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kAtomicMark);
    scope.DecreaseStartTimeForTesting(
        v8::base::TimeDelta::FromMilliseconds(30));
  }
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kAtomicWeak);
    scope.DecreaseStartTimeForTesting(
        v8::base::TimeDelta::FromMilliseconds(50));
  }
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kAtomicCompact);
    scope.DecreaseStartTimeForTesting(
        v8::base::TimeDelta::FromMilliseconds(60));
  }
  {
    StatsCollector::EnabledScope scope(Heap::From(GetHeap())->stats_collector(),
                                       StatsCollector::kAtomicSweep);
    scope.DecreaseStartTimeForTesting(
        v8::base::TimeDelta::FromMilliseconds(70));
  }
  {
    StatsCollector::EnabledConcurrentScope scope(
        Heap::From(GetHeap())->stats_collector(),
        StatsCollector::kConcurrentMark);
    scope.DecreaseStartTimeForTesting(
        v8::base::TimeDelta::FromMilliseconds(80));
  }
  {
    StatsCollector::EnabledConcurrentScope scope(
        Heap::From(GetHeap())->stats_collector(),
        StatsCollector::kConcurrentSweep);
    scope.DecreaseStartTimeForTesting(
        v8::base::TimeDelta::FromMilliseconds(100));
  }
  EndGC(300);
  // Check durations.
  static constexpr int64_t kDurationComparisonTolerance = 5000;
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.main_thread_incremental
                         .mark_duration_us -
                     10000),
            kDurationComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.main_thread_incremental
                         .sweep_duration_us -
                     20000),
            kDurationComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.main_thread_atomic
                         .mark_duration_us -
                     30000),
            kDurationComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.main_thread_atomic
                         .weak_duration_us -
                     50000),
            kDurationComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.main_thread_atomic
                         .compact_duration_us -
                     60000),
            kDurationComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.main_thread_atomic
                         .sweep_duration_us -
                     70000),
            kDurationComparisonTolerance);
  EXPECT_LT(
      std::abs(MetricRecorderImpl::GCCycle_event.main_thread.mark_duration_us -
               40000),
      kDurationComparisonTolerance);
  EXPECT_LT(
      std::abs(MetricRecorderImpl::GCCycle_event.main_thread.weak_duration_us -
               50000),
      kDurationComparisonTolerance);
  EXPECT_LT(
      std::abs(
          MetricRecorderImpl::GCCycle_event.main_thread.compact_duration_us -
          60000),
      kDurationComparisonTolerance);
  EXPECT_LT(
      std::abs(MetricRecorderImpl::GCCycle_event.main_thread.sweep_duration_us -
               90000),
      kDurationComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.total.mark_duration_us -
                     120000),
            kDurationComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.total.weak_duration_us -
                     50000),
            kDurationComparisonTolerance);
  EXPECT_LT(
      std::abs(MetricRecorderImpl::GCCycle_event.total.compact_duration_us -
               60000),
      kDurationComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event.total.sweep_duration_us -
                     190000),
            kDurationComparisonTolerance);
  // Check collection rate and efficiency.
  EXPECT_DOUBLE_EQ(
      0.7, MetricRecorderImpl::GCCycle_event.collection_rate_in_percent);
  static constexpr double kEfficiencyComparisonTolerance = 0.0005;
  EXPECT_LT(
      std::abs(MetricRecorderImpl::GCCycle_event.efficiency_in_bytes_per_us -
               (700.0 / (120000 + 50000 + 60000 + 190000))),
      kEfficiencyComparisonTolerance);
  EXPECT_LT(std::abs(MetricRecorderImpl::GCCycle_event
                         .main_thread_efficiency_in_bytes_per_us -
                     (700.0 / (40000 + 50000 + 60000 + 90000))),
            kEfficiencyComparisonTolerance);
}

TEST_F(MetricRecorderTest, ObjectSizeMetricsNoAllocations) {
  // Populate previous event.
  StartGC();
  EndGC(1000);
  // Populate current event.
  StartGC();
  EndGC(800);
  EXPECT_EQ(1000u, MetricRecorderImpl::GCCycle_event.objects.before_bytes);
  EXPECT_EQ(800u, MetricRecorderImpl::GCCycle_event.objects.after_bytes);
  EXPECT_EQ(200u, MetricRecorderImpl::GCCycle_event.objects.freed_bytes);
  EXPECT_EQ(0u, MetricRecorderImpl::GCCycle_event.memory.before_bytes);
  EXPECT_EQ(0u, MetricRecorderImpl::GCCycle_event.memory.after_bytes);
  EXPECT_EQ(0u, MetricRecorderImpl::GCCycle_event.memory.freed_bytes);
}

TEST_F(MetricRecorderTest, ObjectSizeMetricsWithAllocations) {
  // Populate previous event.
  StartGC();
  EndGC(1000);
  // Populate current event.
  StartGC();
  stats->NotifyAllocation(300);
  stats->NotifyAllocatedMemory(1400);
  stats->NotifyFreedMemory(700);
  stats->NotifyMarkingCompleted(800);
  stats->NotifyAllocation(150);
  stats->NotifyAllocatedMemory(1000);
  stats->NotifyFreedMemory(400);
  stats->NotifySweepingCompleted(GCConfig::SweepingType::kAtomic);
  EXPECT_EQ(1300u, MetricRecorderImpl::GCCycle_event.objects.before_bytes);
  EXPECT_EQ(800, MetricRecorderImpl::GCCycle_event.objects.after_bytes);
  EXPECT_EQ(500u, MetricRecorderImpl::GCCycle_event.objects.freed_bytes);
  EXPECT_EQ(700u, MetricRecorderImpl::GCCycle_event.memory.before_bytes);
  EXPECT_EQ(300u, MetricRecorderImpl::GCCycle_event.memory.after_bytes);
  EXPECT_EQ(400u, MetricRecorderImpl::GCCycle_event.memory.freed_bytes);
}

}  // namespace internal
}  // namespace cppgc
