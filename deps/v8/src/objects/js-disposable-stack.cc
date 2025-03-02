// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-disposable-stack.h"

#include "include/v8-maybe.h"
#include "include/v8-promise.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/debug/debug.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/handles/maybe-handles.h"
#include "src/heap/factory.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-function.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/js-promise.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/objects/oddball.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

#define CHECK_EXCEPTION_ON_DISPOSAL(isolate, disposable_stack, return_value) \
  do {                                                                       \
    DCHECK(isolate->has_exception());                                        \
    Handle<Object> current_error(isolate->exception(), isolate);             \
    DirectHandle<Object> current_error_message(isolate->pending_message(),   \
                                               isolate);                     \
    if (!isolate->is_catchable_by_javascript(*current_error)) {              \
      return return_value;                                                   \
    }                                                                        \
    isolate->clear_internal_exception();                                     \
    isolate->clear_pending_message();                                        \
    HandleErrorInDisposal(isolate, disposable_stack, current_error,          \
                          current_error_message);                            \
  } while (false)

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposeresources
MaybeDirectHandle<Object> JSDisposableStackBase::DisposeResources(
    Isolate* isolate, DirectHandle<JSDisposableStackBase> disposable_stack,
    MaybeHandle<Object> maybe_continuation_error,
    DisposableStackResourcesType resources_type) {
  DCHECK(!IsUndefined(disposable_stack->stack()));

  DirectHandle<FixedArray> stack(disposable_stack->stack(), isolate);

  // 1. Let needsAwait be false.
  // 2. Let hasAwaited be false.
  // `false` is assigned to both in the initialization of the DisposableStack.

  int length = disposable_stack->length();

  MaybeDirectHandle<Object> result;
  Handle<Object> continuation_error;

  if (maybe_continuation_error.ToHandle(&continuation_error)) {
    disposable_stack->set_error(*continuation_error);
    disposable_stack->set_error_message(isolate->pending_message());
  }

  // 3. For each element resource of
  // disposeCapability.[[DisposableResourceStack]], in reverse list order, do
  while (length > 0) {
    //  a. Let value be resource.[[ResourceValue]].
    //  b. Let hint be resource.[[Hint]].
    //  c. Let method be resource.[[DisposeMethod]].
    Tagged<Object> stack_type = stack->get(--length);

    Tagged<Object> tagged_method = stack->get(--length);
    DirectHandle<Object> method(tagged_method, isolate);

    Tagged<Object> tagged_value = stack->get(--length);
    DirectHandle<Object> value(tagged_value, isolate);

    DirectHandle<Object> args[] = {value};

    auto stack_type_case = static_cast<int>(Cast<Smi>(stack_type).value());
    DisposeMethodCallType call_type =
        DisposeCallTypeBit::decode(stack_type_case);
    DisposeMethodHint hint = DisposeHintBit::decode(stack_type_case);

    //  d. If hint is sync-dispose and needsAwait is true and hasAwaited is
    //  false, then
    //    i. Perform ! Await(undefined).
    //    ii. Set needsAwait to false.

    if (hint == DisposeMethodHint::kSyncDispose &&
        disposable_stack->needs_await() == true &&
        disposable_stack->has_awaited() == false) {
      //  i. Perform ! Await(undefined).
      //  ii. Set needsAwait to false.
      disposable_stack->set_needs_await(false);

      return ResolveAPromiseWithValueAndReturnIt(
          isolate, isolate->factory()->undefined_value());
    }

    //  e. If method is not undefined, then
    if (!IsUndefined(*method)) {
      //    i. Let result be Completion(Call(method, value)).

      if (call_type == DisposeMethodCallType::kValueIsReceiver) {
        result = Execution::Call(isolate, method, value, {});
      } else if (call_type == DisposeMethodCallType::kValueIsArgument) {
        result = Execution::Call(isolate, method,
                                 isolate->factory()->undefined_value(),
                                 base::VectorOf(args));
      }

      DirectHandle<Object> result_handle;
      //    ii. If result is a normal completion and hint is async-dispose, then
      //      1. Set result to Completion(Await(result.[[Value]])).
      //      2. Set hasAwaited to true.
      if (result.ToHandle(&result_handle)) {
        if (hint == DisposeMethodHint::kAsyncDispose) {
          DCHECK_NE(resources_type, DisposableStackResourcesType::kAllSync);
          disposable_stack->set_length(length);

          disposable_stack->set_has_awaited(true);

          MaybeDirectHandle<JSReceiver> resolved_promise =
              ResolveAPromiseWithValueAndReturnIt(isolate, result_handle);

          if (resolved_promise.is_null()) {
            //    iii. If result is a throw completion, then
            //      1. If completion is a throw completion, then
            //        a. Set result to result.[[Value]].
            //        b. Let suppressed be completion.[[Value]].
            //        c. Let error be a newly created SuppressedError object.
            //        d. Perform CreateNonEnumerableDataPropertyOrThrow(error,
            //        "error", result).
            //        e. Perform CreateNonEnumerableDataPropertyOrThrow(error,
            //         "suppressed", suppressed).
            //        f. Set completion to ThrowCompletion(error).
            //      2. Else,
            //        a. Set completion to result.
            CHECK_EXCEPTION_ON_DISPOSAL(isolate, disposable_stack, {});
          } else {
            return resolved_promise;
          }
        }
      } else {
        CHECK_EXCEPTION_ON_DISPOSAL(isolate, disposable_stack, {});
      }
    } else {
      //  Else,
      //    i. Assert: hint is async-dispose.
      DCHECK_EQ(hint, DisposeMethodHint::kAsyncDispose);
      //    ii. Set needsAwait to true.
      //    iii. NOTE: This can only indicate a case where either null or
      //    undefined was the initialized value of an await using declaration.
      disposable_stack->set_length(length);
      disposable_stack->set_needs_await(true);
    }
  }

  // 4. If needsAwait is true and hasAwaited is false, then
  //   a. Perform ! Await(undefined).
  if (disposable_stack->needs_await() == true &&
      disposable_stack->has_awaited() == false) {
    disposable_stack->set_length(length);
    disposable_stack->set_has_awaited(true);

    return ResolveAPromiseWithValueAndReturnIt(
        isolate, isolate->factory()->undefined_value());
  }

  // 5. NOTE: After disposeCapability has been disposed, it will never be used
  // again. The contents of disposeCapability.[[DisposableResourceStack]] can be
  // discarded in implementations, such as by garbage collection, at this point.
  // 6. Set disposeCapability.[[DisposableResourceStack]] to a new empty List.
  disposable_stack->set_stack(ReadOnlyRoots(isolate).empty_fixed_array());
  disposable_stack->set_length(0);
  disposable_stack->set_state(DisposableStackState::kDisposed);

  Handle<Object> existing_error_handle(disposable_stack->error(), isolate);
  DirectHandle<Object> existing_error_message_handle(
      disposable_stack->error_message(), isolate);
  disposable_stack->set_error(*(isolate->factory()->uninitialized_value()));
  disposable_stack->set_error_message(
      *(isolate->factory()->uninitialized_value()));

  // 7. Return ? completion.
  if (!IsUninitialized(*existing_error_handle) &&
      !(existing_error_handle.equals(continuation_error))) {
    if (disposable_stack->suppressed_error_created() == true) {
      // Created SuppressedError is intentionally suppressed here for debug.
      SuppressDebug while_processing(isolate->debug());
      isolate->Throw(*existing_error_handle);
    } else {
      isolate->ReThrow(*existing_error_handle, *existing_error_message_handle);
    }
    return MaybeDirectHandle<Object>();
  }
  return isolate->factory()->true_value();
}

