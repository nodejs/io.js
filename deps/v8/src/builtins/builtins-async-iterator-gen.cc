// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/optional.h"
#include "src/builtins/builtins-async-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/execution/frames-inl.h"

namespace v8 {
namespace internal {

namespace {
class AsyncFromSyncBuiltinsAssembler : public AsyncBuiltinsAssembler {
 public:
  // The 'next' and 'return' take an optional value parameter, and the 'throw'
  // method take an optional reason parameter.
  static const int kValueOrReasonArg = 0;

  explicit AsyncFromSyncBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : AsyncBuiltinsAssembler(state) {}

  void ThrowIfNotAsyncFromSyncIterator(const TNode<Context> context,
                                       const TNode<Object> object,
                                       Label* if_exception,
                                       TVariable<Object>* var_exception,
                                       const char* method_name);

  using UndefinedMethodHandler =
      std::function<void(const TNode<NativeContext> native_context,
                         const TNode<JSPromise> promise, Label* if_exception)>;
  using SyncIteratorNodeGenerator =
      std::function<TNode<Object>(TNode<JSReceiver>)>;
  void Generate_AsyncFromSyncIteratorMethod(
      CodeStubArguments* args, const TNode<Context> context,
      const TNode<Object> iterator, const TNode<Object> sent_value,
      const SyncIteratorNodeGenerator& get_method,
      const UndefinedMethodHandler& if_method_undefined,
      const char* operation_name,
      Label::Type reject_label_type = Label::kDeferred,
      base::Optional<TNode<Object>> initial_exception_value = base::nullopt);

  void Generate_AsyncFromSyncIteratorMethod(
      CodeStubArguments* args, const TNode<Context> context,
      const TNode<Object> iterator, const TNode<Object> sent_value,
      Handle<String> name, const UndefinedMethodHandler& if_method_undefined,
      const char* operation_name,
      Label::Type reject_label_type = Label::kDeferred,
      base::Optional<TNode<Object>> initial_exception_value = base::nullopt) {
    auto get_method = [=](const TNode<JSReceiver> sync_iterator) {
      return GetProperty(context, sync_iterator, name);
    };
    return Generate_AsyncFromSyncIteratorMethod(
        args, context, iterator, sent_value, get_method, if_method_undefined,
        operation_name, reject_label_type, initial_exception_value);
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
};

void AsyncFromSyncBuiltinsAssembler::ThrowIfNotAsyncFromSyncIterator(
    const TNode<Context> context, const TNode<Object> object,
    Label* if_exception, TVariable<Object>* var_exception,
    const char* method_name) {
  Label if_receiverisincompatible(this, Label::kDeferred), done(this);

  GotoIf(TaggedIsSmi(object), &if_receiverisincompatible);
  Branch(HasInstanceType(CAST(object), JS_ASYNC_FROM_SYNC_ITERATOR_TYPE), &done,
         &if_receiverisincompatible);

  BIND(&if_receiverisincompatible);
  {
    // If Type(O) is not Object, or if O does not have a [[SyncIterator]]
    // internal slot, then

    // Let badIteratorError be a new TypeError exception.
    TNode<HeapObject> error =
        MakeTypeError(MessageTemplate::kIncompatibleMethodReceiver, context,
                      StringConstant(method_name), object);

    // Perform ! Call(promiseCapability.[[Reject]], undefined,
    //                « badIteratorError »).
    *var_exception = error;
    Goto(if_exception);
  }

  BIND(&done);
}

void AsyncFromSyncBuiltinsAssembler::Generate_AsyncFromSyncIteratorMethod(
    CodeStubArguments* args, const TNode<Context> context,
    const TNode<Object> iterator, const TNode<Object> sent_value,
    const SyncIteratorNodeGenerator& get_method,
    const UndefinedMethodHandler& if_method_undefined,
    const char* operation_name, Label::Type reject_label_type,
    base::Optional<TNode<Object>> initial_exception_value) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<JSPromise> promise = NewJSPromise(context);

  TVARIABLE(
      Object, var_exception,
      initial_exception_value ? *initial_exception_value : UndefinedConstant());
  Label reject_promise(this, reject_label_type);

  ThrowIfNotAsyncFromSyncIterator(context, iterator, &reject_promise,
                                  &var_exception, operation_name);

