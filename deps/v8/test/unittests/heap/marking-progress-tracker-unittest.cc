// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/marking-progress-tracker.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

static constexpr size_t kObjectSize = 1 << 18;

TEST(MarkingProgressTracker, DefaultDisabled) {
  MarkingProgressTracker progress_tracker;
  EXPECT_FALSE(progress_tracker.IsEnabled());
}

TEST(MarkingProgressTracker, EnabledAfterExplicitEnable) {
  MarkingProgressTracker progress_tracker;
  progress_tracker.Enable(kObjectSize);
  EXPECT_TRUE(progress_tracker.IsEnabled());
}

TEST(MarkingProgressTracker, ZerothChunkFirst) {
  MarkingProgressTracker progress_tracker;
  progress_tracker.Enable(kObjectSize);
  ASSERT_TRUE(progress_tracker.IsEnabled());
  EXPECT_EQ(0u, progress_tracker.GetNextChunkToMark());
}

TEST(MarkingProgressTracker, NumberOfChunks) {
  MarkingProgressTracker progress_tracker;
  progress_tracker.Enable(kObjectSize);
  ASSERT_TRUE(progress_tracker.IsEnabled());
  EXPECT_EQ((kObjectSize + MarkingProgressTracker::kChunkSize - 1) /
                MarkingProgressTracker::kChunkSize,
            progress_tracker.TotalNumberOfChunks());
}

TEST(MarkingProgressTracker, GetNextChunkToMarkIncrements) {
  MarkingProgressTracker progress_tracker;
  progress_tracker.Enable(kObjectSize);
  const size_t num_chunks = progress_tracker.TotalNumberOfChunks();
  ASSERT_TRUE(progress_tracker.IsEnabled());
  for (size_t i = 0; i < num_chunks; ++i) {
    EXPECT_EQ(progress_tracker.GetNextChunkToMark(), i);
  }
}

TEST(MarkingProgressTracker, ResetIfEnabledOnDisabled) {
  MarkingProgressTracker progress_tracker;
  progress_tracker.ResetIfEnabled();
  EXPECT_FALSE(progress_tracker.IsEnabled());
}

TEST(MarkingProgressTracker, ResetIfEnabledOnEnabled) {
  MarkingProgressTracker progress_tracker;
  progress_tracker.Enable(kObjectSize);
  progress_tracker.GetNextChunkToMark();
  progress_tracker.ResetIfEnabled();
  ASSERT_TRUE(progress_tracker.IsEnabled());
  EXPECT_EQ(0u, progress_tracker.GetNextChunkToMark());
}

#ifdef DEBUG

TEST(MarkingProgressTrackerDeathTest, DiesOnTrySetValueOnDisabled) {
  MarkingProgressTracker progress_tracker;
  EXPECT_DEATH_IF_SUPPORTED(progress_tracker.GetNextChunkToMark(), "");
}

TEST(MarkingProgressTrackerDeathTest, GetNextChunkToMarkIncrementOOBs) {
  MarkingProgressTracker progress_tracker;
  progress_tracker.Enable(kObjectSize);
  const size_t num_chunks = progress_tracker.TotalNumberOfChunks();
  ASSERT_TRUE(progress_tracker.IsEnabled());
  for (size_t i = 0; i < num_chunks; ++i) {
    EXPECT_EQ(progress_tracker.GetNextChunkToMark(), i);
  }
  EXPECT_DEATH_IF_SUPPORTED(progress_tracker.GetNextChunkToMark(), "");
}

#endif  // DEBUG

}  // namespace internal
}  // namespace v8
