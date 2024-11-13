// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/marking-worklist.h"

#include <cmath>
#include <limits>

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/marking-worklist-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using MarkingWorklistTest = TestWithContext;

TEST_F(MarkingWorklistTest, PushPop) {
  MarkingWorklists holder;
  MarkingWorklists::Local worklists(&holder);
  Tagged<HeapObject> pushed_object =
      Cast<HeapObject>(i_isolate()
                           ->roots_table()
                           .slot(RootIndex::kFirstStrongRoot)
                           .load(i_isolate()));
  worklists.Push(pushed_object);
  Tagged<HeapObject> popped_object;
  EXPECT_TRUE(worklists.Pop(&popped_object));
  EXPECT_EQ(popped_object, pushed_object);
}

TEST_F(MarkingWorklistTest, PushPopOnHold) {
  MarkingWorklists holder;
  MarkingWorklists::Local worklists(&holder);
  Tagged<HeapObject> pushed_object =
      Cast<HeapObject>(i_isolate()
                           ->roots_table()
                           .slot(RootIndex::kFirstStrongRoot)
                           .load(i_isolate()));
  worklists.PushOnHold(pushed_object);
  Tagged<HeapObject> popped_object;
  EXPECT_TRUE(worklists.PopOnHold(&popped_object));
  EXPECT_EQ(popped_object, pushed_object);
}

TEST_F(MarkingWorklistTest, MergeOnHold) {
  MarkingWorklists holder;
  MarkingWorklists::Local main_worklists(&holder);
  MarkingWorklists::Local worker_worklists(&holder);
  Tagged<HeapObject> pushed_object =
      Cast<HeapObject>(i_isolate()
                           ->roots_table()
                           .slot(RootIndex::kFirstStrongRoot)
                           .load(i_isolate()));
  worker_worklists.PushOnHold(pushed_object);
  worker_worklists.Publish();
  main_worklists.MergeOnHold();
  Tagged<HeapObject> popped_object;
  EXPECT_TRUE(main_worklists.Pop(&popped_object));
  EXPECT_EQ(popped_object, pushed_object);
}

TEST_F(MarkingWorklistTest, ShareWorkIfGlobalPoolIsEmpty) {
  MarkingWorklists holder;
  MarkingWorklists::Local main_worklists(&holder);
  MarkingWorklists::Local worker_worklists(&holder);
  Tagged<HeapObject> pushed_object =
      Cast<HeapObject>(i_isolate()
                           ->roots_table()
                           .slot(RootIndex::kFirstStrongRoot)
                           .load(i_isolate()));
  main_worklists.Push(pushed_object);
  main_worklists.ShareWork();
  Tagged<HeapObject> popped_object;
  EXPECT_TRUE(worker_worklists.Pop(&popped_object));
  EXPECT_EQ(popped_object, pushed_object);
}

TEST_F(MarkingWorklistTest, ContextWorklistsPushPop) {
  const Address context = 0xabcdef;
  MarkingWorklists holder;
  holder.CreateContextWorklists({context});
  MarkingWorklists::Local worklists(&holder);
  Tagged<HeapObject> pushed_object =
      Cast<HeapObject>(i_isolate()
                           ->roots_table()
                           .slot(RootIndex::kFirstStrongRoot)
                           .load(i_isolate()));
  worklists.SwitchToContext(context);
  worklists.Push(pushed_object);
  worklists.SwitchToSharedForTesting();
  Tagged<HeapObject> popped_object;
  EXPECT_TRUE(worklists.Pop(&popped_object));
  EXPECT_EQ(popped_object, pushed_object);
  holder.ReleaseContextWorklists();
}

TEST_F(MarkingWorklistTest, ContextWorklistsEmpty) {
  const Address context = 0xabcdef;
  MarkingWorklists holder;
  holder.CreateContextWorklists({context});
  MarkingWorklists::Local worklists(&holder);
  Tagged<HeapObject> pushed_object =
      Cast<HeapObject>(i_isolate()
                           ->roots_table()
                           .slot(RootIndex::kFirstStrongRoot)
                           .load(i_isolate()));
  worklists.SwitchToContext(context);
  worklists.Push(pushed_object);
  EXPECT_FALSE(worklists.IsEmpty());
  worklists.SwitchToSharedForTesting();
  EXPECT_FALSE(worklists.IsEmpty());
  Tagged<HeapObject> popped_object;
  EXPECT_TRUE(worklists.Pop(&popped_object));
  EXPECT_EQ(popped_object, pushed_object);
  EXPECT_TRUE(worklists.IsEmpty());
  holder.ReleaseContextWorklists();
}

TEST_F(MarkingWorklistTest, ContextWorklistCrossTask) {
  const Address context1 = 0x1abcdef;
  const Address context2 = 0x2abcdef;
  MarkingWorklists holder;
  holder.CreateContextWorklists({context1, context2});
  MarkingWorklists::Local main_worklists(&holder);
  MarkingWorklists::Local worker_worklists(&holder);
  Tagged<HeapObject> pushed_object =
      Cast<HeapObject>(i_isolate()
                           ->roots_table()
                           .slot(RootIndex::kFirstStrongRoot)
                           .load(i_isolate()));
  main_worklists.SwitchToContext(context1);
  main_worklists.Push(pushed_object);
  main_worklists.ShareWork();
  worker_worklists.SwitchToContext(context2);
  Tagged<HeapObject> popped_object;
  EXPECT_TRUE(worker_worklists.Pop(&popped_object));
  EXPECT_EQ(popped_object, pushed_object);
  EXPECT_EQ(context1, worker_worklists.Context());
  holder.ReleaseContextWorklists();
}

}  // namespace internal
}  // namespace v8
