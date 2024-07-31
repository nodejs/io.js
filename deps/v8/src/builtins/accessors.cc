// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/accessors.h"

#include "src/api/api-inl.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/execution.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/messages.h"
#include "src/heap/factory.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/contexts.h"
#include "src/objects/field-index-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-shared-array-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/property-details.h"
#include "src/objects/prototype.h"

namespace v8 {
namespace internal {

Handle<AccessorInfo> Accessors::MakeAccessor(
    Isolate* isolate, Handle<Name> name, AccessorNameGetterCallback getter,
    AccessorNameBooleanSetterCallback setter) {
  Factory* factory = isolate->factory();
  name = factory->InternalizeName(name);
  Handle<AccessorInfo> info = factory->NewAccessorInfo();
  {
    DisallowGarbageCollection no_gc;
    Tagged<AccessorInfo> raw = *info;
    raw->set_is_special_data_property(true);
    raw->set_is_sloppy(false);
    raw->set_replace_on_access(false);
    raw->set_getter_side_effect_type(SideEffectType::kHasSideEffect);
    raw->set_setter_side_effect_type(SideEffectType::kHasSideEffect);
    raw->set_name(*name);
    raw->set_getter(isolate, reinterpret_cast<Address>(getter));
    if (setter == nullptr) setter = &ReconfigureToDataProperty;
    raw->set_setter(isolate, reinterpret_cast<Address>(setter));
  }
  return info;
}

static V8_INLINE bool CheckForName(Isolate* isolate, Handle<Name> name,
                                   Handle<String> property_name, int offset,
                                   FieldIndex::Encoding encoding,
                                   FieldIndex* index) {
  if (Name::Equals(isolate, name, property_name)) {
    *index = FieldIndex::ForInObjectOffset(offset, encoding);
    return true;
  }
  return false;
}

// Returns true for properties that are accessors to object fields.
// If true, *object_offset contains offset of object field.
bool Accessors::IsJSObjectFieldAccessor(Isolate* isolate, Handle<Map> map,
                                        Handle<Name> name, FieldIndex* index) {
  if (map->is_dictionary_map()) {
    // There are not descriptors in a dictionary mode map.
    return false;
  }

  switch (map->instance_type()) {
    case JS_ARRAY_TYPE:
      return CheckForName(isolate, name, isolate->factory()->length_string(),
                          JSArray::kLengthOffset, FieldIndex::kTagged, index);
    default:
      if (map->instance_type() < FIRST_NONSTRING_TYPE) {
        return CheckForName(isolate, name, isolate->factory()->length_string(),
                            offsetof(String, length_), FieldIndex::kWord32,
                            index);
      }

      return false;
  }
}

V8_WARN_UNUSED_RESULT MaybeHandle<Object>
Accessors::ReplaceAccessorWithDataProperty(Isolate* isolate,
                                           Handle<Object> receiver,
                                           Handle<JSObject> holder,
                                           Handle<Name> name,
                                           Handle<Object> value) {
  LookupIterator it(isolate, receiver, PropertyKey(isolate, name), holder,
                    LookupIterator::OWN_SKIP_INTERCEPTOR);
  // Skip any access checks we might hit. This accessor should never hit in a
  // situation where the caller does not have access.
  if (it.state() == LookupIterator::ACCESS_CHECK) {
    CHECK(it.HasAccess());
    it.Next();
  }
  DCHECK(holder.is_identical_to(it.GetHolder<JSObject>()));
  CHECK_EQ(LookupIterator::ACCESSOR, it.state());
  it.ReconfigureDataProperty(value, it.property_attributes());
  return value;
}

//
// Accessors::ReconfigureToDataProperty
//
void Accessors::ReconfigureToDataProperty(
    v8::Local<v8::Name> key, v8::Local<v8::Value> val,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kReconfigureToDataProperty);
  HandleScope scope(isolate);
  Handle<Object> receiver = Utils::OpenHandle(*info.This());
  Handle<JSObject> holder =
      Handle<JSObject>::cast(Utils::OpenHandle(*info.Holder()));
  Handle<Name> name = Utils::OpenHandle(*key);
  Handle<Object> value = Utils::OpenHandle(*val);
  MaybeHandle<Object> result = Accessors::ReplaceAccessorWithDataProperty(
      isolate, receiver, holder, name, value);
  if (!result.is_null()) {
    info.GetReturnValue().Set(true);
  }
}

//
// Accessors::ArgumentsIterator
//

void Accessors::ArgumentsIteratorGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  DisallowGarbageCollection no_gc;
  HandleScope scope(isolate);
  Tagged<Object> result = isolate->native_context()->array_values_iterator();
  info.GetReturnValue().Set(Utils::ToLocal(Handle<Object>(result, isolate)));
}

