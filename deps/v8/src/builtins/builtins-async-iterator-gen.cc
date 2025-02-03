// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/builtins/builtins-async-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/execution/frames-inl.h"

namespace v8 {
namespace internal {

#include "src/codegen/define-code-stub-assembler-macros.inc"

namespace {
class AsyncFromSyncBuiltinsAssembler : public AsyncBuiltinsAssembler {
 public:
  // The 'next' and 'return' take an optional value parameter, and the 'throw'
  // method take an optional reason parameter.
  static const int kValueOrReasonArg = 0;

  explicit AsyncFromSyncBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : AsyncBuiltinsAssembler(state) {}

  using UndefinedMethodHandler = std::function<void(
      const TNode<NativeContext> native_context, const TNode<JSPromise> promise,
      const TNode<JSReceiver> sync_iterator, Label* if_exception)>;
  using SyncIteratorNodeGenerator =
      std::function<TNode<Object>(TNode<JSReceiver>)>;
  enum CloseOnRejectionOption { kDoNotCloseOnRejection, kCloseOnRejection };
  void Generate_AsyncFromSyncIteratorMethod(
      CodeStubArguments* args, const TNode<Context> context,
      const TNode<Object> iterator, const TNode<Object> sent_value,
      const SyncIteratorNodeGenerator& get_method,
      const UndefinedMethodHandler& if_method_undefined,
      const char* operation_name, CloseOnRejectionOption close_on_rejection,
      Label::Type reject_label_type = Label::kDeferred,
      std::optional<TNode<Object>> initial_exception_value = std::nullopt);

  void Generate_AsyncFromSyncIteratorMethod(
      CodeStubArguments* args, const TNode<Context> context,
      const TNode<Object> iterator, const TNode<Object> sent_value,
      Handle<String> name, const UndefinedMethodHandler& if_method_undefined,
      const char* operation_name, CloseOnRejectionOption close_on_rejection,
      Label::Type reject_label_type = Label::kDeferred,
      std::optional<TNode<Object>> initial_exception_value = std::nullopt) {
    auto get_method = [=, this](const TNode<JSReceiver> sync_iterator) {
      return GetProperty(context, sync_iterator, name);
    };
    return Generate_AsyncFromSyncIteratorMethod(
        args, context, iterator, sent_value, get_method, if_method_undefined,
        operation_name, close_on_rejection, reject_label_type,
        initial_exception_value);
  }

  // Load "value" and "done" from an iterator result object. If an exception
  // is thrown at any point, jumps to the `if_exception` label with exception
  // stored in `var_exception`.
  //
  // Returns a Pair of Nodes, whose first element is the value of the "value"
  // property, and whose second element is the value of the "done" property,
  // converted to a Boolean if needed.
  std::pair<TNode<Object>, TNode<Boolean>> LoadIteratorResult(
      const TNode<Context> context, const TNode<NativeContext> native_context,
      const TNode<Object> iter_result, Label* if_exception,
      TVariable<Object>* var_exception);

  // Synthetic Context for the AsyncFromSyncIterator rejection closure that
  // closes the underlying sync iterator.
  struct AsyncFromSyncIteratorCloseSyncAndRethrowContext {
    enum Fields { kSyncIterator = Context::MIN_CONTEXT_SLOTS, kLength };
  };

  TNode<JSFunction> CreateAsyncFromSyncIteratorCloseSyncAndRethrowClosure(
      TNode<NativeContext> native_context, TNode<JSReceiver> sync_iterator);

