// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap-controller.h"

#include "src/execution/isolate-inl.h"
#include "src/heap/spaces.h"
#include "src/tracing/trace-event.h"

namespace v8 {
namespace internal {

template <typename Trait>
double MemoryController<Trait>::GrowingFactor(
    Heap* heap, size_t max_heap_size, std::optional<double> gc_speed,
    double mutator_speed, Heap::HeapGrowingMode growing_mode) {
  const double max_factor = MaxGrowingFactor(max_heap_size);
  double factor = DynamicGrowingFactor(gc_speed, mutator_speed, max_factor);
  switch (growing_mode) {
    case Heap::HeapGrowingMode::kConservative:
    case Heap::HeapGrowingMode::kSlow:
      factor = std::min({factor, Trait::kConservativeGrowingFactor});
      break;
    case Heap::HeapGrowingMode::kMinimal:
      factor = Trait::kMinGrowingFactor;
      break;
    case Heap::HeapGrowingMode::kDefault:
      break;
  }
  if (v8_flags.heap_growing_percent > 0) {
    factor = 1.0 + v8_flags.heap_growing_percent / 100.0;
  }
  if (V8_UNLIKELY(v8_flags.trace_gc_verbose)) {
    Isolate::FromHeap(heap)->PrintWithTimestamp(
        "[%s] factor %.1f based on mu=%.3f, speed_ratio=%.f "
        "(gc=%.f, mutator=%.f)\n",
        Trait::kName, factor, Trait::kTargetMutatorUtilization,
        gc_speed.value_or(0) / mutator_speed, gc_speed.value_or(0),
        mutator_speed);
  }
  return factor;
}

template <typename Trait>
double MemoryController<Trait>::MaxGrowingFactor(size_t max_heap_size) {
  constexpr double kMinSmallFactor = 1.3;
  constexpr double kMaxSmallFactor = 2.0;
  constexpr double kHighFactor = 4.0;

  // If we are on a device with lots of memory, we allow a high heap
  // growing factor.
  if (max_heap_size >= Trait::kMaxSize) {
    return kHighFactor;
  }

  size_t max_size = std::max({max_heap_size, Trait::kMinSize});

  DCHECK_GE(max_size, Trait::kMinSize);
  DCHECK_LT(max_size, Trait::kMaxSize);

  // On smaller devices we linearly scale the factor: C+(D-C)*(X-A)/(B-A)
  double factor = kMinSmallFactor + (kMaxSmallFactor - kMinSmallFactor) *
                                        (max_size - Trait::kMinSize) /
                                        (Trait::kMaxSize - Trait::kMinSize);
  return factor;
}

// Given GC speed in bytes per ms, the allocation throughput in bytes per ms
// (mutator speed), this function returns the heap growing factor that will
// achieve the target_mutator_utilization_ if the GC speed and the mutator speed
// remain the same until the next GC.
//
// For a fixed time-frame T = TM + TG, the mutator utilization is the ratio
// TM / (TM + TG), where TM is the time spent in the mutator and TG is the
// time spent in the garbage collector.
//
// Let MU be target_mutator_utilization_, the desired mutator utilization for
// the time-frame from the end of the current GC to the end of the next GC.
// Based on the MU we can compute the heap growing factor F as
//
// F = R * (1 - MU) / (R * (1 - MU) - MU), where R = gc_speed / mutator_speed.
//
// This formula can be derived as follows.
//
// F = Limit / Live by definition, where the Limit is the allocation limit,
// and the Live is size of live objects.
// Let’s assume that we already know the Limit. Then:
//   TG = Limit / gc_speed
//   TM = (TM + TG) * MU, by definition of MU.
//   TM = TG * MU / (1 - MU)
//   TM = Limit *  MU / (gc_speed * (1 - MU))
// On the other hand, if the allocation throughput remains constant:
//   Limit = Live + TM * allocation_throughput = Live + TM * mutator_speed
// Solving it for TM, we get
//   TM = (Limit - Live) / mutator_speed
// Combining the two equation for TM:
//   (Limit - Live) / mutator_speed = Limit * MU / (gc_speed * (1 - MU))
//   (Limit - Live) = Limit * MU * mutator_speed / (gc_speed * (1 - MU))
// substitute R = gc_speed / mutator_speed
//   (Limit - Live) = Limit * MU  / (R * (1 - MU))
// substitute F = Limit / Live
//   F - 1 = F * MU  / (R * (1 - MU))
//   F - F * MU / (R * (1 - MU)) = 1
//   F * (1 - MU / (R * (1 - MU))) = 1
//   F * (R * (1 - MU) - MU) / (R * (1 - MU)) = 1
//   F = R * (1 - MU) / (R * (1 - MU) - MU)
template <typename Trait>
double MemoryController<Trait>::DynamicGrowingFactor(
    std::optional<double> gc_speed, double mutator_speed, double max_factor) {
  DCHECK_LE(Trait::kMinGrowingFactor, max_factor);
  DCHECK_GE(Trait::kMaxGrowingFactor, max_factor);
  if (!gc_speed || mutator_speed == 0) return max_factor;

  const double speed_ratio = *gc_speed / mutator_speed;

  const double a = speed_ratio * (1 - Trait::kTargetMutatorUtilization);
  const double b = speed_ratio * (1 - Trait::kTargetMutatorUtilization) -
                   Trait::kTargetMutatorUtilization;

  // The factor is a / b, but we need to check for small b first.
  double factor = (a < b * max_factor) ? a / b : max_factor;
  DCHECK_LE(factor, max_factor);
  factor = std::max({factor, Trait::kMinGrowingFactor});
  return factor;
}

template <typename Trait>
size_t MemoryController<Trait>::MinimumAllocationLimitGrowingStep(
    Heap::HeapGrowingMode growing_mode) {
  const size_t kRegularAllocationLimitGrowingStep = 8;
  const size_t kLowMemoryAllocationLimitGrowingStep = 2;
  size_t limit = (PageMetadata::kPageSize > MB ? PageMetadata::kPageSize : MB);
  return limit * (growing_mode == Heap::HeapGrowingMode::kConservative
                      ? kLowMemoryAllocationLimitGrowingStep
                      : kRegularAllocationLimitGrowingStep);
}

template <typename Trait>
size_t MemoryController<Trait>::BoundAllocationLimit(
    Heap* heap, size_t current_size, uint64_t limit, size_t min_size,
    size_t max_size, size_t new_space_capacity,
    Heap::HeapGrowingMode growing_mode) {
  CHECK_LT(0, current_size);
  limit = std::max(limit, static_cast<uint64_t>(current_size) +
                              MinimumAllocationLimitGrowingStep(growing_mode)) +
          new_space_capacity;
  const uint64_t halfway_to_the_max =
      (static_cast<uint64_t>(current_size) + max_size) / 2;
  const uint64_t limit_or_halfway =
      std::min<uint64_t>(limit, halfway_to_the_max);
  const size_t result =
      static_cast<size_t>(std::max<uint64_t>(limit_or_halfway, min_size));
  if (V8_UNLIKELY(v8_flags.trace_gc_verbose)) {
    Isolate::FromHeap(heap)->PrintWithTimestamp(
        "[%s] Limit: old size: %zu KB, new limit: %zu KB\n", Trait::kName,
        current_size / KB, result / KB);
  }
  return result;
}

template class V8_EXPORT_PRIVATE MemoryController<V8HeapTrait>;
template class V8_EXPORT_PRIVATE MemoryController<GlobalMemoryTrait>;

}  // namespace internal
}  // namespace v8