Handle<AccessorInfo> Accessors::MakeArgumentsIteratorInfo(Isolate* isolate) {
  Handle<Name> name = isolate->factory()->iterator_symbol();
  return MakeAccessor(isolate, name, &ArgumentsIteratorGetter, nullptr);
}

//
// Accessors::ArrayLength
//

void Accessors::ArrayLengthGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kArrayLengthGetter);
  DisallowGarbageCollection no_gc;
  HandleScope scope(isolate);
  Tagged<JSArray> holder =
      JSArray::cast(*Utils::OpenDirectHandle(*info.Holder()));
  Tagged<Object> result = holder->length();
  info.GetReturnValue().Set(Utils::ToLocal(Handle<Object>(result, isolate)));
}

void Accessors::ArrayLengthSetter(
    v8::Local<v8::Name> name, v8::Local<v8::Value> val,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kArrayLengthSetter);
  HandleScope scope(isolate);

  DCHECK(Object::SameValue(*Utils::OpenDirectHandle(*name),
                           ReadOnlyRoots(isolate).length_string()));

  Handle<JSReceiver> object = Utils::OpenHandle(*info.Holder());
  Handle<JSArray> array = Handle<JSArray>::cast(object);
  Handle<Object> length_obj = Utils::OpenHandle(*val);

  bool was_readonly = JSArray::HasReadOnlyLength(array);

  uint32_t length = 0;
  if (!JSArray::AnythingToArrayLength(isolate, length_obj, &length)) {
    return;
  }

  if (!was_readonly && V8_UNLIKELY(JSArray::HasReadOnlyLength(array))) {
    // AnythingToArrayLength() may have called setter re-entrantly and modified
    // its property descriptor. Don't perform this check if "length" was
    // previously readonly, as this may have been called during
    // DefineOwnPropertyIgnoreAttributes().
    if (length == Object::Number(array->length())) {
      info.GetReturnValue().Set(true);
    } else if (info.ShouldThrowOnError()) {
      Factory* factory = isolate->factory();
      isolate->Throw(*factory->NewTypeError(
          MessageTemplate::kStrictReadOnlyProperty, Utils::OpenHandle(*name),
          i::Object::TypeOf(isolate, object), object));
    } else {
      info.GetReturnValue().Set(false);
    }
    return;
  }

  if (JSArray::SetLength(array, length).IsNothing()) {
    // TODO(victorgomes): AccessorNameBooleanSetterCallback does not handle
    // exceptions.
    FATAL("Fatal JavaScript invalid array length %u", length);
    UNREACHABLE();
  }

  uint32_t actual_new_len = 0;
  CHECK(Object::ToArrayLength(array->length(), &actual_new_len));
  // Fail if there were non-deletable elements.
  if (actual_new_len != length) {
    if (info.ShouldThrowOnError()) {
      Factory* factory = isolate->factory();
      isolate->Throw(*factory->NewTypeError(
          MessageTemplate::kStrictDeleteProperty,
          factory->NewNumberFromUint(actual_new_len - 1), array));
    } else {
      info.GetReturnValue().Set(false);
    }
  } else {
    info.GetReturnValue().Set(true);
  }
}

Handle<AccessorInfo> Accessors::MakeArrayLengthInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->length_string(),
                      &ArrayLengthGetter, &ArrayLengthSetter);
}

//
// Accessors::ModuleNamespaceEntry
//

void Accessors::ModuleNamespaceEntryGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  HandleScope scope(isolate);
  Tagged<JSModuleNamespace> holder =
      JSModuleNamespace::cast(*Utils::OpenDirectHandle(*info.Holder()));
  Handle<Object> result;
  if (holder->GetExport(isolate, Handle<String>::cast(Utils::OpenHandle(*name)))
          .ToHandle(&result)) {
    info.GetReturnValue().Set(Utils::ToLocal(result));
  }
}