  TNode<Context> AllocateAsyncFromSyncIteratorCloseSyncAndRethrowContext(
      TNode<NativeContext> native_context, TNode<JSReceiver> sync_iterator);
};

// This implements common steps found in various AsyncFromSyncIterator prototype
// methods followed by ES#sec-asyncfromsynciteratorcontinuation. The differences
// between the various prototype methods are handled by the get_method and
// if_method_undefined callbacks.
void AsyncFromSyncBuiltinsAssembler::Generate_AsyncFromSyncIteratorMethod(
    CodeStubArguments* args, const TNode<Context> context,
    const TNode<Object> iterator, const TNode<Object> sent_value,
    const SyncIteratorNodeGenerator& get_method,
    const UndefinedMethodHandler& if_method_undefined,
    const char* operation_name, CloseOnRejectionOption close_on_rejection,
    Label::Type reject_label_type,
    std::optional<TNode<Object>> initial_exception_value) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<JSPromise> promise = NewJSPromise(context);

  TVARIABLE(
      Object, var_exception,
      initial_exception_value ? *initial_exception_value : UndefinedConstant());
  Label maybe_close_sync_then_reject_promise(this, reject_label_type);
  Label maybe_close_sync_if_not_done_then_reject_promise(this,
                                                         reject_label_type);

  // At this time %AsyncFromSyncIterator% does not escape to user code, and so
  // cannot be called with an incompatible receiver.
  CSA_CHECK(this,
            HasInstanceType(CAST(iterator), JS_ASYNC_FROM_SYNC_ITERATOR_TYPE));
  TNode<JSAsyncFromSyncIterator> async_iterator = CAST(iterator);
  const TNode<JSReceiver> sync_iterator = LoadObjectField<JSReceiver>(
      async_iterator, JSAsyncFromSyncIterator::kSyncIteratorOffset);

  TNode<Object> method = get_method(sync_iterator);

  if (if_method_undefined) {
    Label if_isnotundefined(this);

    GotoIfNot(IsNullOrUndefined(method), &if_isnotundefined);
    if_method_undefined(native_context, promise, sync_iterator,
                        &maybe_close_sync_then_reject_promise);

    BIND(&if_isnotundefined);
  }

  TVARIABLE(Object, iter_result);
  {
    Label has_sent_value(this), no_sent_value(this), merge(this);
    ScopedExceptionHandler handler(this, &maybe_close_sync_then_reject_promise,
                                   &var_exception);
    Branch(IntPtrGreaterThan(args->GetLengthWithoutReceiver(),
                             IntPtrConstant(kValueOrReasonArg)),
           &has_sent_value, &no_sent_value);
    BIND(&has_sent_value);
    {
      iter_result = Call(context, method, sync_iterator, sent_value);
      Goto(&merge);
    }
    BIND(&no_sent_value);
    {
      iter_result = Call(context, method, sync_iterator);
      Goto(&merge);
    }
    BIND(&merge);
  }

  TNode<Object> value;
  TNode<Boolean> done;
  std::tie(value, done) =
      LoadIteratorResult(context, native_context, iter_result.value(),
                         &maybe_close_sync_then_reject_promise, &var_exception);

  const TNode<JSFunction> promise_fun =
      CAST(LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX));
  CSA_DCHECK(this, IsConstructor(promise_fun));

  // 6. Let valueWrapper be PromiseResolve(%Promise%, « value »).
  //    IfAbruptRejectPromise(valueWrapper, promiseCapability).
  TNode<Object> value_wrapper;
  {
    ScopedExceptionHandler handler(
        this, &maybe_close_sync_if_not_done_then_reject_promise,
        &var_exception);
    value_wrapper = CallBuiltin(Builtin::kPromiseResolve, native_context,
                                promise_fun, value);
  }

  // 10. Let onFulfilled be CreateBuiltinFunction(unwrap, 1, "", « »).
  const TNode<JSFunction> on_fulfilled =
      CreateUnwrapClosure(native_context, done);

