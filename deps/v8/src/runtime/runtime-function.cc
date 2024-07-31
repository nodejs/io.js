// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/accessors.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.

namespace v8 {
namespace internal {

// TODO(5530): Remove once uses in debug.js are gone.
RUNTIME_FUNCTION(Runtime_FunctionGetScriptSource) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSReceiver> function = args.at<JSReceiver>(0);

  if (IsJSFunction(*function)) {
    Handle<Object> script(
        Handle<JSFunction>::cast(function)->shared()->script(), isolate);
    if (IsScript(*script)) return Handle<Script>::cast(script)->source();
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_FunctionGetScriptId) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSReceiver> function = args.at<JSReceiver>(0);

  if (IsJSFunction(*function)) {
    Handle<Object> script(
        Handle<JSFunction>::cast(function)->shared()->script(), isolate);
    if (IsScript(*script)) {
      return Smi::FromInt(Handle<Script>::cast(script)->id());
    }
  }
  return Smi::FromInt(-1);
}

RUNTIME_FUNCTION(Runtime_FunctionGetSourceCode) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSReceiver> function = args.at<JSReceiver>(0);
  if (IsJSFunction(*function)) {
    Handle<SharedFunctionInfo> shared(
        Handle<JSFunction>::cast(function)->shared(), isolate);
    return *SharedFunctionInfo::GetSourceCode(isolate, shared);
  }
  return ReadOnlyRoots(isolate).undefined_value();
}


RUNTIME_FUNCTION(Runtime_FunctionGetScriptSourcePosition) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());

  auto fun = JSFunction::cast(args[0]);
  int pos = fun->shared()->StartPosition();
  return Smi::FromInt(pos);
}


RUNTIME_FUNCTION(Runtime_FunctionIsAPIFunction) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());

  auto f = JSFunction::cast(args[0]);
  return isolate->heap()->ToBoolean(f->shared()->IsApiFunction());
}


RUNTIME_FUNCTION(Runtime_Call) {
  HandleScope scope(isolate);
  DCHECK_LE(2, args.length());
  int const argc = args.length() - 2;
  Handle<Object> target = args.at(0);
  Handle<Object> receiver = args.at(1);
  base::ScopedVector<Handle<Object>> argv(argc);
  for (int i = 0; i < argc; ++i) {
    argv[i] = args.at(2 + i);
  }
  RETURN_RESULT_OR_FAILURE(
      isolate, Execution::Call(isolate, target, receiver, argc, argv.begin()));
}


}  // namespace internal
}  // namespace v8
