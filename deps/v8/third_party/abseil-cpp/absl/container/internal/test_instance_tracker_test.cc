// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/container/internal/test_instance_tracker.h"

#include "gtest/gtest.h"

namespace {

using absl::test_internal::CopyableMovableInstance;
using absl::test_internal::CopyableOnlyInstance;
using absl::test_internal::InstanceTracker;
using absl::test_internal::MovableOnlyInstance;

TEST(TestInstanceTracker, CopyableMovable) {
  InstanceTracker tracker;
  CopyableMovableInstance src(1);
  EXPECT_EQ(1, src.value()) << src;
  CopyableMovableInstance copy(src);
  CopyableMovableInstance move(std::move(src));
  EXPECT_EQ(1, tracker.copies());
  EXPECT_EQ(1, tracker.moves());
  EXPECT_EQ(0, tracker.swaps());
  EXPECT_EQ(3, tracker.instances());
  EXPECT_EQ(2, tracker.live_instances());
  tracker.ResetCopiesMovesSwaps();

  CopyableMovableInstance copy_assign(1);
  copy_assign = copy;
  CopyableMovableInstance move_assign(1);
  move_assign = std::move(move);
  EXPECT_EQ(1, tracker.copies());
  EXPECT_EQ(1, tracker.moves());
  EXPECT_EQ(0, tracker.swaps());
  EXPECT_EQ(5, tracker.instances());
  EXPECT_EQ(3, tracker.live_instances());
  tracker.ResetCopiesMovesSwaps();

  {
    using std::swap;
    swap(move_assign, copy);
    swap(copy, move_assign);
    EXPECT_EQ(2, tracker.swaps());
    EXPECT_EQ(0, tracker.copies());
    EXPECT_EQ(0, tracker.moves());
    EXPECT_EQ(5, tracker.instances());
    EXPECT_EQ(3, tracker.live_instances());
  }
}

TEST(TestInstanceTracker, CopyableOnly) {
  InstanceTracker tracker;
  CopyableOnlyInstance src(1);
  EXPECT_EQ(1, src.value()) << src;
  CopyableOnlyInstance copy(src);
  CopyableOnlyInstance copy2(std::move(src));  // NOLINT
  EXPECT_EQ(2, tracker.copies());
  EXPECT_EQ(0, tracker.moves());
  EXPECT_EQ(3, tracker.instances());
  EXPECT_EQ(3, tracker.live_instances());
  tracker.ResetCopiesMovesSwaps();

  CopyableOnlyInstance copy_assign(1);
  copy_assign = copy;
  CopyableOnlyInstance copy_assign2(1);
  copy_assign2 = std::move(copy2);  // NOLINT
  EXPECT_EQ(2, tracker.copies());
  EXPECT_EQ(0, tracker.moves());
  EXPECT_EQ(5, tracker.instances());
  EXPECT_EQ(5, tracker.live_instances());
  tracker.ResetCopiesMovesSwaps();

  {
    using std::swap;
    swap(src, copy);
    swap(copy, src);
    EXPECT_EQ(2, tracker.swaps());
    EXPECT_EQ(0, tracker.copies());
    EXPECT_EQ(0, tracker.moves());
    EXPECT_EQ(5, tracker.instances());
    EXPECT_EQ(5, tracker.live_instances());
  }
}

TEST(TestInstanceTracker, MovableOnly) {
  InstanceTracker tracker;
  MovableOnlyInstance src(1);
  EXPECT_EQ(1, src.value()) << src;
  MovableOnlyInstance move(std::move(src));
  MovableOnlyInstance move_assign(2);
  move_assign = std::move(move);
  EXPECT_EQ(3, tracker.instances());
  EXPECT_EQ(1, tracker.live_instances());
  EXPECT_EQ(2, tracker.moves());
  EXPECT_EQ(0, tracker.copies());
  tracker.ResetCopiesMovesSwaps();

  {
    using std::swap;
    MovableOnlyInstance other(2);
    swap(move_assign, other);
    swap(other, move_assign);
    EXPECT_EQ(2, tracker.swaps());
    EXPECT_EQ(0, tracker.copies());
    EXPECT_EQ(0, tracker.moves());
    EXPECT_EQ(4, tracker.instances());
    EXPECT_EQ(2, tracker.live_instances());
  }
}

TEST(TestInstanceTracker, ExistingInstances) {
  CopyableMovableInstance uncounted_instance(1);
  CopyableMovableInstance uncounted_live_instance(
      std::move(uncounted_instance));
  InstanceTracker tracker;
  EXPECT_EQ(0, tracker.instances());
  EXPECT_EQ(0, tracker.live_instances());
  EXPECT_EQ(0, tracker.copies());
  {
    CopyableMovableInstance instance1(1);
    EXPECT_EQ(1, tracker.instances());
    EXPECT_EQ(1, tracker.live_instances());
    EXPECT_EQ(0, tracker.copies());
    EXPECT_EQ(0, tracker.moves());
    {
      InstanceTracker tracker2;
      CopyableMovableInstance instance2(instance1);
      CopyableMovableInstance instance3(std::move(instance2));
      EXPECT_EQ(3, tracker.instances());
      EXPECT_EQ(2, tracker.live_instances());
      EXPECT_EQ(1, tracker.copies());
      EXPECT_EQ(1, tracker.moves());
      EXPECT_EQ(2, tracker2.instances());
      EXPECT_EQ(1, tracker2.live_instances());
      EXPECT_EQ(1, tracker2.copies());
      EXPECT_EQ(1, tracker2.moves());
    }
    EXPECT_EQ(1, tracker.instances());
    EXPECT_EQ(1, tracker.live_instances());
    EXPECT_EQ(1, tracker.copies());
    EXPECT_EQ(1, tracker.moves());
  }
  EXPECT_EQ(0, tracker.instances());
  EXPECT_EQ(0, tracker.live_instances());
  EXPECT_EQ(1, tracker.copies());
  EXPECT_EQ(1, tracker.moves());
}

TEST(TestInstanceTracker, Comparisons) {
  InstanceTracker tracker;
  MovableOnlyInstance one(1), two(2);

  EXPECT_EQ(0, tracker.comparisons());
  EXPECT_FALSE(one == two);
  EXPECT_EQ(1, tracker.comparisons());
  EXPECT_TRUE(one != two);
  EXPECT_EQ(2, tracker.comparisons());
  EXPECT_TRUE(one < two);
  EXPECT_EQ(3, tracker.comparisons());
  EXPECT_FALSE(one > two);
  EXPECT_EQ(4, tracker.comparisons());
  EXPECT_TRUE(one <= two);
  EXPECT_EQ(5, tracker.comparisons());
  EXPECT_FALSE(one >= two);
  EXPECT_EQ(6, tracker.comparisons());
  EXPECT_TRUE(one.compare(two) < 0);  // NOLINT
  EXPECT_EQ(7, tracker.comparisons());

  tracker.ResetCopiesMovesSwaps();
  EXPECT_EQ(0, tracker.comparisons());
}

}  // namespace