  // 12. If done is true, or if closeOnRejection is false, then
  //   a. Let onRejected be undefined.
  // 13. Else,
  //   [...]
  //   b. Let onRejected be CreateBuiltinFunction(closeIterator, 1, "", « »).
  TNode<Object> on_rejected;
  if (close_on_rejection == kCloseOnRejection) {
    on_rejected = Select<Object>(
        IsTrue(done), [=, this] { return UndefinedConstant(); },
        [=, this] {
          return CreateAsyncFromSyncIteratorCloseSyncAndRethrowClosure(
              native_context, sync_iterator);
        });
  } else {
    on_rejected = UndefinedConstant();
  }

  // 14. Perform ! PerformPromiseThen(valueWrapper,
  //     onFulfilled, onRejected, promiseCapability).
  args->PopAndReturn(CallBuiltin(Builtin::kPerformPromiseThen, context,
                                 value_wrapper, on_fulfilled, on_rejected,
                                 promise));

  Label reject_promise(this);
  BIND(&maybe_close_sync_if_not_done_then_reject_promise);
  {
    if (close_on_rejection == kCloseOnRejection) {
      GotoIf(IsFalse(done), &maybe_close_sync_then_reject_promise);
    }
    Goto(&reject_promise);
  }
  BIND(&maybe_close_sync_then_reject_promise);
  {
    if (close_on_rejection == kCloseOnRejection) {
      // 7. If valueWrapper is an abrupt completion, done is false, and
      //    closeOnRejection is true, then
      //   a. Set valueWrapper to Completion(IteratorClose(syncIteratorRecord,
      //      valueWrapper)).
      TorqueStructIteratorRecord sync_iterator_record = {sync_iterator, {}};
      IteratorCloseOnException(context, sync_iterator_record);
    }
    Goto(&reject_promise);
  }
  BIND(&reject_promise);
  {
    const TNode<Object> exception = var_exception.value();
    CallBuiltin(Builtin::kRejectPromise, context, promise, exception,
                TrueConstant());
    args->PopAndReturn(promise);
  }
}

std::pair<TNode<Object>, TNode<Boolean>>
AsyncFromSyncBuiltinsAssembler::LoadIteratorResult(
    const TNode<Context> context, const TNode<NativeContext> native_context,
    const TNode<Object> iter_result, Label* if_exception,
    TVariable<Object>* var_exception) {
  Label if_fastpath(this), if_slowpath(this), merge(this), to_boolean(this),
      done(this), if_notanobject(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(iter_result), &if_notanobject);

  const TNode<Map> iter_result_map = LoadMap(CAST(iter_result));
  GotoIfNot(JSAnyIsNotPrimitiveMap(iter_result_map), &if_notanobject);

  const TNode<Object> fast_iter_result_map =
      LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX);

  TVARIABLE(Object, var_value);
  TVARIABLE(Object, var_done);
  Branch(TaggedEqual(iter_result_map, fast_iter_result_map), &if_fastpath,
         &if_slowpath);

  BIND(&if_fastpath);
  {
    TNode<JSObject> fast_iter_result = CAST(iter_result);
    var_done = LoadObjectField(fast_iter_result, JSIteratorResult::kDoneOffset);
    var_value =
        LoadObjectField(fast_iter_result, JSIteratorResult::kValueOffset);
    Goto(&merge);
  }

  BIND(&if_slowpath);
  {
    ScopedExceptionHandler handler(this, if_exception, var_exception);

    // Let nextDone be IteratorComplete(nextResult).
    // IfAbruptRejectPromise(nextDone, promiseCapability).
    const TNode<Object> iter_result_done =
        GetProperty(context, iter_result, factory()->done_string());

    // Let nextValue be IteratorValue(nextResult).
    // IfAbruptRejectPromise(nextValue, promiseCapability).
    const TNode<Object> iter_result_value =
        GetProperty(context, iter_result, factory()->value_string());

    var_value = iter_result_value;
    var_done = iter_result_done;
    Goto(&merge);
  }

