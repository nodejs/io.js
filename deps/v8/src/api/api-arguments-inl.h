// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_API_ARGUMENTS_INL_H_
#define V8_API_API_ARGUMENTS_INL_H_

#include "src/api/api-arguments.h"
#include "src/api/api-inl.h"
#include "src/debug/debug.h"
#include "src/execution/vm-state-inl.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/instance-type.h"
#include "src/objects/slots-inl.h"

namespace v8 {
namespace internal {

#if DEBUG
bool IsApiCallResultType(Tagged<Object> obj) {
  if (IsSmi(obj)) return true;
  DCHECK(IsHeapObject(obj));
  return (IsString(obj) || IsSymbol(obj) || IsJSReceiver(obj) ||
          IsHeapNumber(obj) || IsBigInt(obj) || IsUndefined(obj) ||
          IsTrue(obj) || IsFalse(obj) || IsNull(obj));
}
#endif  // DEBUG

CustomArgumentsBase::CustomArgumentsBase(Isolate* isolate)
    : Relocatable(isolate) {}

template <typename T>
CustomArguments<T>::~CustomArguments() {
  slot_at(kReturnValueIndex).store(Tagged<Object>(kHandleZapValue));
}

template <typename T>
template <typename V>
Handle<V> CustomArguments<T>::GetReturnValue(Isolate* isolate) const {
  // Check the ReturnValue.
  FullObjectSlot slot = slot_at(kReturnValueIndex);
  // Nothing was set, return empty handle as per previous behaviour.
  Tagged<Object> raw_object = *slot;
  if (IsTheHole(raw_object, isolate)) return Handle<V>();
  DCHECK(IsApiCallResultType(raw_object));
  return Handle<V>::cast(Handle<Object>(slot.location()));
}

template <typename T>
template <typename V>
Handle<V> CustomArguments<T>::GetReturnValueNoHoleCheck(
    Isolate* isolate) const {
  // Check the ReturnValue.
  FullObjectSlot slot = slot_at(kReturnValueIndex);
  // TODO(ishell): remove the hole check once it's no longer possible to set
  // return value to the hole.
  CHECK(!IsTheHole(*slot, isolate));
  DCHECK(IsApiCallResultType(*slot));
  return Handle<V>::cast(Handle<Object>(slot.location()));
}

inline Tagged<JSObject> PropertyCallbackArguments::holder() const {
  return JSObject::cast(*slot_at(T::kHolderIndex));
}

inline Tagged<Object> PropertyCallbackArguments::receiver() const {
  return *slot_at(T::kThisIndex);
}

inline Tagged<JSReceiver> FunctionCallbackArguments::holder() const {
  return JSReceiver::cast(*slot_at(T::kHolderIndex));
}

#define DCHECK_NAME_COMPATIBLE(interceptor, name) \
  DCHECK(interceptor->is_named());                \
  DCHECK(!name->IsPrivate());                     \
  DCHECK_IMPLIES(IsSymbol(*name), interceptor->can_intercept_symbols());

#define PREPARE_CALLBACK_INFO_ACCESSOR(ISOLATE, F, API_RETURN_TYPE,            \
                                       ACCESSOR_INFO, RECEIVER, ACCESSOR_KIND) \
  if (ISOLATE->should_check_side_effects() &&                                  \
      !ISOLATE->debug()->PerformSideEffectCheckForAccessor(                    \
          ACCESSOR_INFO, RECEIVER, ACCESSOR_KIND)) {                           \
    return {};                                                                 \
  }                                                                            \
  ExternalCallbackScope call_scope(ISOLATE, FUNCTION_ADDR(F));                 \
  PropertyCallbackInfo<API_RETURN_TYPE> callback_info(values_);

#define PREPARE_CALLBACK_INFO_INTERCEPTOR(ISOLATE, F, API_RETURN_TYPE, \
                                          INTERCEPTOR_INFO)            \
  if (ISOLATE->should_check_side_effects() &&                          \
      !ISOLATE->debug()->PerformSideEffectCheckForInterceptor(         \
          INTERCEPTOR_INFO)) {                                         \
    return {};                                                         \
  }                                                                    \
  ExternalCallbackScope call_scope(ISOLATE, FUNCTION_ADDR(F));         \
  PropertyCallbackInfo<API_RETURN_TYPE> callback_info(values_);