void Accessors::ModuleNamespaceEntrySetter(
    v8::Local<v8::Name> name, v8::Local<v8::Value> val,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  HandleScope scope(isolate);
  Factory* factory = isolate->factory();
  Handle<JSModuleNamespace> holder =
      Handle<JSModuleNamespace>::cast(Utils::OpenHandle(*info.Holder()));

  if (info.ShouldThrowOnError()) {
    isolate->Throw(*factory->NewTypeError(
        MessageTemplate::kStrictReadOnlyProperty, Utils::OpenHandle(*name),
        i::Object::TypeOf(isolate, holder), holder));
  } else {
    info.GetReturnValue().Set(false);
  }
}

Handle<AccessorInfo> Accessors::MakeModuleNamespaceEntryInfo(
    Isolate* isolate, Handle<String> name) {
  return MakeAccessor(isolate, name, &ModuleNamespaceEntryGetter,
                      &ModuleNamespaceEntrySetter);
}

//
// Accessors::StringLength
//

void Accessors::StringLengthGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kStringLengthGetter);
  DisallowGarbageCollection no_gc;
  HandleScope scope(isolate);

  // We have a slight impedance mismatch between the external API and the way we
  // use callbacks internally: Externally, callbacks can only be used with
  // v8::Object, but internally we have callbacks on entities which are higher
  // in the hierarchy, in this case for String values.

  Tagged<Object> value =
      *Utils::OpenDirectHandle(*v8::Local<v8::Value>(info.This()));
  if (!IsString(value)) {
    // Not a string value. That means that we either got a String wrapper or
    // a Value with a String wrapper in its prototype chain.
    value = JSPrimitiveWrapper::cast(*Utils::OpenDirectHandle(*info.Holder()))
                ->value();
  }
  Tagged<Object> result = Smi::FromInt(String::cast(value)->length());
  info.GetReturnValue().Set(Utils::ToLocal(Handle<Object>(result, isolate)));
}

Handle<AccessorInfo> Accessors::MakeStringLengthInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->length_string(),
                      &StringLengthGetter, nullptr);
}

//
// Accessors::FunctionPrototype
//

static Handle<Object> GetFunctionPrototype(Isolate* isolate,
                                           Handle<JSFunction> function) {
  if (!function->has_prototype()) {
    // We lazily allocate .prototype for functions, which confuses debug
    // evaluate which assumes we can write to temporary objects we allocated
    // during evaluation. We err on the side of caution here and prevent the
    // newly allocated prototype from going into the temporary objects set,
    // which means writes to it will be considered a side effect.
    DisableTemporaryObjectTracking no_temp_tracking(isolate->debug());
    Handle<JSObject> proto = isolate->factory()->NewFunctionPrototype(function);
    JSFunction::SetPrototype(function, proto);
  }
  return Handle<Object>(function->prototype(), isolate);
}

void Accessors::FunctionPrototypeGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kFunctionPrototypeGetter);
  HandleScope scope(isolate);
  Handle<JSFunction> function =
      Handle<JSFunction>::cast(Utils::OpenHandle(*info.Holder()));
  DCHECK(function->has_prototype_property());
  Handle<Object> result = GetFunctionPrototype(isolate, function);
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

void Accessors::FunctionPrototypeSetter(
    v8::Local<v8::Name> name, v8::Local<v8::Value> val,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kFunctionPrototypeSetter);
  HandleScope scope(isolate);
  Handle<Object> value = Utils::OpenHandle(*val);
  Handle<JSFunction> object =
      Handle<JSFunction>::cast(Utils::OpenHandle(*info.Holder()));
  DCHECK(object->has_prototype_property());
  JSFunction::SetPrototype(object, value);
  info.GetReturnValue().Set(true);
}

Handle<AccessorInfo> Accessors::MakeFunctionPrototypeInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->prototype_string(),
                      &FunctionPrototypeGetter, &FunctionPrototypeSetter);
}

//
// Accessors::FunctionLength
//

void Accessors::FunctionLengthGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kFunctionLengthGetter);
  HandleScope scope(isolate);
  Handle<JSFunction> function =
      Handle<JSFunction>::cast(Utils::OpenHandle(*info.Holder()));
  int length = function->length();
  Handle<Object> result(Smi::FromInt(length), isolate);
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

