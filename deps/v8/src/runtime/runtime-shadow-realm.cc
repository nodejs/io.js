// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments-inl.h"
#include "src/objects/js-function.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_ShadowRealmWrappedFunctionCreate) {
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  DirectHandle<NativeContext> native_context = args.at<NativeContext>(0);
  DirectHandle<JSReceiver> value = args.at<JSReceiver>(1);

  RETURN_RESULT_OR_FAILURE(
      isolate, JSWrappedFunction::Create(isolate, native_context, value));
}

// https://tc39.es/proposal-shadowrealm/#sec-shadowrealm.prototype.importvalue
RUNTIME_FUNCTION(Runtime_ShadowRealmImportValue) {
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  Handle<String> specifier = args.at<String>(0);

  DirectHandle<JSPromise> inner_capability;

  MaybeDirectHandle<Object> import_options;
  MaybeDirectHandle<Script> referrer;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, inner_capability,
      isolate->RunHostImportModuleDynamicallyCallback(
          referrer, specifier, ModuleImportPhase::kEvaluation, import_options));
  // Check that the promise is created in the eval_context.
  DCHECK_EQ(inner_capability->GetCreationContext().value(),
            isolate->raw_native_context());

  return *inner_capability;
}

RUNTIME_FUNCTION(Runtime_ShadowRealmThrow) {
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  int message_id_smi = args.smi_value_at(0);
  DirectHandle<Object> value = args.at(1);

  MessageTemplate message_id = MessageTemplateFromInt(message_id_smi);

  DirectHandle<String> string = Object::NoSideEffectsToString(isolate, value);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, ShadowRealmNewTypeErrorCopy(value, message_id, string));
}

}  // namespace internal
}  // namespace v8
