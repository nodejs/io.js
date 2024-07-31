// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-value.h"

#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace {

using ValueTest = TestWithContext;

TEST_F(ValueTest, TypecheckWitness) {
  Local<Object> global = context()->Global();
  Local<String> foo = String::NewFromUtf8Literal(isolate(), "foo");
  Local<String> bar = String::NewFromUtf8Literal(isolate(), "bar");
  v8::TypecheckWitness witness(isolate());
  EXPECT_FALSE(witness.Matches(global));
  EXPECT_FALSE(witness.Matches(foo));
  witness.Update(global);
  EXPECT_TRUE(witness.Matches(global));
  EXPECT_FALSE(witness.Matches(foo));
  witness.Update(foo);
  EXPECT_FALSE(witness.Matches(global));
  EXPECT_TRUE(witness.Matches(foo));
  EXPECT_TRUE(witness.Matches(bar));
}

}  // namespace
}  // namespace v8
