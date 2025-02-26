// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/compiler/function-tester.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

using RunJSObjectsTest = TestWithContext;

TEST_F(RunJSObjectsTest, ArgumentsMapped) {
  FunctionTester T(i_isolate(), "(function(a) { return arguments; })");

  DirectHandle<JSAny> arguments = Cast<JSAny>(
      T.Call(T.NewNumber(19), T.NewNumber(23), T.NewNumber(42), T.NewNumber(65))
          .ToHandleChecked());
  CHECK(IsJSObject(*arguments) && !IsJSArray(*arguments));
  CHECK(Cast<JSObject>(*arguments)->HasSloppyArgumentsElements());
  DirectHandle<String> l = T.isolate->factory()->length_string();
  DirectHandle<Object> length =
      Object::GetProperty(T.isolate, arguments, l).ToHandleChecked();
  CHECK_EQ(4, Object::NumberValue(*length));
}

TEST_F(RunJSObjectsTest, ArgumentsUnmapped) {
  FunctionTester T(i_isolate(),
                   "(function(a) { 'use strict'; return arguments; })");

  DirectHandle<JSAny> arguments = Cast<JSAny>(
      T.Call(T.NewNumber(19), T.NewNumber(23), T.NewNumber(42), T.NewNumber(65))
          .ToHandleChecked());
  CHECK(IsJSObject(*arguments) && !IsJSArray(*arguments));
  CHECK(!Cast<JSObject>(*arguments)->HasSloppyArgumentsElements());
  DirectHandle<String> l = T.isolate->factory()->length_string();
  DirectHandle<Object> length =
      Object::GetProperty(T.isolate, arguments, l).ToHandleChecked();
  CHECK_EQ(4, Object::NumberValue(*length));
}

TEST_F(RunJSObjectsTest, ArgumentsRest) {
  FunctionTester T(i_isolate(), "(function(a, ...args) { return args; })");

  DirectHandle<JSAny> arguments = Cast<JSAny>(
      T.Call(T.NewNumber(19), T.NewNumber(23), T.NewNumber(42), T.NewNumber(65))
          .ToHandleChecked());
  CHECK(IsJSObject(*arguments) && IsJSArray(*arguments));
  CHECK(!Cast<JSObject>(*arguments)->HasSloppyArgumentsElements());
  DirectHandle<String> l = T.isolate->factory()->length_string();
  DirectHandle<Object> length =
      Object::GetProperty(T.isolate, arguments, l).ToHandleChecked();
  CHECK_EQ(3, Object::NumberValue(*length));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
