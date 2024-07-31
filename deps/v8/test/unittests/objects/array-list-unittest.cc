// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/heap/factory.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using ArrayListTest = TestWithContext;

TEST_F(ArrayListTest, ArrayList) {
  HandleScope scope(i_isolate());
  Handle<ArrayList> array =
      ReadOnlyRoots(i_isolate()).empty_array_list_handle();
  EXPECT_EQ(0, array->length());
  array = ArrayList::Add(i_isolate(), array,
                         handle(Smi::FromInt(100), i_isolate()));
  EXPECT_EQ(1, array->length());
  EXPECT_EQ(100, Smi::ToInt(array->get(0)));
  array =
      ArrayList::Add(i_isolate(), array, handle(Smi::FromInt(200), i_isolate()),
                     handle(Smi::FromInt(300), i_isolate()));
  EXPECT_EQ(3, array->length());
  EXPECT_EQ(100, Smi::ToInt(array->get(0)));
  EXPECT_EQ(200, Smi::ToInt(array->get(1)));
  EXPECT_EQ(300, Smi::ToInt(array->get(2)));
  array->set(2, Smi::FromInt(400));
  EXPECT_EQ(400, Smi::ToInt(array->get(2)));
  array->set(2, ReadOnlyRoots(i_isolate()).undefined_value());
  array->set_length(2);
  EXPECT_EQ(2, array->length());
  EXPECT_EQ(100, Smi::ToInt(array->get(0)));
  EXPECT_EQ(200, Smi::ToInt(array->get(1)));
}

}  // namespace internal
}  // namespace v8
