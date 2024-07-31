// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/call-optimization.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

template <class IsolateT>
CallOptimization::CallOptimization(IsolateT* isolate, Handle<Object> function) {
  if (IsJSFunction(*function)) {
    Initialize(isolate, Handle<JSFunction>::cast(function));
  } else if (IsFunctionTemplateInfo(*function)) {
    Initialize(isolate, Handle<FunctionTemplateInfo>::cast(function));
  }
}

// Instantiations.
template CallOptimization::CallOptimization(Isolate* isolate,
                                            Handle<Object> function);
template CallOptimization::CallOptimization(LocalIsolate* isolate,
                                            Handle<Object> function);

base::Optional<Tagged<NativeContext>> CallOptimization::GetAccessorContext(
    Tagged<Map> holder_map) const {
  if (is_constant_call()) {
    return constant_function_->native_context();
  }
  Tagged<Object> maybe_native_context =
      holder_map->map()->native_context_or_null();
  if (IsNull(maybe_native_context)) {
    // The holder is a remote object which doesn't have a creation context.
    return {};
  }
  DCHECK(IsNativeContext(maybe_native_context));
  return NativeContext::cast(maybe_native_context);
}

bool CallOptimization::IsCrossContextLazyAccessorPair(
    Tagged<NativeContext> native_context, Tagged<Map> holder_map) const {
  DCHECK(IsNativeContext(native_context));
  if (is_constant_call()) return false;
  base::Optional<Tagged<NativeContext>> maybe_context =
      GetAccessorContext(holder_map);
  if (!maybe_context.has_value()) {
    // The holder is a remote object which doesn't have a creation context.
    return true;
  }
  return native_context != maybe_context.value();
}

template <class IsolateT>
Handle<JSObject> CallOptimization::LookupHolderOfExpectedType(
    IsolateT* isolate, Handle<Map> object_map,
    HolderLookup* holder_lookup) const {
  DCHECK(is_simple_api_call());
  if (!IsJSObjectMap(*object_map)) {
    *holder_lookup = kHolderNotFound;
    return Handle<JSObject>::null();
  }
  if (expected_receiver_type_.is_null() ||
      expected_receiver_type_->IsTemplateFor(*object_map)) {
    *holder_lookup = kHolderIsReceiver;
    return Handle<JSObject>::null();
  }
  if (IsJSGlobalProxyMap(*object_map) && !IsNull(object_map->prototype())) {
    Tagged<JSObject> raw_prototype = JSObject::cast(object_map->prototype());
    Handle<JSObject> prototype(raw_prototype, isolate);
    object_map = handle(prototype->map(), isolate);
    if (expected_receiver_type_->IsTemplateFor(*object_map)) {
      *holder_lookup = kHolderFound;
      return prototype;
    }
  }
  *holder_lookup = kHolderNotFound;
  return Handle<JSObject>::null();
}

// Instantiations.
template Handle<JSObject> CallOptimization::LookupHolderOfExpectedType(
    Isolate* isolate, Handle<Map> object_map,
    HolderLookup* holder_lookup) const;
template Handle<JSObject> CallOptimization::LookupHolderOfExpectedType(
    LocalIsolate* isolate, Handle<Map> object_map,
    HolderLookup* holder_lookup) const;

bool CallOptimization::IsCompatibleReceiverMap(
    Handle<JSObject> api_holder, Handle<JSObject> holder,
    HolderLookup holder_lookup) const {
  DCHECK(is_simple_api_call());
  switch (holder_lookup) {
    case kHolderNotFound:
      return false;
    case kHolderIsReceiver:
      return true;
    case kHolderFound:
      if (api_holder.is_identical_to(holder)) return true;
      // Check if holder is in prototype chain of api_holder.
      {
        Tagged<JSObject> object = *api_holder;
        while (true) {
          Tagged<Object> prototype = object->map()->prototype();
          if (!IsJSObject(prototype)) return false;
          if (prototype == *holder) return true;
          object = JSObject::cast(prototype);
        }
      }
  }
  UNREACHABLE();
}

template <class IsolateT>
void CallOptimization::Initialize(
    IsolateT* isolate, Handle<FunctionTemplateInfo> function_template_info) {
  if (!function_template_info->has_callback(isolate)) return;
  api_call_info_ = function_template_info;

  Tagged<HeapObject> signature = function_template_info->signature();
  if (!IsUndefined(signature, isolate)) {
    expected_receiver_type_ =
        handle(FunctionTemplateInfo::cast(signature), isolate);
  }
  is_simple_api_call_ = true;
  accept_any_receiver_ = function_template_info->accept_any_receiver();
}

template <class IsolateT>
void CallOptimization::Initialize(IsolateT* isolate,
                                  Handle<JSFunction> function) {
  if (function.is_null() || !function->is_compiled(isolate)) return;

  constant_function_ = function;
  AnalyzePossibleApiFunction(isolate, function);
}

template <class IsolateT>
void CallOptimization::AnalyzePossibleApiFunction(IsolateT* isolate,
                                                  Handle<JSFunction> function) {
  if (!function->shared()->IsApiFunction()) return;
  Handle<FunctionTemplateInfo> function_template_info(
      function->shared()->api_func_data(), isolate);
  Initialize(isolate, function_template_info);
}
}  // namespace internal
}  // namespace v8