MaybeDirectHandle<JSReceiver>
JSDisposableStackBase::ResolveAPromiseWithValueAndReturnIt(
    Isolate* isolate, DirectHandle<Object> value) {
  DirectHandle<JSFunction> promise_function = isolate->promise_function();
  DirectHandle<Object> args[] = {value};
  DirectHandle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, result,
      Execution::CallBuiltin(isolate, isolate->promise_resolve(),
                             promise_function, base::VectorOf(args)),
      MaybeDirectHandle<JSReceiver>());
  return Cast<JSReceiver>(result);
}

Maybe<bool> JSAsyncDisposableStack::NextDisposeAsyncIteration(
    Isolate* isolate,
    DirectHandle<JSDisposableStackBase> async_disposable_stack,
    DirectHandle<JSPromise> outer_promise) {
  MaybeDirectHandle<Object> result;

  bool done;
  do {
    done = true;

    // 6. Let result be
    //   DisposeResources(asyncDisposableStack.[[DisposeCapability]],
    //   NormalCompletion(undefined)).
    result =
        DisposeResources(isolate, async_disposable_stack, MaybeHandle<Object>(),
                         DisposableStackResourcesType::kAtLeastOneAsync);

    DirectHandle<Object> result_handle;

    if (result.ToHandle(&result_handle)) {
      if (!IsTrue(*result_handle)) {
        DirectHandle<Context> async_disposable_stack_context =
            isolate->factory()->NewBuiltinContext(
                isolate->native_context(),
                static_cast<int>(
                    JSDisposableStackBase::AsyncDisposableStackContextSlots::
                        kLength));
        async_disposable_stack_context->set(
            static_cast<int>(JSDisposableStackBase::
                                 AsyncDisposableStackContextSlots::kStack),
            *async_disposable_stack);
        async_disposable_stack_context->set(
            static_cast<int>(
                JSDisposableStackBase::AsyncDisposableStackContextSlots::
                    kOuterPromise),
            *outer_promise);

        DirectHandle<JSFunction> on_fulfilled =
            Factory::JSFunctionBuilder{
                isolate,
                isolate->factory()
                    ->async_disposable_stack_on_fulfilled_shared_fun(),
                async_disposable_stack_context}
                .Build();

        DirectHandle<JSFunction> on_rejected =
            Factory::JSFunctionBuilder{
                isolate,
                isolate->factory()
                    ->async_disposable_stack_on_rejected_shared_fun(),
                async_disposable_stack_context}
                .Build();

        DirectHandle<Object> args[] = {on_fulfilled, on_rejected};
        if (Execution::CallBuiltin(isolate, isolate->perform_promise_then(),
                                   Cast<JSPromise>(result_handle),
                                   base::VectorOf(args))
                .is_null()) {
          CHECK_EXCEPTION_ON_DISPOSAL(isolate, async_disposable_stack,
                                      Nothing<bool>());
          done = false;
        }
      } else {
        // 8. Perform ! Call(promiseCapability.[[Resolve]], undefined, « result
        // »).
        if (JSPromise::Resolve(outer_promise,
                               isolate->factory()->undefined_value())
                .is_null()) {
          CHECK_EXCEPTION_ON_DISPOSAL(isolate, async_disposable_stack,
                                      Nothing<bool>());
          done = false;
        }
      }
    } else {
      // 7. IfAbruptRejectPromise(result, promiseCapability).
      DirectHandle<Object> exception(isolate->exception(), isolate);
      if (!isolate->is_catchable_by_javascript(*exception)) {
        return Nothing<bool>();
      }
      isolate->clear_internal_exception();
      isolate->clear_pending_message();
      JSPromise::Reject(outer_promise, exception);
    }
  } while (!done);

  return Just(true);
}

}  // namespace internal
}  // namespace v8