Handle<AccessorInfo> Accessors::MakeFunctionLengthInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->length_string(),
                      &FunctionLengthGetter, &ReconfigureToDataProperty);
}

//
// Accessors::FunctionName
//

void Accessors::FunctionNameGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  HandleScope scope(isolate);
  Handle<JSFunction> function =
      Handle<JSFunction>::cast(Utils::OpenHandle(*info.Holder()));
  Handle<Object> result = JSFunction::GetName(isolate, function);
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

Handle<AccessorInfo> Accessors::MakeFunctionNameInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->name_string(),
                      &FunctionNameGetter, &ReconfigureToDataProperty);
}

//
// Accessors::FunctionArguments
//

namespace {

Handle<JSObject> ArgumentsFromDeoptInfo(JavaScriptFrame* frame,
                                        int inlined_frame_index) {
  Isolate* isolate = frame->isolate();
  Factory* factory = isolate->factory();

  TranslatedState translated_values(frame);
  translated_values.Prepare(frame->fp());

  int argument_count = 0;
  TranslatedFrame* translated_frame =
      translated_values.GetArgumentsInfoFromJSFrameIndex(inlined_frame_index,
                                                         &argument_count);
  TranslatedFrame::iterator iter = translated_frame->begin();

  // Materialize the function.
  bool should_deoptimize = iter->IsMaterializedObject();
  Handle<JSFunction> function = Handle<JSFunction>::cast(iter->GetValue());
  iter++;

  // Skip the receiver.
  iter++;
  argument_count--;

  Handle<JSObject> arguments =
      factory->NewArgumentsObject(function, argument_count);
  Handle<FixedArray> array = factory->NewFixedArray(argument_count);
  for (int i = 0; i < argument_count; ++i) {
    // If we materialize any object, we should deoptimize the frame because we
    // might alias an object that was eliminated by escape analysis.
    should_deoptimize = should_deoptimize || iter->IsMaterializedObject();
    Handle<Object> value = iter->GetValue();
    array->set(i, *value);
    iter++;
  }
  arguments->set_elements(*array);

  if (should_deoptimize) {
    translated_values.StoreMaterializedValuesAndDeopt(frame);
  }

  // Return the freshly allocated arguments object.
  return arguments;
}

int FindFunctionInFrame(JavaScriptFrame* frame, Handle<JSFunction> function) {
  std::vector<FrameSummary> frames;
  frame->Summarize(&frames);
  for (size_t i = frames.size(); i != 0; i--) {
    if (*frames[i - 1].AsJavaScript().function() == *function) {
      return static_cast<int>(i) - 1;
    }
  }
  return -1;
}

Handle<JSObject> GetFrameArguments(Isolate* isolate,
                                   JavaScriptStackFrameIterator* it,
                                   int function_index) {
  JavaScriptFrame* frame = it->frame();

  if (function_index > 0) {
    // The function in question was inlined.  Inlined functions have the
    // correct number of arguments and no allocated arguments object, so
    // we can construct a fresh one by interpreting the function's
    // deoptimization input data.
    return ArgumentsFromDeoptInfo(frame, function_index);
  }

  // Construct an arguments object mirror for the right frame and the underlying
  // function.
  const int length = frame->GetActualArgumentCount();
  Handle<JSFunction> function(frame->function(), isolate);
  Handle<JSObject> arguments =
      isolate->factory()->NewArgumentsObject(function, length);
  Handle<FixedArray> array = isolate->factory()->NewFixedArray(length);

  // Copy the parameters to the arguments object.
  DCHECK(array->length() == length);
  for (int i = 0; i < length; i++) {
    Tagged<Object> value = frame->GetParameter(i);
    if (IsTheHole(value, isolate)) {
      // Generators currently use holes as dummy arguments when resuming.  We
      // must not leak those.
      DCHECK(IsResumableFunction(function->shared()->kind()));
      value = ReadOnlyRoots(isolate).undefined_value();
    }
    array->set(i, value);
  }
  arguments->set_elements(*array);

  // For optimized functions, the frame arguments may be outdated, so we should
  // update them with the deopt info, while keeping the length and extra
  // arguments from the actual frame.
  if (CodeKindCanDeoptimize(frame->LookupCode()->kind()) && length > 0) {
    Handle<JSObject> arguments_from_deopt_info =
        ArgumentsFromDeoptInfo(frame, function_index);
    Handle<FixedArray> elements_from_deopt_info(
        FixedArray::cast(arguments_from_deopt_info->elements()), isolate);
    int common_length = std::min(length, elements_from_deopt_info->length());
    for (int i = 0; i < common_length; i++) {
      array->set(i, elements_from_deopt_info->get(i));
    }
  }

  // Return the freshly allocated arguments object.
  return arguments;
}

}  // namespace

