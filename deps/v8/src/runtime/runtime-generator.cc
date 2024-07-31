// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/objects/js-generator-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_AsyncFunctionAwaitCaught) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_AsyncFunctionAwaitUncaught) {
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
  Handle<JSFunction> function = args.at<JSFunction>(0);
  Handle<Object> receiver = args.at(1);
  CHECK_IMPLIES(IsAsyncFunction(function->shared()->kind()),
                IsAsyncGeneratorFunction(function->shared()->kind()));
  CHECK(IsResumableFunction(function->shared()->kind()));

  // Underlying function needs to have bytecode available.
  DCHECK(function->shared()->HasBytecodeArray());
  int size =
      function->shared()->internal_formal_parameter_count_without_receiver() +
      function->shared()->GetBytecodeArray(isolate)->register_count();
  Handle<FixedArray> parameters_and_registers =
      isolate->factory()->NewFixedArray(size);

  Handle<JSGeneratorObject> generator =
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
    JSAsyncGeneratorObject::cast(raw_generator)->set_is_awaiting(0);
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
  Handle<JSGeneratorObject> generator = args.at<JSGeneratorObject>(0);

  return generator->function();
}

RUNTIME_FUNCTION(Runtime_AsyncGeneratorAwaitCaught) {
  // Runtime call is implemented in InterpreterIntrinsics and lowered in
  // JSIntrinsicLowering
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_AsyncGeneratorAwaitUncaught) {
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

// Return true if {generator}'s PC has a catch handler. This allows
// catch prediction to happen from the AsyncGeneratorResumeNext stub.
RUNTIME_FUNCTION(Runtime_AsyncGeneratorHasCatchHandlerForPC) {
  DisallowGarbageCollection no_gc_scope;
  DCHECK_EQ(1, args.length());
  auto generator = JSAsyncGeneratorObject::cast(args[0]);

  int state = generator->continuation();
  DCHECK_NE(state, JSAsyncGeneratorObject::kGeneratorExecuting);

  // If state is 0 ("suspendedStart"), there is guaranteed to be no catch
  // handler. Otherwise, if state is below 0, the generator is closed and will
  // not reach a catch handler.
  if (state < 1) return ReadOnlyRoots(isolate).false_value();

  Tagged<SharedFunctionInfo> shared = generator->function()->shared();
  DCHECK(shared->HasBytecodeArray());
  HandlerTable handler_table(shared->GetBytecodeArray(isolate));

  int pc = Smi::cast(generator->input_or_debug_pos()).value();
  HandlerTable::CatchPrediction catch_prediction = HandlerTable::ASYNC_AWAIT;
  handler_table.LookupRange(pc, nullptr, &catch_prediction);
  return isolate->heap()->ToBoolean(catch_prediction == HandlerTable::CAUGHT);
}

}  // namespace internal
}  // namespace v8
