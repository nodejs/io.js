// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-async-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/objects/js-generator.h"
#include "src/objects/js-promise.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

class AsyncFunctionBuiltinsAssembler : public AsyncBuiltinsAssembler {
 public:
  explicit AsyncFunctionBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : AsyncBuiltinsAssembler(state) {}

 protected:
  template <typename Descriptor>
  void AsyncFunctionAwait();

  void AsyncFunctionAwaitResumeClosure(
      const TNode<Context> context, const TNode<Object> sent_value,
      JSGeneratorObject::ResumeMode resume_mode);
};

void AsyncFunctionBuiltinsAssembler::AsyncFunctionAwaitResumeClosure(
    TNode<Context> context, TNode<Object> sent_value,
    JSGeneratorObject::ResumeMode resume_mode) {
  DCHECK(resume_mode == JSGeneratorObject::kNext ||
         resume_mode == JSGeneratorObject::kThrow);

  TNode<JSAsyncFunctionObject> async_function_object =
      CAST(LoadContextElement(context, Context::EXTENSION_INDEX));

  // Inline version of GeneratorPrototypeNext / GeneratorPrototypeReturn with
  // unnecessary runtime checks removed.

  // Ensure that the {async_function_object} is neither closed nor running.
  CSA_SLOW_DCHECK(
      this, SmiGreaterThan(
                LoadObjectField<Smi>(async_function_object,
                                     JSGeneratorObject::kContinuationOffset),
                SmiConstant(JSGeneratorObject::kGeneratorClosed)));

  // Remember the {resume_mode} for the {async_function_object}.
  StoreObjectFieldNoWriteBarrier(async_function_object,
                                 JSGeneratorObject::kResumeModeOffset,
                                 SmiConstant(resume_mode));

  // Resume the {receiver} using our trampoline.
  CallBuiltin(Builtin::kResumeGeneratorTrampoline, context, sent_value,
              async_function_object);

  // The resulting Promise is a throwaway, so it doesn't matter what it
  // resolves to. What is important is that we don't end up keeping the
  // whole chain of intermediate Promises alive by returning the return value
  // of ResumeGenerator, as that would create a memory leak.
}

