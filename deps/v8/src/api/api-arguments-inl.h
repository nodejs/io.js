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
  DCHECK(Is<JSAny>(*slot));
  return Cast<V>(Handle<Object>(slot.location()));
}

inline Tagged<JSObject> PropertyCallbackArguments::holder() const {
  return Cast<JSObject>(*slot_at(T::kHolderIndex));
}

inline Tagged<Object> PropertyCallbackArguments::receiver() const {
  return *slot_at(T::kThisIndex);
}

inline Tagged<JSReceiver> FunctionCallbackArguments::holder() const {
  return Cast<JSReceiver>(*slot_at(T::kHolderIndex));
}

#define DCHECK_NAME_COMPATIBLE(interceptor, name) \
  DCHECK(interceptor->is_named());                \
  DCHECK(!name->IsPrivate());                     \
  DCHECK_IMPLIES(IsSymbol(*name), interceptor->can_intercept_symbols());

#define PREPARE_CALLBACK_INFO_ACCESSOR(ISOLATE, F, API_RETURN_TYPE,            \
                                       ACCESSOR_INFO, RECEIVER, ACCESSOR_KIND, \
                                       EXCEPTION_CONTEXT)                      \
  if (ISOLATE->should_check_side_effects() &&                                  \
      !ISOLATE->debug()->PerformSideEffectCheckForAccessor(                    \
          ACCESSOR_INFO, RECEIVER, ACCESSOR_KIND)) {                           \
    return {};                                                                 \
  }                                                                            \
  const PropertyCallbackInfo<API_RETURN_TYPE>& callback_info =                 \
      GetPropertyCallbackInfo<API_RETURN_TYPE>();                              \
  ExternalCallbackScope call_scope(ISOLATE, FUNCTION_ADDR(F),                  \
                                   EXCEPTION_CONTEXT, &callback_info);

#define PREPARE_CALLBACK_INFO_INTERCEPTOR(ISOLATE, F, API_RETURN_TYPE,         \
                                          INTERCEPTOR_INFO, EXCEPTION_CONTEXT) \
  if (ISOLATE->should_check_side_effects() &&                                  \
      !ISOLATE->debug()->PerformSideEffectCheckForInterceptor(                 \
          INTERCEPTOR_INFO)) {                                                 \
    return {};                                                                 \
  }                                                                            \
  const PropertyCallbackInfo<API_RETURN_TYPE>& callback_info =                 \
      GetPropertyCallbackInfo<API_RETURN_TYPE>();                              \
  ExternalCallbackScope call_scope(ISOLATE, FUNCTION_ADDR(F),                  \
                                   EXCEPTION_CONTEXT, &callback_info);

Handle<Object> FunctionCallbackArguments::CallOrConstruct(
    Tagged<FunctionTemplateInfo> function, bool is_construct) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kFunctionCallback);
  v8::FunctionCallback f =
      reinterpret_cast<v8::FunctionCallback>(function->callback(isolate));
  if (isolate->should_check_side_effects() &&
      !isolate->debug()->PerformSideEffectCheckForCallback(
          handle(function, isolate))) {
    return {};
  }
  FunctionCallbackInfo<v8::Value> info(values_, argv_, argc_);
  ExternalCallbackScope call_scope(isolate, FUNCTION_ADDR(f),
                                   is_construct ? ExceptionContext::kConstructor
                                                : ExceptionContext::kOperation,
                                   &info);
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

Maybe<InterceptorResult> PropertyCallbackArguments::GetBooleanReturnValue(
    v8::Intercepted intercepted, const char* callback_kind_for_error_message,
    bool ignore_return_value) {
  Isolate* isolate = this->isolate();
  if (isolate->has_exception()) {
    // TODO(ishell, 328490288): fix Node.js which has Setter/Definer
    // interceptor callbacks not returning v8::Intercepted::kYes on exceptions.
    if ((false) && DEBUG_BOOL && (intercepted == v8::Intercepted::kNo)) {
      FATAL(
          "Check failed: %s interceptor callback has thrown an "
          "exception but hasn't returned v8::Intercepted::kYes.",
          callback_kind_for_error_message);
    }
    return Nothing<InterceptorResult>();
  }

  if (intercepted == v8::Intercepted::kNo) {
    // Not intercepted, there must be no side effects including exceptions.
    DCHECK(!isolate->has_exception());
    return Just(InterceptorResult::kNotIntercepted);
  }
  DCHECK_EQ(intercepted, v8::Intercepted::kYes);
  AcceptSideEffects();

  if (ignore_return_value) return Just(InterceptorResult::kTrue);

  bool result = IsTrue(*GetReturnValue<Boolean>(isolate), isolate);

  // TODO(ishell, 348688196): ensure callbacks comply with this and
  // enable the check.
  if ((false) && DEBUG_BOOL && !result && ShouldThrowOnError()) {
    FATAL(
        "Check failed: %s interceptor callback hasn't thrown an "
        "exception on failure as requested.",
        callback_kind_for_error_message);
  }
  return Just(result ? InterceptorResult::kTrue : InterceptorResult::kFalse);
}

