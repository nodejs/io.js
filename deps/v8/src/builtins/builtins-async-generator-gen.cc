// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-async-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/execution/frames-inl.h"
#include "src/objects/js-generator.h"
#include "src/objects/js-promise.h"

namespace v8 {
namespace internal {

namespace {

class AsyncGeneratorBuiltinsAssembler : public AsyncBuiltinsAssembler {
 public:
  explicit AsyncGeneratorBuiltinsAssembler(CodeAssemblerState* state)
      : AsyncBuiltinsAssembler(state) {}

  inline TNode<Smi> LoadGeneratorState(
      const TNode<JSGeneratorObject> generator) {
    return LoadObjectField<Smi>(generator,
                                JSGeneratorObject::kContinuationOffset);
  }

  inline TNode<BoolT> IsGeneratorStateClosed(const TNode<Smi> state) {
    return SmiEqual(state, SmiConstant(JSGeneratorObject::kGeneratorClosed));
  }
  inline TNode<BoolT> IsGeneratorClosed(
      const TNode<JSGeneratorObject> generator) {
    return IsGeneratorStateClosed(LoadGeneratorState(generator));
  }

  inline TNode<BoolT> IsGeneratorStateSuspended(const TNode<Smi> state) {
    return SmiGreaterThanOrEqual(state, SmiConstant(0));
  }

  inline TNode<BoolT> IsGeneratorSuspended(
      const TNode<JSGeneratorObject> generator) {
    return IsGeneratorStateSuspended(LoadGeneratorState(generator));
  }

  inline TNode<BoolT> IsGeneratorStateSuspendedAtStart(const TNode<Smi> state) {
    return SmiEqual(state, SmiConstant(0));
  }

  inline TNode<BoolT> IsGeneratorStateNotExecuting(const TNode<Smi> state) {
    return SmiNotEqual(state,
                       SmiConstant(JSGeneratorObject::kGeneratorExecuting));
  }
  inline TNode<BoolT> IsGeneratorNotExecuting(
      const TNode<JSGeneratorObject> generator) {
    return IsGeneratorStateNotExecuting(LoadGeneratorState(generator));
  }

  inline TNode<BoolT> IsGeneratorAwaiting(
      const TNode<JSGeneratorObject> generator) {
    TNode<Object> is_generator_awaiting =
        LoadObjectField(generator, JSAsyncGeneratorObject::kIsAwaitingOffset);
    return TaggedEqual(is_generator_awaiting, SmiConstant(1));
  }

  inline void SetGeneratorAwaiting(const TNode<JSGeneratorObject> generator) {
    CSA_DCHECK(this, Word32BinaryNot(IsGeneratorAwaiting(generator)));
    StoreObjectFieldNoWriteBarrier(
        generator, JSAsyncGeneratorObject::kIsAwaitingOffset, SmiConstant(1));
    CSA_DCHECK(this, IsGeneratorAwaiting(generator));
  }

  inline void SetGeneratorNotAwaiting(
      const TNode<JSGeneratorObject> generator) {
    CSA_DCHECK(this, IsGeneratorAwaiting(generator));
    StoreObjectFieldNoWriteBarrier(
        generator, JSAsyncGeneratorObject::kIsAwaitingOffset, SmiConstant(0));
    CSA_DCHECK(this, Word32BinaryNot(IsGeneratorAwaiting(generator)));
  }

  inline void CloseGenerator(const TNode<JSGeneratorObject> generator) {
    StoreObjectFieldNoWriteBarrier(
        generator, JSGeneratorObject::kContinuationOffset,
        SmiConstant(JSGeneratorObject::kGeneratorClosed));
  }

  inline TNode<HeapObject> LoadFirstAsyncGeneratorRequestFromQueue(
      const TNode<JSGeneratorObject> generator) {
    return LoadObjectField<HeapObject>(generator,
                                       JSAsyncGeneratorObject::kQueueOffset);
  }

  inline TNode<Smi> LoadResumeTypeFromAsyncGeneratorRequest(
      const TNode<AsyncGeneratorRequest> request) {
    return LoadObjectField<Smi>(request,
                                AsyncGeneratorRequest::kResumeModeOffset);
  }

  inline TNode<JSPromise> LoadPromiseFromAsyncGeneratorRequest(
      const TNode<AsyncGeneratorRequest> request) {
    return LoadObjectField<JSPromise>(request,
                                      AsyncGeneratorRequest::kPromiseOffset);
  }