TF_BUILTIN(AsyncFunctionEnter, AsyncFunctionBuiltinsAssembler) {
  auto closure = Parameter<JSFunction>(Descriptor::kClosure);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto context = Parameter<Context>(Descriptor::kContext);

  // Compute the number of registers and parameters.
  TNode<SharedFunctionInfo> shared = LoadObjectField<SharedFunctionInfo>(
      closure, JSFunction::kSharedFunctionInfoOffset);
  TNode<IntPtrT> formal_parameter_count = ChangeInt32ToIntPtr(
      LoadSharedFunctionInfoFormalParameterCountWithoutReceiver(shared));
  TNode<BytecodeArray> bytecode_array =
      LoadSharedFunctionInfoBytecodeArray(shared);
  TNode<IntPtrT> frame_size = ChangeInt32ToIntPtr(LoadObjectField<Uint32T>(
      bytecode_array, BytecodeArray::kFrameSizeOffset));
  TNode<IntPtrT> parameters_and_register_length =
      Signed(IntPtrAdd(WordSar(frame_size, IntPtrConstant(kTaggedSizeLog2)),
                       formal_parameter_count));

  // Allocate and initialize the register file.
  TNode<FixedArrayBase> parameters_and_registers =
      AllocateFixedArray(HOLEY_ELEMENTS, parameters_and_register_length);
  FillFixedArrayWithValue(HOLEY_ELEMENTS, parameters_and_registers,
                          IntPtrConstant(0), parameters_and_register_length,
                          RootIndex::kUndefinedValue);

  // Allocate and initialize the promise.
  TNode<JSPromise> promise = NewJSPromise(context);

  // Allocate and initialize the async function object.
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> async_function_object_map = CAST(LoadContextElement(
      native_context, Context::ASYNC_FUNCTION_OBJECT_MAP_INDEX));
  TNode<JSAsyncFunctionObject> async_function_object =
      UncheckedCast<JSAsyncFunctionObject>(
          AllocateInNewSpace(JSAsyncFunctionObject::kHeaderSize));
  StoreMapNoWriteBarrier(async_function_object, async_function_object_map);
  StoreObjectFieldRoot(async_function_object,
                       JSAsyncFunctionObject::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(async_function_object,
                       JSAsyncFunctionObject::kElementsOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldNoWriteBarrier(
      async_function_object, JSAsyncFunctionObject::kFunctionOffset, closure);
  StoreObjectFieldNoWriteBarrier(
      async_function_object, JSAsyncFunctionObject::kContextOffset, context);
  StoreObjectFieldNoWriteBarrier(
      async_function_object, JSAsyncFunctionObject::kReceiverOffset, receiver);
  StoreObjectFieldNoWriteBarrier(async_function_object,
                                 JSAsyncFunctionObject::kInputOrDebugPosOffset,
                                 SmiConstant(0));
  StoreObjectFieldNoWriteBarrier(async_function_object,
                                 JSAsyncFunctionObject::kResumeModeOffset,
                                 SmiConstant(JSAsyncFunctionObject::kNext));
  StoreObjectFieldNoWriteBarrier(
      async_function_object, JSAsyncFunctionObject::kContinuationOffset,
      SmiConstant(JSAsyncFunctionObject::kGeneratorExecuting));
  StoreObjectFieldNoWriteBarrier(
      async_function_object,
      JSAsyncFunctionObject::kParametersAndRegistersOffset,
      parameters_and_registers);
  StoreObjectFieldNoWriteBarrier(
      async_function_object, JSAsyncFunctionObject::kPromiseOffset, promise);

  Return(async_function_object);
}

TF_BUILTIN(AsyncFunctionReject, AsyncFunctionBuiltinsAssembler) {
  auto async_function_object =
      Parameter<JSAsyncFunctionObject>(Descriptor::kAsyncFunctionObject);
  auto reason = Parameter<Object>(Descriptor::kReason);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<JSPromise> promise = LoadObjectField<JSPromise>(
      async_function_object, JSAsyncFunctionObject::kPromiseOffset);

  // Reject the {promise} for the given {reason}, disabling the
  // additional debug event for the rejection since a debug event
  // already happend for the exception that got us here.
  CallBuiltin(Builtin::kRejectPromise, context, promise, reason,
              FalseConstant());

  Return(promise);
}

TF_BUILTIN(AsyncFunctionResolve, AsyncFunctionBuiltinsAssembler) {
  auto async_function_object =
      Parameter<JSAsyncFunctionObject>(Descriptor::kAsyncFunctionObject);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<JSPromise> promise = LoadObjectField<JSPromise>(
      async_function_object, JSAsyncFunctionObject::kPromiseOffset);

  CallBuiltin(Builtin::kResolvePromise, context, promise, value);

  Return(promise);
}

// AsyncFunctionReject and AsyncFunctionResolve are both required to return
// the promise instead of the result of RejectPromise or ResolvePromise
// respectively from a lazy deoptimization.
TF_BUILTIN(AsyncFunctionLazyDeoptContinuation, AsyncFunctionBuiltinsAssembler) {
  auto promise = Parameter<JSPromise>(Descriptor::kPromise);
  Return(promise);
}

TF_BUILTIN(AsyncFunctionAwaitRejectClosure, AsyncFunctionBuiltinsAssembler) {
  CSA_DCHECK_JS_ARGC_EQ(this, 1);
  const auto sentError = Parameter<Object>(Descriptor::kSentError);
  const auto context = Parameter<Context>(Descriptor::kContext);

  AsyncFunctionAwaitResumeClosure(context, sentError,
                                  JSGeneratorObject::kThrow);
  Return(UndefinedConstant());
}

TF_BUILTIN(AsyncFunctionAwaitResolveClosure, AsyncFunctionBuiltinsAssembler) {
  CSA_DCHECK_JS_ARGC_EQ(this, 1);
  const auto sentValue = Parameter<Object>(Descriptor::kSentValue);
  const auto context = Parameter<Context>(Descriptor::kContext);

  AsyncFunctionAwaitResumeClosure(context, sentValue, JSGeneratorObject::kNext);
  Return(UndefinedConstant());
}

// ES#abstract-ops-async-function-await
// AsyncFunctionAwait ( value )
// Shared logic for the core of await. The parser desugars
//   await value
// into
//   yield AsyncFunctionAwait{Caught,Uncaught}(.generator_object, value)
// The 'value' parameter is the value; the .generator_object stands in
// for the asyncContext.
template <typename Descriptor>
void AsyncFunctionBuiltinsAssembler::AsyncFunctionAwait() {
  auto async_function_object =
      Parameter<JSAsyncFunctionObject>(Descriptor::kAsyncFunctionObject);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto context = Parameter<Context>(Descriptor::kContext);

  TNode<SharedFunctionInfo> on_resolve_sfi =
      AsyncFunctionAwaitResolveSharedFunConstant();
  TNode<SharedFunctionInfo> on_reject_sfi =
      AsyncFunctionAwaitRejectSharedFunConstant();
  TNode<JSPromise> outer_promise = LoadObjectField<JSPromise>(
      async_function_object, JSAsyncFunctionObject::kPromiseOffset);
  Await(context, async_function_object, value, outer_promise, on_resolve_sfi,
        on_reject_sfi);

  // Return outer promise to avoid adding an load of the outer promise before
  // suspending in BytecodeGenerator.
  Return(outer_promise);
}

// Called by the parser from the desugaring of 'await'.
TF_BUILTIN(AsyncFunctionAwait, AsyncFunctionBuiltinsAssembler) {
  AsyncFunctionAwait<Descriptor>();
}

}  // namespace internal
}  // namespace v8
