// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/doubly-threaded-list.h"

#include "src/base/vector.h"
#include "test/unittests/test-utils.h"

namespace v8::base {

class DoublyThreadedListTest : public TestWithPlatform {};

TEST_F(DoublyThreadedListTest, BasicTest) {
  struct Elem {
    int val;
    bool operator==(const Elem& other) const {
      return val == other.val && prev_ == other.prev_ && next_ == other.next_;
    }

    Elem** prev_;
    Elem* next_;

    // Defining getters required by the default DoublyThreadedListTraits.
    Elem*** prev() { return &prev_; }
    Elem** next() { return &next_; }
  };

  DoublyThreadedList<Elem*> list;
  Elem e1{1, nullptr, nullptr};
  Elem e2{1, nullptr, nullptr};
  Elem e3{1, nullptr, nullptr};
  Elem e4{1, nullptr, nullptr};

  list.PushFront(&e1);
  EXPECT_EQ(**(list.begin()), e1);

  list.PushFront(&e2);
  list.PushFront(&e3);
  list.PushFront(&e4);
  EXPECT_EQ(**(list.begin()), e4);
  EXPECT_EQ(*e4.next_, e3);
  EXPECT_EQ(*e3.next_, e2);
  EXPECT_EQ(*e2.next_, e1);
  EXPECT_EQ(e1.next_, nullptr);
  EXPECT_EQ(*e1.prev_, &e1);
  EXPECT_EQ(*e2.prev_, &e2);
  EXPECT_EQ(*e3.prev_, &e3);
  EXPECT_EQ(*e4.prev_, &e4);

  // Removing front
  list.Remove(&e4);
  EXPECT_EQ(**(list.begin()), e3);
  EXPECT_EQ(*e3.prev_, &e3);
  EXPECT_EQ(e4.next_, nullptr);
  EXPECT_EQ(e4.prev_, nullptr);

  // Removing middle
  list.Remove(&e2);
  EXPECT_EQ(*e3.next_, e1);
  EXPECT_EQ(e2.prev_, nullptr);
  EXPECT_EQ(e2.next_, nullptr);
  EXPECT_EQ(e1.prev_, &e3.next_);
  EXPECT_EQ(*e1.prev_, &e1);

  // Removing back
  list.Remove(&e1);
  EXPECT_EQ(e3.next_, nullptr);
  EXPECT_EQ(e1.prev_, nullptr);
  EXPECT_EQ(e1.next_, nullptr);
  EXPECT_EQ(**(list.begin()), e3);

  // Removing only item
  list.Remove(&e3);
  EXPECT_EQ(e3.prev_, nullptr);
  EXPECT_EQ(e3.next_, nullptr);
  EXPECT_TRUE(list.empty());
}

TEST_F(DoublyThreadedListTest, IteratorTest) {
  struct Elem {
    int val;
    bool operator==(const Elem& other) const {
      return val == other.val && prev_ == other.prev_ && next_ == other.next_;
    }

    Elem** prev_;
    Elem* next_;

    // Defining getters required by the default DoublyThreadedListTraits.
    Elem*** prev() { return &prev_; }
    Elem** next() { return &next_; }
  };

  DoublyThreadedList<Elem*> list;
  Elem e1{1, nullptr, nullptr};
  Elem e2{1, nullptr, nullptr};
  Elem e3{1, nullptr, nullptr};
  Elem e4{1, nullptr, nullptr};

  list.PushFront(&e1);
  list.PushFront(&e2);
  list.PushFront(&e3);
  list.PushFront(&e4);

  int count = 0;
  for (Elem* e : list) {
    USE(e);
    count++;
  }
  EXPECT_EQ(count, 4);

  // Iterating and checking that all items are where they should be
  auto it = list.begin();
  EXPECT_EQ(**it, e4);
  ++it;
  EXPECT_EQ(**it, e3);
  ++it;
  EXPECT_EQ(**it, e2);
  ++it;
  EXPECT_EQ(**it, e1);
  ++it;
  EXPECT_FALSE(it != list.end());

  // Removing with the iterator
  it = list.begin();
  EXPECT_EQ(**it, e4);
  it = list.RemoveAt(it);
  EXPECT_EQ(**it, e3);
  ++it;
  EXPECT_EQ(**it, e2);
  it = list.RemoveAt(it);
  EXPECT_EQ(**it, e1);
  EXPECT_EQ(*e3.next_, e1);
  it = list.RemoveAt(it);
  EXPECT_FALSE(it != list.end());
  EXPECT_EQ(e3.next_, nullptr);
  it = list.begin();
  it = list.RemoveAt(it);

  EXPECT_TRUE(list.empty());
  EXPECT_EQ(e1.next_, nullptr);
  EXPECT_EQ(e2.next_, nullptr);
  EXPECT_EQ(e3.next_, nullptr);
  EXPECT_EQ(e4.next_, nullptr);
  EXPECT_EQ(e1.prev_, nullptr);
  EXPECT_EQ(e2.prev_, nullptr);
  EXPECT_EQ(e3.prev_, nullptr);
  EXPECT_EQ(e4.prev_, nullptr);
}

}  // namespace v8::base