Handle<Object> FunctionCallbackArguments::Call(
    Tagged<FunctionTemplateInfo> function) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kFunctionCallback);
  v8::FunctionCallback f =
      reinterpret_cast<v8::FunctionCallback>(function->callback(isolate));
  Handle<Object> receiver_check_unsupported;
  if (isolate->should_check_side_effects() &&
      !isolate->debug()->PerformSideEffectCheckForCallback(
          handle(function, isolate))) {
    return {};
  }
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f));
  FunctionCallbackInfo<v8::Value> info(values_, argv_, argc_);
  f(info);
  return GetReturnValue<Object>(isolate);
}

PropertyCallbackArguments::~PropertyCallbackArguments(){
#ifdef DEBUG
// TODO(chromium:1310062): enable this check.
// if (javascript_execution_counter_) {
//   CHECK_WITH_MSG(javascript_execution_counter_ ==
//                      isolate()->javascript_execution_counter(),
//                  "Unexpected side effect detected");
// }
#endif  // DEBUG
}

// -------------------------------------------------------------------------
// Named Interceptor callbacks.

Handle<JSObject> PropertyCallbackArguments::CallNamedEnumerator(
    Handle<InterceptorInfo> interceptor) {
  DCHECK(interceptor->is_named());
  RCS_SCOPE(isolate(), RuntimeCallCounterId::kNamedEnumeratorCallback);
  return CallPropertyEnumerator(interceptor);
}