// -------------------------------------------------------------------------
// Named Interceptor callbacks.

Handle<JSObjectOrUndefined> PropertyCallbackArguments::CallNamedEnumerator(
    Handle<InterceptorInfo> interceptor) {
  DCHECK(interceptor->is_named());
  RCS_SCOPE(isolate(), RuntimeCallCounterId::kNamedEnumeratorCallback);
  return CallPropertyEnumerator(interceptor);
}

// TODO(ishell): return std::optional<PropertyAttributes>.
Handle<Object> PropertyCallbackArguments::CallNamedQuery(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedQueryCallback);
  slot_at(kPropertyKeyIndex).store(*name);
  slot_at(kReturnValueIndex).store(Smi::FromInt(v8::None));
  NamedPropertyQueryCallback f =
      ToCData<NamedPropertyQueryCallback, kApiNamedPropertyQueryCallbackTag>(
          isolate, interceptor->query());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Integer, interceptor,
                                    ExceptionContext::kNamedQuery);
  v8::Intercepted intercepted = f(v8::Utils::ToLocal(name), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValue<Object>(isolate);
}

Handle<JSAny> PropertyCallbackArguments::CallNamedGetter(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedGetterCallback);
  slot_at(kPropertyKeyIndex).store(*name);
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).undefined_value());
  NamedPropertyGetterCallback f =
      ToCData<NamedPropertyGetterCallback, kApiNamedPropertyGetterCallbackTag>(
          isolate, interceptor->getter());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor,
                                    ExceptionContext::kNamedGetter);
  v8::Intercepted intercepted = f(v8::Utils::ToLocal(name), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValue<JSAny>(isolate);
}

Handle<JSAny> PropertyCallbackArguments::CallNamedDescriptor(
    Handle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDescriptorCallback);
  slot_at(kPropertyKeyIndex).store(*name);
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).undefined_value());
  NamedPropertyDescriptorCallback f =
      ToCData<NamedPropertyDescriptorCallback,
              kApiNamedPropertyDescriptorCallbackTag>(
          isolate, interceptor->descriptor());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor,
                                    ExceptionContext::kNamedDescriptor);
  v8::Intercepted intercepted = f(v8::Utils::ToLocal(name), callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValue<JSAny>(isolate);
}

v8::Intercepted PropertyCallbackArguments::CallNamedSetter(
    DirectHandle<InterceptorInfo> interceptor, Handle<Name> name,
    Handle<Object> value) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedSetterCallback);
  slot_at(kPropertyKeyIndex).store(*name);
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).true_value());
  NamedPropertySetterCallback f =
      ToCData<NamedPropertySetterCallback, kApiNamedPropertySetterCallbackTag>(
          isolate, interceptor->setter());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects,
                                    ExceptionContext::kNamedSetter);
  v8::Intercepted intercepted =
      f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
  return intercepted;
}

v8::Intercepted PropertyCallbackArguments::CallNamedDefiner(
    DirectHandle<InterceptorInfo> interceptor, Handle<Name> name,
    const v8::PropertyDescriptor& desc) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDefinerCallback);
  slot_at(kPropertyKeyIndex).store(*name);
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).true_value());
  NamedPropertyDefinerCallback f = ToCData<NamedPropertyDefinerCallback,
                                           kApiNamedPropertyDefinerCallbackTag>(
      isolate, interceptor->definer());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects,
                                    ExceptionContext::kNamedDefiner);
  v8::Intercepted intercepted =
      f(v8::Utils::ToLocal(name), desc, callback_info);
  return intercepted;
}

v8::Intercepted PropertyCallbackArguments::CallNamedDeleter(
    DirectHandle<InterceptorInfo> interceptor, Handle<Name> name) {
  DCHECK_NAME_COMPATIBLE(interceptor, name);
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedDeleterCallback);
  slot_at(kPropertyKeyIndex).store(*name);
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).true_value());
  NamedPropertyDeleterCallback f = ToCData<NamedPropertyDeleterCallback,
                                           kApiNamedPropertyDeleterCallbackTag>(
      isolate, interceptor->deleter());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Boolean, has_side_effects,
                                    ExceptionContext::kNamedDeleter);
  v8::Intercepted intercepted = f(v8::Utils::ToLocal(name), callback_info);
  return intercepted;
}

// -------------------------------------------------------------------------
// Indexed Interceptor callbacks.