  BIND(&if_notanobject);
  {
    // Sync iterator result is not an object --- Produce a TypeError and jump
    // to the `if_exception` path.
    const TNode<Object> error = MakeTypeError(
        MessageTemplate::kIteratorResultNotAnObject, context, iter_result);
    *var_exception = error;
    Goto(if_exception);
  }

  BIND(&merge);
  // Ensure `iterResult.done` is a Boolean.
  GotoIf(TaggedIsSmi(var_done.value()), &to_boolean);
  Branch(IsBoolean(CAST(var_done.value())), &done, &to_boolean);

  BIND(&to_boolean);
  {
    const TNode<Object> result =
        CallBuiltin(Builtin::kToBoolean, context, var_done.value());
    var_done = result;
    Goto(&done);
  }

  BIND(&done);
  return std::make_pair(var_value.value(), CAST(var_done.value()));
}

TNode<JSFunction> AsyncFromSyncBuiltinsAssembler::
    CreateAsyncFromSyncIteratorCloseSyncAndRethrowClosure(
        TNode<NativeContext> native_context, TNode<JSReceiver> sync_iterator) {
  const TNode<Context> closure_context =
      AllocateAsyncFromSyncIteratorCloseSyncAndRethrowContext(native_context,
                                                              sync_iterator);
  return AllocateRootFunctionWithContext(
      RootIndex::kAsyncFromSyncIteratorCloseSyncAndRethrowSharedFun,
      closure_context, native_context);
}

TNode<Context> AsyncFromSyncBuiltinsAssembler::
    AllocateAsyncFromSyncIteratorCloseSyncAndRethrowContext(
        TNode<NativeContext> native_context, TNode<JSReceiver> sync_iterator) {
  TNode<Context> context = AllocateSyntheticFunctionContext(
      native_context, AsyncFromSyncIteratorCloseSyncAndRethrowContext::kLength);
  StoreContextElementNoWriteBarrier(
      context, AsyncFromSyncIteratorCloseSyncAndRethrowContext::kSyncIterator,
      sync_iterator);
  return context;
}

}  // namespace

// ES#sec-%asyncfromsynciteratorprototype%.next
TF_BUILTIN(AsyncFromSyncIteratorPrototypeNext, AsyncFromSyncBuiltinsAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  const TNode<Object> iterator = args.GetReceiver();
  const TNode<Object> value = args.GetOptionalArgumentValue(kValueOrReasonArg);
  const auto context = Parameter<Context>(Descriptor::kContext);

  auto get_method = [=, this](const TNode<JSReceiver> unused) {
    return LoadObjectField(CAST(iterator),
                           JSAsyncFromSyncIterator::kNextOffset);
  };
  Generate_AsyncFromSyncIteratorMethod(
      &args, context, iterator, value, get_method, UndefinedMethodHandler(),
      "[Async-from-Sync Iterator].prototype.next", kCloseOnRejection);
}

// ES#sec-%asyncfromsynciteratorprototype%.return
TF_BUILTIN(AsyncFromSyncIteratorPrototypeReturn,
           AsyncFromSyncBuiltinsAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  const TNode<Object> iterator = args.GetReceiver();
  const TNode<Object> value = args.GetOptionalArgumentValue(kValueOrReasonArg);
  const auto context = Parameter<Context>(Descriptor::kContext);

  auto if_return_undefined = [=, this, &args](
                                 const TNode<NativeContext> native_context,
                                 const TNode<JSPromise> promise,
                                 const TNode<JSReceiver> sync_iterator,
                                 Label* if_exception) {
    // If return is undefined, then
    // Let iterResult be ! CreateIterResultObject(value, true)
    const TNode<Object> iter_result = CallBuiltin(
        Builtin::kCreateIterResultObject, context, value, TrueConstant());

    // Perform ! Call(promiseCapability.[[Resolve]], undefined, « iterResult »).
    // IfAbruptRejectPromise(nextDone, promiseCapability).
    // Return promiseCapability.[[Promise]].
    CallBuiltin(Builtin::kResolvePromise, context, promise, iter_result);
    args.PopAndReturn(promise);
  };

  Generate_AsyncFromSyncIteratorMethod(
      &args, context, iterator, value, factory()->return_string(),
      if_return_undefined, "[Async-from-Sync Iterator].prototype.return",
      kDoNotCloseOnRejection);
}