  inline TNode<Object> LoadValueFromAsyncGeneratorRequest(
      const TNode<AsyncGeneratorRequest> request) {
    return LoadObjectField(request, AsyncGeneratorRequest::kValueOffset);
  }

  inline TNode<BoolT> IsAbruptResumeType(const TNode<Smi> resume_type) {
    return SmiNotEqual(resume_type, SmiConstant(JSGeneratorObject::kNext));
  }

  void AsyncGeneratorEnqueue(CodeStubArguments* args, TNode<Context> context,
                             TNode<Object> receiver, TNode<Object> value,
                             JSAsyncGeneratorObject::ResumeMode resume_mode,
                             const char* method_name);

  TNode<AsyncGeneratorRequest> TakeFirstAsyncGeneratorRequestFromQueue(
      TNode<JSAsyncGeneratorObject> generator);
  void AddAsyncGeneratorRequestToQueue(TNode<JSAsyncGeneratorObject> generator,
                                       TNode<AsyncGeneratorRequest> request);

  TNode<AsyncGeneratorRequest> AllocateAsyncGeneratorRequest(
      JSAsyncGeneratorObject::ResumeMode resume_mode,
      TNode<Object> resume_value, TNode<JSPromise> promise);

  // Shared implementation of the catchable and uncatchable variations of Await
  // for AsyncGenerators.
  template <typename Descriptor>
  void AsyncGeneratorAwait(bool is_catchable);
  void AsyncGeneratorAwaitResume(
      TNode<Context> context,
      TNode<JSAsyncGeneratorObject> async_generator_object, TNode<Object> value,
      JSAsyncGeneratorObject::ResumeMode resume_mode);
  void AsyncGeneratorAwaitResumeClosure(
      TNode<Context> context, TNode<Object> value,
      JSAsyncGeneratorObject::ResumeMode resume_mode);
  void AsyncGeneratorReturnClosedReject(
      TNode<Context> context,
      TNode<JSAsyncGeneratorObject> async_generator_object,
      TNode<Object> value);
};

// Shared implementation for the 3 Async Iterator protocol methods of Async
// Generators.
void AsyncGeneratorBuiltinsAssembler::AsyncGeneratorEnqueue(
    CodeStubArguments* args, TNode<Context> context, TNode<Object> receiver,
    TNode<Object> value, JSAsyncGeneratorObject::ResumeMode resume_mode,
    const char* method_name) {
  // AsyncGeneratorEnqueue produces a new Promise, and appends it to the list
  // of async generator requests to be executed. If the generator is not
  // presently executing, then this method will loop through, processing each
  // request from front to back.
  // This loop resides in AsyncGeneratorResumeNext.
  TNode<JSPromise> promise = NewJSPromise(context);

  Label if_receiverisincompatible(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(receiver), &if_receiverisincompatible);
  GotoIfNot(HasInstanceType(CAST(receiver), JS_ASYNC_GENERATOR_OBJECT_TYPE),
            &if_receiverisincompatible);

  {
    Label done(this);
    const TNode<JSAsyncGeneratorObject> generator = CAST(receiver);
    const TNode<AsyncGeneratorRequest> req =
        AllocateAsyncGeneratorRequest(resume_mode, value, promise);

    AddAsyncGeneratorRequestToQueue(generator, req);

    // Let state be generator.[[AsyncGeneratorState]]
    // If state is not "executing", then
    //     Perform AsyncGeneratorResumeNext(Generator)
    // Check if the {receiver} is running or already closed.
    TNode<Smi> continuation = LoadGeneratorState(generator);

    GotoIf(SmiEqual(continuation,
                    SmiConstant(JSAsyncGeneratorObject::kGeneratorExecuting)),
           &done);

    CallBuiltin(Builtin::kAsyncGeneratorResumeNext, context, generator);

    Goto(&done);
    BIND(&done);
    args->PopAndReturn(promise);
  }

  BIND(&if_receiverisincompatible);
  {
    CallBuiltin(Builtin::kRejectPromise, context, promise,
                MakeTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                              context, StringConstant(method_name), receiver),
                TrueConstant());
    args->PopAndReturn(promise);
  }
}

TNode<AsyncGeneratorRequest>
AsyncGeneratorBuiltinsAssembler::AllocateAsyncGeneratorRequest(
    JSAsyncGeneratorObject::ResumeMode resume_mode, TNode<Object> resume_value,
    TNode<JSPromise> promise) {
  TNode<HeapObject> request = Allocate(AsyncGeneratorRequest::kSize);
  StoreMapNoWriteBarrier(request, RootIndex::kAsyncGeneratorRequestMap);
  StoreObjectFieldNoWriteBarrier(request, AsyncGeneratorRequest::kNextOffset,
                                 UndefinedConstant());
  StoreObjectFieldNoWriteBarrier(request,
                                 AsyncGeneratorRequest::kResumeModeOffset,
                                 SmiConstant(resume_mode));
  StoreObjectFieldNoWriteBarrier(request, AsyncGeneratorRequest::kValueOffset,
                                 resume_value);
  StoreObjectFieldNoWriteBarrier(request, AsyncGeneratorRequest::kPromiseOffset,
                                 promise);
  StoreObjectFieldRoot(request, AsyncGeneratorRequest::kNextOffset,
                       RootIndex::kUndefinedValue);
  return CAST(request);
}

void AsyncGeneratorBuiltinsAssembler::AsyncGeneratorAwaitResume(
    TNode<Context> context,
    TNode<JSAsyncGeneratorObject> async_generator_object, TNode<Object> value,
    JSAsyncGeneratorObject::ResumeMode resume_mode) {
  SetGeneratorNotAwaiting(async_generator_object);

  CSA_SLOW_DCHECK(this, IsGeneratorSuspended(async_generator_object));

  // Remember the {resume_mode} for the {async_generator_object}.
  StoreObjectFieldNoWriteBarrier(async_generator_object,
                                 JSGeneratorObject::kResumeModeOffset,
                                 SmiConstant(resume_mode));

  // Push the promise for the {async_generator_object} back onto the catch
  // prediction stack to handle exceptions thrown after resuming from the
  // await properly.
  Label if_instrumentation(this, Label::kDeferred),
      if_instrumentation_done(this);
  Branch(IsDebugActive(), &if_instrumentation, &if_instrumentation_done);
  BIND(&if_instrumentation);
  {
    TNode<AsyncGeneratorRequest> request =
        CAST(LoadFirstAsyncGeneratorRequestFromQueue(async_generator_object));
    TNode<JSPromise> promise = LoadObjectField<JSPromise>(
        request, AsyncGeneratorRequest::kPromiseOffset);
    CallRuntime(Runtime::kDebugPushPromise, context, promise);
    Goto(&if_instrumentation_done);
  }
  BIND(&if_instrumentation_done);

  CallBuiltin(Builtin::kResumeGeneratorTrampoline, context, value,
              async_generator_object);

  TailCallBuiltin(Builtin::kAsyncGeneratorResumeNext, context,
                  async_generator_object);
}

void AsyncGeneratorBuiltinsAssembler::AsyncGeneratorAwaitResumeClosure(
    TNode<Context> context, TNode<Object> value,
    JSAsyncGeneratorObject::ResumeMode resume_mode) {
  const TNode<JSAsyncGeneratorObject> async_generator_object =
      CAST(LoadContextElement(context, Context::EXTENSION_INDEX));

  AsyncGeneratorAwaitResume(context, async_generator_object, value,
                            resume_mode);
}

template <typename Descriptor>
void AsyncGeneratorBuiltinsAssembler::AsyncGeneratorAwait(bool is_catchable) {
  auto async_generator_object =
      Parameter<JSAsyncGeneratorObject>(Descriptor::kAsyncGeneratorObject);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto context = Parameter<Context>(Descriptor::kContext);

  TNode<AsyncGeneratorRequest> request =
      CAST(LoadFirstAsyncGeneratorRequestFromQueue(async_generator_object));
  TNode<JSPromise> outer_promise = LoadObjectField<JSPromise>(
      request, AsyncGeneratorRequest::kPromiseOffset);

  Await(context, async_generator_object, value, outer_promise,
        AsyncGeneratorAwaitResolveSharedFunConstant(),
        AsyncGeneratorAwaitRejectSharedFunConstant(), is_catchable);
  SetGeneratorAwaiting(async_generator_object);
  Return(UndefinedConstant());
}

void AsyncGeneratorBuiltinsAssembler::AddAsyncGeneratorRequestToQueue(
    TNode<JSAsyncGeneratorObject> generator,
    TNode<AsyncGeneratorRequest> request) {
  TVARIABLE(HeapObject, var_current);
  Label empty(this), loop(this, &var_current), done(this);

  var_current = LoadObjectField<HeapObject>(
      generator, JSAsyncGeneratorObject::kQueueOffset);
  Branch(IsUndefined(var_current.value()), &empty, &loop);

  BIND(&empty);
  {
    StoreObjectField(generator, JSAsyncGeneratorObject::kQueueOffset, request);
    Goto(&done);
  }

  BIND(&loop);
  {
    Label loop_next(this), next_empty(this);
    TNode<AsyncGeneratorRequest> current = CAST(var_current.value());
    TNode<HeapObject> next = LoadObjectField<HeapObject>(
        current, AsyncGeneratorRequest::kNextOffset);

    Branch(IsUndefined(next), &next_empty, &loop_next);
    BIND(&next_empty);
    {
      StoreObjectField(current, AsyncGeneratorRequest::kNextOffset, request);
      Goto(&done);
    }

    BIND(&loop_next);
    {
      var_current = next;
      Goto(&loop);
    }
  }
  BIND(&done);
}

TNode<AsyncGeneratorRequest>
AsyncGeneratorBuiltinsAssembler::TakeFirstAsyncGeneratorRequestFromQueue(
    TNode<JSAsyncGeneratorObject> generator) {
  // Removes and returns the first AsyncGeneratorRequest from a
  // JSAsyncGeneratorObject's queue. Asserts that the queue is not empty.
  TNode<AsyncGeneratorRequest> request = LoadObjectField<AsyncGeneratorRequest>(
      generator, JSAsyncGeneratorObject::kQueueOffset);

  TNode<Object> next =
      LoadObjectField(request, AsyncGeneratorRequest::kNextOffset);

  StoreObjectField(generator, JSAsyncGeneratorObject::kQueueOffset, next);
  return request;
}

void AsyncGeneratorBuiltinsAssembler::AsyncGeneratorReturnClosedReject(
    TNode<Context> context, TNode<JSAsyncGeneratorObject> generator,
    TNode<Object> value) {
  SetGeneratorNotAwaiting(generator);

  // https://tc39.github.io/proposal-async-iteration/
  //    #async-generator-resume-next-return-processor-rejected step 2:
  // Return ! AsyncGeneratorReject(_F_.[[Generator]], _reason_).
  CallBuiltin(Builtin::kAsyncGeneratorReject, context, generator, value);

  TailCallBuiltin(Builtin::kAsyncGeneratorResumeNext, context, generator);
}
}  // namespace

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-asyncgenerator-prototype-next
TF_BUILTIN(AsyncGeneratorPrototypeNext, AsyncGeneratorBuiltinsAssembler) {
  const int kValueArg = 0;

  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  TNode<Object> generator = args.GetReceiver();
  TNode<Object> value = args.GetOptionalArgumentValue(kValueArg);
  auto context = Parameter<Context>(Descriptor::kContext);

  AsyncGeneratorEnqueue(&args, context, generator, value,
                        JSAsyncGeneratorObject::kNext,
                        "[AsyncGenerator].prototype.next");
}

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-asyncgenerator-prototype-return
TF_BUILTIN(AsyncGeneratorPrototypeReturn, AsyncGeneratorBuiltinsAssembler) {
  const int kValueArg = 0;

  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  TNode<Object> generator = args.GetReceiver();
  TNode<Object> value = args.GetOptionalArgumentValue(kValueArg);
  auto context = Parameter<Context>(Descriptor::kContext);

  AsyncGeneratorEnqueue(&args, context, generator, value,
                        JSAsyncGeneratorObject::kReturn,
                        "[AsyncGenerator].prototype.return");
}

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-asyncgenerator-prototype-throw
TF_BUILTIN(AsyncGeneratorPrototypeThrow, AsyncGeneratorBuiltinsAssembler) {
  const int kValueArg = 0;

  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  TNode<Object> generator = args.GetReceiver();
  TNode<Object> value = args.GetOptionalArgumentValue(kValueArg);
  auto context = Parameter<Context>(Descriptor::kContext);

  AsyncGeneratorEnqueue(&args, context, generator, value,
                        JSAsyncGeneratorObject::kThrow,
                        "[AsyncGenerator].prototype.throw");
}

TF_BUILTIN(AsyncGeneratorAwaitResolveClosure, AsyncGeneratorBuiltinsAssembler) {
  auto value = Parameter<Object>(Descriptor::kValue);
  auto context = Parameter<Context>(Descriptor::kContext);
  AsyncGeneratorAwaitResumeClosure(context, value,
                                   JSAsyncGeneratorObject::kNext);
}

TF_BUILTIN(AsyncGeneratorAwaitRejectClosure, AsyncGeneratorBuiltinsAssembler) {
  auto value = Parameter<Object>(Descriptor::kValue);
  auto context = Parameter<Context>(Descriptor::kContext);
  // Restart in Rethrow mode, as this exception was already thrown and we don't
  // want to trigger a second debug break event or change the message location.
  AsyncGeneratorAwaitResumeClosure(context, value,
                                   JSAsyncGeneratorObject::kRethrow);
}

TF_BUILTIN(AsyncGeneratorAwaitUncaught, AsyncGeneratorBuiltinsAssembler) {
  const bool kIsCatchable = false;
  AsyncGeneratorAwait<Descriptor>(kIsCatchable);
}

TF_BUILTIN(AsyncGeneratorAwaitCaught, AsyncGeneratorBuiltinsAssembler) {
  const bool kIsCatchable = true;
  AsyncGeneratorAwait<Descriptor>(kIsCatchable);
}

TF_BUILTIN(AsyncGeneratorResumeNext, AsyncGeneratorBuiltinsAssembler) {
  const auto generator =
      Parameter<JSAsyncGeneratorObject>(Descriptor::kGenerator);
  const auto context = Parameter<Context>(Descriptor::kContext);

  // The penultimate step of proposal-async-iteration/#sec-asyncgeneratorresolve
  // and proposal-async-iteration/#sec-asyncgeneratorreject both recursively
  // invoke AsyncGeneratorResumeNext() again.
  //
  // This implementation does not implement this recursively, but instead
  // performs a loop in AsyncGeneratorResumeNext, which  continues as long as
  // there is an AsyncGeneratorRequest in the queue, and as long as the
  // generator is not suspended due to an AwaitExpression.
  TVARIABLE(Smi, var_state, LoadGeneratorState(generator));
  TVARIABLE(HeapObject, var_next,
            LoadFirstAsyncGeneratorRequestFromQueue(generator));
  Label start(this, {&var_state, &var_next});
  Goto(&start);
  BIND(&start);

  CSA_DCHECK(this, IsGeneratorNotExecuting(generator));

  // Stop resuming if suspended for Await.
  ReturnIf(IsGeneratorAwaiting(generator), UndefinedConstant());

  // Stop resuming if request queue is empty.
  ReturnIf(IsUndefined(var_next.value()), UndefinedConstant());

  const TNode<AsyncGeneratorRequest> next = CAST(var_next.value());
  const TNode<Smi> resume_type = LoadResumeTypeFromAsyncGeneratorRequest(next);

  Label if_abrupt(this), if_normal(this), resume_generator(this);
  Branch(IsAbruptResumeType(resume_type), &if_abrupt, &if_normal);
  BIND(&if_abrupt);
  {
    Label settle_promise(this), if_return(this), if_throw(this);
    GotoIfNot(IsGeneratorStateSuspendedAtStart(var_state.value()),
              &settle_promise);
    CloseGenerator(generator);
    var_state = SmiConstant(JSGeneratorObject::kGeneratorClosed);
    Goto(&settle_promise);

    BIND(&settle_promise);
    TNode<Object> next_value = LoadValueFromAsyncGeneratorRequest(next);
    Branch(SmiEqual(resume_type, SmiConstant(JSGeneratorObject::kReturn)),
           &if_return, &if_throw);

    BIND(&if_return);
    // For "return" completions, await the sent value. If the Await succeeds,
    // and the generator is not closed, resume the generator with a "return"
    // completion to allow `finally` blocks to be evaluated. Otherwise, perform
    // AsyncGeneratorResolve(awaitedValue, true). If the await fails and the
    // generator is not closed, resume the generator with a "throw" completion.
    // If the generator was closed, perform AsyncGeneratorReject(thrownValue).
    // In all cases, the last step is to call AsyncGeneratorResumeNext.
    TNode<Object> is_caught = CallRuntime(
        Runtime::kAsyncGeneratorHasCatchHandlerForPC, context, generator);
    TailCallBuiltin(Builtin::kAsyncGeneratorReturn, context, generator,
                    next_value, is_caught);

    BIND(&if_throw);
    GotoIfNot(IsGeneratorStateClosed(var_state.value()), &resume_generator);
    CallBuiltin(Builtin::kAsyncGeneratorReject, context, generator, next_value);
    var_next = LoadFirstAsyncGeneratorRequestFromQueue(generator);
    Goto(&start);
  }

  BIND(&if_normal);
  {
    GotoIfNot(IsGeneratorStateClosed(var_state.value()), &resume_generator);
    CallBuiltin(Builtin::kAsyncGeneratorResolve, context, generator,
                UndefinedConstant(), TrueConstant());
    var_state = LoadGeneratorState(generator);
    var_next = LoadFirstAsyncGeneratorRequestFromQueue(generator);
    Goto(&start);
  }

  BIND(&resume_generator);
  {
    // Remember the {resume_type} for the {generator}.
    StoreObjectFieldNoWriteBarrier(
        generator, JSGeneratorObject::kResumeModeOffset, resume_type);

    Label if_instrumentation(this, Label::kDeferred),
        if_instrumentation_done(this);
    Branch(IsDebugActive(), &if_instrumentation, &if_instrumentation_done);
    BIND(&if_instrumentation);
    {
      const TNode<JSPromise> promise = LoadObjectField<JSPromise>(
          next, AsyncGeneratorRequest::kPromiseOffset);
      CallRuntime(Runtime::kDebugPushPromise, context, promise);
      Goto(&if_instrumentation_done);
    }
    BIND(&if_instrumentation_done);

    CallBuiltin(Builtin::kResumeGeneratorTrampoline, context,
                LoadValueFromAsyncGeneratorRequest(next), generator);
    var_state = LoadGeneratorState(generator);
    var_next = LoadFirstAsyncGeneratorRequestFromQueue(generator);
    Goto(&start);
  }
}

TF_BUILTIN(AsyncGeneratorResolve, AsyncGeneratorBuiltinsAssembler) {
  const auto generator =
      Parameter<JSAsyncGeneratorObject>(Descriptor::kGenerator);
  const auto value = Parameter<Object>(Descriptor::kValue);
  const auto done = Parameter<Object>(Descriptor::kDone);
  const auto context = Parameter<Context>(Descriptor::kContext);

  CSA_DCHECK(this, Word32BinaryNot(IsGeneratorAwaiting(generator)));

  // This operation should be called only when the `value` parameter has been
  // Await-ed. Typically, this means `value` is not a JSPromise value. However,
  // it may be a JSPromise value whose "then" method has been overridden to a
  // non-callable value. This can't be checked with assertions due to being
  // observable, but keep it in mind.

  const TNode<AsyncGeneratorRequest> next =
      TakeFirstAsyncGeneratorRequestFromQueue(generator);
  const TNode<JSPromise> promise = LoadPromiseFromAsyncGeneratorRequest(next);

  // Let iteratorResult be CreateIterResultObject(value, done).
  const TNode<HeapObject> iter_result = Allocate(JSIteratorResult::kSize);
  {
    TNode<Map> map = CAST(LoadContextElement(
        LoadNativeContext(context), Context::ITERATOR_RESULT_MAP_INDEX));
    StoreMapNoWriteBarrier(iter_result, map);
    StoreObjectFieldRoot(iter_result, JSIteratorResult::kPropertiesOrHashOffset,
                         RootIndex::kEmptyFixedArray);
    StoreObjectFieldRoot(iter_result, JSIteratorResult::kElementsOffset,
                         RootIndex::kEmptyFixedArray);
    StoreObjectFieldNoWriteBarrier(iter_result, JSIteratorResult::kValueOffset,
                                   value);
    StoreObjectFieldNoWriteBarrier(iter_result, JSIteratorResult::kDoneOffset,
                                   done);
  }

  // We know that {iter_result} itself doesn't have any "then" property (a
  // freshly allocated IterResultObject only has "value" and "done" properties)
  // and we also know that the [[Prototype]] of {iter_result} is the intrinsic
  // %ObjectPrototype%. So we can skip the [[Resolve]] logic here completely
  // and directly call into the FulfillPromise operation if we can prove
  // that the %ObjectPrototype% also doesn't have any "then" property. This
  // is guarded by the Promise#then() protector.
  // If the PromiseHooks are enabled, we cannot take the shortcut here, since
  // the "promiseResolve" hook would not be fired otherwise.
  Label if_fast(this), if_slow(this, Label::kDeferred), return_promise(this);
  GotoIfForceSlowPath(&if_slow);
  GotoIf(IsIsolatePromiseHookEnabledOrHasAsyncEventDelegate(), &if_slow);
  Branch(IsPromiseThenProtectorCellInvalid(), &if_slow, &if_fast);

  BIND(&if_fast);
  {
    // Skip the "then" on {iter_result} and directly fulfill the {promise}
    // with the {iter_result}.
    CallBuiltin(Builtin::kFulfillPromise, context, promise, iter_result);
    Goto(&return_promise);
  }

  BIND(&if_slow);
  {
    // Perform Call(promiseCapability.[[Resolve]], undefined, «iteratorResult»).
    CallBuiltin(Builtin::kResolvePromise, context, promise, iter_result);
    Goto(&return_promise);
  }

  // Per spec, AsyncGeneratorResolve() returns undefined. However, for the
  // benefit of %TraceExit(), return the Promise.
  BIND(&return_promise);
  Return(promise);
}

TF_BUILTIN(AsyncGeneratorReject, AsyncGeneratorBuiltinsAssembler) {
  const auto generator =
      Parameter<JSAsyncGeneratorObject>(Descriptor::kGenerator);
  const auto value = Parameter<Object>(Descriptor::kValue);
  const auto context = Parameter<Context>(Descriptor::kContext);

  TNode<AsyncGeneratorRequest> next =
      TakeFirstAsyncGeneratorRequestFromQueue(generator);
  TNode<JSPromise> promise = LoadPromiseFromAsyncGeneratorRequest(next);

  // No debug event needed, there was already a debug event that got us here.
  Return(CallBuiltin(Builtin::kRejectPromise, context, promise, value,
                     FalseConstant()));
}

TF_BUILTIN(AsyncGeneratorYieldWithAwait, AsyncGeneratorBuiltinsAssembler) {
  const auto generator = Parameter<JSGeneratorObject>(Descriptor::kGenerator);
  const auto value = Parameter<Object>(Descriptor::kValue);
  const auto is_caught = Parameter<Boolean>(Descriptor::kIsCaught);
  const auto context = Parameter<Context>(Descriptor::kContext);

  const TNode<AsyncGeneratorRequest> request =
      CAST(LoadFirstAsyncGeneratorRequestFromQueue(generator));
  const TNode<JSPromise> outer_promise =
      LoadPromiseFromAsyncGeneratorRequest(request);

  Await(context, generator, value, outer_promise,
        AsyncGeneratorYieldWithAwaitResolveSharedFunConstant(),
        AsyncGeneratorAwaitRejectSharedFunConstant(), is_caught);
  SetGeneratorAwaiting(generator);
  Return(UndefinedConstant());
}

TF_BUILTIN(AsyncGeneratorYieldWithAwaitResolveClosure,
           AsyncGeneratorBuiltinsAssembler) {
  const auto context = Parameter<Context>(Descriptor::kContext);
  const auto value = Parameter<Object>(Descriptor::kValue);
  const TNode<JSAsyncGeneratorObject> generator =
      CAST(LoadContextElement(context, Context::EXTENSION_INDEX));

  SetGeneratorNotAwaiting(generator);

  // Per proposal-async-iteration/#sec-asyncgeneratoryield step 9
  // Return ! AsyncGeneratorResolve(_F_.[[Generator]], _value_, *false*).
  CallBuiltin(Builtin::kAsyncGeneratorResolve, context, generator, value,
              FalseConstant());

  TailCallBuiltin(Builtin::kAsyncGeneratorResumeNext, context, generator);
}

TF_BUILTIN(AsyncGeneratorReturn, AsyncGeneratorBuiltinsAssembler) {
  // AsyncGeneratorReturn is called when resuming requests with "return" resume
  // modes. It is similar to AsyncGeneratorAwait(), but selects different
  // resolve/reject closures depending on whether or not the generator is marked
  // as closed, and handles exception on Await explicitly.
  //
  // In particular, non-closed generators will resume the generator with either
  // "return" or "throw" resume modes, allowing finally blocks or catch blocks
  // to be evaluated, as if the `await` were performed within the body of the
  // generator. (per proposal-async-iteration/#sec-asyncgeneratoryield step 8.b)
  //
  // Closed generators do not resume the generator in the resolve/reject
  // closures, but instead simply perform AsyncGeneratorResolve or
  // AsyncGeneratorReject with the awaited value
  // (per proposal-async-iteration/#sec-asyncgeneratorresumenext step 10.b.i)
  //
  // In all cases, the final step is to jump back to AsyncGeneratorResumeNext.
  const auto generator =
      Parameter<JSAsyncGeneratorObject>(Descriptor::kGenerator);
  const auto value = Parameter<Object>(Descriptor::kValue);
  const auto is_caught = Parameter<Boolean>(Descriptor::kIsCaught);
  const TNode<AsyncGeneratorRequest> req =
      CAST(LoadFirstAsyncGeneratorRequestFromQueue(generator));

  Label perform_await(this);
  TVARIABLE(SharedFunctionInfo, var_on_resolve,
            AsyncGeneratorReturnClosedResolveSharedFunConstant());

  TVARIABLE(SharedFunctionInfo, var_on_reject,
            AsyncGeneratorReturnClosedRejectSharedFunConstant());

  const TNode<Smi> state = LoadGeneratorState(generator);
  GotoIf(IsGeneratorStateClosed(state), &perform_await);
  var_on_resolve = AsyncGeneratorReturnResolveSharedFunConstant();
  var_on_reject = AsyncGeneratorAwaitRejectSharedFunConstant();

  Goto(&perform_await);

  BIND(&perform_await);

  SetGeneratorAwaiting(generator);
  auto context = Parameter<Context>(Descriptor::kContext);
  const TNode<JSPromise> outer_promise =
      LoadPromiseFromAsyncGeneratorRequest(req);

  Label done(this), await_exception(this, Label::kDeferred),
      closed_await_exception(this, Label::kDeferred);
  TVARIABLE(Object, var_exception);
  {
    compiler::ScopedExceptionHandler handler(this, &await_exception,
                                             &var_exception);

    Await(context, generator, value, outer_promise, var_on_resolve.value(),
          var_on_reject.value(), is_caught);
  }
  Goto(&done);

  BIND(&await_exception);
  {
    GotoIf(IsGeneratorStateClosed(state), &closed_await_exception);
    // Tail call to AsyncGeneratorResumeNext
    AsyncGeneratorAwaitResume(context, generator, var_exception.value(),
                              JSGeneratorObject::kThrow);
  }

  BIND(&closed_await_exception);
  {
    // Tail call to AsyncGeneratorResumeNext
    AsyncGeneratorReturnClosedReject(context, generator, var_exception.value());
  }

  BIND(&done);
  Return(UndefinedConstant());
}

// On-resolve closure for Await in AsyncGeneratorReturn
// Resume the generator with "return" resume_mode, and finally perform
// AsyncGeneratorResumeNext. Per
// proposal-async-iteration/#sec-asyncgeneratoryield step 8.e
TF_BUILTIN(AsyncGeneratorReturnResolveClosure,
           AsyncGeneratorBuiltinsAssembler) {
  const auto context = Parameter<Context>(Descriptor::kContext);
  const auto value = Parameter<Object>(Descriptor::kValue);
  AsyncGeneratorAwaitResumeClosure(context, value, JSGeneratorObject::kReturn);
}

// On-resolve closure for Await in AsyncGeneratorReturn
// Perform AsyncGeneratorResolve({awaited_value}, true) and finally perform
// AsyncGeneratorResumeNext.
TF_BUILTIN(AsyncGeneratorReturnClosedResolveClosure,
           AsyncGeneratorBuiltinsAssembler) {
  const auto context = Parameter<Context>(Descriptor::kContext);
  const auto value = Parameter<Object>(Descriptor::kValue);
  const TNode<JSAsyncGeneratorObject> generator =
      CAST(LoadContextElement(context, Context::EXTENSION_INDEX));

  SetGeneratorNotAwaiting(generator);

  // https://tc39.github.io/proposal-async-iteration/
  //    #async-generator-resume-next-return-processor-fulfilled step 2:
  //  Return ! AsyncGeneratorResolve(_F_.[[Generator]], _value_, *true*).
  CallBuiltin(Builtin::kAsyncGeneratorResolve, context, generator, value,
              TrueConstant());

  TailCallBuiltin(Builtin::kAsyncGeneratorResumeNext, context, generator);
}

TF_BUILTIN(AsyncGeneratorReturnClosedRejectClosure,
           AsyncGeneratorBuiltinsAssembler) {
  const auto context = Parameter<Context>(Descriptor::kContext);
  const auto value = Parameter<Object>(Descriptor::kValue);
  const TNode<JSAsyncGeneratorObject> generator =
      CAST(LoadContextElement(context, Context::EXTENSION_INDEX));

  AsyncGeneratorReturnClosedReject(context, generator, value);
}

}  // namespace internal
}  // namespace v8