Handle<Object> PropertyCallbackArguments::CallNamedQuery(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedQueryCallback);
  Handle<Object> receiver_check_unsupported;
  if (interceptor->has_new_callbacks_signature()) {
    NamedPropertyQueryCallback f =
        ToCData<NamedPropertyQueryCallback>(interceptor->query());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Integer, interceptor);
    auto intercepted = f(v8::Utils::ToLocal(name), callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    return GetReturnValueNoHoleCheck<Object>(isolate);

  } else {
    GenericNamedPropertyQueryCallback f =
        ToCData<GenericNamedPropertyQueryCallback>(interceptor->query());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Integer, interceptor);
    f(v8::Utils::ToLocal(name), callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

Handle<Object> PropertyCallbackArguments::CallNamedGetter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedGetterCallback);
  if (interceptor->has_new_callbacks_signature()) {
    NamedPropertyGetterCallback f =
        ToCData<NamedPropertyGetterCallback>(interceptor->getter());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
    auto intercepted = f(v8::Utils::ToLocal(name), callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    return GetReturnValueNoHoleCheck<Object>(isolate);

  } else {
    GenericNamedPropertyGetterCallback f =
        ToCData<GenericNamedPropertyGetterCallback>(interceptor->getter());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
    f(v8::Utils::ToLocal(name), callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

Handle<Object> PropertyCallbackArguments::CallNamedDescriptor(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDescriptorCallback);
  if (interceptor->has_new_callbacks_signature()) {
    NamedPropertyDescriptorCallback f =
        ToCData<NamedPropertyDescriptorCallback>(interceptor->descriptor());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
    auto intercepted = f(v8::Utils::ToLocal(name), callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    return GetReturnValueNoHoleCheck<Object>(isolate);

  } else {
    GenericNamedPropertyDescriptorCallback f =
        ToCData<GenericNamedPropertyDescriptorCallback>(
            interceptor->descriptor());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
    f(v8::Utils::ToLocal(name), callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

// TODO(ishell): just return v8::Intercepted.
Handle<Object> PropertyCallbackArguments::CallNamedSetter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name,
    Handle<Object> value) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedSetterCallback);
  if (interceptor->has_new_callbacks_signature()) {
    NamedPropertySetterCallback f =
        ToCData<NamedPropertySetterCallback>(interceptor->setter());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects);
    auto intercepted =
        f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    // Non-empty handle indicates that the request was intercepted.
    return isolate->factory()->undefined_value();

  } else {
    GenericNamedPropertySetterCallback f =
        ToCData<GenericNamedPropertySetterCallback>(interceptor->setter());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, has_side_effects);
    f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

// TODO(ishell): just return v8::Intercepted.
Handle<Object> PropertyCallbackArguments::CallNamedDefiner(
    Handle<InterceptorInfo> interceptor, Handle<Name> name,
    const v8::PropertyDescriptor& desc) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDefinerCallback);
  if (interceptor->has_new_callbacks_signature()) {
    NamedPropertyDefinerCallback f =
        ToCData<NamedPropertyDefinerCallback>(interceptor->definer());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects);
    auto intercepted = f(v8::Utils::ToLocal(name), desc, callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    // Non-empty handle indicates that the request was intercepted.
    return isolate->factory()->undefined_value();

  } else {
    GenericNamedPropertyDefinerCallback f =
        ToCData<GenericNamedPropertyDefinerCallback>(interceptor->definer());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, has_side_effects);
    f(v8::Utils::ToLocal(name), desc, callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

// TODO(ishell): return Handle<Boolean>
Handle<Object> PropertyCallbackArguments::CallNamedDeleter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDeleterCallback);
  if (interceptor->has_new_callbacks_signature()) {
    NamedPropertyDeleterCallback f =
        ToCData<NamedPropertyDeleterCallback>(interceptor->deleter());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Boolean,
                                      has_side_effects);
    auto intercepted = f(v8::Utils::ToLocal(name), callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    return GetReturnValue<Object>(isolate);

  } else {
    GenericNamedPropertyDeleterCallback f =
        ToCData<GenericNamedPropertyDeleterCallback>(interceptor->deleter());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Boolean,
                                      has_side_effects);
    f(v8::Utils::ToLocal(name), callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

// -------------------------------------------------------------------------
// Indexed Interceptor callbacks.

Handle<JSObject> PropertyCallbackArguments::CallIndexedEnumerator(
    Handle<InterceptorInfo> interceptor) {
  DCHECK(!interceptor->is_named());
  RCS_SCOPE(isolate(), RuntimeCallCounterId::kIndexedEnumeratorCallback);
  return CallPropertyEnumerator(interceptor);
}

Handle<Object> PropertyCallbackArguments::CallIndexedQuery(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedQueryCallback);
  if (interceptor->has_new_callbacks_signature()) {
    IndexedPropertyQueryCallbackV2 f =
        ToCData<IndexedPropertyQueryCallbackV2>(interceptor->query());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Integer, interceptor);
    auto intercepted = f(index, callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    return GetReturnValueNoHoleCheck<Object>(isolate);

  } else {
    IndexedPropertyQueryCallback f =
        ToCData<IndexedPropertyQueryCallback>(interceptor->query());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Integer, interceptor);
    f(index, callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

Handle<Object> PropertyCallbackArguments::CallIndexedGetter(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  RCS_SCOPE(isolate(), RuntimeCallCounterId::kNamedGetterCallback);
  if (interceptor->has_new_callbacks_signature()) {
    IndexedPropertyGetterCallbackV2 f =
        ToCData<IndexedPropertyGetterCallbackV2>(interceptor->getter());
    Isolate* isolate = this->isolate();
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
    auto intercepted = f(index, callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    return GetReturnValueNoHoleCheck<Object>(isolate);

  } else {
    IndexedPropertyGetterCallback f =
        ToCData<IndexedPropertyGetterCallback>(interceptor->getter());
    Isolate* isolate = this->isolate();
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
    f(index, callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

Handle<Object> PropertyCallbackArguments::CallIndexedDescriptor(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDescriptorCallback);
  if (interceptor->has_new_callbacks_signature()) {
    IndexedPropertyDescriptorCallbackV2 f =
        ToCData<IndexedPropertyDescriptorCallbackV2>(interceptor->descriptor());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
    auto intercepted = f(index, callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    return GetReturnValueNoHoleCheck<Object>(isolate);

  } else {
    IndexedPropertyDescriptorCallback f =
        ToCData<IndexedPropertyDescriptorCallback>(interceptor->descriptor());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor);
    f(index, callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

// TODO(ishell): just return v8::Intercepted.
Handle<Object> PropertyCallbackArguments::CallIndexedSetter(
    Handle<InterceptorInfo> interceptor, uint32_t index, Handle<Object> value) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedSetterCallback);
  if (interceptor->has_new_callbacks_signature()) {
    IndexedPropertySetterCallbackV2 f =
        ToCData<IndexedPropertySetterCallbackV2>(interceptor->setter());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects);
    auto intercepted = f(index, v8::Utils::ToLocal(value), callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    // Non-empty handle indicates that the request was intercepted.
    return isolate->factory()->undefined_value();

  } else {
    IndexedPropertySetterCallback f =
        ToCData<IndexedPropertySetterCallback>(interceptor->setter());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, has_side_effects);
    f(index, v8::Utils::ToLocal(value), callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

// TODO(ishell): just return v8::Intercepted.
Handle<Object> PropertyCallbackArguments::CallIndexedDefiner(
    Handle<InterceptorInfo> interceptor, uint32_t index,
    const v8::PropertyDescriptor& desc) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDefinerCallback);
  if (interceptor->has_new_callbacks_signature()) {
    IndexedPropertyDefinerCallbackV2 f =
        ToCData<IndexedPropertyDefinerCallbackV2>(interceptor->definer());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects);
    auto intercepted = f(index, desc, callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    // Non-empty handle indicates that the request was intercepted.
    return isolate->factory()->undefined_value();

  } else {
    IndexedPropertyDefinerCallback f =
        ToCData<IndexedPropertyDefinerCallback>(interceptor->definer());
    Handle<InterceptorInfo> has_side_effects;
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, has_side_effects);
    f(index, desc, callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

// TODO(ishell): return Handle<Boolean>
Handle<Object> PropertyCallbackArguments::CallIndexedDeleter(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDeleterCallback);
  if (interceptor->has_new_callbacks_signature()) {
    IndexedPropertyDeleterCallbackV2 f =
        ToCData<IndexedPropertyDeleterCallbackV2>(interceptor->deleter());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Boolean, interceptor);
    auto intercepted = f(index, callback_info);
    if (intercepted == v8::Intercepted::kNo) return {};
    return GetReturnValueNoHoleCheck<Object>(isolate);

  } else {
    IndexedPropertyDeleterCallback f =
        ToCData<IndexedPropertyDeleterCallback>(interceptor->deleter());
    PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Boolean, interceptor);
    f(index, callback_info);
    return GetReturnValue<Object>(isolate);
  }
}

Handle<JSObject> PropertyCallbackArguments::CallPropertyEnumerator(
    Handle<InterceptorInfo> interceptor) {
  // Named and indexed enumerator callbacks have same signatures.
  static_assert(std::is_same<NamedPropertyEnumeratorCallback,
                             IndexedPropertyEnumeratorCallback>::value);
  IndexedPropertyEnumeratorCallback f =
      v8::ToCData<IndexedPropertyEnumeratorCallback>(interceptor->enumerator());
  Isolate* isolate = this->isolate();
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Array, interceptor);
  f(callback_info);
  return GetReturnValue<JSObject>(isolate);
}

// -------------------------------------------------------------------------
// Accessors

Handle<Object> PropertyCallbackArguments::CallAccessorGetter(
    Handle<AccessorInfo> info, Handle<Name> name) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kAccessorGetterCallback);
  // Unlike interceptor callbacks we know that the property exists, so
  // the callback is allowed to have side effects.
  AcceptSideEffects();

  AccessorNameGetterCallback f =
      reinterpret_cast<AccessorNameGetterCallback>(info->getter(isolate));
  PREPARE_CALLBACK_INFO_ACCESSOR(isolate, f, v8::Value, info,
                                 handle(receiver(), isolate), ACCESSOR_GETTER);
  f(v8::Utils::ToLocal(name), callback_info);
  return GetReturnValue<Object>(isolate);
}

Handle<Object> PropertyCallbackArguments::CallAccessorSetter(
    Handle<AccessorInfo> accessor_info, Handle<Name> name,
    Handle<Object> value) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kAccessorSetterCallback);
  // Unlike interceptor callbacks we know that the property exists, so
  // the callback is allowed to have side effects.
  AcceptSideEffects();

  AccessorNameSetterCallback f = reinterpret_cast<AccessorNameSetterCallback>(
      accessor_info->setter(isolate));
  PREPARE_CALLBACK_INFO_ACCESSOR(isolate, f, void, accessor_info,
                                 handle(receiver(), isolate), ACCESSOR_SETTER);
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
  return GetReturnValue<Object>(isolate);
}

#undef PREPARE_CALLBACK_INFO_ACCESSOR
#undef PREPARE_CALLBACK_INFO_INTERCEPTOR

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_ARGUMENTS_INL_H_
