// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/heap/heap-inl.h"  // For ToBoolean.
#include "src/logging/counters.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

#define CHECK_CALLSITE(frame, method)                                         \
  CHECK_RECEIVER(JSObject, receiver, method);                                 \
  LookupIterator it(isolate, receiver,                                        \
                    isolate->factory()->call_site_info_symbol(),              \
                    LookupIterator::OWN_SKIP_INTERCEPTOR);                    \
  if (it.state() != LookupIterator::DATA) {                                   \
    THROW_NEW_ERROR_RETURN_FAILURE(                                           \
        isolate,                                                              \
        NewTypeError(MessageTemplate::kCallSiteMethod,                        \
                     isolate->factory()->NewStringFromAsciiChecked(method))); \
  }                                                                           \
  auto frame = Cast<CallSiteInfo>(it.GetDataValue())

namespace {

Tagged<Object> PositiveNumberOrNull(int value, Isolate* isolate) {
  if (value > 0) return *isolate->factory()->NewNumberFromInt(value);
  return ReadOnlyRoots(isolate).null_value();
}

bool NativeContextIsForShadowRealm(Tagged<NativeContext> native_context) {
  return native_context->scope_info()->scope_type() == SHADOW_REALM_SCOPE;
}

}  // namespace

BUILTIN(CallSitePrototypeGetColumnNumber) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getColumnNumber");
  return PositiveNumberOrNull(CallSiteInfo::GetColumnNumber(frame), isolate);
}

BUILTIN(CallSitePrototypeGetEnclosingColumnNumber) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getEnclosingColumnNumber");
  return PositiveNumberOrNull(CallSiteInfo::GetEnclosingColumnNumber(frame),
                              isolate);
}

BUILTIN(CallSitePrototypeGetEnclosingLineNumber) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getEnclosingLineNumber");
  return PositiveNumberOrNull(CallSiteInfo::GetEnclosingLineNumber(frame),
                              isolate);
}

BUILTIN(CallSitePrototypeGetEvalOrigin) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getEvalOrigin");
  return *CallSiteInfo::GetEvalOrigin(frame);
}

BUILTIN(CallSitePrototypeGetFileName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getFileName");
  return frame->GetScriptName();
}

BUILTIN(CallSitePrototypeGetFunction) {
  static const char method_name[] = "getFunction";
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, method_name);
  // ShadowRealms have a boundary: references to outside objects must not exist
  // in the ShadowRealm, and references to ShadowRealm objects must not exist
  // outside the ShadowRealm.
  if (NativeContextIsForShadowRealm(isolate->raw_native_context()) ||
      (IsJSFunction(frame->function()) &&
       NativeContextIsForShadowRealm(
           Cast<JSFunction>(frame->function())->native_context()))) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(
            MessageTemplate::kCallSiteMethodUnsupportedInShadowRealm,
            isolate->factory()->NewStringFromAsciiChecked(method_name)));
  }
  if (frame->IsStrict() ||
      (IsJSFunction(frame->function()) &&
       Cast<JSFunction>(frame->function())->shared()->is_toplevel())) {
    return ReadOnlyRoots(isolate).undefined_value();
  }
  isolate->CountUsage(v8::Isolate::kCallSiteAPIGetFunctionSloppyCall);
  return frame->function();
}

BUILTIN(CallSitePrototypeGetFunctionName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getFunctionName");
  return *CallSiteInfo::GetFunctionName(frame);
}

BUILTIN(CallSitePrototypeGetLineNumber) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getLineNumber");
  return PositiveNumberOrNull(CallSiteInfo::GetLineNumber(frame), isolate);
}

BUILTIN(CallSitePrototypeGetMethodName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getMethodName");
  return *CallSiteInfo::GetMethodName(frame);
}

BUILTIN(CallSitePrototypeGetPosition) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getPosition");
  return Smi::FromInt(CallSiteInfo::GetSourcePosition(frame));
}

BUILTIN(CallSitePrototypeGetPromiseIndex) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getPromiseIndex");
  if (!frame->IsPromiseAll() && !frame->IsPromiseAny() &&
      !frame->IsPromiseAllSettled()) {
    return ReadOnlyRoots(isolate).null_value();
  }
  return Smi::FromInt(CallSiteInfo::GetSourcePosition(frame));
}

BUILTIN(CallSitePrototypeGetScriptHash) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getScriptHash");
  return *CallSiteInfo::GetScriptHash(frame);
}

BUILTIN(CallSitePrototypeGetScriptNameOrSourceURL) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getScriptNameOrSourceUrl");
  return frame->GetScriptNameOrSourceURL();
}

BUILTIN(CallSitePrototypeGetThis) {
  static const char method_name[] = "getThis";
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, method_name);
  // ShadowRealms have a boundary: references to outside objects must not exist
  // in the ShadowRealm, and references to ShadowRealm objects must not exist
  // outside the ShadowRealm.
  if (NativeContextIsForShadowRealm(isolate->raw_native_context()) ||
      (IsJSFunction(frame->function()) &&
       NativeContextIsForShadowRealm(
           Cast<JSFunction>(frame->function())->native_context()))) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(
            MessageTemplate::kCallSiteMethodUnsupportedInShadowRealm,
            isolate->factory()->NewStringFromAsciiChecked(method_name)));
  }
  if (frame->IsStrict()) return ReadOnlyRoots(isolate).undefined_value();
  isolate->CountUsage(v8::Isolate::kCallSiteAPIGetThisSloppyCall);
#if V8_ENABLE_WEBASSEMBLY
  if (frame->IsAsmJsWasm()) {
    return frame->GetWasmInstance()
        ->trusted_data(isolate)
        ->native_context()
        ->global_proxy();
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return frame->receiver_or_instance();
}

BUILTIN(CallSitePrototypeGetTypeName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "getTypeName");
  return *CallSiteInfo::GetTypeName(frame);
}

BUILTIN(CallSitePrototypeIsAsync) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "isAsync");
  return isolate->heap()->ToBoolean(frame->IsAsync());
}

BUILTIN(CallSitePrototypeIsConstructor) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "isConstructor");
  return isolate->heap()->ToBoolean(frame->IsConstructor());
}

BUILTIN(CallSitePrototypeIsEval) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "isEval");
  return isolate->heap()->ToBoolean(frame->IsEval());
}

BUILTIN(CallSitePrototypeIsNative) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "isNative");
  return isolate->heap()->ToBoolean(frame->IsNative());
}

BUILTIN(CallSitePrototypeIsPromiseAll) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "isPromiseAll");
  return isolate->heap()->ToBoolean(frame->IsPromiseAll());
}

BUILTIN(CallSitePrototypeIsToplevel) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "isToplevel");
  return isolate->heap()->ToBoolean(frame->IsToplevel());
}

BUILTIN(CallSitePrototypeToString) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(frame, "toString");
  RETURN_RESULT_OR_FAILURE(isolate, SerializeCallSiteInfo(isolate, frame));
}

#undef CHECK_CALLSITE

}  // namespace internal
}  // namespace v8
