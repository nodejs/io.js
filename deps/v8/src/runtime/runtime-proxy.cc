// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_IsJSProxy) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  Tagged<Object> obj = args[0];
  return isolate->heap()->ToBoolean(IsJSProxy(obj));
}

RUNTIME_FUNCTION(Runtime_JSProxyGetHandler) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  auto proxy = Cast<JSProxy>(args[0]);
  return proxy->handler();
}

RUNTIME_FUNCTION(Runtime_JSProxyGetTarget) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  auto proxy = Cast<JSProxy>(args[0]);
  return proxy->target();
}

RUNTIME_FUNCTION(Runtime_GetPropertyWithReceiver) {
  HandleScope scope(isolate);

  DCHECK_EQ(4, args.length());
  DirectHandle<JSReceiver> holder = args.at<JSReceiver>(0);
  Handle<Object> key = args.at(1);
  DirectHandle<JSAny> receiver = args.at<JSAny>(2);
  // TODO(mythria): Remove the on_non_existent parameter to this function. This
  // should only be called when getting named properties on receiver. This
  // doesn't handle the global variable loads.
#ifdef DEBUG
  int on_non_existent = args.smi_value_at(3);
  DCHECK_NE(static_cast<OnNonExistent>(on_non_existent),
            OnNonExistent::kThrowReferenceError);
#endif

  bool success = false;
  PropertyKey lookup_key(isolate, key, &success);
  if (!success) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  LookupIterator it(isolate, receiver, lookup_key, holder);

  RETURN_RESULT_OR_FAILURE(isolate, Object::GetProperty(&it));
}

RUNTIME_FUNCTION(Runtime_SetPropertyWithReceiver) {
  HandleScope scope(isolate);

  DCHECK_EQ(4, args.length());
  DirectHandle<JSReceiver> holder = args.at<JSReceiver>(0);
  Handle<Object> key = args.at(1);
  DirectHandle<Object> value = args.at(2);
  DirectHandle<JSAny> receiver = args.at<JSAny>(3);

  bool success = false;
  PropertyKey lookup_key(isolate, key, &success);
  if (!success) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  LookupIterator it(isolate, receiver, lookup_key, holder);
  Maybe<bool> result =
      Object::SetSuperProperty(&it, value, StoreOrigin::kMaybeKeyed);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return *isolate->factory()->ToBoolean(result.FromJust());
}

RUNTIME_FUNCTION(Runtime_CheckProxyGetSetTrapResult) {
  HandleScope scope(isolate);

  DCHECK_EQ(4, args.length());
  DirectHandle<Name> name = args.at<Name>(0);
  DirectHandle<JSReceiver> target = args.at<JSReceiver>(1);
  DirectHandle<Object> trap_result = args.at(2);
  int64_t access_kind = NumberToInt64(args[3]);

  RETURN_RESULT_OR_FAILURE(isolate, JSProxy::CheckGetSetTrapResult(
                                        isolate, name, target, trap_result,
                                        JSProxy::AccessKind(access_kind)));
}

RUNTIME_FUNCTION(Runtime_CheckProxyHasTrapResult) {
  HandleScope scope(isolate);

  DCHECK_EQ(2, args.length());
  DirectHandle<Name> name = args.at<Name>(0);
  DirectHandle<JSReceiver> target = args.at<JSReceiver>(1);

  Maybe<bool> result = JSProxy::CheckHasTrap(isolate, name, target);
  if (!result.IsJust()) return ReadOnlyRoots(isolate).exception();
  return isolate->heap()->ToBoolean(result.FromJust());
}

RUNTIME_FUNCTION(Runtime_CheckProxyDeleteTrapResult) {
  HandleScope scope(isolate);

  DCHECK_EQ(2, args.length());
  DirectHandle<Name> name = args.at<Name>(0);
  DirectHandle<JSReceiver> target = args.at<JSReceiver>(1);

  Maybe<bool> result = JSProxy::CheckDeleteTrap(isolate, name, target);
  if (!result.IsJust()) return ReadOnlyRoots(isolate).exception();
  return isolate->heap()->ToBoolean(result.FromJust());
}

}  // namespace internal
}  // namespace v8