Handle<JSObject> Accessors::FunctionGetArguments(JavaScriptFrame* frame,
                                                 int inlined_jsframe_index) {
  Isolate* isolate = frame->isolate();
  Address requested_frame_fp = frame->fp();
  // Forward a frame iterator to the requested frame. This is needed because we
  // potentially need for advance it to the inlined arguments frame later.
  for (JavaScriptStackFrameIterator it(isolate); !it.done(); it.Advance()) {
    if (it.frame()->fp() != requested_frame_fp) continue;
    return GetFrameArguments(isolate, &it, inlined_jsframe_index);
  }
  UNREACHABLE();  // Requested frame not found.
}

void Accessors::FunctionArgumentsGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  isolate->CountUsage(v8::Isolate::kFunctionPrototypeArguments);
  HandleScope scope(isolate);
  Handle<JSFunction> function =
      Handle<JSFunction>::cast(Utils::OpenHandle(*info.Holder()));
  Handle<Object> result = isolate->factory()->null_value();
  if (!function->shared()->native()) {
    // Find the top invocation of the function by traversing frames.
    for (JavaScriptStackFrameIterator it(isolate); !it.done(); it.Advance()) {
      JavaScriptFrame* frame = it.frame();
      int function_index = FindFunctionInFrame(frame, function);
      if (function_index >= 0) {
        result = GetFrameArguments(isolate, &it, function_index);
        break;
      }
    }
  }
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

Handle<AccessorInfo> Accessors::MakeFunctionArgumentsInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->arguments_string(),
                      &FunctionArgumentsGetter, nullptr);
}

//
// Accessors::FunctionCaller
//

static inline bool AllowAccessToFunction(Tagged<Context> current_context,
                                         Tagged<JSFunction> function) {
  return current_context->HasSameSecurityTokenAs(function->context());
}

class FrameFunctionIterator {
 public:
  explicit FrameFunctionIterator(Isolate* isolate)
      : isolate_(isolate), frame_iterator_(isolate), inlined_frame_index_(-1) {
    GetFrames();
  }

  // Iterate through functions until the first occurrence of 'function'.
  // Returns true if one is found, and false if the iterator ends before.
  bool Find(Handle<JSFunction> function) {
    do {
      if (!next().ToHandle(&function_)) return false;
    } while (!function_.is_identical_to(function));
    return true;
  }

  // Iterate through functions until the next non-toplevel one is found.
  // Returns true if one is found, and false if the iterator ends before.
  bool FindNextNonTopLevel() {
    do {
      if (!next().ToHandle(&function_)) return false;
    } while (function_->shared()->is_toplevel());
    return true;
  }

  // Iterate through function until the first native or user-provided function
  // is found. Functions not defined in user-provided scripts are not visible
  // unless directly exposed, in which case the native flag is set on them.
  // Returns true if one is found, and false if the iterator ends before.
  bool FindFirstNativeOrUserJavaScript() {
    while (!function_->shared()->native() &&
           !function_->shared()->IsUserJavaScript()) {
      if (!next().ToHandle(&function_)) return false;
    }
    return true;
  }

  // In case of inlined frames the function could have been materialized from
  // deoptimization information. If that is the case we need to make sure that
  // subsequent call will see the same function, since we are about to hand out
  // the value to JavaScript. Make sure to store the materialized value and
  // trigger a deoptimization of the underlying frame.
  Handle<JSFunction> MaterializeFunction() {
    if (inlined_frame_index_ == 0) return function_;

    JavaScriptFrame* frame = frame_iterator_.frame();
    TranslatedState translated_values(frame);
    translated_values.Prepare(frame->fp());

    TranslatedFrame* translated_frame =
        translated_values.GetFrameFromJSFrameIndex(inlined_frame_index_);
    TranslatedFrame::iterator iter = translated_frame->begin();

    // First value is the function.
    bool should_deoptimize = iter->IsMaterializedObject();
    Handle<Object> value = iter->GetValue();
    if (should_deoptimize) {
      translated_values.StoreMaterializedValuesAndDeopt(frame);
    }

    return Handle<JSFunction>::cast(value);
  }