Handle<JSObjectOrUndefined> PropertyCallbackArguments::CallIndexedEnumerator(
    Handle<InterceptorInfo> interceptor) {
  DCHECK(!interceptor->is_named());
  RCS_SCOPE(isolate(), RuntimeCallCounterId::kIndexedEnumeratorCallback);
  return CallPropertyEnumerator(interceptor);
}

// TODO(ishell): return std::optional<PropertyAttributes>.
Handle<Object> PropertyCallbackArguments::CallIndexedQuery(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedQueryCallback);
  index_ = index;
  slot_at(kPropertyKeyIndex).store(Smi::zero());  // indexed callback marker
  slot_at(kReturnValueIndex).store(Smi::FromInt(v8::None));
  IndexedPropertyQueryCallbackV2 f =
      ToCData<IndexedPropertyQueryCallbackV2,
              kApiIndexedPropertyQueryCallbackTag>(isolate,
                                                   interceptor->query());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Integer, interceptor,
                                    ExceptionContext::kIndexedQuery);
  v8::Intercepted intercepted = f(index, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValue<Object>(isolate);
}

Handle<JSAny> PropertyCallbackArguments::CallIndexedGetter(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kNamedGetterCallback);
  index_ = index;
  slot_at(kPropertyKeyIndex).store(Smi::zero());  // indexed callback marker
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).undefined_value());
  IndexedPropertyGetterCallbackV2 f =
      ToCData<IndexedPropertyGetterCallbackV2,
              kApiIndexedPropertyGetterCallbackTag>(isolate,
                                                    interceptor->getter());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor,
                                    ExceptionContext::kIndexedGetter);
  v8::Intercepted intercepted = f(index, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValue<JSAny>(isolate);
}

Handle<JSAny> PropertyCallbackArguments::CallIndexedDescriptor(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDescriptorCallback);
  index_ = index;
  slot_at(kPropertyKeyIndex).store(Smi::zero());  // indexed callback marker
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).undefined_value());
  IndexedPropertyDescriptorCallbackV2 f =
      ToCData<IndexedPropertyDescriptorCallbackV2,
              kApiIndexedPropertyDescriptorCallbackTag>(
          isolate, interceptor->descriptor());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Value, interceptor,
                                    ExceptionContext::kIndexedDescriptor);
  v8::Intercepted intercepted = f(index, callback_info);
  if (intercepted == v8::Intercepted::kNo) return {};
  return GetReturnValue<JSAny>(isolate);
}

v8::Intercepted PropertyCallbackArguments::CallIndexedSetter(
    DirectHandle<InterceptorInfo> interceptor, uint32_t index,
    Handle<Object> value) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedSetterCallback);
  index_ = index;
  slot_at(kPropertyKeyIndex).store(Smi::zero());  // indexed callback marker
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).true_value());
  IndexedPropertySetterCallbackV2 f =
      ToCData<IndexedPropertySetterCallbackV2,
              kApiIndexedPropertySetterCallbackTag>(isolate,
                                                    interceptor->setter());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects,
                                    ExceptionContext::kIndexedSetter);
  v8::Intercepted intercepted =
      f(index, v8::Utils::ToLocal(value), callback_info);
  return intercepted;
}

v8::Intercepted PropertyCallbackArguments::CallIndexedDefiner(
    DirectHandle<InterceptorInfo> interceptor, uint32_t index,
    const v8::PropertyDescriptor& desc) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDefinerCallback);
  index_ = index;
  slot_at(kPropertyKeyIndex).store(Smi::zero());  // indexed callback marker
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).true_value());
  IndexedPropertyDefinerCallbackV2 f =
      ToCData<IndexedPropertyDefinerCallbackV2,
              kApiIndexedPropertyDefinerCallbackTag>(isolate,
                                                     interceptor->definer());
  Handle<InterceptorInfo> has_side_effects;
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, void, has_side_effects,
                                    ExceptionContext::kIndexedDefiner);
  v8::Intercepted intercepted = f(index, desc, callback_info);
  return intercepted;
}

v8::Intercepted PropertyCallbackArguments::CallIndexedDeleter(
    Handle<InterceptorInfo> interceptor, uint32_t index) {
  DCHECK(!interceptor->is_named());
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kIndexedDeleterCallback);
  index_ = index;
  slot_at(kPropertyKeyIndex).store(Smi::zero());  // indexed callback marker
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).true_value());
  IndexedPropertyDeleterCallbackV2 f =
      ToCData<IndexedPropertyDeleterCallbackV2,
              kApiIndexedPropertyDeleterCallbackTag>(isolate,
                                                     interceptor->deleter());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Boolean, interceptor,
                                    ExceptionContext::kIndexedDeleter);
  v8::Intercepted intercepted = f(index, callback_info);
  return intercepted;
}

