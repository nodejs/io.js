// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-isolate.h"
#include "src/flags/flags.h"
#include "src/heap/heap-controller.h"
#include "src/heap/heap.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

TEST(ResourceConstraints, ConfigureDefaultsFromHeapSizeSmall) {
  const size_t MB = static_cast<size_t>(i::MB);
  v8::ResourceConstraints constraints;
  constraints.ConfigureDefaultsFromHeapSize(1 * MB, 1 * MB);
  ASSERT_EQ(i::Heap::MinOldGenerationSize(),
            constraints.max_old_generation_size_in_bytes());
  ASSERT_EQ(i::Heap::MinYoungGenerationSize(),
            constraints.max_young_generation_size_in_bytes());
  ASSERT_EQ(0u, constraints.initial_old_generation_size_in_bytes());
  ASSERT_EQ(0u, constraints.initial_young_generation_size_in_bytes());
}

TEST(ResourceConstraints, ConfigureDefaultsFromHeapSizeLarge) {
  const size_t KB = static_cast<size_t>(i::KB);
  const size_t MB = static_cast<size_t>(i::MB);
  const size_t pm = i::Heap::kPointerMultiplier;
  v8::ResourceConstraints constraints;
  const size_t expected_young_gen_max_size =
      i::Heap::DefaultMaxSemiSpaceSize() *
      (internal::v8_flags.minor_ms ? 2 : 3);
  constraints.ConfigureDefaultsFromHeapSize(
      50u * MB, internal::V8HeapTrait::kMaxSize + expected_young_gen_max_size);
  // Check that for large heap sizes max semi space size is set to the maximum
  // supported capacity (i.e. 8MB with pointer compression and 16MB without;
  // MinorMS supports double capacity).
  ASSERT_EQ(expected_young_gen_max_size,
            constraints.max_young_generation_size_in_bytes());
  ASSERT_EQ(internal::V8HeapTrait::kMaxSize,
            constraints.max_old_generation_size_in_bytes());
  // Check that for small initial heap sizes initial semi space size is set to
  // the minimum supported capacity (i.e. 1MB with pointer compression and 512KB
  // without).
  ASSERT_EQ((internal::v8_flags.minor_ms ? 2 : 3) * 512 * pm * KB,
            constraints.initial_young_generation_size_in_bytes());
  ASSERT_EQ(50u * MB - (internal::v8_flags.minor_ms ? 2 : 3) * 512 * pm * KB,
            constraints.initial_old_generation_size_in_bytes());
}

TEST(ResourceConstraints, ConfigureDefaults) {
  const size_t GB = static_cast<size_t>(i::GB);
  v8::ResourceConstraints constraints;
  constraints.ConfigureDefaults(2u * GB, 0u);
  ASSERT_EQ(i::V8HeapTrait::kMaxSize / 2,
            constraints.max_old_generation_size_in_bytes());
  ASSERT_EQ(0u, constraints.initial_old_generation_size_in_bytes());
  ASSERT_EQ(i::Heap::DefaultMaxSemiSpaceSize() / 2 *
                (internal::v8_flags.minor_ms ? (2 * 2) : 3),
            constraints.max_young_generation_size_in_bytes());
  ASSERT_EQ(0u, constraints.initial_young_generation_size_in_bytes());
}

}  // namespace v8
