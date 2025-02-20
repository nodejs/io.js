// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/debug/debug.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/microtask-queue.h"
#include "src/objects/js-promise-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_PromiseRejectEventFromStack) {
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  DirectHandle<JSPromise> promise = args.at<JSPromise>(0);
  DirectHandle<Object> value = args.at(1);

  isolate->RunAllPromiseHooks(PromiseHookType::kResolve, promise,
                              isolate->factory()->undefined_value());
  isolate->debug()->OnPromiseReject(promise, value);

  // Report only if we don't actually have a handler.
  if (!promise->has_handler()) {
    isolate->ReportPromiseReject(promise, value,
                                 v8::kPromiseRejectWithNoHandler);
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseRejectAfterResolved) {
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  DirectHandle<JSPromise> promise = args.at<JSPromise>(0);
  DirectHandle<Object> reason = args.at(1);
  isolate->ReportPromiseReject(promise, reason,
                               v8::kPromiseRejectAfterResolved);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseResolveAfterResolved) {
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  DirectHandle<JSPromise> promise = args.at<JSPromise>(0);
  DirectHandle<Object> resolution = args.at(1);
  isolate->ReportPromiseReject(promise, resolution,
                               v8::kPromiseResolveAfterResolved);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseRevokeReject) {
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  DirectHandle<JSPromise> promise = args.at<JSPromise>(0);
  // At this point, no revocation has been issued before
  CHECK(!promise->has_handler());
  isolate->ReportPromiseReject(promise, DirectHandle<Object>(),
                               v8::kPromiseHandlerAddedAfterReject);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_EnqueueMicrotask) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> function = args.at<JSFunction>(0);

  DirectHandle<CallableTask> microtask = isolate->factory()->NewCallableTask(
      function, direct_handle(function->native_context(), isolate));
  MicrotaskQueue* microtask_queue =
      function->native_context()->microtask_queue();
  if (microtask_queue) microtask_queue->EnqueueMicrotask(*microtask);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_PerformMicrotaskCheckpoint) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  MicrotasksScope::PerformCheckpoint(reinterpret_cast<v8::Isolate*>(isolate));
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_RunMicrotaskCallback) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Tagged<Object> microtask_callback = args[0];
  Tagged<Object> microtask_data = args[1];
  MicrotaskCallback callback =
      ToCData<MicrotaskCallback, kMicrotaskCallbackTag>(isolate,
                                                        microtask_callback);
  void* data =
      ToCData<void*, kMicrotaskCallbackDataTag>(isolate, microtask_data);
  callback(data);
  RETURN_FAILURE_IF_EXCEPTION(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseHookInit) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<JSPromise> promise = args.at<JSPromise>(0);
  DirectHandle<Object> parent = args.at(1);
  isolate->RunPromiseHook(PromiseHookType::kInit, promise, parent);
  RETURN_FAILURE_IF_EXCEPTION(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseHookBefore) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSReceiver> promise = args.at<JSReceiver>(0);
  if (IsJSPromise(*promise)) {
    isolate->OnPromiseBefore(Cast<JSPromise>(promise));
    RETURN_FAILURE_IF_EXCEPTION(isolate);
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_PromiseHookAfter) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSReceiver> promise = args.at<JSReceiver>(0);
  if (IsJSPromise(*promise)) {
    isolate->OnPromiseAfter(Cast<JSPromise>(promise));
    RETURN_FAILURE_IF_EXCEPTION(isolate);
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_RejectPromise) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  DirectHandle<JSPromise> promise = args.at<JSPromise>(0);
  DirectHandle<Object> reason = args.at(1);
  DirectHandle<Boolean> debug_event = args.at<Boolean>(2);
  return *JSPromise::Reject(promise, reason,
                            Object::BooleanValue(*debug_event, isolate));
}

RUNTIME_FUNCTION(Runtime_ResolvePromise) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<JSPromise> promise = args.at<JSPromise>(0);
  DirectHandle<Object> resolution = args.at(1);
  DirectHandle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result,
                                     JSPromise::Resolve(promise, resolution));
  return *result;
}

// A helper function to be called when constructing AggregateError objects. This
// takes care of the Error-related construction, e.g., stack traces.
RUNTIME_FUNCTION(Runtime_ConstructAggregateErrorHelper) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  DirectHandle<JSFunction> target = args.at<JSFunction>(0);
  DirectHandle<Object> new_target = args.at(1);
  DirectHandle<Object> message = args.at(2);
  DirectHandle<Object> options = args.at(3);

  DCHECK_EQ(*target, *isolate->aggregate_error_function());

  DirectHandle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      ErrorUtils::Construct(isolate, target, new_target, message, options));
  return *result;
}

// A helper function to be called when constructing AggregateError objects. This
// takes care of the Error-related construction, e.g., stack traces.
RUNTIME_FUNCTION(Runtime_ConstructInternalAggregateErrorHelper) {
  HandleScope scope(isolate);
  DCHECK_GE(args.length(), 1);
  int message_template_index = args.smi_value_at(0);

  constexpr int kMaxMessageArgs = 3;
  DirectHandle<Object> message_args[kMaxMessageArgs];
  int num_message_args = 0;

  while (num_message_args < kMaxMessageArgs &&
         args.length() > num_message_args + 1) {
    message_args[num_message_args] = args.at(num_message_args + 1);
  }

  DirectHandle<Object> options =
      args.length() >= 5 ? args.at(4) : isolate->factory()->undefined_value();

  DirectHandle<Object> message_string =
      MessageFormatter::Format(isolate, MessageTemplate(message_template_index),
                               base::VectorOf(message_args, num_message_args));

  DirectHandle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      ErrorUtils::Construct(isolate, isolate->aggregate_error_function(),
                            isolate->aggregate_error_function(), message_string,
                            options));
  return *result;
}

RUNTIME_FUNCTION(Runtime_ConstructSuppressedError) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  DirectHandle<JSFunction> target = args.at<JSFunction>(0);
  DirectHandle<Object> new_target = args.at(1);
  DirectHandle<Object> message = args.at(2);

  DCHECK_EQ(*target, *isolate->suppressed_error_function());

  DirectHandle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      ErrorUtils::Construct(isolate, target, new_target, message,
                            isolate->factory()->undefined_value()));
  return *result;
}

}  // namespace internal
}  // namespace v8