 private:
  MaybeHandle<JSFunction> next() {
    while (true) {
      if (inlined_frame_index_ <= 0) {
        if (!frame_iterator_.done()) {
          frame_iterator_.Advance();
          frames_.clear();
          inlined_frame_index_ = -1;
          GetFrames();
        }
        if (inlined_frame_index_ == -1) return MaybeHandle<JSFunction>();
      }

      --inlined_frame_index_;
      Handle<JSFunction> next_function =
          frames_[inlined_frame_index_].AsJavaScript().function();
      // Skip functions from other origins.
      if (!AllowAccessToFunction(isolate_->context(), *next_function)) continue;
      return next_function;
    }
  }
  void GetFrames() {
    DCHECK_EQ(-1, inlined_frame_index_);
    if (frame_iterator_.done()) return;
    JavaScriptFrame* frame = frame_iterator_.frame();
    frame->Summarize(&frames_);
    inlined_frame_index_ = static_cast<int>(frames_.size());
    DCHECK_LT(0, inlined_frame_index_);
  }
  Isolate* isolate_;
  Handle<JSFunction> function_;
  JavaScriptStackFrameIterator frame_iterator_;
  std::vector<FrameSummary> frames_;
  int inlined_frame_index_;
};

MaybeHandle<JSFunction> FindCaller(Isolate* isolate,
                                   Handle<JSFunction> function) {
  FrameFunctionIterator it(isolate);
  if (function->shared()->native()) {
    return MaybeHandle<JSFunction>();
  }
  // Find the function from the frames. Return null in case no frame
  // corresponding to the given function was found.
  if (!it.Find(function)) {
    return MaybeHandle<JSFunction>();
  }
  // Find previously called non-toplevel function.
  if (!it.FindNextNonTopLevel()) {
    return MaybeHandle<JSFunction>();
  }
  // Find the first user-land JavaScript function (or the entry point into
  // native JavaScript builtins in case such a builtin was the caller).
  if (!it.FindFirstNativeOrUserJavaScript()) {
    return MaybeHandle<JSFunction>();
  }

  // Materialize the function that the iterator is currently sitting on. Note
  // that this might trigger deoptimization in case the function was actually
  // materialized. Identity of the function must be preserved because we are
  // going to return it to JavaScript after this point.
  Handle<JSFunction> caller = it.MaterializeFunction();

  // Censor if the caller is not a sloppy mode function.
  // Change from ES5, which used to throw, see:
  // https://bugs.ecmascript.org/show_bug.cgi?id=310
  if (is_strict(caller->shared()->language_mode())) {
    return MaybeHandle<JSFunction>();
  }
  // Don't return caller from another security context.
  if (!AllowAccessToFunction(isolate->context(), *caller)) {
    return MaybeHandle<JSFunction>();
  }
  return caller;
}

void Accessors::FunctionCallerGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  isolate->CountUsage(v8::Isolate::kFunctionPrototypeCaller);
  HandleScope scope(isolate);
  Handle<JSFunction> function =
      Handle<JSFunction>::cast(Utils::OpenHandle(*info.Holder()));
  Handle<Object> result;
  MaybeHandle<JSFunction> maybe_caller;
  maybe_caller = FindCaller(isolate, function);
  Handle<JSFunction> caller;
  // We don't support caller access with correctness fuzzing.
  if (!v8_flags.correctness_fuzzer_suppressions &&
      maybe_caller.ToHandle(&caller)) {
    result = caller;
  } else {
    result = isolate->factory()->null_value();
  }
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

Handle<AccessorInfo> Accessors::MakeFunctionCallerInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->caller_string(),
                      &FunctionCallerGetter, nullptr);
}

//
// Accessors::BoundFunctionLength
//

void Accessors::BoundFunctionLengthGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kBoundFunctionLengthGetter);
  HandleScope scope(isolate);
  Handle<JSBoundFunction> function =
      Handle<JSBoundFunction>::cast(Utils::OpenHandle(*info.Holder()));

  int length = 0;
  if (!JSBoundFunction::GetLength(isolate, function).To(&length)) {
    return;
  }
  Handle<Object> result(Smi::FromInt(length), isolate);
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

