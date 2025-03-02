// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_Add) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Object> lhs = args.at(0);
  Handle<Object> rhs = args.at(1);
  RETURN_RESULT_OR_FAILURE(isolate, Object::Add(isolate, lhs, rhs));
}


RUNTIME_FUNCTION(Runtime_Equal) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> x = args.at(0);
  DirectHandle<Object> y = args.at(1);
  Maybe<bool> result = Object::Equals(isolate, x, y);
  if (result.IsNothing()) return ReadOnlyRoots(isolate).exception();
  return isolate->heap()->ToBoolean(result.FromJust());
}

RUNTIME_FUNCTION(Runtime_NotEqual) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> x = args.at(0);
  DirectHandle<Object> y = args.at(1);
  Maybe<bool> result = Object::Equals(isolate, x, y);
  if (result.IsNothing()) return ReadOnlyRoots(isolate).exception();
  return isolate->heap()->ToBoolean(!result.FromJust());
}

RUNTIME_FUNCTION(Runtime_StrictEqual) {
  SealHandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Tagged<Object> x = args[0];
  Tagged<Object> y = args[1];
  return isolate->heap()->ToBoolean(Object::StrictEquals(x, y));
}

RUNTIME_FUNCTION(Runtime_StrictNotEqual) {
  SealHandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Tagged<Object> x = args[0];
  Tagged<Object> y = args[1];
  return isolate->heap()->ToBoolean(!Object::StrictEquals(x, y));
}

RUNTIME_FUNCTION(Runtime_ReferenceEqual) {
  SealHandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Tagged<Object> x = args[0];
  Tagged<Object> y = args[1];
  return isolate->heap()->ToBoolean(x == y);
}

RUNTIME_FUNCTION(Runtime_LessThan) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> x = args.at(0);
  DirectHandle<Object> y = args.at(1);
  Maybe<bool> result = Object::LessThan(isolate, x, y);
  if (result.IsNothing()) return ReadOnlyRoots(isolate).exception();
  return isolate->heap()->ToBoolean(result.FromJust());
}

RUNTIME_FUNCTION(Runtime_GreaterThan) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> x = args.at(0);
  DirectHandle<Object> y = args.at(1);
  Maybe<bool> result = Object::GreaterThan(isolate, x, y);
  if (result.IsNothing()) return ReadOnlyRoots(isolate).exception();
  return isolate->heap()->ToBoolean(result.FromJust());
}

RUNTIME_FUNCTION(Runtime_LessThanOrEqual) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> x = args.at(0);
  DirectHandle<Object> y = args.at(1);
  Maybe<bool> result = Object::LessThanOrEqual(isolate, x, y);
  if (result.IsNothing()) return ReadOnlyRoots(isolate).exception();
  return isolate->heap()->ToBoolean(result.FromJust());
}

RUNTIME_FUNCTION(Runtime_GreaterThanOrEqual) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> x = args.at(0);
  DirectHandle<Object> y = args.at(1);
  Maybe<bool> result = Object::GreaterThanOrEqual(isolate, x, y);
  if (result.IsNothing()) return ReadOnlyRoots(isolate).exception();
  return isolate->heap()->ToBoolean(result.FromJust());
}

}  // namespace internal
}  // namespace v8
