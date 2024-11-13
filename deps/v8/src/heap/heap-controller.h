// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_CONTROLLER_H_
#define V8_HEAP_HEAP_CONTROLLER_H_

#include <cstddef>
#include "src/heap/heap.h"
#include "src/utils/allocation.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

struct BaseControllerTrait {
  static constexpr size_t kMinSize = 128u * Heap::kHeapLimitMultiplier * MB;
  static constexpr size_t kMaxSize = 1024u * Heap::kHeapLimitMultiplier * MB;

  static constexpr double kMinGrowingFactor = 1.1;
  static constexpr double kMaxGrowingFactor = 4.0;
  static constexpr double kConservativeGrowingFactor = 1.3;
  static constexpr double kTargetMutatorUtilization = 0.97;
};

struct V8HeapTrait : public BaseControllerTrait {
  static constexpr char kName[] = "HeapController";
};

struct GlobalMemoryTrait : public BaseControllerTrait {
  static constexpr char kName[] = "GlobalMemoryController";
};

template <typename Trait>
class V8_EXPORT_PRIVATE MemoryController : public AllStatic {
 public:
  // Computes the growing step when the limit increases.
  static size_t MinimumAllocationLimitGrowingStep(
      Heap::HeapGrowingMode growing_mode);

  static double GrowingFactor(Heap* heap, size_t max_heap_size, double gc_speed,
                              double mutator_speed);

  static size_t CalculateAllocationLimit(Heap* heap, size_t current_size,
                                         size_t min_size, size_t max_size,
                                         size_t new_space_capacity,
                                         double factor,
                                         Heap::HeapGrowingMode growing_mode);

 private:
  static double MaxGrowingFactor(size_t max_heap_size);
  static double DynamicGrowingFactor(double gc_speed, double mutator_speed,
                                     double max_factor);

  FRIEND_TEST(MemoryControllerTest, HeapGrowingFactor);
  FRIEND_TEST(MemoryControllerTest, MaxHeapGrowingFactor);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_CONTROLLER_H_