  TNode<JSAsyncFromSyncIterator> async_iterator = CAST(iterator);
  const TNode<JSReceiver> sync_iterator = LoadObjectField<JSReceiver>(
      async_iterator, JSAsyncFromSyncIterator::kSyncIteratorOffset);

  TNode<Object> method = get_method(sync_iterator);

  if (if_method_undefined) {
    Label if_isnotundefined(this);

    GotoIfNot(IsNullOrUndefined(method), &if_isnotundefined);
    if_method_undefined(native_context, promise, &reject_promise);

    BIND(&if_isnotundefined);
  }

  TVARIABLE(Object, iter_result);
  {
    Label has_sent_value(this), no_sent_value(this), merge(this);
    ScopedExceptionHandler handler(this, &reject_promise, &var_exception);
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
                         &reject_promise, &var_exception);

  const TNode<JSFunction> promise_fun =
      CAST(LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX));
  CSA_DCHECK(this, IsConstructor(promise_fun));

  // Let valueWrapper be PromiseResolve(%Promise%, « value »).
  // IfAbruptRejectPromise(valueWrapper, promiseCapability).
  TNode<Object> value_wrapper;
  {
    ScopedExceptionHandler handler(this, &reject_promise, &var_exception);
    value_wrapper = CallBuiltin(Builtin::kPromiseResolve, native_context,
                                promise_fun, value);
  }

  // Let onFulfilled be a new built-in function object as defined in
  // Async Iterator Value Unwrap Functions.
  // Set onFulfilled.[[Done]] to throwDone.
  const TNode<JSFunction> on_fulfilled =
      CreateUnwrapClosure(native_context, done);

  // Perform ! PerformPromiseThen(valueWrapper,
  //     onFulfilled, undefined, promiseCapability).
  args->PopAndReturn(CallBuiltin(Builtin::kPerformPromiseThen, context,
                                 value_wrapper, on_fulfilled,
                                 UndefinedConstant(), promise));

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

}  // namespace

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-%asyncfromsynciteratorprototype%.next
TF_BUILTIN(AsyncFromSyncIteratorPrototypeNext, AsyncFromSyncBuiltinsAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  const TNode<Object> iterator = args.GetReceiver();
  const TNode<Object> value = args.GetOptionalArgumentValue(kValueOrReasonArg);
  const auto context = Parameter<Context>(Descriptor::kContext);

  auto get_method = [=](const TNode<JSReceiver> unused) {
    return LoadObjectField(CAST(iterator),
                           JSAsyncFromSyncIterator::kNextOffset);
  };
  Generate_AsyncFromSyncIteratorMethod(
      &args, context, iterator, value, get_method, UndefinedMethodHandler(),
      "[Async-from-Sync Iterator].prototype.next");
}

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-%asyncfromsynciteratorprototype%.return
TF_BUILTIN(AsyncFromSyncIteratorPrototypeReturn,
           AsyncFromSyncBuiltinsAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  const TNode<Object> iterator = args.GetReceiver();
  const TNode<Object> value = args.GetOptionalArgumentValue(kValueOrReasonArg);
  const auto context = Parameter<Context>(Descriptor::kContext);

  auto if_return_undefined = [=, &args](
                                 const TNode<NativeContext> native_context,
                                 const TNode<JSPromise> promise,
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
      if_return_undefined, "[Async-from-Sync Iterator].prototype.return");
}

// https://tc39.github.io/proposal-async-iteration/
// Section #sec-%asyncfromsynciteratorprototype%.throw
TF_BUILTIN(AsyncFromSyncIteratorPrototypeThrow,
           AsyncFromSyncBuiltinsAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);

  const TNode<Object> iterator = args.GetReceiver();
  const TNode<Object> reason = args.GetOptionalArgumentValue(kValueOrReasonArg);
  const auto context = Parameter<Context>(Descriptor::kContext);

  auto if_throw_undefined = [=](const TNode<NativeContext> native_context,
                                const TNode<JSPromise> promise,
                                Label* if_exception) { Goto(if_exception); };

  Generate_AsyncFromSyncIteratorMethod(
      &args, context, iterator, reason, factory()->throw_string(),
      if_throw_undefined, "[Async-from-Sync Iterator].prototype.throw",
      Label::kNonDeferred, reason);
}

}  // namespace internal
}  // namespace v8