Handle<JSObjectOrUndefined> PropertyCallbackArguments::CallPropertyEnumerator(
    Handle<InterceptorInfo> interceptor) {
  // Named and indexed enumerator callbacks have same signatures.
  static_assert(std::is_same<NamedPropertyEnumeratorCallback,
                             IndexedPropertyEnumeratorCallback>::value);
  Isolate* isolate = this->isolate();
  slot_at(kPropertyKeyIndex).store(Smi::zero());  // not relevant
  // Enumerator callback's return value is initialized with undefined even
  // though it's supposed to return v8::Array.
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).undefined_value());
  // TODO(ishell): consider making it return v8::Intercepted to indicate
  // whether the result was set or not.
  IndexedPropertyEnumeratorCallback f =
      v8::ToCData<IndexedPropertyEnumeratorCallback,
                  kApiIndexedPropertyEnumeratorCallbackTag>(
          isolate, interceptor->enumerator());
  PREPARE_CALLBACK_INFO_INTERCEPTOR(isolate, f, v8::Array, interceptor,
                                    ExceptionContext::kNamedEnumerator);
  f(callback_info);
  Handle<JSAny> result = GetReturnValue<JSAny>(isolate);
  DCHECK(IsUndefined(*result) || IsJSObject(*result));
  return Cast<JSObjectOrUndefined>(result);
}

// -------------------------------------------------------------------------
// Accessors

Handle<JSAny> PropertyCallbackArguments::CallAccessorGetter(
    DirectHandle<AccessorInfo> info, Handle<Name> name) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kAccessorGetterCallback);
  // Unlike interceptor callbacks we know that the property exists, so
  // the callback is allowed to have side effects.
  AcceptSideEffects();

  slot_at(kPropertyKeyIndex).store(*name);
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).undefined_value());
  AccessorNameGetterCallback f =
      reinterpret_cast<AccessorNameGetterCallback>(info->getter(isolate));
  PREPARE_CALLBACK_INFO_ACCESSOR(isolate, f, v8::Value, info,
                                 handle(receiver(), isolate), ACCESSOR_GETTER,
                                 ExceptionContext::kAttributeGet);
  f(v8::Utils::ToLocal(name), callback_info);
  return GetReturnValue<JSAny>(isolate);
}

bool PropertyCallbackArguments::CallAccessorSetter(
    DirectHandle<AccessorInfo> accessor_info, Handle<Name> name,
    Handle<Object> value) {
  Isolate* isolate = this->isolate();
  RCS_SCOPE(isolate, RuntimeCallCounterId::kAccessorSetterCallback);
  // Unlike interceptor callbacks we know that the property exists, so
  // the callback is allowed to have side effects.
  AcceptSideEffects();

  slot_at(kPropertyKeyIndex).store(*name);
  slot_at(kReturnValueIndex).store(ReadOnlyRoots(isolate).true_value());
  // The actual type of setter callback is either
  // v8::AccessorNameSetterCallback or
  // i::Accesors::AccessorNameBooleanSetterCallback, depending on whether the
  // AccessorInfo was created by the API or internally (see accessors.cc).
  // Here we handle both cases using the AccessorNameSetterCallback signature
  // and checking whether the returned result is set to default value
  // (the undefined value).
  // TODO(ishell, 348660658): update V8 Api to allow setter callbacks provide
  // the result of [[Set]] operation according to JavaScript semantics.
  AccessorNameSetterCallback f = reinterpret_cast<AccessorNameSetterCallback>(
      accessor_info->setter(isolate));
  PREPARE_CALLBACK_INFO_ACCESSOR(isolate, f, void, accessor_info,
                                 handle(receiver(), isolate), ACCESSOR_SETTER,
                                 ExceptionContext::kAttributeSet);
  f(v8::Utils::ToLocal(name), v8::Utils::ToLocal(value), callback_info);
  // Historically, in case of v8::AccessorNameSetterCallback it wasn't allowed
  // to set the result and not setting the result was treated as successful
  // execution.
  // During interceptors Api refactoring it was temporarily allowed to call
  // v8::ReturnValue<void>::Set[NonEmpty](Local<S>) and the result was just
  // converted to v8::Boolean which was then treated as a result of [[Set]].
  // In case of AccessorNameBooleanSetterCallback, the result is always
  // set to v8::Boolean or an exception is be thrown (in which case the
  // result is ignored anyway). So, regardless of whether the signature was
  // v8::AccessorNameSetterCallback or AccessorNameBooleanSetterCallback
  // the result is guaranteed to be v8::Boolean value indicating success or
  // failure.
  DirectHandle<Boolean> result = GetReturnValue<Boolean>(isolate);
  return IsTrue(*result, isolate);
}

#undef PREPARE_CALLBACK_INFO_ACCESSOR
#undef PREPARE_CALLBACK_INFO_INTERCEPTOR

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_ARGUMENTS_INL_H_