// ES#sec-%asyncfromsynciteratorprototype%.throw
TF_BUILTIN(AsyncFromSyncIteratorPrototypeThrow,
           AsyncFromSyncBuiltinsAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  const TNode<Object> iterator = args.GetReceiver();
  const TNode<Object> reason = args.GetOptionalArgumentValue(kValueOrReasonArg);
  const auto context = Parameter<Context>(Descriptor::kContext);

  // 8. If throw is undefined, then
  auto if_throw_undefined =
      [=, this, &args](const TNode<NativeContext> native_context,
                       const TNode<JSPromise> promise,
                       const TNode<JSReceiver> sync_iterator,
                       Label* if_exception) {
        // a. NOTE: If syncIterator does not have a `throw` method, close it to
        //    give it a chance to clean up before we reject the capability.
        // b. Let closeCompletion be NormalCompletion(~empty~).
        // c. Let result be Completion(IteratorClose(syncIteratorRecord,
        //    closeCompletion)).
        TVARIABLE(Object, var_reject_value);
        Label done(this);
        {
          ScopedExceptionHandler handler(this, &done, &var_reject_value);
          TorqueStructIteratorRecord sync_iterator_record = {sync_iterator, {}};
          IteratorClose(context, sync_iterator_record);

          // d. IfAbruptRejectPromise(result, promiseCapability).
          // (Done below)
        }

        // e. NOTE: The next step throws a *TypeError* to indicate that there
        //    was a protocol violation: syncIterator does not have a `throw`
        //    method.
        // f. NOTE: If closing syncIterator does not throw then the result of
        //    that operation is ignored, even if it yields a rejected promise.
        // g. Perform ! Call(promiseCapability.[[Reject]], *undefined*, « a
        //    newly created *TypeError* object »).
        var_reject_value =
            MakeTypeError(MessageTemplate::kThrowMethodMissing, context);
        Goto(&done);
        BIND(&done);
        CallBuiltin(Builtin::kRejectPromise, context, promise,
                    var_reject_value.value(), TrueConstant());
        args.PopAndReturn(promise);
      };

  Generate_AsyncFromSyncIteratorMethod(
      &args, context, iterator, reason, factory()->throw_string(),
      if_throw_undefined, "[Async-from-Sync Iterator].prototype.throw",
      kCloseOnRejection, Label::kNonDeferred, reason);
}

TF_BUILTIN(AsyncFromSyncIteratorCloseSyncAndRethrow,
           AsyncFromSyncBuiltinsAssembler) {
  // #sec-asyncfromsynciteratorcontinuation
  //
  // 13. [...]
  //   a. Let closeIterator be a new Abstract Closure with parameters (error)
  //      that captures syncIteratorRecord and performs the following steps
  //      when called:
  //        i. Return ? IteratorClose(syncIteratorRecord,
  //           ThrowCompletion(error)).

  auto error = Parameter<Object>(Descriptor::kError);
  auto context = Parameter<Context>(Descriptor::kContext);

  const TNode<JSReceiver> sync_iterator = CAST(LoadContextElement(
      context, AsyncFromSyncIteratorCloseSyncAndRethrowContext::kSyncIterator));
  // iterator.next field is not used by IteratorCloseOnException.
  TorqueStructIteratorRecord sync_iterator_record = {sync_iterator, {}};
  IteratorCloseOnException(context, sync_iterator_record);
  Return(CallRuntime(Runtime::kReThrow, context, error));
}

#include "src/codegen/undef-code-stub-assembler-macros.inc"

}  // namespace internal
}  // namespace v8