Handle<AccessorInfo> Accessors::MakeBoundFunctionLengthInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->length_string(),
                      &BoundFunctionLengthGetter, &ReconfigureToDataProperty);
}

//
// Accessors::BoundFunctionName
//

void Accessors::BoundFunctionNameGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kBoundFunctionNameGetter);
  HandleScope scope(isolate);
  Handle<JSBoundFunction> function =
      Handle<JSBoundFunction>::cast(Utils::OpenHandle(*info.Holder()));
  Handle<Object> result;
  if (!JSBoundFunction::GetName(isolate, function).ToHandle(&result)) {
    return;
  }
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

Handle<AccessorInfo> Accessors::MakeBoundFunctionNameInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->name_string(),
                      &BoundFunctionNameGetter, &ReconfigureToDataProperty);
}

//
// Accessors::WrappedFunctionLength
//

void Accessors::WrappedFunctionLengthGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kBoundFunctionLengthGetter);
  HandleScope scope(isolate);
  Handle<JSWrappedFunction> function =
      Handle<JSWrappedFunction>::cast(Utils::OpenHandle(*info.Holder()));

  int length = 0;
  if (!JSWrappedFunction::GetLength(isolate, function).To(&length)) {
    return;
  }
  Handle<Object> result(Smi::FromInt(length), isolate);
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

Handle<AccessorInfo> Accessors::MakeWrappedFunctionLengthInfo(
    Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->length_string(),
                      &WrappedFunctionLengthGetter, &ReconfigureToDataProperty);
}

//
// Accessors::ValueUnavailable
//

void Accessors::ValueUnavailableGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  Isolate* isolate = reinterpret_cast<Isolate*>(info.GetIsolate());
  HandleScope scope(isolate);
  isolate->Throw(*isolate->factory()->NewReferenceError(
      MessageTemplate::kAccessedUnavailableVariable, Utils::OpenHandle(*name)));
}

Handle<AccessorInfo> Accessors::MakeValueUnavailableInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->empty_string(),
                      &ValueUnavailableGetter, &ReconfigureToDataProperty);
}

//
// Accessors::WrappedFunctionName
//

void Accessors::WrappedFunctionNameGetter(
    v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kWrappedFunctionNameGetter);
  HandleScope scope(isolate);
  Handle<JSWrappedFunction> function =
      Handle<JSWrappedFunction>::cast(Utils::OpenHandle(*info.Holder()));
  Handle<Object> result;
  if (!JSWrappedFunction::GetName(isolate, function).ToHandle(&result)) {
    return;
  }
  info.GetReturnValue().Set(Utils::ToLocal(result));
}

Handle<AccessorInfo> Accessors::MakeWrappedFunctionNameInfo(Isolate* isolate) {
  return MakeAccessor(isolate, isolate->factory()->name_string(),
                      &WrappedFunctionNameGetter, &ReconfigureToDataProperty);
}

//
// Accessors::ErrorStack
//

void Accessors::ErrorStackGetter(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  Isolate* isolate = reinterpret_cast<Isolate*>(info.GetIsolate());
  HandleScope scope(isolate);
  Handle<Object> formatted_stack = isolate->factory()->undefined_value();
  Handle<JSReceiver> maybe_error_object = Utils::OpenHandle(*info.This());
  if (IsJSObject(*maybe_error_object)) {
    if (!ErrorUtils::GetFormattedStack(
             isolate, Handle<JSObject>::cast(maybe_error_object))
             .ToHandle(&formatted_stack)) {
      return;
    }
  }
  v8::Local<v8::Value> result = Utils::ToLocal(formatted_stack);
  CHECK(result->IsValue());
  info.GetReturnValue().Set(result);
}

void Accessors::ErrorStackSetter(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  Isolate* isolate = reinterpret_cast<Isolate*>(info.GetIsolate());
  HandleScope scope(isolate);
  Handle<JSReceiver> maybe_error_object = Utils::OpenHandle(*info.This());
  if (IsJSObject(*maybe_error_object)) {
    v8::Local<v8::Value> value = info[0];
    ErrorUtils::SetFormattedStack(isolate,
                                  Handle<JSObject>::cast(maybe_error_object),
                                  Utils::OpenHandle(*value));
  }
}

}  // namespace internal
}  // namespace v8
