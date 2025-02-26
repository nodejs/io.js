// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/objects/js-generator-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_AsyncFunctionAwait) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_AsyncFunctionEnter) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_AsyncFunctionReject) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_AsyncFunctionResolve) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_CreateJSGeneratorObject) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);
  DirectHandle<JSAny> receiver = args.at<JSAny>(1);
  CHECK_IMPLIES(IsAsyncFunction(function->shared()->kind()),
                IsAsyncGeneratorFunction(function->shared()->kind()));
  CHECK(IsResumableFunction(function->shared()->kind()));

  // Underlying function needs to have bytecode available.
  DCHECK(function->shared()->HasBytecodeArray());
  int length;
  {
    // TODO(40931165): load bytecode array from function's dispatch table entry
    // when available instead of shared function info.
    Tagged<BytecodeArray> bytecode =
        function->shared()->GetBytecodeArray(isolate);

    length = bytecode->parameter_count_without_receiver() +
             bytecode->register_count();
  }
  DirectHandle<FixedArray> parameters_and_registers =
      isolate->factory()->NewFixedArray(length);

  DirectHandle<JSGeneratorObject> generator =
      isolate->factory()->NewJSGeneratorObject(function);
  DisallowGarbageCollection no_gc;
  Tagged<JSGeneratorObject> raw_generator = *generator;
  raw_generator->set_function(*function);
  raw_generator->set_context(isolate->context());
  raw_generator->set_receiver(*receiver);
  raw_generator->set_parameters_and_registers(*parameters_and_registers);
  raw_generator->set_resume_mode(JSGeneratorObject::ResumeMode::kNext);
  raw_generator->set_continuation(JSGeneratorObject::kGeneratorExecuting);
  if (IsJSAsyncGeneratorObject(*raw_generator)) {
    Cast<JSAsyncGeneratorObject>(raw_generator)->set_is_awaiting(0);
  }
  return raw_generator;
}

RUNTIME_FUNCTION(Runtime_GeneratorClose) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_GeneratorGetFunction) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSGeneratorObject> generator = args.at<JSGeneratorObject>(0);

  return generator->function();
}

RUNTIME_FUNCTION(Runtime_AsyncGeneratorAwait) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_AsyncGeneratorResolve) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_AsyncGeneratorReject) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_AsyncGeneratorYieldWithAwait) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_GeneratorGetResumeMode) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8
