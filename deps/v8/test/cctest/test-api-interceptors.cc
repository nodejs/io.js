// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <optional>

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/codegen/compilation-cache.h"
#include "src/execution/execution.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/runtime/runtime.h"
#include "src/strings/unicode-inl.h"
#include "test/cctest/heap/heap-utils.h"
#include "test/cctest/test-api.h"

using ::v8::Context;
using ::v8::Function;
using ::v8::FunctionTemplate;
using ::v8::Local;
using ::v8::Name;
using ::v8::Object;
using ::v8::ObjectTemplate;
using ::v8::Script;
using ::v8::String;
using ::v8::Symbol;
using ::v8::Value;

namespace {

void Returns42(const v8::FunctionCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(42);
}

void Return239Callback(Local<Name> name,
                       const v8::PropertyCallbackInfo<Value>& info) {
  ApiTestFuzzer::Fuzz();
  CheckReturnValue(info, FUNCTION_ADDR(Return239Callback));
  info.GetReturnValue().Set(v8_str("bad value"));
  info.GetReturnValue().Set(v8_num(239));
}

v8::Intercepted EmptyInterceptorGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  return v8::Intercepted::kNo;
}

v8::Intercepted EmptyInterceptorSetter(
    Local<Name> name, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  return v8::Intercepted::kNo;
}

v8::Intercepted EmptyInterceptorQuery(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  return v8::Intercepted::kNo;
}

v8::Intercepted EmptyInterceptorDeleter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  return v8::Intercepted::kNo;
}

void EmptyInterceptorEnumerator(
    const v8::PropertyCallbackInfo<v8::Array>& info) {}

v8::Intercepted EmptyInterceptorDefinerWithSideEffect(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  ApiTestFuzzer::Fuzz();
  v8::Local<v8::Value> result = CompileRun("interceptor_definer_side_effect()");
  if (!result->IsNull()) {
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

void SimpleGetterImpl(Local<String> name_str,
                      const v8::FunctionCallbackInfo<v8::Value>& info) {
  Local<Object> self = info.This();
  info.GetReturnValue().Set(
      self->Get(
              info.GetIsolate()->GetCurrentContext(),
              String::Concat(info.GetIsolate(), v8_str("accessor_"), name_str))
          .ToLocalChecked());
}

void SimpleSetterImpl(Local<String> name_str,
                      const v8::FunctionCallbackInfo<v8::Value>& info) {
  Local<Object> self = info.This();
  Local<Value> value = info[0];
  self->Set(info.GetIsolate()->GetCurrentContext(),
            String::Concat(info.GetIsolate(), v8_str("accessor_"), name_str),
            value)
      .FromJust();
}

void SimpleGetterCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Local<String> name_str = args.Data().As<String>();
  SimpleGetterImpl(name_str, args);
}

void SimpleSetterCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  Local<String> name_str = info.Data().As<String>();
  SimpleSetterImpl(name_str, info);
}

void SymbolGetterCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  Local<Name> name = info.Data().As<Name>();
  CHECK(name->IsSymbol());
  v8::Isolate* isolate = info.GetIsolate();
  Local<Symbol> sym = name.As<Symbol>();
  if (sym->Description(isolate)->IsUndefined()) return;
  SimpleGetterImpl(sym->Description(isolate).As<String>(), info);
}

void SymbolSetterCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  Local<Name> name = info.Data().As<Name>();
  CHECK(name->IsSymbol());
  v8::Isolate* isolate = info.GetIsolate();
  Local<Symbol> sym = name.As<Symbol>();
  if (sym->Description(isolate)->IsUndefined()) return;
  SimpleSetterImpl(sym->Description(isolate).As<String>(), info);
}

v8::Intercepted InterceptorGetter(
    Local<Name> generic_name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (generic_name->IsSymbol()) return v8::Intercepted::kNo;
  Local<String> name = generic_name.As<String>();
  String::Utf8Value utf8(info.GetIsolate(), name);
  char* name_str = *utf8;
  char prefix[] = "interceptor_";
  int i;
  for (i = 0; name_str[i] && prefix[i]; ++i) {
    if (name_str[i] != prefix[i]) return v8::Intercepted::kNo;
  }
  Local<Object> self = info.This().As<Object>();
  info.GetReturnValue().Set(
      self->GetPrivate(
              info.GetIsolate()->GetCurrentContext(),
              v8::Private::ForApi(info.GetIsolate(), v8_str(name_str + i)))
          .ToLocalChecked());
  return v8::Intercepted::kYes;
}

v8::Intercepted InterceptorSetter(Local<Name> generic_name, Local<Value> value,
                                  const v8::PropertyCallbackInfo<void>& info) {
  if (generic_name->IsSymbol()) return v8::Intercepted::kNo;
  Local<String> name = generic_name.As<String>();
  // Intercept accesses that set certain integer values, for which the name does
  // not start with 'accessor_'.
  String::Utf8Value utf8(info.GetIsolate(), name);
  char* name_str = *utf8;
  char prefix[] = "accessor_";
  int i;
  for (i = 0; name_str[i] && prefix[i]; ++i) {
    if (name_str[i] != prefix[i]) break;
  }
  if (!prefix[i]) return v8::Intercepted::kNo;

  Local<Context> context = info.GetIsolate()->GetCurrentContext();
  if (value->IsInt32() && value->Int32Value(context).FromJust() < 10000) {
    Local<Object> self = info.This().As<Object>();
    Local<v8::Private> symbol = v8::Private::ForApi(info.GetIsolate(), name);
    self->SetPrivate(context, symbol, value).FromJust();
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

v8::Intercepted GenericInterceptorGetter(
    Local<Name> generic_name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  Local<String> str;
  if (generic_name->IsSymbol()) {
    Local<Value> name = generic_name.As<Symbol>()->Description(isolate);
    if (name->IsUndefined()) return v8::Intercepted::kNo;
    str = String::Concat(info.GetIsolate(), v8_str("_sym_"), name.As<String>());
  } else {
    Local<String> name = generic_name.As<String>();
    String::Utf8Value utf8(info.GetIsolate(), name);
    char* name_str = *utf8;
    if (*name_str == '_') return v8::Intercepted::kNo;
    str = String::Concat(info.GetIsolate(), v8_str("_str_"), name);
  }

  Local<Object> self = info.This().As<Object>();
  info.GetReturnValue().Set(
      self->Get(info.GetIsolate()->GetCurrentContext(), str).ToLocalChecked());
  return v8::Intercepted::kYes;
}

v8::Intercepted GenericInterceptorSetter(
    Local<Name> generic_name, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  Local<String> str;
  if (generic_name->IsSymbol()) {
    Local<Value> name = generic_name.As<Symbol>()->Description(isolate);
    if (name->IsUndefined()) return v8::Intercepted::kNo;
    str = String::Concat(info.GetIsolate(), v8_str("_sym_"), name.As<String>());
  } else {
    Local<String> name = generic_name.As<String>();
    String::Utf8Value utf8(info.GetIsolate(), name);
    char* name_str = *utf8;
    if (*name_str == '_') return v8::Intercepted::kNo;
    str = String::Concat(info.GetIsolate(), v8_str("_str_"), name);
  }

  Local<Object> self = info.This().As<Object>();
  self->Set(info.GetIsolate()->GetCurrentContext(), str, value).FromJust();
  return v8::Intercepted::kYes;
}

void AddAccessor(v8::Isolate* isolate, Local<FunctionTemplate> templ,
                 Local<Name> name, v8::FunctionCallback getter,
                 v8::FunctionCallback setter) {
  Local<FunctionTemplate> getter_templ =
      FunctionTemplate::New(isolate, getter, name);
  Local<FunctionTemplate> setter_templ =
      FunctionTemplate::New(isolate, setter, name);

  templ->PrototypeTemplate()->SetAccessorProperty(name, getter_templ,
                                                  setter_templ);
}

void AddStringOnlyInterceptor(Local<FunctionTemplate> templ,
                              v8::NamedPropertyGetterCallback getter,
                              v8::NamedPropertySetterCallback setter) {
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      getter, setter, nullptr, nullptr, nullptr, Local<v8::Value>(),
      v8::PropertyHandlerFlags::kOnlyInterceptStrings));
}

void AddInterceptor(Local<FunctionTemplate> templ,
                    v8::NamedPropertyGetterCallback getter,
                    v8::NamedPropertySetterCallback setter) {
  templ->InstanceTemplate()->SetHandler(
      v8::NamedPropertyHandlerConfiguration(getter, setter));
}

v8::Global<v8::Object> bottom_global;

v8::Intercepted CheckThisIndexedPropertyHandler(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisIndexedPropertyHandler));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisNamedPropertyHandler(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisNamedPropertyHandler));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisIndexedPropertyDefiner(
    uint32_t index, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisIndexedPropertyDefiner));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisNamedPropertyDefiner(
    Local<Name> property, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisNamedPropertyDefiner));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisIndexedPropertySetter(
    uint32_t index, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisIndexedPropertySetter));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisNamedPropertySetter(
    Local<Name> property, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisNamedPropertySetter));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisIndexedPropertyDescriptor(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisIndexedPropertyDescriptor));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisNamedPropertyDescriptor(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisNamedPropertyDescriptor));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisIndexedPropertyQuery(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisIndexedPropertyQuery));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisNamedPropertyQuery(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisNamedPropertyQuery));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisIndexedPropertyDeleter(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisIndexedPropertyDeleter));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

v8::Intercepted CheckThisNamedPropertyDeleter(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisNamedPropertyDeleter));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
  return v8::Intercepted::kNo;
}

void CheckThisIndexedPropertyEnumerator(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisIndexedPropertyEnumerator));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
}


void CheckThisNamedPropertyEnumerator(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  CheckReturnValue(info, FUNCTION_ADDR(CheckThisNamedPropertyEnumerator));
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  CHECK(info.This()
            ->Equals(isolate->GetCurrentContext(), bottom_global.Get(isolate))
            .FromJust());
}


int echo_named_call_count;

v8::Intercepted EchoNamedProperty(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(v8_str("data")
            ->Equals(info.GetIsolate()->GetCurrentContext(), info.Data())
            .FromJust());
  echo_named_call_count++;
  info.GetReturnValue().Set(name);
  return v8::Intercepted::kYes;
}

v8::Intercepted InterceptorHasOwnPropertyGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  return v8::Intercepted::kNo;
}

v8::Intercepted InterceptorHasOwnPropertyGetterGC(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  i::heap::InvokeMajorGC(CcTest::heap());
  return v8::Intercepted::kNo;
}

int query_counter_int = 0;

v8::Intercepted QueryCallback(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  query_counter_int++;
  return v8::Intercepted::kNo;
}

}  // namespace

// Examples that show when the query callback is triggered.
THREADED_TEST(QueryInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
  templ->InstanceTemplate()->SetHandler(
      v8::NamedPropertyHandlerConfiguration(nullptr, nullptr, QueryCallback));
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  CHECK_EQ(0, query_counter_int);
  v8::Local<Value> result =
      v8_compile("Object.getOwnPropertyDescriptor(obj, 'x');")
          ->Run(env.local())
          .ToLocalChecked();
  CHECK_EQ(1, query_counter_int);
  CHECK_EQ(v8::PropertyAttribute::None,
           static_cast<v8::PropertyAttribute>(
               result->Int32Value(env.local()).FromJust()));

  v8_compile("Object.defineProperty(obj, 'not_enum', {value: 17});")
      ->Run(env.local())
      .ToLocalChecked();
  CHECK_EQ(2, query_counter_int);

  v8_compile(
      "Object.defineProperty(obj, 'enum', {value: 17, enumerable: true, "
      "writable: true});")
      ->Run(env.local())
      .ToLocalChecked();
  CHECK_EQ(3, query_counter_int);

  CHECK(v8_compile("obj.propertyIsEnumerable('enum');")
            ->Run(env.local())
            .ToLocalChecked()
            ->BooleanValue(isolate));
  CHECK_EQ(4, query_counter_int);

  CHECK(!v8_compile("obj.propertyIsEnumerable('not_enum');")
             ->Run(env.local())
             .ToLocalChecked()
             ->BooleanValue(isolate));
  CHECK_EQ(5, query_counter_int);

  CHECK(v8_compile("obj.hasOwnProperty('enum');")
            ->Run(env.local())
            .ToLocalChecked()
            ->BooleanValue(isolate));
  CHECK_EQ(5, query_counter_int);

  CHECK(v8_compile("obj.hasOwnProperty('not_enum');")
            ->Run(env.local())
            .ToLocalChecked()
            ->BooleanValue(isolate));
  CHECK_EQ(5, query_counter_int);

  CHECK(!v8_compile("obj.hasOwnProperty('x');")
             ->Run(env.local())
             .ToLocalChecked()
             ->BooleanValue(isolate));
  CHECK_EQ(6, query_counter_int);

  CHECK(!v8_compile("obj.propertyIsEnumerable('undef');")
             ->Run(env.local())
             .ToLocalChecked()
             ->BooleanValue(isolate));
  CHECK_EQ(7, query_counter_int);

  v8_compile("Object.defineProperty(obj, 'enum', {value: 42});")
      ->Run(env.local())
      .ToLocalChecked();
  CHECK_EQ(8, query_counter_int);

  v8_compile("Object.isFrozen('obj.x');")->Run(env.local()).ToLocalChecked();
  CHECK_EQ(8, query_counter_int);

  v8_compile("'x' in obj;")->Run(env.local()).ToLocalChecked();
  CHECK_EQ(9, query_counter_int);
}

namespace {

bool get_was_called = false;
bool set_was_called = false;

int set_was_called_counter = 0;

v8::Intercepted GetterCallback(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
  get_was_called = true;
  return v8::Intercepted::kNo;
}

v8::Intercepted SetterCallback(Local<Name> property, Local<Value> value,
                               const v8::PropertyCallbackInfo<void>& info) {
  set_was_called = true;
  set_was_called_counter++;
  return v8::Intercepted::kNo;
}

v8::Intercepted InterceptingSetterCallback(
    Local<Name> property, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  return v8::Intercepted::kYes;
}

}  // namespace

// Check that get callback is called in defineProperty with accessor descriptor.
THREADED_TEST(DefinerCallbackAccessorInterceptor) {
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());
  templ->InstanceTemplate()->SetHandler(
      v8::NamedPropertyHandlerConfiguration(GetterCallback, SetterCallback));
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();

  get_was_called = false;
  set_was_called = false;

  v8_compile("Object.defineProperty(obj, 'x', {set: function() {return 17;}});")
      ->Run(env.local())
      .ToLocalChecked();
  CHECK(get_was_called);
  CHECK(!set_was_called);
}

// Check that set callback is called for function declarations.
THREADED_TEST(SetterCallbackFunctionDeclarationInterceptor) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());

  v8::Local<ObjectTemplate> object_template = templ->InstanceTemplate();
  object_template->SetHandler(
      v8::NamedPropertyHandlerConfiguration(nullptr, SetterCallback));
  v8::Local<v8::Context> ctx =
      v8::Context::New(CcTest::isolate(), nullptr, object_template);

  set_was_called_counter = 0;

  // Declare function.
  v8::Local<v8::String> code = v8_str("function x() {return 42;}; x();");
  CHECK_EQ(42, v8::Script::Compile(ctx, code)
                   .ToLocalChecked()
                   ->Run(ctx)
                   .ToLocalChecked()
                   ->Int32Value(ctx)
                   .FromJust());
  CHECK_EQ(1, set_was_called_counter);

  // Redeclare function.
  code = v8_str("function x() {return 43;}; x();");
  CHECK_EQ(43, v8::Script::Compile(ctx, code)
                   .ToLocalChecked()
                   ->Run(ctx)
                   .ToLocalChecked()
                   ->Int32Value(ctx)
                   .FromJust());
  CHECK_EQ(2, set_was_called_counter);

  // Redefine function.
  code = v8_str("x = function() {return 44;}; x();");
  CHECK_EQ(44, v8::Script::Compile(ctx, code)
                   .ToLocalChecked()
                   ->Run(ctx)
                   .ToLocalChecked()
                   ->Int32Value(ctx)
                   .FromJust());
  CHECK_EQ(3, set_was_called_counter);
}

namespace {
int descriptor_was_called;

v8::Intercepted PropertyDescriptorCallback(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  // Intercept the callback and create some descriptor.
  descriptor_was_called++;
  const char* code =
      "var desc = {value: 5};"
      "desc;";
  Local<Value> descriptor = v8_compile(code)
                                ->Run(info.GetIsolate()->GetCurrentContext())
                                .ToLocalChecked();
  info.GetReturnValue().Set(descriptor);
  return v8::Intercepted::kYes;
}
}  // namespace

// Check that the descriptor callback is called on the global object.
THREADED_TEST(DescriptorCallbackOnGlobalObject) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());

  v8::Local<ObjectTemplate> object_template = templ->InstanceTemplate();
  object_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, nullptr, PropertyDescriptorCallback, nullptr, nullptr, nullptr));
  v8::Local<v8::Context> ctx =
      v8::Context::New(CcTest::isolate(), nullptr, object_template);

  descriptor_was_called = 0;

  // Declare function.
  v8::Local<v8::String> code = v8_str(
      "var x = 42; var desc = Object.getOwnPropertyDescriptor(this, 'x'); "
      "desc.value;");
  CHECK_EQ(5, v8::Script::Compile(ctx, code)
                  .ToLocalChecked()
                  ->Run(ctx)
                  .ToLocalChecked()
                  ->Int32Value(ctx)
                  .FromJust());
  CHECK_EQ(1, descriptor_was_called);
}

namespace {
v8::Intercepted QueryCallbackSetDontDelete(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  info.GetReturnValue().Set(v8::DontDelete);
  return v8::Intercepted::kYes;
}

}  // namespace

// Regression for a Node.js test that fails in debug mode.
THREADED_TEST(InterceptorFunctionRedeclareWithQueryCallback) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());

  v8::Local<ObjectTemplate> object_template = templ->InstanceTemplate();
  object_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, nullptr, QueryCallbackSetDontDelete));
  v8::Local<v8::Context> ctx =
      v8::Context::New(CcTest::isolate(), nullptr, object_template);

  // Declare and redeclare function.
  v8::Local<v8::String> code = v8_str(
      "function x() {return 42;};"
      "function x() {return 43;};");
  v8::Script::Compile(ctx, code).ToLocalChecked()->Run(ctx).ToLocalChecked();
}

// Regression test for chromium bug 656648.
// Do not crash on non-masking, intercepting setter callbacks.
THREADED_TEST(NonMaskingInterceptor) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());

  v8::Local<ObjectTemplate> object_template = templ->InstanceTemplate();
  object_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, InterceptingSetterCallback, nullptr, nullptr, nullptr,
      Local<Value>(), v8::PropertyHandlerFlags::kNonMasking));
  v8::Local<v8::Context> ctx =
      v8::Context::New(CcTest::isolate(), nullptr, object_template);

  v8::Local<v8::String> code = v8_str("function x() {return 43;};");
  v8::Script::Compile(ctx, code).ToLocalChecked()->Run(ctx).ToLocalChecked();
}

// Check that function re-declarations throw if they are read-only.
THREADED_TEST(SetterCallbackFunctionDeclarationInterceptorThrow) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());

  v8::Local<ObjectTemplate> object_template = templ->InstanceTemplate();
  object_template->SetHandler(
      v8::NamedPropertyHandlerConfiguration(nullptr, SetterCallback));
  v8::Local<v8::Context> ctx =
      v8::Context::New(CcTest::isolate(), nullptr, object_template);

  set_was_called = false;

  v8::Local<v8::String> code = v8_str(
      "function x() {return 42;};"
      "Object.defineProperty(this, 'x', {"
      "configurable: false, "
      "writable: false});"
      "x();");
  CHECK_EQ(42, v8::Script::Compile(ctx, code)
                   .ToLocalChecked()
                   ->Run(ctx)
                   .ToLocalChecked()
                   ->Int32Value(ctx)
                   .FromJust());

  CHECK(set_was_called);

  v8::TryCatch try_catch(CcTest::isolate());
  set_was_called = false;

  // Redeclare function that is read-only.
  code = v8_str("function x() {return 43;};");
  CHECK(v8::Script::Compile(ctx, code).ToLocalChecked()->Run(ctx).IsEmpty());
  CHECK(try_catch.HasCaught());

  CHECK(!set_was_called);
}


namespace {

bool get_was_called_in_order = false;
bool define_was_called_in_order = false;

v8::Intercepted GetterCallbackOrder(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
  get_was_called_in_order = true;
  CHECK(!define_was_called_in_order);
  info.GetReturnValue().Set(property);
  return v8::Intercepted::kYes;
}

v8::Intercepted DefinerCallbackOrder(
    Local<Name> property, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  // Get called before DefineProperty because we query the descriptor first.
  CHECK(get_was_called_in_order);
  define_was_called_in_order = true;
  return v8::Intercepted::kNo;
}

}  // namespace

// Check that getter callback is called before definer callback.
THREADED_TEST(DefinerCallbackGetAndDefine) {
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      GetterCallbackOrder, SetterCallback, nullptr, nullptr, nullptr,
      DefinerCallbackOrder));
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();

  CHECK(!get_was_called_in_order);
  CHECK(!define_was_called_in_order);

  v8_compile("Object.defineProperty(obj, 'x', {set: function() {return 17;}});")
      ->Run(env.local())
      .ToLocalChecked();
  CHECK(get_was_called_in_order);
  CHECK(define_was_called_in_order);
}

namespace {  //  namespace for InObjectLiteralDefinitionWithInterceptor

// Workaround for no-snapshot builds: only intercept once Context::New() is
// done, otherwise we'll intercept
// bootstrapping like defining array on the global object.
bool context_is_done = false;
bool getter_callback_was_called = false;

v8::Intercepted ReturnUndefinedGetterCallback(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (context_is_done) {
    getter_callback_was_called = true;
    info.GetReturnValue().SetUndefined();
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

}  // namespace

// Check that an interceptor is not invoked during ES6 style definitions inside
// an object literal.
THREADED_TEST(InObjectLiteralDefinitionWithInterceptor) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;

  // Set up a context in which all global object definitions are intercepted.
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());
  v8::Local<ObjectTemplate> object_template = templ->InstanceTemplate();
  object_template->SetHandler(
      v8::NamedPropertyHandlerConfiguration(ReturnUndefinedGetterCallback));
  v8::Local<v8::Context> ctx =
      v8::Context::New(CcTest::isolate(), nullptr, object_template);

  context_is_done = true;

  // The interceptor returns undefined for any global object,
  // so setting a property on an object should throw.
  v8::Local<v8::String> code = v8_str("var o = {}; o.x = 5");
  {
    getter_callback_was_called = false;
    v8::TryCatch try_catch(CcTest::isolate());
    CHECK(v8::Script::Compile(ctx, code).ToLocalChecked()->Run(ctx).IsEmpty());
    CHECK(try_catch.HasCaught());
    CHECK(getter_callback_was_called);
  }

  // Defining a property in the object literal should not throw
  // because the interceptor is not invoked.
  {
    getter_callback_was_called = false;
    v8::TryCatch try_catch(CcTest::isolate());
    code = v8_str("var l = {x: 5};");
    CHECK(v8::Script::Compile(ctx, code)
              .ToLocalChecked()
              ->Run(ctx)
              .ToLocalChecked()
              ->IsUndefined());
    CHECK(!try_catch.HasCaught());
    CHECK(!getter_callback_was_called);
  }
}

THREADED_TEST(InterceptorHasOwnProperty) {
  LocalContext context;
  v8::Isolate* isolate = context->GetIsolate();
  v8::HandleScope scope(isolate);
  Local<v8::FunctionTemplate> fun_templ = v8::FunctionTemplate::New(isolate);
  Local<v8::ObjectTemplate> instance_templ = fun_templ->InstanceTemplate();
  instance_templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorHasOwnPropertyGetter));
  Local<Function> function =
      fun_templ->GetFunction(context.local()).ToLocalChecked();
  context->Global()
      ->Set(context.local(), v8_str("constructor"), function)
      .FromJust();
  v8::Local<Value> value = CompileRun(
      "var o = new constructor();"
      "o.hasOwnProperty('ostehaps');");
  CHECK(!value->BooleanValue(isolate));
  value = CompileRun(
      "o.ostehaps = 42;"
      "o.hasOwnProperty('ostehaps');");
  CHECK(value->BooleanValue(isolate));
  value = CompileRun(
      "var p = new constructor();"
      "p.hasOwnProperty('ostehaps');");
  CHECK(!value->BooleanValue(isolate));
}


THREADED_TEST(InterceptorHasOwnPropertyCausingGC) {
  LocalContext context;
  v8::Isolate* isolate = context->GetIsolate();
  v8::HandleScope scope(isolate);
  Local<v8::FunctionTemplate> fun_templ = v8::FunctionTemplate::New(isolate);
  Local<v8::ObjectTemplate> instance_templ = fun_templ->InstanceTemplate();
  instance_templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorHasOwnPropertyGetterGC));
  Local<Function> function =
      fun_templ->GetFunction(context.local()).ToLocalChecked();
  context->Global()
      ->Set(context.local(), v8_str("constructor"), function)
      .FromJust();
  // Let's first make some stuff so we can be sure to get a good GC.
  CompileRun(
      "function makestr(size) {"
      "  switch (size) {"
      "    case 1: return 'f';"
      "    case 2: return 'fo';"
      "    case 3: return 'foo';"
      "  }"
      "  return makestr(size >> 1) + makestr((size + 1) >> 1);"
      "}"
      "var x = makestr(12345);"
      "x = makestr(31415);"
      "x = makestr(23456);");
  v8::Local<Value> value = CompileRun(
      "var o = new constructor();"
      "o.__proto__ = new String(x);"
      "o.hasOwnProperty('ostehaps');");
  CHECK(!value->BooleanValue(isolate));
}

namespace {

void CheckInterceptorIC(v8::NamedPropertyGetterCallback getter,
                        v8::NamedPropertySetterCallback setter,
                        v8::NamedPropertyQueryCallback query,
                        v8::NamedPropertyDefinerCallback definer,
                        v8::PropertyHandlerFlags flags, const char* source,
                        std::optional<int> expected) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(
      getter, setter, query, nullptr /* deleter */, nullptr /* enumerator */,
      definer, nullptr /* descriptor */, v8_str("data"), flags));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> value = CompileRun(source);
  if (expected) {
    CHECK_EQ(*expected, value->Int32Value(context.local()).FromJust());
  } else {
    CHECK(value.IsEmpty());
  }
}

void CheckInterceptorIC(v8::NamedPropertyGetterCallback getter,
                        v8::NamedPropertyQueryCallback query,
                        const char* source, std::optional<int> expected) {
  CheckInterceptorIC(getter, nullptr, query, nullptr,
                     v8::PropertyHandlerFlags::kNone, source, expected);
}

void CheckInterceptorLoadIC(v8::NamedPropertyGetterCallback getter,
                            const char* source, int expected) {
  CheckInterceptorIC(getter, nullptr, nullptr, nullptr,
                     v8::PropertyHandlerFlags::kNone, source, expected);
}

v8::Intercepted InterceptorLoadICGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  v8::Isolate* isolate = CcTest::isolate();
  CHECK_EQ(isolate, info.GetIsolate());
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  CHECK(v8_str("data")->Equals(context, info.Data()).FromJust());
  CHECK(v8_str("x")->Equals(context, name).FromJust());
  info.GetReturnValue().Set(v8::Integer::New(isolate, 42));
  return v8::Intercepted::kYes;
}

}  // namespace

// This test should hit the load IC for the interceptor case.
THREADED_TEST(InterceptorLoadIC) {
  CheckInterceptorLoadIC(InterceptorLoadICGetter,
                         "var result = 0;"
                         "for (var i = 0; i < 1000; i++) {"
                         "  result = o.x;"
                         "}",
                         42);
}


// Below go several tests which verify that JITing for various
// configurations of interceptor and explicit fields works fine
// (those cases are special cased to get better performance).

namespace {

v8::Intercepted InterceptorLoadXICGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  if (v8_str("x")->Equals(isolate->GetCurrentContext(), name).FromJust()) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().Set(42);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

v8::Intercepted InterceptorLoadXICGetterWithSideEffects(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  // TODO(ishell): figure out what is the test supposed to do regarding
  // producing side effects but claiming that the interceptor hasn't
  // intercepted the operation. Is it about restarting the lookup iterator?
  ApiTestFuzzer::Fuzz();
  CompileRun("interceptor_getter_side_effect()");
  v8::Isolate* isolate = info.GetIsolate();
  if (v8_str("x")->Equals(isolate->GetCurrentContext(), name).FromJust()) {
    info.GetReturnValue().Set(42);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

}  // namespace

THREADED_TEST(InterceptorLoadICWithFieldOnHolder) {
  CheckInterceptorLoadIC(InterceptorLoadXICGetter,
                         "var result = 0;"
                         "o.y = 239;"
                         "for (var i = 0; i < 1000; i++) {"
                         "  result = o.y;"
                         "}",
                         239);
}


THREADED_TEST(InterceptorLoadICWithSubstitutedProto) {
  CheckInterceptorLoadIC(InterceptorLoadXICGetter,
                         "var result = 0;"
                         "o.__proto__ = { 'y': 239 };"
                         "for (var i = 0; i < 1000; i++) {"
                         "  result = o.y + o.x;"
                         "}",
                         239 + 42);
}


THREADED_TEST(InterceptorLoadICWithPropertyOnProto) {
  CheckInterceptorLoadIC(InterceptorLoadXICGetter,
                         "var result = 0;"
                         "o.__proto__.y = 239;"
                         "for (var i = 0; i < 1000; i++) {"
                         "  result = o.y + o.x;"
                         "}",
                         239 + 42);
}


THREADED_TEST(InterceptorLoadICUndefined) {
  CheckInterceptorLoadIC(InterceptorLoadXICGetter,
                         "var result = 0;"
                         "for (var i = 0; i < 1000; i++) {"
                         "  result = (o.y == undefined) ? 239 : 42;"
                         "}",
                         239);
}


THREADED_TEST(InterceptorLoadICWithOverride) {
  CheckInterceptorLoadIC(InterceptorLoadXICGetter,
                         "fst = new Object();  fst.__proto__ = o;"
                         "snd = new Object();  snd.__proto__ = fst;"
                         "var result1 = 0;"
                         "for (var i = 0; i < 1000;  i++) {"
                         "  result1 = snd.x;"
                         "}"
                         "fst.x = 239;"
                         "var result = 0;"
                         "for (var i = 0; i < 1000; i++) {"
                         "  result = snd.x;"
                         "}"
                         "result + result1",
                         239 + 42);
}


// Test the case when we stored field into
// a stub, but interceptor produced value on its own.
THREADED_TEST(InterceptorLoadICFieldNotNeeded) {
  CheckInterceptorLoadIC(
      InterceptorLoadXICGetter,
      "proto = new Object();"
      "o.__proto__ = proto;"
      "proto.x = 239;"
      "for (var i = 0; i < 1000; i++) {"
      "  o.x;"
      // Now it should be ICed and keep a reference to x defined on proto
      "}"
      "var result = 0;"
      "for (var i = 0; i < 1000; i++) {"
      "  result += o.x;"
      "}"
      "result;",
      42 * 1000);
}


// Test the case when we stored field into
// a stub, but it got invalidated later on.
THREADED_TEST(InterceptorLoadICInvalidatedField) {
  CheckInterceptorLoadIC(
      InterceptorLoadXICGetter,
      "proto1 = new Object();"
      "proto2 = new Object();"
      "o.__proto__ = proto1;"
      "proto1.__proto__ = proto2;"
      "proto2.y = 239;"
      "for (var i = 0; i < 1000; i++) {"
      "  o.y;"
      // Now it should be ICed and keep a reference to y defined on proto2
      "}"
      "proto1.y = 42;"
      "var result = 0;"
      "for (var i = 0; i < 1000; i++) {"
      "  result += o.y;"
      "}"
      "result;",
      42 * 1000);
}

namespace {

int interceptor_load_not_handled_calls = 0;
v8::Intercepted InterceptorLoadNotHandled(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ++interceptor_load_not_handled_calls;
  return v8::Intercepted::kNo;
}
}  // namespace

// Test how post-interceptor lookups are done in the non-cacheable
// case: the interceptor should not be invoked during this lookup.
THREADED_TEST(InterceptorLoadICPostInterceptor) {
  interceptor_load_not_handled_calls = 0;
  CheckInterceptorLoadIC(InterceptorLoadNotHandled,
                         "receiver = new Object();"
                         "receiver.__proto__ = o;"
                         "proto = new Object();"
                         "/* Make proto a slow-case object. */"
                         "for (var i = 0; i < 1000; i++) {"
                         "  proto[\"xxxxxxxx\" + i] = [];"
                         "}"
                         "proto.x = 17;"
                         "o.__proto__ = proto;"
                         "var result = 0;"
                         "for (var i = 0; i < 1000; i++) {"
                         "  result += receiver.x;"
                         "}"
                         "result;",
                         17 * 1000);
  CHECK_EQ(1000, interceptor_load_not_handled_calls);
}


// Test the case when we stored field into
// a stub, but it got invalidated later on due to override on
// global object which is between interceptor and fields' holders.
THREADED_TEST(InterceptorLoadICInvalidatedFieldViaGlobal) {
  CheckInterceptorLoadIC(
      InterceptorLoadXICGetter,
      "o.__proto__ = this;"  // set a global to be a proto of o.
      "this.__proto__.y = 239;"
      "for (var i = 0; i < 10; i++) {"
      "  if (o.y != 239) throw 'oops: ' + o.y;"
      // Now it should be ICed and keep a reference to y defined on
      // field_holder.
      "}"
      "this.y = 42;"  // Assign on a global.
      "var result = 0;"
      "for (var i = 0; i < 10; i++) {"
      "  result += o.y;"
      "}"
      "result;",
      42 * 10);
}

static void SetOnThis(Local<Name> name, Local<Value> value,
                      const v8::PropertyCallbackInfo<void>& info) {
  info.This()
      .As<Object>()
      ->CreateDataProperty(info.GetIsolate()->GetCurrentContext(), name, value)
      .FromJust();
}

THREADED_TEST(InterceptorLoadICWithCallbackOnHolder) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorLoadXICGetter));
  templ->SetNativeDataProperty(v8_str("y"), Return239Callback);
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  // Check the case when receiver and interceptor's holder
  // are the same objects.
  v8::Local<Value> value = CompileRun(
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result = o.y;"
      "}");
  CHECK_EQ(239, value->Int32Value(context.local()).FromJust());

  // Check the case when interceptor's holder is in proto chain
  // of receiver.
  value = CompileRun(
      "r = { __proto__: o };"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result = r.y;"
      "}");
  CHECK_EQ(239, value->Int32Value(context.local()).FromJust());
}


THREADED_TEST(InterceptorLoadICWithCallbackOnProto) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_o = ObjectTemplate::New(isolate);
  templ_o->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorLoadXICGetter));
  v8::Local<v8::ObjectTemplate> templ_p = ObjectTemplate::New(isolate);
  templ_p->SetNativeDataProperty(v8_str("y"), Return239Callback);

  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ_o->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  context->Global()
      ->Set(context.local(), v8_str("p"),
            templ_p->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  // Check the case when receiver and interceptor's holder
  // are the same objects.
  v8::Local<Value> value = CompileRun(
      "o.__proto__ = p;"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result = o.x + o.y;"
      "}");
  CHECK_EQ(239 + 42, value->Int32Value(context.local()).FromJust());

  // Check the case when interceptor's holder is in proto chain
  // of receiver.
  value = CompileRun(
      "r = { __proto__: o };"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result = r.x + r.y;"
      "}");
  CHECK_EQ(239 + 42, value->Int32Value(context.local()).FromJust());
}


THREADED_TEST(InterceptorLoadICForCallbackWithOverride) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorLoadXICGetter));
  templ->SetNativeDataProperty(v8_str("y"), Return239Callback);

  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  v8::Local<Value> value = CompileRun(
      "fst = new Object();  fst.__proto__ = o;"
      "snd = new Object();  snd.__proto__ = fst;"
      "var result1 = 0;"
      "for (var i = 0; i < 7;  i++) {"
      "  result1 = snd.x;"
      "}"
      "fst.x = 239;"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result = snd.x;"
      "}"
      "result + result1");
  CHECK_EQ(239 + 42, value->Int32Value(context.local()).FromJust());
}


// Test the case when we stored callback into
// a stub, but interceptor produced value on its own.
THREADED_TEST(InterceptorLoadICCallbackNotNeeded) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_o = ObjectTemplate::New(isolate);
  templ_o->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorLoadXICGetter));
  v8::Local<v8::ObjectTemplate> templ_p = ObjectTemplate::New(isolate);
  templ_p->SetNativeDataProperty(v8_str("y"), Return239Callback);

  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ_o->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  context->Global()
      ->Set(context.local(), v8_str("p"),
            templ_p->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  v8::Local<Value> value = CompileRun(
      "o.__proto__ = p;"
      "for (var i = 0; i < 7; i++) {"
      "  o.x;"
      // Now it should be ICed and keep a reference to x defined on p
      "}"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result += o.x;"
      "}"
      "result");
  CHECK_EQ(42 * 7, value->Int32Value(context.local()).FromJust());
}


// Test the case when we stored callback into
// a stub, but it got invalidated later on.
THREADED_TEST(InterceptorLoadICInvalidatedCallback) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_o = ObjectTemplate::New(isolate);
  templ_o->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorLoadXICGetter));
  v8::Local<v8::ObjectTemplate> templ_p = ObjectTemplate::New(isolate);
  templ_p->SetNativeDataProperty(v8_str("y"), Return239Callback, SetOnThis);

  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ_o->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  context->Global()
      ->Set(context.local(), v8_str("p"),
            templ_p->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  v8::Local<Value> value = CompileRun(
      "inbetween = new Object();"
      "o.__proto__ = inbetween;"
      "inbetween.__proto__ = p;"
      "for (var i = 0; i < 10; i++) {"
      "  o.y;"
      // Now it should be ICed and keep a reference to y defined on p
      "}"
      "inbetween.y = 42;"
      "var result = 0;"
      "for (var i = 0; i < 10; i++) {"
      "  result += o.y;"
      "}"
      "result");
  CHECK_EQ(42 * 10, value->Int32Value(context.local()).FromJust());
}


// Test the case when we stored callback into
// a stub, but it got invalidated later on due to override on
// global object which is between interceptor and callbacks' holders.
THREADED_TEST(InterceptorLoadICInvalidatedCallbackViaGlobal) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_o = ObjectTemplate::New(isolate);
  templ_o->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorLoadXICGetter));
  v8::Local<v8::ObjectTemplate> templ_p = ObjectTemplate::New(isolate);
  templ_p->SetNativeDataProperty(v8_str("y"), Return239Callback, SetOnThis);

  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ_o->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  context->Global()
      ->Set(context.local(), v8_str("p"),
            templ_p->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  v8::Local<Value> value = CompileRun(
      "o.__proto__ = this;"
      "this.__proto__ = p;"
      "for (var i = 0; i < 10; i++) {"
      "  if (o.y != 239) throw 'oops: ' + o.y;"
      // Now it should be ICed and keep a reference to y defined on p
      "}"
      "this.y = 42;"
      "var result = 0;"
      "for (var i = 0; i < 10; i++) {"
      "  result += o.y;"
      "}"
      "result");
  CHECK_EQ(42 * 10, value->Int32Value(context.local()).FromJust());
}

// Test load of a non-existing global when a global object has an interceptor.
THREADED_TEST(InterceptorLoadGlobalICGlobalWithInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_global = v8::ObjectTemplate::New(isolate);
  templ_global->SetHandler(v8::NamedPropertyHandlerConfiguration(
      EmptyInterceptorGetter, EmptyInterceptorSetter));

  LocalContext context(nullptr, templ_global);
  i::DirectHandle<i::JSReceiver> global_proxy =
      v8::Utils::OpenDirectHandle<Object, i::JSReceiver>(context->Global());
  CHECK(IsJSGlobalProxy(*global_proxy));
  i::DirectHandle<i::JSGlobalObject> global(
      i::Cast<i::JSGlobalObject>(global_proxy->map()->prototype()),
      global_proxy->GetIsolate());
  CHECK(global->map()->has_named_interceptor());

  v8::Local<Value> value = CompileRun(
      "var f = function() { "
      "  try {"
      "    x1;"
      "  } catch(e) {"
      "  }"
      "  return typeof x1 === 'undefined';"
      "};"
      "for (var i = 0; i < 10; i++) {"
      "  f();"
      "};"
      "f();");
  CHECK(value->BooleanValue(isolate));

  value = CompileRun(
      "var f = function() { "
      "  try {"
      "    x2;"
      "    return false;"
      "  } catch(e) {"
      "    return true;"
      "  }"
      "};"
      "for (var i = 0; i < 10; i++) {"
      "  f();"
      "};"
      "f();");
  CHECK(value->BooleanValue(isolate));

  value = CompileRun(
      "var f = function() { "
      "  try {"
      "    typeof(x3);"
      "    return true;"
      "  } catch(e) {"
      "    return false;"
      "  }"
      "};"
      "for (var i = 0; i < 10; i++) {"
      "  f();"
      "};"
      "f();");
  CHECK(value->BooleanValue(isolate));
}

// Test load of a non-existing global through prototype chain when a global
// object has an interceptor.
THREADED_TEST(InterceptorLoadICGlobalWithInterceptor) {
  i::v8_flags.allow_natives_syntax = true;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_global = v8::ObjectTemplate::New(isolate);
  templ_global->SetHandler(v8::NamedPropertyHandlerConfiguration(
      GenericInterceptorGetter, GenericInterceptorSetter));

  LocalContext context(nullptr, templ_global);
  i::DirectHandle<i::JSReceiver> global_proxy =
      v8::Utils::OpenDirectHandle<Object, i::JSReceiver>(context->Global());
  CHECK(IsJSGlobalProxy(*global_proxy));
  i::DirectHandle<i::JSGlobalObject> global(
      i::Cast<i::JSGlobalObject>(global_proxy->map()->prototype()),
      global_proxy->GetIsolate());
  CHECK(global->map()->has_named_interceptor());

  ExpectInt32(
      "(function() {"
      "  var f = function(obj) { "
      "    return obj.foo;"
      "  };"
      "  var obj = { __proto__: this, _str_foo: 42 };"
      "  for (var i = 0; i < 1500; i++) obj['p' + i] = 0;"
      "  /* Ensure that |obj| is in dictionary mode. */"
      "  if (%HasFastProperties(obj)) return -1;"
      "  for (var i = 0; i < 3; i++) {"
      "    f(obj);"
      "  };"
      "  return f(obj);"
      "})();",
      42);
}

namespace {
v8::Intercepted InterceptorLoadICGetter0(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(v8_str("x")
            ->Equals(info.GetIsolate()->GetCurrentContext(), name)
            .FromJust());
  info.GetReturnValue().Set(0);
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(InterceptorReturningZero) {
  CheckInterceptorLoadIC(InterceptorLoadICGetter0, "o.x == undefined ? 1 : 0",
                         0);
}

namespace {

template <typename TKey, v8::internal::PropertyAttributes attribute>
v8::Intercepted HasICQuery(TKey name,
                           const v8::PropertyCallbackInfo<v8::Integer>& info) {
  v8::Isolate* isolate = CcTest::isolate();
  CHECK_EQ(isolate, info.GetIsolate());
  if (attribute != v8::internal::ABSENT) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().Set(attribute);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

template <typename TKey>
v8::Intercepted HasICQueryToggle(
    TKey name, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  static bool is_absent = false;
  is_absent = !is_absent;
  v8::Isolate* isolate = CcTest::isolate();
  CHECK_EQ(isolate, info.GetIsolate());
  if (!is_absent) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().Set(v8::None);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

template <typename TKey, v8::internal::PropertyAttributes attribute>
v8::Intercepted HasICQuerySideEffect(
    TKey name, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  if (attribute != v8::internal::ABSENT) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
  }
  v8::Isolate* isolate = CcTest::isolate();
  CHECK_EQ(isolate, info.GetIsolate());
  CompileRun("interceptor_query_side_effect()");
  if (attribute != v8::internal::ABSENT) {
    info.GetReturnValue().Set(attribute);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

int named_query_counter = 0;
v8::Intercepted NamedQueryCallback(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  named_query_counter++;
  return v8::Intercepted::kNo;
}

}  // namespace

THREADED_TEST(InterceptorHasIC) {
  named_query_counter = 0;
  CheckInterceptorIC(nullptr, NamedQueryCallback,
                     "var result = 0;"
                     "for (var i = 0; i < 1000; i++) {"
                     "  'x' in o;"
                     "}",
                     0);
  CHECK_EQ(1000, named_query_counter);
}

THREADED_TEST(InterceptorHasICQueryAbsent) {
  CheckInterceptorIC(nullptr, HasICQuery<Local<Name>, v8::internal::ABSENT>,
                     "var result = 0;"
                     "for (var i = 0; i < 1000; i++) {"
                     "  if ('x' in o) ++result;"
                     "}",
                     0);
}

THREADED_TEST(InterceptorHasICQueryNone) {
  CheckInterceptorIC(nullptr, HasICQuery<Local<Name>, v8::internal::NONE>,
                     "var result = 0;"
                     "for (var i = 0; i < 1000; i++) {"
                     "  if ('x' in o) ++result;"
                     "}",
                     1000);
}

THREADED_TEST(InterceptorHasICGetter) {
  CheckInterceptorIC(InterceptorLoadICGetter, nullptr,
                     "var result = 0;"
                     "for (var i = 0; i < 1000; i++) {"
                     "  if ('x' in o) ++result;"
                     "}",
                     1000);
}

THREADED_TEST(InterceptorHasICQueryGetter) {
  CheckInterceptorIC(InterceptorLoadICGetter,
                     HasICQuery<Local<Name>, v8::internal::ABSENT>,
                     "var result = 0;"
                     "for (var i = 0; i < 1000; i++) {"
                     "  if ('x' in o) ++result;"
                     "}",
                     0);
}

THREADED_TEST(InterceptorHasICQueryToggle) {
  CheckInterceptorIC(InterceptorLoadICGetter, HasICQueryToggle<Local<Name>>,
                     "var result = 0;"
                     "for (var i = 0; i < 1000; i++) {"
                     "  if ('x' in o) ++result;"
                     "}",
                     500);
}

THREADED_TEST(InterceptorStoreICWithSideEffectfulCallbacks1) {
  CheckInterceptorIC(EmptyInterceptorGetter,
                     HasICQuerySideEffect<Local<Name>, v8::internal::NONE>,
                     "let r;"
                     "let inside_side_effect = false;"
                     "let interceptor_query_side_effect = function() {"
                     "  if (!inside_side_effect) {"
                     "    inside_side_effect = true;"
                     "    r.x = 153;"
                     "    inside_side_effect = false;"
                     "  }"
                     "};"
                     "for (var i = 0; i < 20; i++) {"
                     "  r = { __proto__: o };"
                     "  r.x = i;"
                     "}",
                     19);
}

TEST(Crash_InterceptorStoreICWithSideEffectfulCallbacks1) {
  CheckInterceptorIC(EmptyInterceptorGetter,
                     HasICQuerySideEffect<Local<Name>, v8::internal::ABSENT>,
                     "let r;"
                     "let inside_side_effect = false;"
                     "let interceptor_query_side_effect = function() {"
                     "  if (!inside_side_effect) {"
                     "    inside_side_effect = true;"
                     "    r.x = 153;"
                     "    inside_side_effect = false;"
                     "  }"
                     "};"
                     "for (var i = 0; i < 20; i++) {"
                     "  r = { __proto__: o };"
                     "  r.x = i;"
                     "}",
                     19);
}

TEST(Crash_InterceptorStoreICWithSideEffectfulCallbacks2) {
  CheckInterceptorIC(InterceptorLoadXICGetterWithSideEffects,
                     nullptr,  // query callback is not provided
                     "let r;"
                     "let inside_side_effect = false;"
                     "let interceptor_getter_side_effect = function() {"
                     "  if (!inside_side_effect) {"
                     "    inside_side_effect = true;"
                     "    r.y = 153;"
                     "    inside_side_effect = false;"
                     "  }"
                     "};"
                     "for (var i = 0; i < 20; i++) {"
                     "  r = { __proto__: o };"
                     "  r.y = i;"
                     "}",
                     19);
}

THREADED_TEST(InterceptorDefineICWithSideEffectfulCallbacks) {
  CheckInterceptorIC(EmptyInterceptorGetter, EmptyInterceptorSetter,
                     EmptyInterceptorQuery,
                     EmptyInterceptorDefinerWithSideEffect,
                     v8::PropertyHandlerFlags::kNonMasking,
                     "let inside_side_effect = false;"
                     "let interceptor_definer_side_effect = function() {"
                     "  if (!inside_side_effect) {"
                     "    inside_side_effect = true;"
                     "    o.y = 153;"
                     "    inside_side_effect = false;"
                     "  }"
                     "  return true;"  // Accept the request.
                     "};"
                     "class Base {"
                     "  constructor(arg) {"
                     "    return arg;"
                     "  }"
                     "}"
                     "class ClassWithField extends Base {"
                     "  y = (() => {"
                     "    return 42;"
                     "  })();"
                     "  constructor(arg) {"
                     "    super(arg);"
                     "  }"
                     "}"
                     "new ClassWithField(o);"
                     "o.y",
                     153);
}

TEST(Crash_InterceptorDefineICWithSideEffectfulCallbacks) {
  CheckInterceptorIC(EmptyInterceptorGetter, EmptyInterceptorSetter,
                     EmptyInterceptorQuery,
                     EmptyInterceptorDefinerWithSideEffect,
                     v8::PropertyHandlerFlags::kNonMasking,
                     "let inside_side_effect = false;"
                     "let interceptor_definer_side_effect = function() {"
                     "  if (!inside_side_effect) {"
                     "    inside_side_effect = true;"
                     "    o.y = 153;"
                     "    inside_side_effect = false;"
                     "  }"
                     "  return null;"  // Decline the request.
                     "};"
                     "class Base {"
                     "  constructor(arg) {"
                     "    return arg;"
                     "  }"
                     "}"
                     "class ClassWithField extends Base {"
                     "  y = (() => {"
                     "    return 42;"
                     "  })();"
                     "  constructor(arg) {"
                     "    super(arg);"
                     "  }"
                     "}"
                     "new ClassWithField(o);"
                     "o.y",
                     42);
}

namespace {
v8::Intercepted InterceptorStoreICSetter(
    Local<Name> key, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
  CHECK(v8_str("x")->Equals(context, key).FromJust());
  CHECK_EQ(42, value->Int32Value(context).FromJust());
  return v8::Intercepted::kYes;
}
}  // namespace

// This test should hit the store IC for the interceptor case.
THREADED_TEST(InterceptorStoreIC) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(
      InterceptorLoadICGetter, InterceptorStoreICSetter, nullptr, nullptr,
      nullptr, v8_str("data")));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "for (var i = 0; i < 1000; i++) {"
      "  o.x = 42;"
      "}");
}


THREADED_TEST(InterceptorStoreICWithNoSetter) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorLoadXICGetter));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> value = CompileRun(
      "for (var i = 0; i < 1000; i++) {"
      "  o.y = 239;"
      "}"
      "42 + o.y");
  CHECK_EQ(239 + 42, value->Int32Value(context.local()).FromJust());
}

THREADED_TEST(EmptyInterceptorDoesNotShadowReadOnlyProperty) {
  // Interceptor should not shadow readonly property 'x' on the prototype, and
  // attempt to store to 'x' must throw.
  CheckInterceptorIC(EmptyInterceptorGetter,
                     HasICQuery<Local<Name>, v8::internal::ABSENT>,
                     "'use strict';"
                     "let p = {};"
                     "Object.defineProperty(p, 'x', "
                     "                      {value: 153, writable: false});"
                     "o.__proto__ = p;"
                     "let result = 0;"
                     "let r;"
                     "for (var i = 0; i < 20; i++) {"
                     "  r = { __proto__: o };"
                     "  try {"
                     "    r.x = i;"
                     "  } catch (e) {"
                     "    result++;"
                     "  }"
                     "}"
                     "result",
                     20);
}

THREADED_TEST(InterceptorShadowsReadOnlyProperty) {
  // Interceptor claims that it has a writable property 'x', so the existence
  // of the readonly property 'x' on the prototype should not cause exceptions.
  CheckInterceptorIC(InterceptorLoadXICGetter,
                     nullptr,  // query callback
                     "'use strict';"
                     "let p = {};"
                     "Object.defineProperty(p, 'x', "
                     "                      {value: 153, writable: false});"
                     "o.__proto__ = p;"
                     "let result = 0;"
                     "let r;"
                     "for (var i = 0; i < 20; i++) {"
                     "  r = { __proto__: o };"
                     "  try {"
                     "    r.x = i;"
                     "    result++;"
                     "  } catch (e) {}"
                     "}"
                     "result",
                     20);
}

THREADED_TEST(EmptyInterceptorDoesNotShadowAccessors) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> parent = FunctionTemplate::New(isolate);
  Local<FunctionTemplate> child = FunctionTemplate::New(isolate);
  child->Inherit(parent);
  AddAccessor(isolate, parent, v8_str("age"), SimpleGetterCallback,
              SimpleSetterCallback);
  AddInterceptor(child, EmptyInterceptorGetter, EmptyInterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var child = new Child;"
      "child.age = 10;");
  ExpectBoolean("child.hasOwnProperty('age')", false);
  ExpectInt32("child.age", 10);
  ExpectInt32("child.accessor_age", 10);
}

THREADED_TEST(EmptyInterceptorVsStoreGlobalICs) {
  // In sloppy mode storing to global must succeed.
  CheckInterceptorIC(EmptyInterceptorGetter,
                     HasICQuery<Local<Name>, v8::internal::ABSENT>,
                     "globalThis.__proto__ = o;"
                     "let result = 0;"
                     "for (var i = 0; i < 20; i++) {"
                     "  try {"
                     "    x = i;"
                     "    result++;"
                     "  } catch (e) {}"
                     "}"
                     "result + x",
                     20 + 19);

  // In strict mode storing to global must throw.
  CheckInterceptorIC(EmptyInterceptorGetter,
                     HasICQuery<Local<Name>, v8::internal::ABSENT>,
                     "'use strict';"
                     "globalThis.__proto__ = o;"
                     "let result = 0;"
                     "for (var i = 0; i < 20; i++) {"
                     "  try {"
                     "    x = i;"
                     "  } catch (e) {"
                     "    result++;"
                     "  }"
                     "}"
                     "result + (typeof(x) === 'undefined' ? 100 : 0)",
                     120);
}

THREADED_TEST(LegacyInterceptorDoesNotSeeSymbols) {
  LocalContext env;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> parent = FunctionTemplate::New(isolate);
  Local<FunctionTemplate> child = FunctionTemplate::New(isolate);
  v8::Local<v8::Symbol> age = v8::Symbol::New(isolate, v8_str("age"));

  child->Inherit(parent);
  AddAccessor(isolate, parent, age, SymbolGetterCallback, SymbolSetterCallback);
  AddStringOnlyInterceptor(child, InterceptorGetter, InterceptorSetter);

  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  env->Global()->Set(env.local(), v8_str("age"), age).FromJust();
  CompileRun(
      "var child = new Child;"
      "child[age] = 10;");
  ExpectInt32("child[age]", 10);
  ExpectBoolean("child.hasOwnProperty('age')", false);
  ExpectBoolean("child.hasOwnProperty('accessor_age')", true);
}


THREADED_TEST(GenericInterceptorDoesSeeSymbols) {
  LocalContext env;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> parent = FunctionTemplate::New(isolate);
  Local<FunctionTemplate> child = FunctionTemplate::New(isolate);
  v8::Local<v8::Symbol> age = v8::Symbol::New(isolate, v8_str("age"));
  v8::Local<v8::Symbol> anon = v8::Symbol::New(isolate);

  child->Inherit(parent);
  AddAccessor(isolate, parent, age, SymbolGetterCallback, SymbolSetterCallback);
  AddInterceptor(child, GenericInterceptorGetter, GenericInterceptorSetter);

  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  env->Global()->Set(env.local(), v8_str("age"), age).FromJust();
  env->Global()->Set(env.local(), v8_str("anon"), anon).FromJust();
  CompileRun(
      "var child = new Child;"
      "child[age] = 10;");
  ExpectInt32("child[age]", 10);
  ExpectInt32("child._sym_age", 10);

  // Check that it also sees strings.
  CompileRun("child.foo = 47");
  ExpectInt32("child.foo", 47);
  ExpectInt32("child._str_foo", 47);

  // Check that the interceptor can punt (in this case, on anonymous symbols).
  CompileRun("child[anon] = 31337");
  ExpectInt32("child[anon]", 31337);
}


THREADED_TEST(NamedPropertyHandlerGetter) {
  echo_named_call_count = 0;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      EchoNamedProperty, nullptr, nullptr, nullptr, nullptr, v8_str("data")));
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  CHECK_EQ(0, echo_named_call_count);
  v8_compile("obj.x")->Run(env.local()).ToLocalChecked();
  CHECK_EQ(1, echo_named_call_count);
  const char* code = "var str = 'oddle'; obj[str] + obj.poddle;";
  v8::Local<Value> str = CompileRun(code);
  String::Utf8Value value(isolate, str);
  CHECK_EQ(0, strcmp(*value, "oddlepoddle"));
  // Check default behavior
  CHECK_EQ(10, v8_compile("obj.flob = 10;")
                   ->Run(env.local())
                   .ToLocalChecked()
                   ->Int32Value(env.local())
                   .FromJust());
  CHECK(v8_compile("'myProperty' in obj")
            ->Run(env.local())
            .ToLocalChecked()
            ->BooleanValue(isolate));
  CHECK(v8_compile("delete obj.myProperty")
            ->Run(env.local())
            .ToLocalChecked()
            ->BooleanValue(isolate));
}

namespace {
v8::Intercepted NotInterceptingPropertyDefineCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  return v8::Intercepted::kNo;
}

v8::Intercepted InterceptingPropertyDefineCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  return v8::Intercepted::kYes;
}

v8::Intercepted CheckDescriptorInDefineCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  CHECK(!desc.has_writable());
  CHECK(!desc.has_value());
  CHECK(!desc.has_enumerable());
  CHECK(desc.has_configurable());
  CHECK(!desc.configurable());
  CHECK(desc.has_get());
  CHECK(desc.get()->IsFunction());
  CHECK(desc.has_set());
  CHECK(desc.set()->IsUndefined());
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(PropertyDefinerCallback) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;

  {  // Intercept defineProperty()
    v8::Local<v8::FunctionTemplate> templ =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
        nullptr, nullptr, nullptr, nullptr, nullptr,
        NotInterceptingPropertyDefineCallback));
    env->Global()
        ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                              .ToLocalChecked()
                                              ->NewInstance(env.local())
                                              .ToLocalChecked())
        .FromJust();
    const char* code =
        "obj.x = 17; "
        "Object.defineProperty(obj, 'x', {value: 42});"
        "obj.x;";
    CHECK_EQ(42, v8_compile(code)
                     ->Run(env.local())
                     .ToLocalChecked()
                     ->Int32Value(env.local())
                     .FromJust());
  }

  {  // Intercept defineProperty() for correct accessor descriptor
    v8::Local<v8::FunctionTemplate> templ =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
        nullptr, nullptr, nullptr, nullptr, nullptr,
        CheckDescriptorInDefineCallback));
    env->Global()
        ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                              .ToLocalChecked()
                                              ->NewInstance(env.local())
                                              .ToLocalChecked())
        .FromJust();
    const char* code =
        "obj.x = 17; "
        "Object.defineProperty(obj, 'x', {"
        "get: function(){ return 42; }, "
        "set: undefined,"
        "configurable: 0"
        "});"
        "obj.x;";
    CHECK_EQ(17, v8_compile(code)
                     ->Run(env.local())
                     .ToLocalChecked()
                     ->Int32Value(env.local())
                     .FromJust());
  }

  {  // Do not intercept defineProperty()
    v8::Local<v8::FunctionTemplate> templ2 =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ2->InstanceTemplate()->SetHandler(
        v8::NamedPropertyHandlerConfiguration(
            nullptr, nullptr, nullptr, nullptr, nullptr,
            InterceptingPropertyDefineCallback));
    env->Global()
        ->Set(env.local(), v8_str("obj"), templ2->GetFunction(env.local())
                                              .ToLocalChecked()
                                              ->NewInstance(env.local())
                                              .ToLocalChecked())
        .FromJust();

    const char* code =
        "obj.x = 17; "
        "Object.defineProperty(obj, 'x', {value: 42});"
        "obj.x;";
    CHECK_EQ(17, v8_compile(code)
                     ->Run(env.local())
                     .ToLocalChecked()
                     ->Int32Value(env.local())
                     .FromJust());
  }
}

namespace {
v8::Intercepted NotInterceptingPropertyDefineCallbackIndexed(
    uint32_t index, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  return v8::Intercepted::kNo;
}

v8::Intercepted InterceptingPropertyDefineCallbackIndexed(
    uint32_t index, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  return v8::Intercepted::kYes;
}

v8::Intercepted CheckDescriptorInDefineCallbackIndexed(
    uint32_t index, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  CHECK(!desc.has_writable());
  CHECK(!desc.has_value());
  CHECK(desc.has_enumerable());
  CHECK(desc.enumerable());
  CHECK(!desc.has_configurable());
  CHECK(desc.has_get());
  CHECK(desc.get()->IsFunction());
  CHECK(desc.has_set());
  CHECK(desc.set()->IsUndefined());
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(PropertyDefinerCallbackIndexed) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;

  {  // Intercept defineProperty()
    v8::Local<v8::FunctionTemplate> templ =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ->InstanceTemplate()->SetHandler(
        v8::IndexedPropertyHandlerConfiguration(
            nullptr, nullptr, nullptr, nullptr, nullptr,
            NotInterceptingPropertyDefineCallbackIndexed));
    env->Global()
        ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                              .ToLocalChecked()
                                              ->NewInstance(env.local())
                                              .ToLocalChecked())
        .FromJust();
    const char* code =
        "obj[2] = 17; "
        "Object.defineProperty(obj, 2, {value: 42});"
        "obj[2];";
    CHECK_EQ(42, v8_compile(code)
                     ->Run(env.local())
                     .ToLocalChecked()
                     ->Int32Value(env.local())
                     .FromJust());
  }

  {  // Intercept defineProperty() for correct accessor descriptor
    v8::Local<v8::FunctionTemplate> templ =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ->InstanceTemplate()->SetHandler(
        v8::IndexedPropertyHandlerConfiguration(
            nullptr, nullptr, nullptr, nullptr, nullptr,
            CheckDescriptorInDefineCallbackIndexed));
    env->Global()
        ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                              .ToLocalChecked()
                                              ->NewInstance(env.local())
                                              .ToLocalChecked())
        .FromJust();
    const char* code =
        "obj[2] = 17; "
        "Object.defineProperty(obj, 2, {"
        "get: function(){ return 42; }, "
        "set: undefined,"
        "enumerable: true"
        "});"
        "obj[2];";
    CHECK_EQ(17, v8_compile(code)
                     ->Run(env.local())
                     .ToLocalChecked()
                     ->Int32Value(env.local())
                     .FromJust());
  }

  {  // Do not intercept defineProperty()
    v8::Local<v8::FunctionTemplate> templ2 =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ2->InstanceTemplate()->SetHandler(
        v8::IndexedPropertyHandlerConfiguration(
            nullptr, nullptr, nullptr, nullptr, nullptr,
            InterceptingPropertyDefineCallbackIndexed));
    env->Global()
        ->Set(env.local(), v8_str("obj"), templ2->GetFunction(env.local())
                                              .ToLocalChecked()
                                              ->NewInstance(env.local())
                                              .ToLocalChecked())
        .FromJust();

    const char* code =
        "obj[2] = 17; "
        "Object.defineProperty(obj, 2, {value: 42});"
        "obj[2];";
    CHECK_EQ(17, v8_compile(code)
                     ->Run(env.local())
                     .ToLocalChecked()
                     ->Int32Value(env.local())
                     .FromJust());
  }
}

// Test that freeze() is intercepted.
THREADED_TEST(PropertyDefinerCallbackForFreeze) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, nullptr, nullptr, nullptr, nullptr,
      InterceptingPropertyDefineCallback));
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  const char* code =
      "obj.x = 17; "
      "Object.freeze(obj.x); "
      "Object.isFrozen(obj.x);";

  CHECK(v8_compile(code)
            ->Run(env.local())
            .ToLocalChecked()
            ->BooleanValue(isolate));
}

// Check that the descriptor passed to the callback is enumerable.
namespace {
v8::Intercepted CheckEnumerablePropertyDefineCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  CHECK(desc.has_value());
  CHECK_EQ(42, desc.value()
                   ->Int32Value(info.GetIsolate()->GetCurrentContext())
                   .FromJust());
  CHECK(desc.has_enumerable());
  CHECK(desc.enumerable());
  CHECK(!desc.has_writable());
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(PropertyDefinerCallbackEnumerable) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, nullptr, nullptr, nullptr, nullptr,
      CheckEnumerablePropertyDefineCallback));
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  const char* code =
      "obj.x = 17; "
      "Object.defineProperty(obj, 'x', {value: 42, enumerable: true});"
      "obj.x;";
  CHECK_EQ(17, v8_compile(code)
                   ->Run(env.local())
                   .ToLocalChecked()
                   ->Int32Value(env.local())
                   .FromJust());
}

// Check that the descriptor passed to the callback is configurable.
namespace {
v8::Intercepted CheckConfigurablePropertyDefineCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  CHECK(desc.has_value());
  CHECK_EQ(42, desc.value()
                   ->Int32Value(info.GetIsolate()->GetCurrentContext())
                   .FromJust());
  CHECK(desc.has_configurable());
  CHECK(desc.configurable());
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(PropertyDefinerCallbackConfigurable) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, nullptr, nullptr, nullptr, nullptr,
      CheckConfigurablePropertyDefineCallback));
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  const char* code =
      "obj.x = 17; "
      "Object.defineProperty(obj, 'x', {value: 42, configurable: true});"
      "obj.x;";
  CHECK_EQ(17, v8_compile(code)
                   ->Run(env.local())
                   .ToLocalChecked()
                   ->Int32Value(env.local())
                   .FromJust());
}

// Check that the descriptor passed to the callback is writable.
namespace {
v8::Intercepted CheckWritablePropertyDefineCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  CHECK(desc.has_writable());
  CHECK(desc.writable());
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(PropertyDefinerCallbackWritable) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, nullptr, nullptr, nullptr, nullptr,
      CheckWritablePropertyDefineCallback));
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  const char* code =
      "obj.x = 17; "
      "Object.defineProperty(obj, 'x', {value: 42, writable: true});"
      "obj.x;";
  CHECK_EQ(17, v8_compile(code)
                   ->Run(env.local())
                   .ToLocalChecked()
                   ->Int32Value(env.local())
                   .FromJust());
}

// Check that the descriptor passed to the callback has a getter.
namespace {
v8::Intercepted CheckGetterPropertyDefineCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  CHECK(desc.has_get());
  CHECK(!desc.has_set());
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(PropertyDefinerCallbackWithGetter) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, nullptr, nullptr, nullptr, nullptr,
      CheckGetterPropertyDefineCallback));
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  const char* code =
      "obj.x = 17;"
      "Object.defineProperty(obj, 'x', {get: function() {return 42;}});"
      "obj.x;";
  CHECK_EQ(17, v8_compile(code)
                   ->Run(env.local())
                   .ToLocalChecked()
                   ->Int32Value(env.local())
                   .FromJust());
}

// Check that the descriptor passed to the callback has a setter.
namespace {
v8::Intercepted CheckSetterPropertyDefineCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  CHECK(desc.has_set());
  CHECK(!desc.has_get());
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(PropertyDefinerCallbackWithSetter) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;
  v8::Local<v8::FunctionTemplate> templ =
      v8::FunctionTemplate::New(CcTest::isolate());
  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, nullptr, nullptr, nullptr, nullptr,
      CheckSetterPropertyDefineCallback));
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  const char* code =
      "Object.defineProperty(obj, 'x', {set: function() {return 42;}});"
      "obj.x = 17;";
  CHECK_EQ(17, v8_compile(code)
                   ->Run(env.local())
                   .ToLocalChecked()
                   ->Int32Value(env.local())
                   .FromJust());
}

namespace {
std::vector<std::string> definer_calls;
v8::Intercepted LogDefinerCallsAndContinueCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  String::Utf8Value utf8(info.GetIsolate(), name);
  definer_calls.push_back(*utf8);
  return v8::Intercepted::kNo;
}
v8::Intercepted LogDefinerCallsAndStopCallback(
    Local<Name> name, const v8::PropertyDescriptor& desc,
    const v8::PropertyCallbackInfo<void>& info) {
  String::Utf8Value utf8(info.GetIsolate(), name);
  definer_calls.push_back(*utf8);
  return v8::Intercepted::kYes;
}

struct DefineNamedOwnICInterceptorConfig {
  std::string code;
  std::vector<std::string> intercepted_defines;
};

std::vector<DefineNamedOwnICInterceptorConfig> configs{
    {
        R"(
          class ClassWithNormalField extends Base {
            field = (() => {
              Object.defineProperty(
                this,
                'normalField',
                { writable: true, configurable: true, value: 'initial'}
              );
              return 1;
            })();
            normalField = 'written';
            constructor(arg) {
              super(arg);
            }
          }
          new ClassWithNormalField(obj);
          stop ? (obj.field === undefined && obj.normalField === undefined)
            : (obj.field === 1 && obj.normalField === 'written'))",
        {"normalField", "field", "normalField"},  // intercepted defines
    },
    {
        R"(
            let setterCalled = false;
            class ClassWithSetterField extends Base {
              field = (() => {
                Object.defineProperty(
                  this,
                  'setterField',
                  { configurable: true, set(val) { setterCalled = true; } }
                );
                return 1;
              })();
              setterField = 'written';
              constructor(arg) {
                super(arg);
              }
            }
            new ClassWithSetterField(obj);
            !setterCalled &&
              (stop ? (obj.field === undefined && obj.setterField === undefined)
                : (obj.field === 1 && obj.setterField === 'written')))",
        {"setterField", "field", "setterField"},  // intercepted defines
    },
    {
        R"(
          class ClassWithReadOnlyField extends Base {
            field = (() => {
              Object.defineProperty(
                this,
                'readOnlyField',
                { writable: false, configurable: true, value: 'initial'}
              );
              return 1;
            })();
            readOnlyField = 'written';
            constructor(arg) {
              super(arg);
            }
          }
          new ClassWithReadOnlyField(obj);
          stop ? (obj.field === undefined && obj.readOnlyField === undefined)
            : (obj.field === 1 && obj.readOnlyField === 'written'))",
        {"readOnlyField", "field", "readOnlyField"},  // intercepted defines
    },
    {
        R"(
          class ClassWithNonConfigurableField extends Base {
            field = (() => {
              Object.defineProperty(
                this,
                'nonConfigurableField',
                { writable: false, configurable: false, value: 'initial'}
              );
              return 1;
            })();
            nonConfigurableField = 'configured';
            constructor(arg) {
              super(arg);
            }
          }
          let nonConfigurableThrown = false;
          try { new ClassWithNonConfigurableField(obj); }
          catch { nonConfigurableThrown = true; }
          stop ? (!nonConfigurableThrown && obj.field === undefined
                  && obj.nonConfigurableField === undefined)
              : (nonConfigurableThrown && obj.field === 1
                && obj.nonConfigurableField === 'initial'))",
        // intercepted defines
        {"nonConfigurableField", "field", "nonConfigurableField"}}
    // We don't test non-extensible objects here because objects with
    // interceptors cannot prevent extensions.
};
}  // namespace

void CheckPropertyDefinerCallbackInDefineNamedOwnIC(Local<Context> context,
                                                    bool stop) {
  v8_compile(R"(
    class Base {
      constructor(arg) {
        return arg;
      }
    })")
      ->Run(context)
      .ToLocalChecked();

  v8_compile(stop ? "var stop = true;" : "var stop = false;")
      ->Run(context)
      .ToLocalChecked();

  for (auto& config : configs) {
    printf("stop = %s, running...\n%s\n", stop ? "true" : "false",
           config.code.c_str());

    definer_calls.clear();

    // Create the object with interceptors.
    v8::Local<v8::FunctionTemplate> templ =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
        nullptr, nullptr, nullptr, nullptr, nullptr,
        stop ? LogDefinerCallsAndStopCallback
             : LogDefinerCallsAndContinueCallback,
        nullptr));
    Local<Object> obj = templ->GetFunction(context)
                            .ToLocalChecked()
                            ->NewInstance(context)
                            .ToLocalChecked();
    context->Global()->Set(context, v8_str("obj"), obj).FromJust();

    CHECK(v8_compile(config.code.c_str())
              ->Run(context)
              .ToLocalChecked()
              ->IsTrue());
    for (size_t i = 0; i < definer_calls.size(); ++i) {
      printf("define %s\n", definer_calls[i].c_str());
    }

    CHECK_EQ(config.intercepted_defines.size(), definer_calls.size());
    for (size_t i = 0; i < config.intercepted_defines.size(); ++i) {
      CHECK_EQ(config.intercepted_defines[i], definer_calls[i]);
    }
  }
}

THREADED_TEST(PropertyDefinerCallbackInDefineNamedOwnIC) {
  {
    LocalContext env;
    v8::HandleScope scope(env->GetIsolate());
    CheckPropertyDefinerCallbackInDefineNamedOwnIC(env.local(), true);
  }

  {
    LocalContext env;
    v8::HandleScope scope(env->GetIsolate());
    CheckPropertyDefinerCallbackInDefineNamedOwnIC(env.local(), false);
  }

  {
    i::v8_flags.lazy_feedback_allocation = false;
    i::FlagList::EnforceFlagImplications();
    LocalContext env;
    v8::HandleScope scope(env->GetIsolate());
    CheckPropertyDefinerCallbackInDefineNamedOwnIC(env.local(), true);
  }

  {
    i::v8_flags.lazy_feedback_allocation = false;
    i::FlagList::EnforceFlagImplications();
    LocalContext env;
    v8::HandleScope scope(env->GetIsolate());
    CheckPropertyDefinerCallbackInDefineNamedOwnIC(env.local(), false);
  }
}

namespace {
v8::Intercepted EmptyPropertyDescriptorCallback(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  return v8::Intercepted::kNo;
}

v8::Intercepted InterceptingPropertyDescriptorCallback(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  // Intercept the operation and return some descriptor.
  const char* code =
      "var desc = {value: 42};"
      "desc;";
  Local<Value> descriptor = v8_compile(code)
                                ->Run(info.GetIsolate()->GetCurrentContext())
                                .ToLocalChecked();
  info.GetReturnValue().Set(descriptor);
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(PropertyDescriptorCallback) {
  v8::HandleScope scope(CcTest::isolate());
  LocalContext env;

  {  // Normal behavior of getOwnPropertyDescriptor() with empty callback.
    v8::Local<v8::FunctionTemplate> templ =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
        nullptr, nullptr, EmptyPropertyDescriptorCallback, nullptr, nullptr,
        nullptr));
    env->Global()
        ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                              .ToLocalChecked()
                                              ->NewInstance(env.local())
                                              .ToLocalChecked())
        .FromJust();
    const char* code =
        "obj.x = 17; "
        "var desc = Object.getOwnPropertyDescriptor(obj, 'x');"
        "desc.value;";
    CHECK_EQ(17, v8_compile(code)
                     ->Run(env.local())
                     .ToLocalChecked()
                     ->Int32Value(env.local())
                     .FromJust());
  }

  {  // Intercept getOwnPropertyDescriptor().
    v8::Local<v8::FunctionTemplate> templ =
        v8::FunctionTemplate::New(CcTest::isolate());
    templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
        nullptr, nullptr, InterceptingPropertyDescriptorCallback, nullptr,
        nullptr, nullptr));
    env->Global()
        ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                              .ToLocalChecked()
                                              ->NewInstance(env.local())
                                              .ToLocalChecked())
        .FromJust();
    const char* code =
        "obj.x = 17; "
        "var desc = Object.getOwnPropertyDescriptor(obj, 'x');"
        "desc.value;";
    CHECK_EQ(42, v8_compile(code)
                     ->Run(env.local())
                     .ToLocalChecked()
                     ->Int32Value(env.local())
                     .FromJust());
  }
}

namespace {
int echo_indexed_call_count = 0;

v8::Intercepted EchoIndexedProperty(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(v8_num(637)
            ->Equals(info.GetIsolate()->GetCurrentContext(), info.Data())
            .FromJust());
  echo_indexed_call_count++;
  info.GetReturnValue().Set(v8_num(index));
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(IndexedPropertyHandlerGetter) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
  templ->InstanceTemplate()->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      EchoIndexedProperty, nullptr, nullptr, nullptr, nullptr, v8_num(637)));
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  Local<Script> script = v8_compile("obj[900]");
  CHECK_EQ(900, script->Run(env.local())
                    .ToLocalChecked()
                    ->Int32Value(env.local())
                    .FromJust());
}


THREADED_TEST(PropertyHandlerInPrototype) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
  templ->InstanceTemplate()->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      CheckThisIndexedPropertyHandler, CheckThisIndexedPropertySetter,
      CheckThisIndexedPropertyQuery, CheckThisIndexedPropertyDeleter,
      CheckThisIndexedPropertyEnumerator));

  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      CheckThisNamedPropertyHandler, CheckThisNamedPropertySetter,
      CheckThisNamedPropertyQuery, CheckThisNamedPropertyDeleter,
      CheckThisNamedPropertyEnumerator));

  Local<v8::Object> bottom = templ->GetFunction(env.local())
                                 .ToLocalChecked()
                                 ->NewInstance(env.local())
                                 .ToLocalChecked();
  bottom_global.Reset(isolate, bottom);
  Local<v8::Object> top = templ->GetFunction(env.local())
                              .ToLocalChecked()
                              ->NewInstance(env.local())
                              .ToLocalChecked();
  Local<v8::Object> middle = templ->GetFunction(env.local())
                                 .ToLocalChecked()
                                 ->NewInstance(env.local())
                                 .ToLocalChecked();

  bottom->SetPrototypeV2(env.local(), middle).FromJust();
  middle->SetPrototypeV2(env.local(), top).FromJust();
  env->Global()->Set(env.local(), v8_str("obj"), bottom).FromJust();

  // Indexed and named get.
  CompileRun("obj[0]");
  CompileRun("obj.x");

  // Indexed and named set.
  CompileRun("obj[1] = 42");
  CompileRun("obj.y = 42");

  // Indexed and named query.
  CompileRun("0 in obj");
  CompileRun("'x' in obj");

  // Indexed and named deleter.
  CompileRun("delete obj[0]");
  CompileRun("delete obj.x");

  // Enumerators.
  CompileRun("for (var p in obj) ;");

  bottom_global.Reset();
}

TEST(PropertyHandlerInPrototypeWithDefine) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
  templ->InstanceTemplate()->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      CheckThisIndexedPropertyHandler, CheckThisIndexedPropertySetter,
      CheckThisIndexedPropertyDescriptor, CheckThisIndexedPropertyDeleter,
      CheckThisIndexedPropertyEnumerator, CheckThisIndexedPropertyDefiner));

  templ->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      CheckThisNamedPropertyHandler, CheckThisNamedPropertySetter,
      CheckThisNamedPropertyDescriptor, CheckThisNamedPropertyDeleter,
      CheckThisNamedPropertyEnumerator, CheckThisNamedPropertyDefiner));

  Local<v8::Object> bottom = templ->GetFunction(env.local())
                                 .ToLocalChecked()
                                 ->NewInstance(env.local())
                                 .ToLocalChecked();
  bottom_global.Reset(isolate, bottom);
  Local<v8::Object> top = templ->GetFunction(env.local())
                              .ToLocalChecked()
                              ->NewInstance(env.local())
                              .ToLocalChecked();
  Local<v8::Object> middle = templ->GetFunction(env.local())
                                 .ToLocalChecked()
                                 ->NewInstance(env.local())
                                 .ToLocalChecked();

  bottom->SetPrototypeV2(env.local(), middle).FromJust();
  middle->SetPrototypeV2(env.local(), top).FromJust();
  env->Global()->Set(env.local(), v8_str("obj"), bottom).FromJust();

  // Indexed and named get.
  CompileRun("obj[0]");
  CompileRun("obj.x");

  // Indexed and named set.
  CompileRun("obj[1] = 42");
  CompileRun("obj.y = 42");

  // Indexed and named deleter.
  CompileRun("delete obj[0]");
  CompileRun("delete obj.x");

  // Enumerators.
  CompileRun("for (var p in obj) ;");

  // Indexed and named definer.
  CompileRun("Object.defineProperty(obj, 2, {});");
  CompileRun("Object.defineProperty(obj, 'z', {});");

  // Indexed and named propertyDescriptor.
  CompileRun("Object.getOwnPropertyDescriptor(obj, 2);");
  CompileRun("Object.getOwnPropertyDescriptor(obj, 'z');");

  bottom_global.Reset();
}

namespace {
bool is_bootstrapping = false;
v8::Intercepted PrePropertyHandlerGet(
    Local<Name> key, const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (!is_bootstrapping &&
      v8_str("pre")
          ->Equals(info.GetIsolate()->GetCurrentContext(), key)
          .FromJust()) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().Set(v8_str("PrePropertyHandler: pre"));
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

v8::Intercepted PrePropertyHandlerQuery(
    Local<Name> key, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  if (!is_bootstrapping &&
      v8_str("pre")
          ->Equals(info.GetIsolate()->GetCurrentContext(), key)
          .FromJust()) {
    info.GetReturnValue().Set(v8::None);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

THREADED_TEST(PrePropertyHandler) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::FunctionTemplate> desc = v8::FunctionTemplate::New(isolate);
  desc->InstanceTemplate()->SetHandler(v8::NamedPropertyHandlerConfiguration(
      PrePropertyHandlerGet, nullptr, PrePropertyHandlerQuery));
  is_bootstrapping = true;
  LocalContext env(nullptr, desc->InstanceTemplate());
  is_bootstrapping = false;
  CompileRun("var pre = 'Object: pre'; var on = 'Object: on';");
  v8::Local<Value> result_pre = CompileRun("pre");
  CHECK(v8_str("PrePropertyHandler: pre")
            ->Equals(env.local(), result_pre)
            .FromJust());
  v8::Local<Value> result_on = CompileRun("on");
  CHECK(v8_str("Object: on")->Equals(env.local(), result_on).FromJust());
  v8::Local<Value> result_post = CompileRun("post");
  CHECK(result_post.IsEmpty());
}


THREADED_TEST(EmptyInterceptorBreakTransitions) {
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(CcTest::isolate());
  AddInterceptor(templ, EmptyInterceptorGetter, EmptyInterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Constructor"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var o1 = new Constructor;"
      "o1.a = 1;"  // Ensure a and x share the descriptor array.
      "Object.defineProperty(o1, 'x', {value: 10});");
  CompileRun(
      "var o2 = new Constructor;"
      "o2.a = 1;"
      "Object.defineProperty(o2, 'x', {value: 10});");
}


THREADED_TEST(EmptyInterceptorDoesNotShadowJSAccessors) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> parent = FunctionTemplate::New(isolate);
  Local<FunctionTemplate> child = FunctionTemplate::New(isolate);
  child->Inherit(parent);
  AddInterceptor(child, EmptyInterceptorGetter, EmptyInterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var child = new Child;"
      "var parent = child.__proto__;"
      "Object.defineProperty(parent, 'age', "
      "  {get: function(){ return this.accessor_age; }, "
      "   set: function(v){ this.accessor_age = v; }, "
      "   enumerable: true, configurable: true});"
      "child.age = 10;");
  ExpectBoolean("child.hasOwnProperty('age')", false);
  ExpectInt32("child.age", 10);
  ExpectInt32("child.accessor_age", 10);
}


THREADED_TEST(EmptyInterceptorDoesNotShadowApiAccessors) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> parent = FunctionTemplate::New(isolate);
  auto returns_42 = FunctionTemplate::New(isolate, Returns42);
  parent->PrototypeTemplate()->SetAccessorProperty(v8_str("age"), returns_42);
  Local<FunctionTemplate> child = FunctionTemplate::New(isolate);
  child->Inherit(parent);
  AddInterceptor(child, EmptyInterceptorGetter, EmptyInterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var child = new Child;"
      "var parent = child.__proto__;");
  ExpectBoolean("child.hasOwnProperty('age')", false);
  ExpectInt32("child.age", 42);
  // Check interceptor followup.
  ExpectInt32(
      "var result;"
      "for (var i = 0; i < 4; ++i) {"
      "  result = child.age;"
      "}"
      "result",
      42);
}


THREADED_TEST(EmptyInterceptorDoesNotAffectJSProperties) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> parent = FunctionTemplate::New(isolate);
  Local<FunctionTemplate> child = FunctionTemplate::New(isolate);
  child->Inherit(parent);
  AddInterceptor(child, EmptyInterceptorGetter, EmptyInterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var child = new Child;"
      "var parent = child.__proto__;"
      "parent.name = 'Alice';");
  ExpectBoolean("child.hasOwnProperty('name')", false);
  ExpectString("child.name", "Alice");
  CompileRun("child.name = 'Bob';");
  ExpectString("child.name", "Bob");
  ExpectBoolean("child.hasOwnProperty('name')", true);
  ExpectString("parent.name", "Alice");
}


THREADED_TEST(SwitchFromInterceptorToAccessor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> templ = FunctionTemplate::New(isolate);
  AddAccessor(isolate, templ, v8_str("age"), SimpleGetterCallback,
              SimpleSetterCallback);
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Obj"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var obj = new Obj;"
      "function setAge(i){ obj.age = i; };"
      "for(var i = 0; i <= 10000; i++) setAge(i);");
  // All i < 10000 go to the interceptor.
  ExpectInt32("obj.interceptor_age", 9999);
  // The last i goes to the accessor.
  ExpectInt32("obj.accessor_age", 10000);
}


THREADED_TEST(SwitchFromAccessorToInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> templ = FunctionTemplate::New(isolate);
  AddAccessor(isolate, templ, v8_str("age"), SimpleGetterCallback,
              SimpleSetterCallback);
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Obj"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var obj = new Obj;"
      "function setAge(i){ obj.age = i; };"
      "for(var i = 20000; i >= 9999; i--) setAge(i);");
  // All i >= 10000 go to the accessor.
  ExpectInt32("obj.accessor_age", 10000);
  // The last i goes to the interceptor.
  ExpectInt32("obj.interceptor_age", 9999);
}


THREADED_TEST(SwitchFromInterceptorToAccessorWithInheritance) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> parent = FunctionTemplate::New(isolate);
  Local<FunctionTemplate> child = FunctionTemplate::New(isolate);
  child->Inherit(parent);
  AddAccessor(isolate, parent, v8_str("age"), SimpleGetterCallback,
              SimpleSetterCallback);
  AddInterceptor(child, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var child = new Child;"
      "function setAge(i){ child.age = i; };"
      "for(var i = 0; i <= 10000; i++) setAge(i);");
  // All i < 10000 go to the interceptor.
  ExpectInt32("child.interceptor_age", 9999);
  // The last i goes to the accessor.
  ExpectInt32("child.accessor_age", 10000);
}


THREADED_TEST(SwitchFromAccessorToInterceptorWithInheritance) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<FunctionTemplate> parent = FunctionTemplate::New(isolate);
  Local<FunctionTemplate> child = FunctionTemplate::New(isolate);
  child->Inherit(parent);
  AddAccessor(isolate, parent, v8_str("age"), SimpleGetterCallback,
              SimpleSetterCallback);
  AddInterceptor(child, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var child = new Child;"
      "function setAge(i){ child.age = i; };"
      "for(var i = 20000; i >= 9999; i--) setAge(i);");
  // All i >= 10000 go to the accessor.
  ExpectInt32("child.accessor_age", 10000);
  // The last i goes to the interceptor.
  ExpectInt32("child.interceptor_age", 9999);
}


THREADED_TEST(SwitchFromInterceptorToJSAccessor) {
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(CcTest::isolate());
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Obj"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var obj = new Obj;"
      "function setter(i) { this.accessor_age = i; };"
      "function getter() { return this.accessor_age; };"
      "function setAge(i) { obj.age = i; };"
      "Object.defineProperty(obj, 'age', { get:getter, set:setter });"
      "for(var i = 0; i <= 10000; i++) setAge(i);");
  // All i < 10000 go to the interceptor.
  ExpectInt32("obj.interceptor_age", 9999);
  // The last i goes to the JavaScript accessor.
  ExpectInt32("obj.accessor_age", 10000);
  // The installed JavaScript getter is still intact.
  // This last part is a regression test for issue 1651 and relies on the fact
  // that both interceptor and accessor are being installed on the same object.
  ExpectInt32("obj.age", 10000);
  ExpectBoolean("obj.hasOwnProperty('age')", true);
  ExpectUndefined("Object.getOwnPropertyDescriptor(obj, 'age').value");
}


THREADED_TEST(SwitchFromJSAccessorToInterceptor) {
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(CcTest::isolate());
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Obj"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var obj = new Obj;"
      "function setter(i) { this.accessor_age = i; };"
      "function getter() { return this.accessor_age; };"
      "function setAge(i) { obj.age = i; };"
      "Object.defineProperty(obj, 'age', { get:getter, set:setter });"
      "for(var i = 20000; i >= 9999; i--) setAge(i);");
  // All i >= 10000 go to the accessor.
  ExpectInt32("obj.accessor_age", 10000);
  // The last i goes to the interceptor.
  ExpectInt32("obj.interceptor_age", 9999);
  // The installed JavaScript getter is still intact.
  // This last part is a regression test for issue 1651 and relies on the fact
  // that both interceptor and accessor are being installed on the same object.
  ExpectInt32("obj.age", 10000);
  ExpectBoolean("obj.hasOwnProperty('age')", true);
  ExpectUndefined("Object.getOwnPropertyDescriptor(obj, 'age').value");
}


THREADED_TEST(SwitchFromInterceptorToProperty) {
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> parent = FunctionTemplate::New(CcTest::isolate());
  Local<FunctionTemplate> child = FunctionTemplate::New(CcTest::isolate());
  child->Inherit(parent);
  AddInterceptor(child, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var child = new Child;"
      "function setAge(i){ child.age = i; };"
      "for(var i = 0; i <= 10000; i++) setAge(i);");
  // All i < 10000 go to the interceptor.
  ExpectInt32("child.interceptor_age", 9999);
  // The last i goes to child's own property.
  ExpectInt32("child.age", 10000);
}


THREADED_TEST(SwitchFromPropertyToInterceptor) {
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> parent = FunctionTemplate::New(CcTest::isolate());
  Local<FunctionTemplate> child = FunctionTemplate::New(CcTest::isolate());
  child->Inherit(parent);
  AddInterceptor(child, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Child"),
            child->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var child = new Child;"
      "function setAge(i){ child.age = i; };"
      "for(var i = 20000; i >= 9999; i--) setAge(i);");
  // All i >= 10000 go to child's own property.
  ExpectInt32("child.age", 10000);
  // The last i goes to the interceptor.
  ExpectInt32("child.interceptor_age", 9999);
}

namespace {
bool interceptor_for_hidden_properties_called;
v8::Intercepted InterceptorForHiddenProperties(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  interceptor_for_hidden_properties_called = true;
  return v8::Intercepted::kNo;
}
}  // namespace

THREADED_TEST(NoSideEffectPropertyHandler) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  LocalContext context;

  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(
      EmptyInterceptorGetter, EmptyInterceptorSetter, EmptyInterceptorQuery,
      EmptyInterceptorDeleter, EmptyInterceptorEnumerator));
  v8::Local<v8::Object> object =
      templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), object).FromJust();

  CHECK(v8::debug::EvaluateGlobal(
            isolate, v8_str("obj.x"),
            v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
            .IsEmpty());
  CHECK(v8::debug::EvaluateGlobal(
            isolate, v8_str("obj.x = 1"),
            v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
            .IsEmpty());
  CHECK(v8::debug::EvaluateGlobal(
            isolate, v8_str("'x' in obj"),
            v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
            .IsEmpty());
  CHECK(v8::debug::EvaluateGlobal(
            isolate, v8_str("delete obj.x"),
            v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
            .IsEmpty());
  // Wrap the variable declaration since declaring globals is a side effect.
  CHECK(v8::debug::EvaluateGlobal(
            isolate, v8_str("(function() { for (var p in obj) ; })()"),
            v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
            .IsEmpty());

  // Side-effect-free version.
  Local<ObjectTemplate> templ2 = ObjectTemplate::New(isolate);
  templ2->SetHandler(v8::NamedPropertyHandlerConfiguration(
      EmptyInterceptorGetter, EmptyInterceptorSetter, EmptyInterceptorQuery,
      EmptyInterceptorDeleter, EmptyInterceptorEnumerator,
      v8::Local<v8::Value>(), v8::PropertyHandlerFlags::kHasNoSideEffect));
  v8::Local<v8::Object> object2 =
      templ2->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj2"), object2).FromJust();

  v8::debug::EvaluateGlobal(
      isolate, v8_str("obj2.x"),
      v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
      .ToLocalChecked();
  CHECK(v8::debug::EvaluateGlobal(
            isolate, v8_str("obj2.x = 1"),
            v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
            .IsEmpty());
  v8::debug::EvaluateGlobal(
      isolate, v8_str("'x' in obj2"),
      v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
      .ToLocalChecked();
  CHECK(v8::debug::EvaluateGlobal(
            isolate, v8_str("delete obj2.x"),
            v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
            .IsEmpty());
  v8::debug::EvaluateGlobal(
      isolate, v8_str("(function() { for (var p in obj2) ; })()"),
      v8::debug::EvaluateGlobalMode::kDisableBreaksAndThrowOnSideEffect)
      .ToLocalChecked();
}

THREADED_TEST(HiddenPropertiesWithInterceptors) {
  LocalContext context;
  v8::Isolate* isolate = context->GetIsolate();
  v8::HandleScope scope(isolate);

  interceptor_for_hidden_properties_called = false;

  v8::Local<v8::Private> key =
      v8::Private::New(isolate, v8_str("api-test::hidden-key"));

  // Associate an interceptor with an object and start setting hidden values.
  Local<v8::FunctionTemplate> fun_templ = v8::FunctionTemplate::New(isolate);
  Local<v8::ObjectTemplate> instance_templ = fun_templ->InstanceTemplate();
  instance_templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorForHiddenProperties));
  Local<v8::Function> function =
      fun_templ->GetFunction(context.local()).ToLocalChecked();
  Local<v8::Object> obj =
      function->NewInstance(context.local()).ToLocalChecked();
  CHECK(obj->SetPrivate(context.local(), key, v8::Integer::New(isolate, 2302))
            .FromJust());
  CHECK_EQ(2302, obj->GetPrivate(context.local(), key)
                     .ToLocalChecked()
                     ->Int32Value(context.local())
                     .FromJust());
  CHECK(!interceptor_for_hidden_properties_called);
}

namespace {
v8::Intercepted XPropertyGetter(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.Data()->IsUndefined());
  info.GetReturnValue().Set(property);
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(NamedInterceptorPropertyRead) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(XPropertyGetter));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  Local<Script> script = v8_compile("obj.x");
  for (int i = 0; i < 10; i++) {
    Local<Value> result = script->Run(context.local()).ToLocalChecked();
    CHECK(result->Equals(context.local(), v8_str("x")).FromJust());
  }
}


THREADED_TEST(NamedInterceptorDictionaryIC) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(XPropertyGetter));
  LocalContext context;
  // Create an object with a named interceptor.
  context->Global()
      ->Set(context.local(), v8_str("interceptor_obj"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  Local<Script> script = v8_compile("interceptor_obj.x");
  for (int i = 0; i < 10; i++) {
    Local<Value> result = script->Run(context.local()).ToLocalChecked();
    CHECK(result->Equals(context.local(), v8_str("x")).FromJust());
  }
  // Create a slow case object and a function accessing a property in
  // that slow case object (with dictionary probing in generated
  // code). Then force object with a named interceptor into slow-case,
  // pass it to the function, and check that the interceptor is called
  // instead of accessing the local property.
  Local<Value> result = CompileRun(
      "function get_x(o) { return o.x; };"
      "var obj = { x : 42, y : 0 };"
      "delete obj.y;"
      "for (var i = 0; i < 10; i++) get_x(obj);"
      "interceptor_obj.x = 42;"
      "interceptor_obj.y = 10;"
      "delete interceptor_obj.y;"
      "get_x(interceptor_obj)");
  CHECK(result->Equals(context.local(), v8_str("x")).FromJust());
}


THREADED_TEST(NamedInterceptorDictionaryICMultipleContext) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<Context> context1 = Context::New(isolate);

  context1->Enter();
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(XPropertyGetter));
  // Create an object with a named interceptor.
  v8::Local<v8::Object> object = templ->NewInstance(context1).ToLocalChecked();
  context1->Global()
      ->Set(context1, v8_str("interceptor_obj"), object)
      .FromJust();

  // Force the object into the slow case.
  CompileRun(
      "interceptor_obj.y = 0;"
      "delete interceptor_obj.y;");
  context1->Exit();

  {
    // Introduce the object into a different context.
    // Repeat named loads to exercise ICs.
    LocalContext context2;
    context2->Global()
        ->Set(context2.local(), v8_str("interceptor_obj"), object)
        .FromJust();
    Local<Value> result = CompileRun(
        "function get_x(o) { return o.x; }"
        "interceptor_obj.x = 42;"
        "for (var i=0; i != 10; i++) {"
        "  get_x(interceptor_obj);"
        "}"
        "get_x(interceptor_obj)");
    // Check that the interceptor was actually invoked.
    CHECK(result->Equals(context2.local(), v8_str("x")).FromJust());
  }

  // Return to the original context and force some object to the slow case
  // to cause the NormalizedMapCache to verify.
  context1->Enter();
  CompileRun("var obj = { x : 0 }; delete obj.x;");
  context1->Exit();
}

namespace {
v8::Intercepted SetXOnPrototypeGetter(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
  // Set x on the prototype object and do not handle the get request.
  v8::Local<v8::Value> proto = info.HolderV2()->GetPrototypeV2();
  proto.As<v8::Object>()
      ->Set(info.GetIsolate()->GetCurrentContext(), v8_str("x"),
            v8::Integer::New(info.GetIsolate(), 23))
      .FromJust();
  return v8::Intercepted::kNo;
}
}  // namespace

// This is a regression test for http://crbug.com/20104. Map
// transitions should not interfere with post interceptor lookup.
THREADED_TEST(NamedInterceptorMapTransitionRead) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<v8::FunctionTemplate> function_template =
      v8::FunctionTemplate::New(isolate);
  Local<v8::ObjectTemplate> instance_template =
      function_template->InstanceTemplate();
  instance_template->SetHandler(
      v8::NamedPropertyHandlerConfiguration(SetXOnPrototypeGetter));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("F"),
            function_template->GetFunction(context.local()).ToLocalChecked())
      .FromJust();
  // Create an instance of F and introduce a map transition for x.
  CompileRun("var o = new F(); o.x = 23;");
  // Create an instance of F and invoke the getter. The result should be 23.
  Local<Value> result = CompileRun("o = new F(); o.x");
  CHECK_EQ(23, result->Int32Value(context.local()).FromJust());
}

namespace {
v8::Intercepted IndexedPropertyGetter(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (index == 37) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().Set(v8_num(625));
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

v8::Intercepted IndexedPropertySetter(
    uint32_t index, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  if (index == 39) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

THREADED_TEST(IndexedInterceptorWithIndexedAccessor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      IndexedPropertyGetter, IndexedPropertySetter));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  Local<Script> getter_script =
      v8_compile("obj.__defineGetter__(\"3\", function(){return 5;});obj[3];");
  Local<Script> setter_script = v8_compile(
      "obj.__defineSetter__(\"17\", function(val){this.foo = val;});"
      "obj[17] = 23;"
      "obj.foo;");
  Local<Script> interceptor_setter_script = v8_compile(
      "obj.__defineSetter__(\"39\", function(val){this.foo = \"hit\";});"
      "obj[39] = 47;"
      "obj.foo;");  // This setter should not run, due to the interceptor.
  Local<Script> interceptor_getter_script = v8_compile("obj[37];");
  Local<Value> result = getter_script->Run(context.local()).ToLocalChecked();
  CHECK(v8_num(5)->Equals(context.local(), result).FromJust());
  result = setter_script->Run(context.local()).ToLocalChecked();
  CHECK(v8_num(23)->Equals(context.local(), result).FromJust());
  result = interceptor_setter_script->Run(context.local()).ToLocalChecked();
  CHECK(v8_num(23)->Equals(context.local(), result).FromJust());
  result = interceptor_getter_script->Run(context.local()).ToLocalChecked();
  CHECK(v8_num(625)->Equals(context.local(), result).FromJust());
}

namespace {
v8::Intercepted UnboxedDoubleIndexedPropertyGetter(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (index < 25) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().Set(v8_num(index));
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

v8::Intercepted UnboxedDoubleIndexedPropertySetter(
    uint32_t index, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  if (index < 25) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

void UnboxedDoubleIndexedPropertyEnumerator(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  // Force the list of returned keys to be stored in a FastDoubleArray.
  Local<Script> indexed_property_names_script = v8_compile(
      "keys = new Array(); keys[125000] = 1;"
      "for(i = 0; i < 80000; i++) { keys[i] = i; };"
      "keys.length = 25; keys;");
  Local<Value> result =
      indexed_property_names_script->Run(info.GetIsolate()->GetCurrentContext())
          .ToLocalChecked();
  info.GetReturnValue().Set(result.As<v8::Array>());
}
}  // namespace

// Make sure that the the interceptor code in the runtime properly handles
// merging property name lists for double-array-backed arrays.
THREADED_TEST(IndexedInterceptorUnboxedDoubleWithIndexedAccessor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      UnboxedDoubleIndexedPropertyGetter, UnboxedDoubleIndexedPropertySetter,
      nullptr, nullptr, UnboxedDoubleIndexedPropertyEnumerator));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  // When obj is created, force it to be Stored in a FastDoubleArray.
  Local<Script> create_unboxed_double_script = v8_compile(
      "obj[125000] = 1; for(i = 0; i < 80000; i+=2) { obj[i] = i; } "
      "key_count = 0; "
      "for (x in obj) {key_count++;};"
      "obj;");
  Local<Value> result =
      create_unboxed_double_script->Run(context.local()).ToLocalChecked();
  CHECK(result->ToObject(context.local())
            .ToLocalChecked()
            ->HasRealIndexedProperty(context.local(), 2000)
            .FromJust());
  Local<Script> key_count_check = v8_compile("key_count;");
  result = key_count_check->Run(context.local()).ToLocalChecked();
  CHECK(v8_num(40013)->Equals(context.local(), result).FromJust());
}

namespace {
void SloppyArgsIndexedPropertyEnumerator(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  // Force the list of returned keys to be stored in a Arguments object.
  Local<Script> indexed_property_names_script = v8_compile(
      "function f(w,x) {"
      " return arguments;"
      "}"
      "keys = f(0, 1, 2, 3);"
      "keys;");
  Local<Object> result =
      indexed_property_names_script->Run(info.GetIsolate()->GetCurrentContext())
          .ToLocalChecked()
          .As<Object>();
  // Have to populate the handle manually, as it's not Cast-able.
  i::DirectHandle<i::JSReceiver> o =
      v8::Utils::OpenDirectHandle<Object, i::JSReceiver>(result);
  i::Handle<i::JSArray> array(i::UncheckedCast<i::JSArray>(*o),
                              o->GetIsolate());
  info.GetReturnValue().Set(v8::Utils::ToLocal(array));
}

v8::Intercepted SloppyIndexedPropertyGetter(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  if (index < 4) {
    info.GetReturnValue().Set(v8_num(index));
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

// Make sure that the the interceptor code in the runtime properly handles
// merging property name lists for non-string arguments arrays.
THREADED_TEST(IndexedInterceptorSloppyArgsWithIndexedAccessor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      SloppyIndexedPropertyGetter, nullptr, nullptr, nullptr,
      SloppyArgsIndexedPropertyEnumerator));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  Local<Script> create_args_script = v8_compile(
      "var key_count = 0;"
      "for (x in obj) {key_count++;} key_count;");
  Local<Value> result =
      create_args_script->Run(context.local()).ToLocalChecked();
  CHECK(v8_num(4)->Equals(context.local(), result).FromJust());
}

namespace {
v8::Intercepted IdentityIndexedPropertyGetter(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(index);
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(IndexedInterceptorWithGetOwnPropertyDescriptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  // Check fast object case.
  const char* fast_case_code =
      "Object.getOwnPropertyDescriptor(obj, 0).value.toString()";
  ExpectString(fast_case_code, "0");

  // Check slow case.
  const char* slow_case_code =
      "obj.x = 1; delete obj.x;"
      "Object.getOwnPropertyDescriptor(obj, 1).value.toString()";
  ExpectString(slow_case_code, "1");
}


THREADED_TEST(IndexedInterceptorWithNoSetter) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  const char* code =
      "try {"
      "  obj[0] = 239;"
      "  for (var i = 0; i < 100; i++) {"
      "    var v = obj[0];"
      "    if (v != 0) throw 'Wrong value ' + v + ' at iteration ' + i;"
      "  }"
      "  'PASSED'"
      "} catch(e) {"
      "  e"
      "}";
  ExpectString(code, "PASSED");
}

static bool AccessAlwaysBlocked(Local<v8::Context> accessing_context,
                                Local<v8::Object> accessed_object,
                                Local<v8::Value> data) {
  return false;
}


THREADED_TEST(IndexedInterceptorWithAccessorCheck) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  templ->SetAccessCheckCallback(AccessAlwaysBlocked);

  LocalContext context;
  Local<v8::Object> obj = templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();

  const char* code =
      "var result = 'PASSED';"
      "for (var i = 0; i < 100; i++) {"
      "  try {"
      "    var v = obj[0];"
      "    result = 'Wrong value ' + v + ' at iteration ' + i;"
      "    break;"
      "  } catch (e) {"
      "    /* pass */"
      "  }"
      "}"
      "result";
  ExpectString(code, "PASSED");
}


THREADED_TEST(IndexedInterceptorWithDifferentIndices) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  LocalContext context;
  Local<v8::Object> obj = templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();

  const char* code =
      "try {"
      "  for (var i = 0; i < 100; i++) {"
      "    var v = obj[i];"
      "    if (v != i) throw 'Wrong value ' + v + ' at iteration ' + i;"
      "  }"
      "  'PASSED'"
      "} catch(e) {"
      "  e"
      "}";
  ExpectString(code, "PASSED");
}


THREADED_TEST(IndexedInterceptorWithNegativeIndices) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  LocalContext context;
  Local<v8::Object> obj = templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();

  const char* code =
      "try {"
      "  for (var i = 0; i < 100; i++) {"
      "    var expected = i;"
      "    var key = i;"
      "    if (i == 25) {"
      "       key = -1;"
      "       expected = undefined;"
      "    }"
      "    if (i == 50) {"
      "       /* probe minimal Smi number on 32-bit platforms */"
      "       key = -(1 << 30);"
      "       expected = undefined;"
      "    }"
      "    if (i == 75) {"
      "       /* probe minimal Smi number on 64-bit platforms */"
      "       key = 1 << 31;"
      "       expected = undefined;"
      "    }"
      "    var v = obj[key];"
      "    if (v != expected) throw 'Wrong value ' + v + ' at iteration ' + i;"
      "  }"
      "  'PASSED'"
      "} catch(e) {"
      "  e"
      "}";
  ExpectString(code, "PASSED");
}


THREADED_TEST(IndexedInterceptorWithNotSmiLookup) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  LocalContext context;
  Local<v8::Object> obj = templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();

  const char* code =
      "try {"
      "  for (var i = 0; i < 100; i++) {"
      "    var expected = i;"
      "    var key = i;"
      "    if (i == 50) {"
      "       key = 'foobar';"
      "       expected = undefined;"
      "    }"
      "    var v = obj[key];"
      "    if (v != expected) throw 'Wrong value ' + v + ' at iteration ' + i;"
      "  }"
      "  'PASSED'"
      "} catch(e) {"
      "  e"
      "}";
  ExpectString(code, "PASSED");
}


THREADED_TEST(IndexedInterceptorGoingMegamorphic) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  LocalContext context;
  Local<v8::Object> obj = templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();

  const char* code =
      "var original = obj;"
      "try {"
      "  for (var i = 0; i < 100; i++) {"
      "    var expected = i;"
      "    if (i == 50) {"
      "       obj = {50: 'foobar'};"
      "       expected = 'foobar';"
      "    }"
      "    var v = obj[i];"
      "    if (v != expected) throw 'Wrong value ' + v + ' at iteration ' + i;"
      "    if (i == 50) obj = original;"
      "  }"
      "  'PASSED'"
      "} catch(e) {"
      "  e"
      "}";
  ExpectString(code, "PASSED");
}


THREADED_TEST(IndexedInterceptorReceiverTurningSmi) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  LocalContext context;
  Local<v8::Object> obj = templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();

  const char* code =
      "var original = obj;"
      "try {"
      "  for (var i = 0; i < 100; i++) {"
      "    var expected = i;"
      "    if (i == 5) {"
      "       obj = 239;"
      "       expected = undefined;"
      "    }"
      "    var v = obj[i];"
      "    if (v != expected) throw 'Wrong value ' + v + ' at iteration ' + i;"
      "    if (i == 5) obj = original;"
      "  }"
      "  'PASSED'"
      "} catch(e) {"
      "  e"
      "}";
  ExpectString(code, "PASSED");
}


THREADED_TEST(IndexedInterceptorOnProto) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  Local<ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::IndexedPropertyHandlerConfiguration(IdentityIndexedPropertyGetter));

  LocalContext context;
  Local<v8::Object> obj = templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();

  const char* code =
      "var o = {__proto__: obj};"
      "try {"
      "  for (var i = 0; i < 100; i++) {"
      "    var v = o[i];"
      "    if (v != i) throw 'Wrong value ' + v + ' at iteration ' + i;"
      "  }"
      "  'PASSED'"
      "} catch(e) {"
      "  e"
      "}";
  ExpectString(code, "PASSED");
}

namespace {

void CheckIndexedInterceptorHasIC(v8::IndexedPropertyGetterCallbackV2 getter,
                                  v8::IndexedPropertyQueryCallbackV2 query,
                                  const char* source, int expected) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      getter, nullptr, query, nullptr, nullptr, v8_str("data")));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> value = CompileRun(source);
  CHECK_EQ(expected, value->Int32Value(context.local()).FromJust());
}

int indexed_query_counter = 0;
v8::Intercepted IndexedQueryCallback(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  indexed_query_counter++;
  return v8::Intercepted::kNo;
}

v8::Intercepted IndexHasICQueryAbsent(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  // The request is not intercepted so don't call ApiTestFuzzer::Fuzz() here.
  v8::Isolate* isolate = CcTest::isolate();
  CHECK_EQ(isolate, info.GetIsolate());
  // TODO(ishell): the PropertyAttributes::ABSENT is not exposed in the Api,
  // so it can't be officially returned. We should fix the tests instead.
  info.GetReturnValue().Set(v8::internal::ABSENT);
  return v8::Intercepted::kNo;
}

}  // namespace

THREADED_TEST(IndexedInterceptorHasIC) {
  indexed_query_counter = 0;
  CheckIndexedInterceptorHasIC(nullptr, IndexedQueryCallback,
                               "var result = 0;"
                               "for (var i = 0; i < 1000; i++) {"
                               "  i in o;"
                               "}",
                               0);
  CHECK_EQ(1000, indexed_query_counter);
}

THREADED_TEST(IndexedInterceptorHasICQueryAbsent) {
  CheckIndexedInterceptorHasIC(nullptr,
                               // HasICQuery<uint32_t, v8::internal::ABSENT>,
                               IndexHasICQueryAbsent,
                               "var result = 0;"
                               "for (var i = 0; i < 1000; i++) {"
                               "  if (i in o) ++result;"
                               "}",
                               0);
}

THREADED_TEST(IndexedInterceptorHasICQueryNone) {
  CheckIndexedInterceptorHasIC(nullptr,
                               HasICQuery<uint32_t, v8::internal::NONE>,
                               "var result = 0;"
                               "for (var i = 0; i < 1000; i++) {"
                               "  if (i in o) ++result;"
                               "}",
                               1000);
}

THREADED_TEST(IndexedInterceptorHasICGetter) {
  CheckIndexedInterceptorHasIC(IdentityIndexedPropertyGetter, nullptr,
                               "var result = 0;"
                               "for (var i = 0; i < 1000; i++) {"
                               "  if (i in o) ++result;"
                               "}",
                               1000);
}

THREADED_TEST(IndexedInterceptorHasICQueryGetter) {
  CheckIndexedInterceptorHasIC(IdentityIndexedPropertyGetter,
                               HasICQuery<uint32_t, v8::internal::ABSENT>,
                               "var result = 0;"
                               "for (var i = 0; i < 1000; i++) {"
                               "  if (i in o) ++result;"
                               "}",
                               0);
}

THREADED_TEST(IndexedInterceptorHasICQueryToggle) {
  CheckIndexedInterceptorHasIC(IdentityIndexedPropertyGetter,
                               HasICQueryToggle<uint32_t>,
                               "var result = 0;"
                               "for (var i = 0; i < 1000; i++) {"
                               "  if (i in o) ++result;"
                               "}",
                               500);
}

namespace {
v8::Intercepted NoBlockGetterX(Local<Name> name,
                               const v8::PropertyCallbackInfo<v8::Value>&) {
  return v8::Intercepted::kNo;
}

v8::Intercepted NoBlockGetterI(uint32_t index,
                               const v8::PropertyCallbackInfo<v8::Value>&) {
  return v8::Intercepted::kNo;
}

v8::Intercepted PDeleter(Local<Name> name,
                         const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  if (!name->Equals(info.GetIsolate()->GetCurrentContext(), v8_str("foo"))
           .FromJust()) {
    return v8::Intercepted::kNo;
  }

  // Intercepted, but property deletion failed.
  info.GetReturnValue().Set(false);
  return v8::Intercepted::kYes;
}

v8::Intercepted IDeleter(uint32_t index,
                         const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  if (index != 2) {
    return v8::Intercepted::kNo;
  }

  // Intercepted, but property deletion failed.
  info.GetReturnValue().Set(false);
  return v8::Intercepted::kYes;
}
}  // namespace

THREADED_TEST(Deleter) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> obj = ObjectTemplate::New(isolate);
  obj->SetHandler(v8::NamedPropertyHandlerConfiguration(
      NoBlockGetterX, nullptr, nullptr, PDeleter, nullptr));
  obj->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      NoBlockGetterI, nullptr, nullptr, IDeleter, nullptr));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("k"),
            obj->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "k.foo = 'foo';"
      "k.bar = 'bar';"
      "k[2] = 2;"
      "k[4] = 4;");
  CHECK(v8_compile("delete k.foo")
            ->Run(context.local())
            .ToLocalChecked()
            ->IsFalse());
  CHECK(v8_compile("delete k.bar")
            ->Run(context.local())
            .ToLocalChecked()
            ->IsTrue());

  CHECK(v8_compile("k.foo")
            ->Run(context.local())
            .ToLocalChecked()
            ->Equals(context.local(), v8_str("foo"))
            .FromJust());
  CHECK(v8_compile("k.bar")
            ->Run(context.local())
            .ToLocalChecked()
            ->IsUndefined());

  CHECK(v8_compile("delete k[2]")
            ->Run(context.local())
            .ToLocalChecked()
            ->IsFalse());
  CHECK(v8_compile("delete k[4]")
            ->Run(context.local())
            .ToLocalChecked()
            ->IsTrue());

  CHECK(v8_compile("k[2]")
            ->Run(context.local())
            .ToLocalChecked()
            ->Equals(context.local(), v8_num(2))
            .FromJust());
  CHECK(
      v8_compile("k[4]")->Run(context.local()).ToLocalChecked()->IsUndefined());
}

namespace {
v8::Intercepted GetK(Local<Name> name,
                     const v8::PropertyCallbackInfo<v8::Value>& info) {
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
  if (name->Equals(context, v8_str("foo")).FromJust() ||
      name->Equals(context, v8_str("bar")).FromJust() ||
      name->Equals(context, v8_str("baz")).FromJust()) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().SetUndefined();
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}

v8::Intercepted IndexedGetK(uint32_t index,
                            const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (index == 0 || index == 1) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().SetUndefined();
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

static void NamedEnum(const v8::PropertyCallbackInfo<v8::Array>& info) {
  ApiTestFuzzer::Fuzz();
  v8::Local<v8::Array> result = v8::Array::New(info.GetIsolate(), 3);
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
  CHECK(
      result
          ->Set(context, v8::Integer::New(info.GetIsolate(), 0), v8_str("foo"))
          .FromJust());
  CHECK(
      result
          ->Set(context, v8::Integer::New(info.GetIsolate(), 1), v8_str("bar"))
          .FromJust());
  CHECK(
      result
          ->Set(context, v8::Integer::New(info.GetIsolate(), 2), v8_str("baz"))
          .FromJust());
  info.GetReturnValue().Set(result);
}


static void IndexedEnum(const v8::PropertyCallbackInfo<v8::Array>& info) {
  ApiTestFuzzer::Fuzz();
  v8::Local<v8::Array> result = v8::Array::New(info.GetIsolate(), 2);
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
  CHECK(
      result->Set(context, v8::Integer::New(info.GetIsolate(), 0), v8_str("0"))
          .FromJust());
  CHECK(
      result->Set(context, v8::Integer::New(info.GetIsolate(), 1), v8_str("1"))
          .FromJust());
  info.GetReturnValue().Set(result);
}


THREADED_TEST(Enumerators) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> obj = ObjectTemplate::New(isolate);
  obj->SetHandler(v8::NamedPropertyHandlerConfiguration(GetK, nullptr, nullptr,
                                                        nullptr, NamedEnum));
  obj->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      IndexedGetK, nullptr, nullptr, nullptr, IndexedEnum));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("k"),
            obj->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<v8::Array> result = CompileRun(
                                    "k[10] = 0;"
                                    "k.a = 0;"
                                    "k[5] = 0;"
                                    "k.b = 0;"
                                    "k[4294967294] = 0;"
                                    "k.c = 0;"
                                    "k[4294967295] = 0;"
                                    "k.d = 0;"
                                    "k[140000] = 0;"
                                    "k.e = 0;"
                                    "k[30000000000] = 0;"
                                    "k.f = 0;"
                                    "var result = [];"
                                    "for (var prop in k) {"
                                    "  result.push(prop);"
                                    "}"
                                    "result")
                                    .As<v8::Array>();
  // Check that we get all the property names returned including the
  // ones from the enumerators in the right order: indexed properties
  // in numerical order, indexed interceptor properties, named
  // properties in insertion order, named interceptor properties.
  // This order is not mandated by the spec, so this test is just
  // documenting our behavior.
  CHECK_EQ(17u, result->Length());
  // Indexed properties.
  CHECK(v8_str("5")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 0))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("10")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 1))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("140000")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 2))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("4294967294")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 3))
                         .ToLocalChecked())
            .FromJust());
  // Indexed Interceptor properties
  CHECK(v8_str("0")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 4))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("1")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 5))
                         .ToLocalChecked())
            .FromJust());
  // Named properties in insertion order.
  CHECK(v8_str("a")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 6))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("b")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 7))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("c")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 8))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("4294967295")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 9))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("d")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 10))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("e")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 11))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("30000000000")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 12))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("f")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 13))
                         .ToLocalChecked())
            .FromJust());
  // Named interceptor properties.
  CHECK(v8_str("foo")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 14))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("bar")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 15))
                         .ToLocalChecked())
            .FromJust());
  CHECK(v8_str("baz")
            ->Equals(context.local(),
                     result->Get(context.local(), v8::Integer::New(isolate, 16))
                         .ToLocalChecked())
            .FromJust());
}

namespace {
v8::Global<Value> call_ic_function_global;
v8::Global<Value> call_ic_function2_global;
v8::Global<Value> call_ic_function3_global;

v8::Intercepted InterceptorCallICGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(v8_str("x")
            ->Equals(info.GetIsolate()->GetCurrentContext(), name)
            .FromJust());
  info.GetReturnValue().Set(call_ic_function_global);
  return v8::Intercepted::kYes;
}
}  // namespace

// This test should hit the call IC for the interceptor case.
THREADED_TEST(InterceptorCallIC) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorCallICGetter));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  Local<Value> call_ic_function =
      v8_compile("function f(x) { return x + 1; }; f")
          ->Run(context.local())
          .ToLocalChecked();
  call_ic_function_global.Reset(isolate, call_ic_function);
  v8::Local<Value> value = CompileRun(
      "var result = 0;"
      "for (var i = 0; i < 1000; i++) {"
      "  result = o.x(41);"
      "}");
  CHECK_EQ(42, value->Int32Value(context.local()).FromJust());
  call_ic_function_global.Reset();
}


// This test checks that if interceptor doesn't provide
// a value, we can fetch regular value.
THREADED_TEST(InterceptorCallICSeesOthers) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> value = CompileRun(
      "o.x = function f(x) { return x + 1; };"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result = o.x(41);"
      "}");
  CHECK_EQ(42, value->Int32Value(context.local()).FromJust());
}

namespace {
v8::Global<Value> call_ic_function4_global;
v8::Intercepted InterceptorCallICGetter4(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(v8_str("x")
            ->Equals(info.GetIsolate()->GetCurrentContext(), name)
            .FromJust());
  info.GetReturnValue().Set(call_ic_function4_global);
  return v8::Intercepted::kYes;
}
}  // namespace

// This test checks that if interceptor provides a function,
// even if we cached shadowed variant, interceptor's function
// is invoked
THREADED_TEST(InterceptorCallICCacheableNotNeeded) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorCallICGetter4));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> call_ic_function4 =
      v8_compile("function f(x) { return x - 1; }; f")
          ->Run(context.local())
          .ToLocalChecked();
  call_ic_function4_global.Reset(isolate, call_ic_function4);
  v8::Local<Value> value = CompileRun(
      "Object.getPrototypeOf(o).x = function(x) { return x + 1; };"
      "var result = 0;"
      "for (var i = 0; i < 1000; i++) {"
      "  result = o.x(42);"
      "}");
  CHECK_EQ(41, value->Int32Value(context.local()).FromJust());
  call_ic_function4_global.Reset();
}


// Test the case when we stored cacheable lookup into
// a stub, but it got invalidated later on
THREADED_TEST(InterceptorCallICInvalidatedCacheable) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> value = CompileRun(
      "proto1 = new Object();"
      "proto2 = new Object();"
      "o.__proto__ = proto1;"
      "proto1.__proto__ = proto2;"
      "proto2.y = function(x) { return x + 1; };"
      // Invoke it many times to compile a stub
      "for (var i = 0; i < 7; i++) {"
      "  o.y(42);"
      "}"
      "proto1.y = function(x) { return x - 1; };"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result += o.y(42);"
      "}");
  CHECK_EQ(41 * 7, value->Int32Value(context.local()).FromJust());
}


// This test checks that if interceptor doesn't provide a function,
// cached constant function is used
THREADED_TEST(InterceptorCallICConstantFunctionUsed) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> value = CompileRun(
      "function inc(x) { return x + 1; };"
      "inc(1);"
      "o.x = inc;"
      "var result = 0;"
      "for (var i = 0; i < 1000; i++) {"
      "  result = o.x(42);"
      "}");
  CHECK_EQ(43, value->Int32Value(context.local()).FromJust());
}

namespace {
v8::Global<Value> call_ic_function5_global;
v8::Intercepted InterceptorCallICGetter5(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  v8::Isolate* isolate = info.GetIsolate();
  if (v8_str("x")->Equals(isolate->GetCurrentContext(), name).FromJust()) {
    info.GetReturnValue().Set(call_ic_function5_global);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

// This test checks that if interceptor provides a function,
// even if we cached constant function, interceptor's function
// is invoked
THREADED_TEST(InterceptorCallICConstantFunctionNotNeeded) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorCallICGetter5));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> call_ic_function5 =
      v8_compile("function f(x) { return x - 1; }; f")
          ->Run(context.local())
          .ToLocalChecked();
  call_ic_function5_global.Reset(isolate, call_ic_function5);
  v8::Local<Value> value = CompileRun(
      "function inc(x) { return x + 1; };"
      "inc(1);"
      "o.x = inc;"
      "var result = 0;"
      "for (var i = 0; i < 1000; i++) {"
      "  result = o.x(42);"
      "}");
  CHECK_EQ(41, value->Int32Value(context.local()).FromJust());
  call_ic_function5_global.Reset();
}

namespace {
v8::Global<Value> call_ic_function6_global;
v8::Intercepted InterceptorCallICGetter6(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  v8::Isolate* isolate = info.GetIsolate();
  if (v8_str("x")->Equals(isolate->GetCurrentContext(), name).FromJust()) {
    info.GetReturnValue().Set(call_ic_function6_global);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

// Same test as above, except the code is wrapped in a function
// to test the optimized compiler.
THREADED_TEST(InterceptorCallICConstantFunctionNotNeededWrapped) {
  i::v8_flags.allow_natives_syntax = true;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorCallICGetter6));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> call_ic_function6 =
      v8_compile("function f(x) { return x - 1; }; f")
          ->Run(context.local())
          .ToLocalChecked();
  call_ic_function6_global.Reset(isolate, call_ic_function6);
  v8::Local<Value> value = CompileRun(
      "function inc(x) { return x + 1; };"
      "inc(1);"
      "o.x = inc;"
      "function test() {"
      "  var result = 0;"
      "  for (var i = 0; i < 1000; i++) {"
      "    result = o.x(42);"
      "  }"
      "  return result;"
      "};"
      "%PrepareFunctionForOptimization(test);"
      "test();"
      "test();"
      "test();"
      "%OptimizeFunctionOnNextCall(test);"
      "test()");
  CHECK_EQ(41, value->Int32Value(context.local()).FromJust());
  call_ic_function6_global.Reset();
}


// Test the case when we stored constant function into
// a stub, but it got invalidated later on
THREADED_TEST(InterceptorCallICInvalidatedConstantFunction) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> value = CompileRun(
      "function inc(x) { return x + 1; };"
      "inc(1);"
      "proto1 = new Object();"
      "proto2 = new Object();"
      "o.__proto__ = proto1;"
      "proto1.__proto__ = proto2;"
      "proto2.y = inc;"
      // Invoke it many times to compile a stub
      "for (var i = 0; i < 7; i++) {"
      "  o.y(42);"
      "}"
      "proto1.y = function(x) { return x - 1; };"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result += o.y(42);"
      "}");
  CHECK_EQ(41 * 7, value->Int32Value(context.local()).FromJust());
}


// Test the case when we stored constant function into
// a stub, but it got invalidated later on due to override on
// global object which is between interceptor and constant function' holders.
THREADED_TEST(InterceptorCallICInvalidatedConstantFunctionViaGlobal) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<Value> value = CompileRun(
      "function inc(x) { return x + 1; };"
      "inc(1);"
      "o.__proto__ = this;"
      "this.__proto__.y = inc;"
      // Invoke it many times to compile a stub
      "for (var i = 0; i < 7; i++) {"
      "  if (o.y(42) != 43) throw 'oops: ' + o.y(42);"
      "}"
      "this.y = function(x) { return x - 1; };"
      "var result = 0;"
      "for (var i = 0; i < 7; i++) {"
      "  result += o.y(42);"
      "}");
  CHECK_EQ(41 * 7, value->Int32Value(context.local()).FromJust());
}


// Test the case when actual function to call sits on global object.
THREADED_TEST(InterceptorCallICCachedFromGlobal) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_o = ObjectTemplate::New(isolate);
  templ_o->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));

  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ_o->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  v8::Local<Value> value = CompileRun(
      "try {"
      "  o.__proto__ = this;"
      "  for (var i = 0; i < 10; i++) {"
      "    var v = o.parseFloat('239');"
      "    if (v != 239) throw v;"
      // Now it should be ICed and keep a reference to parseFloat.
      "  }"
      "  var result = 0;"
      "  for (var i = 0; i < 10; i++) {"
      "    result += o.parseFloat('239');"
      "  }"
      "  result"
      "} catch(e) {"
      "  e"
      "};");
  CHECK_EQ(239 * 10, value->Int32Value(context.local()).FromJust());
}

namespace {
v8::Global<Value> keyed_call_ic_function_global;

v8::Intercepted InterceptorKeyedCallICGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  ApiTestFuzzer::Fuzz();
  if (v8_str("x")
          ->Equals(info.GetIsolate()->GetCurrentContext(), name)
          .FromJust()) {
    info.GetReturnValue().Set(keyed_call_ic_function_global);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

// Test the case when we stored cacheable lookup into
// a stub, but the function name changed (to another cacheable function).
THREADED_TEST(InterceptorKeyedCallICKeyChange1) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "proto = new Object();"
      "proto.y = function(x) { return x + 1; };"
      "proto.z = function(x) { return x - 1; };"
      "o.__proto__ = proto;"
      "var result = 0;"
      "var method = 'y';"
      "for (var i = 0; i < 10; i++) {"
      "  if (i == 5) { method = 'z'; };"
      "  result += o[method](41);"
      "}");
  CHECK_EQ(42 * 5 + 40 * 5, context->Global()
                                ->Get(context.local(), v8_str("result"))
                                .ToLocalChecked()
                                ->Int32Value(context.local())
                                .FromJust());
}


// Test the case when we stored cacheable lookup into
// a stub, but the function name changed (and the new function is present
// both before and after the interceptor in the prototype chain).
THREADED_TEST(InterceptorKeyedCallICKeyChange2) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorKeyedCallICGetter));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("proto1"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<v8::Value> keyed_call_ic_function =
      v8_compile("function f(x) { return x - 1; }; f")
          ->Run(context.local())
          .ToLocalChecked();
  keyed_call_ic_function_global.Reset(isolate, keyed_call_ic_function);
  CompileRun(
      "o = new Object();"
      "proto2 = new Object();"
      "o.y = function(x) { return x + 1; };"
      "proto2.y = function(x) { return x + 2; };"
      "o.__proto__ = proto1;"
      "proto1.__proto__ = proto2;"
      "var result = 0;"
      "var method = 'x';"
      "for (var i = 0; i < 10; i++) {"
      "  if (i == 5) { method = 'y'; };"
      "  result += o[method](41);"
      "}");
  CHECK_EQ(42 * 5 + 40 * 5, context->Global()
                                ->Get(context.local(), v8_str("result"))
                                .ToLocalChecked()
                                ->Int32Value(context.local())
                                .FromJust());
  keyed_call_ic_function_global.Reset();
}


// Same as InterceptorKeyedCallICKeyChange1 only the cacheable function sit
// on the global object.
THREADED_TEST(InterceptorKeyedCallICKeyChangeOnGlobal) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "function inc(x) { return x + 1; };"
      "inc(1);"
      "function dec(x) { return x - 1; };"
      "dec(1);"
      "o.__proto__ = this;"
      "this.__proto__.x = inc;"
      "this.__proto__.y = dec;"
      "var result = 0;"
      "var method = 'x';"
      "for (var i = 0; i < 10; i++) {"
      "  if (i == 5) { method = 'y'; };"
      "  result += o[method](41);"
      "}");
  CHECK_EQ(42 * 5 + 40 * 5, context->Global()
                                ->Get(context.local(), v8_str("result"))
                                .ToLocalChecked()
                                ->Int32Value(context.local())
                                .FromJust());
}


// Test the case when actual function to call sits on global object.
THREADED_TEST(InterceptorKeyedCallICFromGlobal) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_o = ObjectTemplate::New(isolate);
  templ_o->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ_o->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  CompileRun(
      "function len(x) { return x.length; };"
      "o.__proto__ = this;"
      "var m = 'parseFloat';"
      "var result = 0;"
      "for (var i = 0; i < 10; i++) {"
      "  if (i == 5) {"
      "    m = 'len';"
      "    saved_result = result;"
      "  };"
      "  result = o[m]('239');"
      "}");
  CHECK_EQ(3, context->Global()
                  ->Get(context.local(), v8_str("result"))
                  .ToLocalChecked()
                  ->Int32Value(context.local())
                  .FromJust());
  CHECK_EQ(239, context->Global()
                    ->Get(context.local(), v8_str("saved_result"))
                    .ToLocalChecked()
                    ->Int32Value(context.local())
                    .FromJust());
}


// Test the map transition before the interceptor.
THREADED_TEST(InterceptorKeyedCallICMapChangeBefore) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_o = ObjectTemplate::New(isolate);
  templ_o->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("proto"),
            templ_o->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  CompileRun(
      "var o = new Object();"
      "o.__proto__ = proto;"
      "o.method = function(x) { return x + 1; };"
      "var m = 'method';"
      "var result = 0;"
      "for (var i = 0; i < 10; i++) {"
      "  if (i == 5) { o.method = function(x) { return x - 1; }; };"
      "  result += o[m](41);"
      "}");
  CHECK_EQ(42 * 5 + 40 * 5, context->Global()
                                ->Get(context.local(), v8_str("result"))
                                .ToLocalChecked()
                                ->Int32Value(context.local())
                                .FromJust());
}


// Test the map transition after the interceptor.
THREADED_TEST(InterceptorKeyedCallICMapChangeAfter) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ_o = ObjectTemplate::New(isolate);
  templ_o->SetHandler(v8::NamedPropertyHandlerConfiguration(NoBlockGetterX));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("o"),
            templ_o->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  CompileRun(
      "var proto = new Object();"
      "o.__proto__ = proto;"
      "proto.method = function(x) { return x + 1; };"
      "var m = 'method';"
      "var result = 0;"
      "for (var i = 0; i < 10; i++) {"
      "  if (i == 5) { proto.method = function(x) { return x - 1; }; };"
      "  result += o[m](41);"
      "}");
  CHECK_EQ(42 * 5 + 40 * 5, context->Global()
                                ->Get(context.local(), v8_str("result"))
                                .ToLocalChecked()
                                ->Int32Value(context.local())
                                .FromJust());
}

namespace {
int interceptor_call_count = 0;

v8::Intercepted InterceptorICRefErrorGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (!is_bootstrapping &&
      v8_str("x")
          ->Equals(info.GetIsolate()->GetCurrentContext(), name)
          .FromJust() &&
      interceptor_call_count++ < 20) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().Set(call_ic_function2_global);
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

// This test should hit load and call ICs for the interceptor case.
// Once in a while, the interceptor will reply that a property was not
// found in which case we should get a reference error.
THREADED_TEST(InterceptorICReferenceErrors) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorICRefErrorGetter));
  is_bootstrapping = true;
  LocalContext context(nullptr, templ, v8::Local<Value>());
  is_bootstrapping = false;
  v8::Local<Value> call_ic_function2 =
      v8_compile("function h(x) { return x; }; h")
          ->Run(context.local())
          .ToLocalChecked();
  call_ic_function2_global.Reset(isolate, call_ic_function2);
  v8::Local<Value> value = CompileRun(
      "function f() {"
      "  for (var i = 0; i < 1000; i++) {"
      "    try { x; } catch(e) { return true; }"
      "  }"
      "  return false;"
      "};"
      "f();");
  CHECK(value->BooleanValue(isolate));
  interceptor_call_count = 0;
  value = CompileRun(
      "function g() {"
      "  for (var i = 0; i < 1000; i++) {"
      "    try { x(42); } catch(e) { return true; }"
      "  }"
      "  return false;"
      "};"
      "g();");
  CHECK(value->BooleanValue(isolate));
  call_ic_function2_global.Reset();
}

namespace {
int interceptor_ic_exception_get_count = 0;

v8::Intercepted InterceptorICExceptionGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  if (is_bootstrapping) return v8::Intercepted::kNo;
  if (v8_str("x")
          ->Equals(info.GetIsolate()->GetCurrentContext(), name)
          .FromJust() &&
      ++interceptor_ic_exception_get_count < 20) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetReturnValue().Set(call_ic_function3_global);
  }
  if (interceptor_ic_exception_get_count == 20) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetIsolate()->ThrowException(v8_num(42));
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

// Test interceptor load/call IC where the interceptor throws an
// exception once in a while.
THREADED_TEST(InterceptorICGetterExceptions) {
  interceptor_ic_exception_get_count = 0;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorICExceptionGetter));
  is_bootstrapping = true;
  LocalContext context(nullptr, templ, v8::Local<Value>());
  is_bootstrapping = false;
  v8::Local<Value> call_ic_function3 =
      v8_compile("function h(x) { return x; }; h")
          ->Run(context.local())
          .ToLocalChecked();
  call_ic_function3_global.Reset(isolate, call_ic_function3);
  v8::Local<Value> value = CompileRun(
      "function f() {"
      "  for (var i = 0; i < 100; i++) {"
      "    try { x; } catch(e) { return true; }"
      "  }"
      "  return false;"
      "};"
      "f();");
  CHECK(value->BooleanValue(isolate));
  interceptor_ic_exception_get_count = 0;
  value = CompileRun(
      "function f() {"
      "  for (var i = 0; i < 100; i++) {"
      "    try { x(42); } catch(e) { return true; }"
      "  }"
      "  return false;"
      "};"
      "f();");
  CHECK(value->BooleanValue(isolate));
  call_ic_function3_global.Reset();
}

namespace {
int interceptor_ic_exception_set_count = 0;

v8::Intercepted InterceptorICExceptionSetter(
    Local<Name> key, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  if (++interceptor_ic_exception_set_count > 20) {
    // Side effects are allowed only when the property is present or throws.
    ApiTestFuzzer::Fuzz();
    info.GetIsolate()->ThrowException(v8_num(42));
    return v8::Intercepted::kYes;
  }
  return v8::Intercepted::kNo;
}
}  // namespace

// Test interceptor store IC where the interceptor throws an exception
// once in a while.
THREADED_TEST(InterceptorICSetterExceptions) {
  interceptor_ic_exception_set_count = 0;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(
      nullptr, InterceptorICExceptionSetter));
  LocalContext context(nullptr, templ, v8::Local<Value>());
  v8::Local<Value> value = CompileRun(
      "function f() {"
      "  for (var i = 0; i < 100; i++) {"
      "    try { x = 42; } catch(e) { return true; }"
      "  }"
      "  return false;"
      "};"
      "f();");
  CHECK(value->BooleanValue(isolate));
}


// Test that we ignore null interceptors.
THREADED_TEST(NullNamedInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::NamedPropertyHandlerConfiguration(
      static_cast<v8::NamedPropertyGetterCallback>(nullptr)));
  LocalContext context;
  templ->Set(CcTest::isolate(), "x", v8_num(42));
  v8::Local<v8::Object> obj =
      templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();
  v8::Local<Value> value = CompileRun("obj.x");
  CHECK(value->IsInt32());
  CHECK_EQ(42, value->Int32Value(context.local()).FromJust());
}


// Test that we ignore null interceptors.
THREADED_TEST(NullIndexedInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> templ = ObjectTemplate::New(isolate);
  templ->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      static_cast<v8::IndexedPropertyGetterCallbackV2>(nullptr)));
  LocalContext context;
  templ->Set(CcTest::isolate(), "42", v8_num(42));
  v8::Local<v8::Object> obj =
      templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()->Set(context.local(), v8_str("obj"), obj).FromJust();
  v8::Local<Value> value = CompileRun("obj[42]");
  CHECK(value->IsInt32());
  CHECK_EQ(42, value->Int32Value(context.local()).FromJust());
}


THREADED_TEST(NamedPropertyHandlerGetterAttributes) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(isolate);
  templ->InstanceTemplate()->SetHandler(
      v8::NamedPropertyHandlerConfiguration(InterceptorLoadXICGetter));
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("obj"), templ->GetFunction(env.local())
                                            .ToLocalChecked()
                                            ->NewInstance(env.local())
                                            .ToLocalChecked())
      .FromJust();
  ExpectTrue("obj.x === 42");
  ExpectTrue("!obj.propertyIsEnumerable('x')");
}


THREADED_TEST(Regress256330) {
  if (!i::v8_flags.turbofan) return;
  i::v8_flags.allow_natives_syntax = true;
  LocalContext context;
  v8::HandleScope scope(context->GetIsolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(context->GetIsolate());
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  context->Global()
      ->Set(context.local(), v8_str("Bug"),
            templ->GetFunction(context.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "\"use strict\"; var o = new Bug;"
      "function f(o) { o.x = 10; };"
      "%PrepareFunctionForOptimization(f);"
      "f(o); f(o); f(o);"
      "%OptimizeFunctionOnNextCall(f);"
      "f(o);");
  int status = v8_run_int32value(v8_compile("%GetOptimizationStatus(f)"));
  int mask = static_cast<int>(i::OptimizationStatus::kIsFunction) |
             static_cast<int>(i::OptimizationStatus::kOptimized);
  CHECK_EQ(mask, status & mask);
}

THREADED_TEST(OptimizedInterceptorSetter) {
  i::v8_flags.allow_natives_syntax = true;
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(CcTest::isolate());
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Obj"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var obj = new Obj;"
      // Initialize fields to avoid transitions later.
      "obj.age = 0;"
      "obj.accessor_age = 42;"
      "function setter(i) { this.accessor_age = i; };"
      "function getter() { return this.accessor_age; };"
      "function setAge(i) { obj.age = i; };"
      "Object.defineProperty(obj, 'age', { get:getter, set:setter });"
      "%PrepareFunctionForOptimization(setAge);"
      "setAge(1);"
      "setAge(2);"
      "setAge(3);"
      "%OptimizeFunctionOnNextCall(setAge);"
      "setAge(4);");
  // All stores went through the interceptor.
  ExpectInt32("obj.interceptor_age", 4);
  ExpectInt32("obj.accessor_age", 42);
}

THREADED_TEST(OptimizedInterceptorGetter) {
  i::v8_flags.allow_natives_syntax = true;
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(CcTest::isolate());
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Obj"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var obj = new Obj;"
      // Initialize fields to avoid transitions later.
      "obj.age = 1;"
      "obj.accessor_age = 42;"
      "function getter() { return this.accessor_age; };"
      "function getAge() { return obj.interceptor_age; };"
      "Object.defineProperty(obj, 'interceptor_age', { get:getter });"
      "%PrepareFunctionForOptimization(getAge);"
      "getAge();"
      "getAge();"
      "getAge();"
      "%OptimizeFunctionOnNextCall(getAge);");
  // Access through interceptor.
  ExpectInt32("getAge()", 1);
}

THREADED_TEST(OptimizedInterceptorFieldRead) {
  i::v8_flags.allow_natives_syntax = true;
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(CcTest::isolate());
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Obj"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var obj = new Obj;"
      "obj.__proto__.interceptor_age = 42;"
      "obj.age = 100;"
      "function getAge() { return obj.interceptor_age; };"
      "%PrepareFunctionForOptimization(getAge);");
  ExpectInt32("getAge();", 100);
  ExpectInt32("getAge();", 100);
  ExpectInt32("getAge();", 100);
  CompileRun("%OptimizeFunctionOnNextCall(getAge);");
  // Access through interceptor.
  ExpectInt32("getAge();", 100);
}

THREADED_TEST(OptimizedInterceptorFieldWrite) {
  i::v8_flags.allow_natives_syntax = true;
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(CcTest::isolate());
  AddInterceptor(templ, InterceptorGetter, InterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Obj"),
            templ->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var obj = new Obj;"
      "obj.age = 100000;"
      "function setAge(i) { obj.age = i };"
      "%PrepareFunctionForOptimization(setAge);"
      "setAge(100);"
      "setAge(101);"
      "setAge(102);"
      "%OptimizeFunctionOnNextCall(setAge);"
      "setAge(103);");
  ExpectInt32("obj.age", 100000);
  ExpectInt32("obj.interceptor_age", 103);
}


THREADED_TEST(Regress149912) {
  LocalContext context;
  v8::HandleScope scope(context->GetIsolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(context->GetIsolate());
  AddInterceptor(templ, EmptyInterceptorGetter, EmptyInterceptorSetter);
  context->Global()
      ->Set(context.local(), v8_str("Bug"),
            templ->GetFunction(context.local()).ToLocalChecked())
      .FromJust();
  CompileRun("Number.prototype.__proto__ = new Bug; var x = 0; x.foo();");
}

THREADED_TEST(Regress625155) {
  LocalContext context;
  v8::HandleScope scope(context->GetIsolate());
  Local<FunctionTemplate> templ = FunctionTemplate::New(context->GetIsolate());
  AddInterceptor(templ, EmptyInterceptorGetter, EmptyInterceptorSetter);
  context->Global()
      ->Set(context.local(), v8_str("Bug"),
            templ->GetFunction(context.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "Number.prototype.__proto__ = new Bug;"
      "var x;"
      "x = 0xDEAD;"
      "x.boom = 0;"
      "x = 's';"
      "x.boom = 0;"
      "x = 1.5;"
      "x.boom = 0;");
}

THREADED_TEST(Regress125988) {
  v8::HandleScope scope(CcTest::isolate());
  Local<FunctionTemplate> intercept = FunctionTemplate::New(CcTest::isolate());
  AddInterceptor(intercept, EmptyInterceptorGetter, EmptyInterceptorSetter);
  LocalContext env;
  env->Global()
      ->Set(env.local(), v8_str("Intercept"),
            intercept->GetFunction(env.local()).ToLocalChecked())
      .FromJust();
  CompileRun(
      "var a = new Object();"
      "var b = new Intercept();"
      "var c = new Object();"
      "c.__proto__ = b;"
      "b.__proto__ = a;"
      "a.x = 23;"
      "for (var i = 0; i < 3; i++) c.x;");
  ExpectBoolean("c.hasOwnProperty('x')", false);
  ExpectInt32("c.x", 23);
  CompileRun(
      "a.y = 42;"
      "for (var i = 0; i < 3; i++) c.x;");
  ExpectBoolean("c.hasOwnProperty('x')", false);
  ExpectInt32("c.x", 23);
  ExpectBoolean("c.hasOwnProperty('y')", false);
  ExpectInt32("c.y", 42);
}

namespace {
void IndexedPropertyEnumerator(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  v8::Local<v8::Array> result = v8::Array::New(info.GetIsolate(), 1);
  result->Set(info.GetIsolate()->GetCurrentContext(), 0,
              v8::Integer::New(info.GetIsolate(), 7))
      .FromJust();
  info.GetReturnValue().Set(result);
}

void NamedPropertyEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info) {
  v8::Local<v8::Array> result = v8::Array::New(info.GetIsolate(), 2);
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
  result->Set(context, 0, v8_str("x")).FromJust();
  result->Set(context, 1, v8::Symbol::GetIterator(info.GetIsolate()))
      .FromJust();
  info.GetReturnValue().Set(result);
}
}  // namespace

THREADED_TEST(GetOwnPropertyNamesWithInterceptor) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::ObjectTemplate> obj_template = v8::ObjectTemplate::New(isolate);

  obj_template->Set(isolate, "7", v8::Integer::New(isolate, 7));
  obj_template->Set(isolate, "x", v8::Integer::New(isolate, 42));
  obj_template->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      static_cast<v8::IndexedPropertyGetterCallbackV2>(nullptr), nullptr,
      nullptr, nullptr, IndexedPropertyEnumerator));
  obj_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
      static_cast<v8::NamedPropertyGetterCallback>(nullptr), nullptr, nullptr,
      nullptr, NamedPropertyEnumerator));

  LocalContext context;
  v8::Local<v8::Object> global = context->Global();
  global->Set(context.local(), v8_str("object"),
              obj_template->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  v8::Local<v8::Value> result =
      CompileRun("Object.getOwnPropertyNames(object)");
  CHECK(result->IsArray());
  v8::Local<v8::Array> result_array = result.As<v8::Array>();
  CHECK_EQ(2u, result_array->Length());
  CHECK(result_array->Get(context.local(), 0).ToLocalChecked()->IsString());
  CHECK(result_array->Get(context.local(), 1).ToLocalChecked()->IsString());
  CHECK(v8_str("7")
            ->Equals(context.local(),
                     result_array->Get(context.local(), 0).ToLocalChecked())
            .FromJust());
  CHECK(v8_str("x")
            ->Equals(context.local(),
                     result_array->Get(context.local(), 1).ToLocalChecked())
            .FromJust());

  result = CompileRun("var ret = []; for (var k in object) ret.push(k); ret");
  CHECK(result->IsArray());
  result_array = result.As<v8::Array>();
  CHECK_EQ(2u, result_array->Length());
  CHECK(result_array->Get(context.local(), 0).ToLocalChecked()->IsString());
  CHECK(result_array->Get(context.local(), 1).ToLocalChecked()->IsString());
  CHECK(v8_str("7")
            ->Equals(context.local(),
                     result_array->Get(context.local(), 0).ToLocalChecked())
            .FromJust());
  CHECK(v8_str("x")
            ->Equals(context.local(),
                     result_array->Get(context.local(), 1).ToLocalChecked())
            .FromJust());

  result = CompileRun("Object.getOwnPropertySymbols(object)");
  CHECK(result->IsArray());
  result_array = result.As<v8::Array>();
  CHECK_EQ(1u, result_array->Length());
  CHECK(result_array->Get(context.local(), 0)
            .ToLocalChecked()
            ->Equals(context.local(), v8::Symbol::GetIterator(isolate))
            .FromJust());
}

namespace {
void IndexedPropertyEnumeratorException(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  info.GetIsolate()->ThrowException(v8_num(42));
}
}  // namespace

THREADED_TEST(GetOwnPropertyNamesWithIndexedInterceptorExceptions_regress4026) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::ObjectTemplate> obj_template = v8::ObjectTemplate::New(isolate);

  obj_template->Set(isolate, "7", v8::Integer::New(isolate, 7));
  obj_template->Set(isolate, "x", v8::Integer::New(isolate, 42));
  // First just try a failing indexed interceptor.
  obj_template->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      static_cast<v8::IndexedPropertyGetterCallbackV2>(nullptr), nullptr,
      nullptr, nullptr, IndexedPropertyEnumeratorException));

  LocalContext context;
  v8::Local<v8::Object> global = context->Global();
  global->Set(context.local(), v8_str("object"),
              obj_template->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  v8::Local<v8::Value> result = CompileRun(
      "var result  = []; "
      "try { "
      "  for (var k in object) result .push(k);"
      "} catch (e) {"
      "  result  = e"
      "}"
      "result ");
  CHECK(!result->IsArray());
  CHECK(v8_num(42)->Equals(context.local(), result).FromJust());

  result = CompileRun(
      "var result = [];"
      "try { "
      "  result = Object.keys(object);"
      "} catch (e) {"
      "  result = e;"
      "}"
      "result");
  CHECK(!result->IsArray());
  CHECK(v8_num(42)->Equals(context.local(), result).FromJust());
}

namespace {
void NamedPropertyEnumeratorException(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
  info.GetIsolate()->ThrowException(v8_num(43));
}
}  // namespace

THREADED_TEST(GetOwnPropertyNamesWithNamedInterceptorExceptions_regress4026) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::ObjectTemplate> obj_template = v8::ObjectTemplate::New(isolate);

  obj_template->Set(isolate, "7", v8::Integer::New(isolate, 7));
  obj_template->Set(isolate, "x", v8::Integer::New(isolate, 42));
  // First just try a failing indexed interceptor.
  obj_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
      static_cast<v8::NamedPropertyGetterCallback>(nullptr), nullptr, nullptr,
      nullptr, NamedPropertyEnumeratorException));

  LocalContext context;
  v8::Local<v8::Object> global = context->Global();
  global->Set(context.local(), v8_str("object"),
              obj_template->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  v8::Local<v8::Value> result = CompileRun(
      "var result = []; "
      "try { "
      "  for (var k in object) result.push(k);"
      "} catch (e) {"
      "  result = e"
      "}"
      "result");
  CHECK(!result->IsArray());
  CHECK(v8_num(43)->Equals(context.local(), result).FromJust());

  result = CompileRun(
      "var result = [];"
      "try { "
      "  result = Object.keys(object);"
      "} catch (e) {"
      "  result = e;"
      "}"
      "result");
  CHECK(!result->IsArray());
  CHECK(v8_num(43)->Equals(context.local(), result).FromJust());
}

namespace {

struct PreprocessExceptionTestConfig {
  bool query_should_throw : 1 = false;
  bool getter_should_throw : 1 = false;
  bool descriptor_should_throw : 1 = false;
};

template <typename T>
PreprocessExceptionTestConfig* GetPETConfig(
    const v8::PropertyCallbackInfo<T>& info) {
  return reinterpret_cast<PreprocessExceptionTestConfig*>(
      v8::External::Cast(*info.Data())->Value());
}

const char* ToString(v8::ExceptionContext kind) {
  switch (kind) {
    case v8::ExceptionContext::kUnknown:
      return "Unknown";
    case v8::ExceptionContext::kConstructor:
      return "Constructor";
    case v8::ExceptionContext::kOperation:
      return "Operation";
    case v8::ExceptionContext::kAttributeGet:
      return "AttributeGet";
    case v8::ExceptionContext::kAttributeSet:
      return "AttributeSet";
    case v8::ExceptionContext::kIndexedQuery:
      return "IndexedQuery";
    case v8::ExceptionContext::kIndexedGetter:
      return "IndexedGetter";
    case v8::ExceptionContext::kIndexedDescriptor:
      return "IndexedDescriptor";
    case v8::ExceptionContext::kIndexedSetter:
      return "IndexedSetter";
    case v8::ExceptionContext::kIndexedDefiner:
      return "IndexedDefiner";
    case v8::ExceptionContext::kIndexedDeleter:
      return "IndexedDeleter";
    case v8::ExceptionContext::kNamedQuery:
      return "NamedQuery";
    case v8::ExceptionContext::kNamedGetter:
      return "NamedGetter";
    case v8::ExceptionContext::kNamedDescriptor:
      return "NamedDescriptor";
    case v8::ExceptionContext::kNamedSetter:
      return "NamedSetter";
    case v8::ExceptionContext::kNamedDefiner:
      return "NamedDefiner";
    case v8::ExceptionContext::kNamedDeleter:
      return "NamedDeleter";
    case v8::ExceptionContext::kNamedEnumerator:
      return "NamedEnumerator";
  }
  UNREACHABLE();
}

void PreprocessExceptionTestCallback(v8::ExceptionPropagationMessage info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Object> exception = info.GetException();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::String> message_key = v8_str("message");

  v8::Local<v8::String> message_value = exception->Get(context, message_key)
                                            .ToLocalChecked()
                                            ->ToString(context)
                                            .ToLocalChecked();
  String::Utf8Value interface_name(isolate, info.GetInterfaceName());
  String::Utf8Value property_name(isolate, info.GetPropertyName());
  String::Utf8Value message(isolate, message_value);

  v8::base::ScopedVector<char> buf(256);
  v8::base::SNPrintF(buf, "%s:%s:%s: %s", *interface_name, *property_name,
                     ToString(info.GetExceptionContext()), *message);

  std::ignore =
      exception->CreateDataProperty(context, message_key, v8_str(buf.data()));
}

void CheckMessage(v8::TryCatch& try_catch, const char* expected_message) {
  CHECK(try_catch.HasCaught());
  v8::Local<v8::String> message_key = v8_str("message");
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();

  Local<v8::Value> result = try_catch.Exception()
                                .As<v8::Object>()
                                ->Get(context, message_key)
                                .ToLocalChecked();
  CHECK(result->IsString());
  String::Utf8Value message(isolate, result.As<String>());

  // Compare as std::string in order to see a readable message on failure.
  CHECK_EQ(std::string(*message), std::string(expected_message));
  try_catch.Reset();
}

// Named interceptor callbacks.

v8::Intercepted PETNamedQuery(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  if (GetPETConfig(info)->query_should_throw) {
    info.GetIsolate()->ThrowError(v8_str("Named query failed."));
  } else {
    info.GetReturnValue().Set(v8::None);
  }
  return v8::Intercepted::kYes;
}
v8::Intercepted PETNamedGetter(Local<Name> name,
                               const v8::PropertyCallbackInfo<Value>& info) {
  if (GetPETConfig(info)->getter_should_throw) {
    info.GetIsolate()->ThrowError(v8_str("Named getter failed."));
  } else {
    info.GetReturnValue().Set(153);
  }
  return v8::Intercepted::kYes;
}
v8::Intercepted PETNamedSetter(Local<Name> name, Local<Value> value,
                               const v8::PropertyCallbackInfo<void>& info) {
  info.GetIsolate()->ThrowError(v8_str("Named setter failed."));
  return v8::Intercepted::kYes;
}
v8::Intercepted PETNamedDeleter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  info.GetIsolate()->ThrowError(v8_str("Named deleter failed."));
  return v8::Intercepted::kYes;
}
v8::Intercepted PETNamedDefiner(Local<Name> name,
                                const v8::PropertyDescriptor& desc,
                                const v8::PropertyCallbackInfo<void>& info) {
  info.GetIsolate()->ThrowError(v8_str("Named definer failed."));
  return v8::Intercepted::kYes;
}
v8::Intercepted PETNamedDescriptor(
    Local<Name> property, const v8::PropertyCallbackInfo<Value>& info) {
  if (GetPETConfig(info)->descriptor_should_throw) {
    info.GetIsolate()->ThrowError(v8_str("Named descriptor failed."));
  } else {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Object> descriptor = v8::Object::New(isolate);
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    v8::Local<v8::Boolean> true_value = v8::Boolean::New(isolate, true);

    std::ignore = descriptor->Set(ctx, v8_str("value"), property);
    std::ignore = descriptor->Set(ctx, v8_str("writable"), true_value);
    std::ignore = descriptor->Set(ctx, v8_str("enumerable"), true_value);
    std::ignore = descriptor->Set(ctx, v8_str("configurable"), true_value);

    info.GetReturnValue().Set(descriptor);
  }
  return v8::Intercepted::kYes;
}

// Indexed interceptor callbacks.

v8::Intercepted PETIndexedQuery(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  if (GetPETConfig(info)->query_should_throw) {
    info.GetIsolate()->ThrowError(v8_str("Indexed query failed."));
  } else {
    info.GetReturnValue().Set(v8::None);
  }
  return v8::Intercepted::kYes;
}
v8::Intercepted PETIndexedGetter(uint32_t index,
                                 const v8::PropertyCallbackInfo<Value>& info) {
  if (GetPETConfig(info)->getter_should_throw) {
    info.GetIsolate()->ThrowError(v8_str("Indexed getter failed."));
  } else {
    info.GetReturnValue().Set(153);
  }
  return v8::Intercepted::kYes;
}
v8::Intercepted PETIndexedSetter(uint32_t index, Local<Value> value,
                                 const v8::PropertyCallbackInfo<void>& info) {
  info.GetIsolate()->ThrowError(v8_str("Indexed setter failed."));
  return v8::Intercepted::kYes;
}
v8::Intercepted PETIndexedDeleter(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
  info.GetIsolate()->ThrowError(v8_str("Indexed deleter failed."));
  return v8::Intercepted::kYes;
}
v8::Intercepted PETIndexedDefiner(uint32_t index,
                                  const v8::PropertyDescriptor& desc,
                                  const v8::PropertyCallbackInfo<void>& info) {
  info.GetIsolate()->ThrowError(v8_str("Indexed definer failed."));
  return v8::Intercepted::kYes;
}
v8::Intercepted PETIndexedDescriptor(
    uint32_t index, const v8::PropertyCallbackInfo<Value>& info) {
  if (GetPETConfig(info)->descriptor_should_throw) {
    info.GetIsolate()->ThrowError(v8_str("Indexed descriptor failed."));
  } else {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Object> descriptor = v8::Object::New(isolate);
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    v8::Local<v8::Boolean> true_value = v8::Boolean::New(isolate, true);

    std::ignore = descriptor->Set(ctx, v8_str("value"), v8_uint(index));
    std::ignore = descriptor->Set(ctx, v8_str("writable"), true_value);
    std::ignore = descriptor->Set(ctx, v8_str("enumerable"), true_value);
    std::ignore = descriptor->Set(ctx, v8_str("configurable"), true_value);

    info.GetReturnValue().Set(descriptor);
  }
  return v8::Intercepted::kYes;
}

}  // namespace

void TestPreprocessExceptionFromInterceptors(
    v8::Isolate* isolate, PreprocessExceptionTestConfig& config,
    Local<Context> ctx, v8::Local<v8::Object> obj,
    bool is_descriptor_callback_available) {
  v8::TryCatch try_catch(isolate);

  config.query_should_throw = true;
  config.getter_should_throw = true;
  config.descriptor_should_throw = true;

  const char* expected;
  //
  // Check query callbacks.
  //
  {
    expected = "MyClass:foo:NamedQuery: Named query failed.";
    std::ignore = obj->GetPropertyAttributes(ctx, v8_str("foo"));
    CheckMessage(try_catch, expected);
    std::ignore = obj->HasOwnProperty(ctx, v8_str("foo"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.hasOwn(obj, 'foo');");
    CheckMessage(try_catch, expected);

    expected = "MyClass:1:IndexedQuery: Indexed query failed.";
    std::ignore = obj->GetPropertyAttributes(ctx, v8_uint(1));
    CheckMessage(try_catch, expected);
    std::ignore = obj->HasOwnProperty(ctx, v8_str("1"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.hasOwn(obj, 1);");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967294:IndexedQuery: Indexed query failed.";
    std::ignore = obj->GetPropertyAttributes(ctx, v8_uint(0xfffffffe));
    CheckMessage(try_catch, expected);
    std::ignore = obj->HasOwnProperty(ctx, v8_str("4294967294"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.hasOwn(obj, 0xfffffffe);");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967295:NamedQuery: Named query failed.";
    std::ignore = obj->GetPropertyAttributes(ctx, v8_uint(0xffffffff));
    CheckMessage(try_catch, expected);
    std::ignore = obj->HasOwnProperty(ctx, v8_str("4294967295"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.hasOwn(obj, 0xffffffff);");
    CheckMessage(try_catch, expected);
  }

  //
  // Check getter callbacks.
  //
  {
    expected = "MyClass:foo:NamedGetter: Named getter failed.";
    std::ignore = obj->Get(ctx, v8_str("foo"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj.foo");
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj['foo']");
    CheckMessage(try_catch, expected);

    expected = "MyClass:1:IndexedGetter: Indexed getter failed.";
    std::ignore = obj->Get(ctx, v8_uint(1));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj[1]");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967294:IndexedGetter: Indexed getter failed.";
    std::ignore = obj->Get(ctx, v8_uint(0xfffffffe));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj[0xfffffffe]");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967295:NamedGetter: Named getter failed.";
    std::ignore = obj->Get(ctx, v8_uint(0xffffffff));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj[0xffffffff]");
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj[4294967295]");
    CheckMessage(try_catch, expected);
  }

  //
  // Check setter callbacks.
  //
  {
    v8::Local<v8::Value> value = v8_str("value");

    expected = "MyClass:foo:NamedSetter: Named setter failed.";
    std::ignore = obj->Set(ctx, v8_str("foo"), value);
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj.foo = 42;");
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj['foo'] = 42;");
    CheckMessage(try_catch, expected);

    expected = "MyClass:1:IndexedSetter: Indexed setter failed.";
    std::ignore = obj->Set(ctx, v8_uint(1), value);
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj[1] = 42;");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967294:IndexedSetter: Indexed setter failed.";
    std::ignore = obj->Set(ctx, v8_uint(0xfffffffe), value);
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj[0xfffffffe] = 42;");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967295:NamedSetter: Named setter failed.";
    std::ignore = obj->Set(ctx, v8_uint(0xffffffff), value);
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("obj[0xffffffff] = 42;");
    CheckMessage(try_catch, expected);
  }

  //
  // Check deleter callbacks.
  //
  {
    expected = "MyClass:foo:NamedDeleter: Named deleter failed.";
    std::ignore = obj->Delete(ctx, v8_str("foo"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("delete obj.foo;");
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("delete obj['foo'];");
    CheckMessage(try_catch, expected);

    expected = "MyClass:1:IndexedDeleter: Indexed deleter failed.";
    std::ignore = obj->Delete(ctx, v8_str("1"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("delete obj[1];");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967294:IndexedDeleter: Indexed deleter failed.";
    std::ignore = obj->Delete(ctx, v8_str("4294967294"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("delete obj[0xfffffffe];");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967295:NamedDeleter: Named deleter failed.";
    std::ignore = obj->Delete(ctx, v8_str("4294967295"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("delete obj[0xffffffff];");
    CheckMessage(try_catch, expected);
  }

  //
  // Check descriptor callbacks.
  //
  {
    expected = is_descriptor_callback_available
                   ? "MyClass:foo:NamedDescriptor: Named descriptor failed."
                   : "MyClass:foo:NamedQuery: Named query failed.";
    std::ignore = obj->GetOwnPropertyDescriptor(ctx, v8_str("foo"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.getOwnPropertyDescriptor(obj, 'foo');");
    CheckMessage(try_catch, expected);

    expected = is_descriptor_callback_available
                   ? "MyClass:1:IndexedDescriptor: Indexed descriptor failed."
                   : "MyClass:1:IndexedQuery: Indexed query failed.";
    std::ignore = obj->GetOwnPropertyDescriptor(ctx, v8_str("1"));
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.getOwnPropertyDescriptor(obj, 1);");
    CheckMessage(try_catch, expected);

    expected =
        is_descriptor_callback_available
            ? "MyClass:4294967294:IndexedDescriptor: Indexed descriptor failed."
            : "MyClass:4294967294:IndexedQuery: Indexed query failed.";
    std::ignore = obj->GetOwnPropertyDescriptor(ctx, v8_str("4294967294"));
    CheckMessage(try_catch, expected);
    std::ignore =
        CompileRun("Object.getOwnPropertyDescriptor(obj, 0xfffffffe);");
    CheckMessage(try_catch, expected);

    expected =
        is_descriptor_callback_available
            ? "MyClass:4294967295:NamedDescriptor: Named descriptor failed."
            : "MyClass:4294967295:NamedQuery: Named query failed.";
    std::ignore = obj->GetOwnPropertyDescriptor(ctx, v8_str("4294967295"));
    CheckMessage(try_catch, expected);
    std::ignore =
        CompileRun("Object.getOwnPropertyDescriptor(obj, 0xffffffff);");
    CheckMessage(try_catch, expected);
  }

  //
  // Check definer callbacks.
  //
  config.query_should_throw = false;
  config.getter_should_throw = false;
  config.descriptor_should_throw = false;
  {
    v8::Local<v8::Value> value = v8_str("value");
    v8::PropertyDescriptor descriptor(value);

    expected = "MyClass:foo:NamedDefiner: Named definer failed.";
    std::ignore = obj->DefineOwnProperty(ctx, v8_str("foo"), value);
    CheckMessage(try_catch, expected);
    std::ignore = obj->DefineProperty(ctx, v8_str("foo"), descriptor);
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.defineProperty(obj, 'foo', {});");
    CheckMessage(try_catch, expected);

    expected = "MyClass:1:IndexedDefiner: Indexed definer failed.";
    std::ignore = obj->DefineOwnProperty(ctx, v8_str("1"), value);
    CheckMessage(try_catch, expected);
    std::ignore = obj->DefineProperty(ctx, v8_str("1"), descriptor);
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.defineProperty(obj, 1, {});");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967294:IndexedDefiner: Indexed definer failed.";
    std::ignore = obj->DefineOwnProperty(ctx, v8_str("4294967294"), value);
    CheckMessage(try_catch, expected);
    std::ignore = obj->DefineProperty(ctx, v8_str("4294967294"), descriptor);
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.defineProperty(obj, 0xfffffffe, {});");
    CheckMessage(try_catch, expected);

    expected = "MyClass:4294967295:NamedDefiner: Named definer failed.";
    std::ignore = obj->DefineOwnProperty(ctx, v8_str("4294967295"), value);
    CheckMessage(try_catch, expected);
    std::ignore = obj->DefineProperty(ctx, v8_str("4294967295"), descriptor);
    CheckMessage(try_catch, expected);
    std::ignore = CompileRun("Object.defineProperty(obj, 0xffffffff, {});");
    CheckMessage(try_catch, expected);
  }
}

// Can't use THREADED_TEST because this test requires setting an exception
// preprocessing callback which might be observable in other tests.
TEST(PreprocessExceptionFromInterceptorsWithoutDescriptorCallback) {
  i::v8_flags.experimental_report_exceptions_from_callbacks = true;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);

  v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(isolate);
  ctor->SetClassName(v8_str("MyClass"));
  v8::Local<v8::ObjectTemplate> obj_template = ctor->InstanceTemplate();

  isolate->SetExceptionPropagationCallback(PreprocessExceptionTestCallback);

  PreprocessExceptionTestConfig config;

  obj_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
      PETNamedGetter, PETNamedSetter, PETNamedQuery, PETNamedDeleter,
      nullptr,  // enumerator
      PETNamedDefiner,
      nullptr,  // descriptor
      v8::External::New(isolate, &config)));
  obj_template->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      PETIndexedGetter, PETIndexedSetter, PETIndexedQuery, PETIndexedDeleter,
      nullptr,  // enumerator
      PETIndexedDefiner,
      nullptr,  // descriptor
      v8::External::New(isolate, &config)));

  LocalContext env;
  Local<Context> ctx = env.local();
  v8::Local<v8::Object> obj = obj_template->NewInstance(ctx).ToLocalChecked();

  v8::Local<v8::Object> global = ctx->Global();
  global->Set(ctx, v8_str("obj"), obj).FromJust();

  constexpr bool is_descriptor_callback_available = false;
  TestPreprocessExceptionFromInterceptors(isolate, config, ctx, obj,
                                          is_descriptor_callback_available);
}

// Can't use THREADED_TEST because this test requires setting an exception
// preprocessing callback which might be observable in other tests.
TEST(PreprocessExceptionFromInterceptorsWithDescriptorCallback) {
  i::v8_flags.experimental_report_exceptions_from_callbacks = true;
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);

  v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(isolate);
  ctor->SetClassName(v8_str("MyClass"));
  v8::Local<v8::ObjectTemplate> obj_template = ctor->InstanceTemplate();

  isolate->SetExceptionPropagationCallback(PreprocessExceptionTestCallback);

  PreprocessExceptionTestConfig config;

  obj_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
      PETNamedGetter, PETNamedSetter, PETNamedQuery, PETNamedDeleter,
      nullptr,  // enumerator
      PETNamedDefiner, PETNamedDescriptor,
      v8::External::New(isolate, &config)));
  obj_template->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      PETIndexedGetter, PETIndexedSetter, PETIndexedQuery, PETIndexedDeleter,
      nullptr,  // enumerator
      PETIndexedDefiner, PETIndexedDescriptor,
      v8::External::New(isolate, &config)));

  LocalContext env;
  Local<Context> ctx = env.local();
  v8::Local<v8::Object> obj = obj_template->NewInstance(ctx).ToLocalChecked();

  v8::Local<v8::Object> global = ctx->Global();
  global->Set(ctx, v8_str("obj"), obj).FromJust();

  constexpr bool is_descriptor_callback_available = true;
  TestPreprocessExceptionFromInterceptors(isolate, config, ctx, obj,
                                          is_descriptor_callback_available);
}

namespace {

template <typename T>
Local<Object> BuildWrappedObject(v8::Isolate* isolate, T* data) {
  auto templ = v8::ObjectTemplate::New(isolate);
  templ->SetInternalFieldCount(1);
  auto instance =
      templ->NewInstance(isolate->GetCurrentContext()).ToLocalChecked();
  instance->SetAlignedPointerInInternalField(0, data);
  return instance;
}


template <typename T>
T* GetWrappedObject(Local<Value> data) {
  return reinterpret_cast<T*>(
      Object::Cast(*data)->GetAlignedPointerFromInternalField(0));
}


struct AccessCheckData {
  int count;
  bool result;
};

struct ShouldInterceptData {
  int value;
  bool should_intercept;
};

v8::Intercepted ShouldNamedInterceptor(
    Local<Name> name, const v8::PropertyCallbackInfo<Value>& info) {
  CheckReturnValue(info, FUNCTION_ADDR(ShouldNamedInterceptor));
  auto data = GetWrappedObject<ShouldInterceptData>(info.Data());
  if (!data->should_intercept) return v8::Intercepted::kNo;
  // Side effects are allowed only when the property is present or throws.
  ApiTestFuzzer::Fuzz();
  info.GetReturnValue().Set(v8_num(data->value));
  return v8::Intercepted::kYes;
}

v8::Intercepted ShouldIndexedInterceptor(
    uint32_t, const v8::PropertyCallbackInfo<Value>& info) {
  CheckReturnValue(info, FUNCTION_ADDR(ShouldIndexedInterceptor));
  auto data = GetWrappedObject<ShouldInterceptData>(info.Data());
  if (!data->should_intercept) return v8::Intercepted::kNo;
  // Side effects are allowed only when the property is present or throws.
  ApiTestFuzzer::Fuzz();
  info.GetReturnValue().Set(v8_num(data->value));
  return v8::Intercepted::kYes;
}

}  // namespace

THREADED_TEST(NonMaskingInterceptorOwnProperty) {
  auto isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);
  LocalContext context;

  ShouldInterceptData intercept_data;
  intercept_data.value = 239;
  intercept_data.should_intercept = true;

  auto interceptor_templ = v8::ObjectTemplate::New(isolate);
  v8::NamedPropertyHandlerConfiguration conf(ShouldNamedInterceptor);
  conf.flags = v8::PropertyHandlerFlags::kNonMasking;
  conf.data = BuildWrappedObject<ShouldInterceptData>(isolate, &intercept_data);
  interceptor_templ->SetHandler(conf);

  auto interceptor =
      interceptor_templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()
      ->Set(context.local(), v8_str("obj"), interceptor)
      .FromJust();

  ExpectInt32("obj.whatever", 239);

  CompileRun("obj.whatever = 4;");

  // obj.whatever exists, thus it is not affected by the non-masking
  // interceptor.
  ExpectInt32("obj.whatever", 4);

  CompileRun("delete obj.whatever;");
  ExpectInt32("obj.whatever", 239);
}


THREADED_TEST(NonMaskingInterceptorPrototypeProperty) {
  auto isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);
  LocalContext context;

  ShouldInterceptData intercept_data;
  intercept_data.value = 239;
  intercept_data.should_intercept = true;

  auto interceptor_templ = v8::ObjectTemplate::New(isolate);
  v8::NamedPropertyHandlerConfiguration conf(ShouldNamedInterceptor);
  conf.flags = v8::PropertyHandlerFlags::kNonMasking;
  conf.data = BuildWrappedObject<ShouldInterceptData>(isolate, &intercept_data);
  interceptor_templ->SetHandler(conf);

  auto interceptor =
      interceptor_templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()
      ->Set(context.local(), v8_str("obj"), interceptor)
      .FromJust();

  ExpectInt32("obj.whatever", 239);

  CompileRun("obj.__proto__ = {'whatever': 4};");
  ExpectInt32("obj.whatever", 4);

  CompileRun("delete obj.__proto__.whatever;");
  ExpectInt32("obj.whatever", 239);
}


THREADED_TEST(NonMaskingInterceptorPrototypePropertyIC) {
  auto isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);
  LocalContext context;

  ShouldInterceptData intercept_data;
  intercept_data.value = 239;
  intercept_data.should_intercept = true;

  auto interceptor_templ = v8::ObjectTemplate::New(isolate);
  v8::NamedPropertyHandlerConfiguration conf(ShouldNamedInterceptor);
  conf.flags = v8::PropertyHandlerFlags::kNonMasking;
  conf.data = BuildWrappedObject<ShouldInterceptData>(isolate, &intercept_data);
  interceptor_templ->SetHandler(conf);

  auto interceptor =
      interceptor_templ->NewInstance(context.local()).ToLocalChecked();
  context->Global()
      ->Set(context.local(), v8_str("obj"), interceptor)
      .FromJust();

  CompileRun(
      "outer = {};"
      "outer.__proto__ = obj;"
      "function f(obj) {"
      "  var x;"
      "  for (var i = 0; i < 4; i++) {"
      "    x = obj.whatever;"
      "  }"
      "  return x;"
      "}");

  // Receiver == holder.
  CompileRun("obj.__proto__ = null;");
  ExpectInt32("f(obj)", 239);
  ExpectInt32("f(outer)", 239);

  // Receiver != holder.
  CompileRun("Object.setPrototypeOf(obj, {});");
  ExpectInt32("f(obj)", 239);
  ExpectInt32("f(outer)", 239);

  // Masked value on prototype.
  CompileRun("obj.__proto__.whatever = 4;");
  CompileRun("obj.__proto__.__proto__ = { 'whatever' : 5 };");
  ExpectInt32("f(obj)", 4);
  ExpectInt32("f(outer)", 4);

  // Masked value on prototype prototype.
  CompileRun("delete obj.__proto__.whatever;");
  ExpectInt32("f(obj)", 5);
  ExpectInt32("f(outer)", 5);

  // Reset.
  CompileRun("delete obj.__proto__.__proto__.whatever;");
  ExpectInt32("f(obj)", 239);
  ExpectInt32("f(outer)", 239);

  // Masked value on self.
  CompileRun("obj.whatever = 4;");
  ExpectInt32("f(obj)", 4);
  ExpectInt32("f(outer)", 4);

  // Reset.
  CompileRun("delete obj.whatever;");
  ExpectInt32("f(obj)", 239);
  ExpectInt32("f(outer)", 239);

  CompileRun("outer.whatever = 4;");
  ExpectInt32("f(obj)", 239);
  ExpectInt32("f(outer)", 4);
}

namespace {

v8::Intercepted ConcatNamedPropertyGetter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(
      // Return the property name concatenated with itself.
      String::Concat(info.GetIsolate(), name.As<String>(), name.As<String>()));
  return v8::Intercepted::kYes;
}

v8::Intercepted ConcatIndexedPropertyGetter(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(
      // Return the double value of the index.
      v8_num(index + index));
  return v8::Intercepted::kYes;
}

void EnumCallbackWithNames(const v8::PropertyCallbackInfo<v8::Array>& info) {
  ApiTestFuzzer::Fuzz();
  v8::Local<v8::Array> result = v8::Array::New(info.GetIsolate(), 4);
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
  CHECK(
      result
          ->Set(context, v8::Integer::New(info.GetIsolate(), 0), v8_str("foo"))
          .FromJust());
  CHECK(
      result
          ->Set(context, v8::Integer::New(info.GetIsolate(), 1), v8_str("bar"))
          .FromJust());
  CHECK(
      result
          ->Set(context, v8::Integer::New(info.GetIsolate(), 2), v8_str("baz"))
          .FromJust());
  CHECK(
      result->Set(context, v8::Integer::New(info.GetIsolate(), 3), v8_str("10"))
          .FromJust());

  //  Create a holey array.
  CHECK(result->Delete(context, v8::Integer::New(info.GetIsolate(), 1))
            .FromJust());
  info.GetReturnValue().Set(result);
}

void EnumCallbackWithIndices(const v8::PropertyCallbackInfo<v8::Array>& info) {
  ApiTestFuzzer::Fuzz();
  v8::Local<v8::Array> result = v8::Array::New(info.GetIsolate(), 4);
  v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();

  CHECK(result->Set(context, v8::Integer::New(info.GetIsolate(), 0), v8_num(10))
            .FromJust());
  CHECK(result->Set(context, v8::Integer::New(info.GetIsolate(), 1), v8_num(11))
            .FromJust());
  CHECK(result->Set(context, v8::Integer::New(info.GetIsolate(), 2), v8_num(12))
            .FromJust());
  CHECK(result->Set(context, v8::Integer::New(info.GetIsolate(), 3), v8_num(14))
            .FromJust());

  //  Create a holey array.
  CHECK(result->Delete(context, v8::Integer::New(info.GetIsolate(), 1))
            .FromJust());
  info.GetReturnValue().Set(result);
}

v8::Intercepted RestrictiveNamedQuery(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  // Only "foo" is enumerable.
  if (v8_str("foo")
          ->Equals(info.GetIsolate()->GetCurrentContext(), property)
          .FromJust()) {
    info.GetReturnValue().Set(v8::None);
    return v8::Intercepted::kYes;
  }
  info.GetReturnValue().Set(v8::DontEnum);
  return v8::Intercepted::kYes;
}

v8::Intercepted RestrictiveIndexedQuery(
    uint32_t index, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  // Only index 2 and 12 are enumerable.
  if (index == 2 || index == 12) {
    info.GetReturnValue().Set(v8::None);
    return v8::Intercepted::kYes;
  }
  info.GetReturnValue().Set(v8::DontEnum);
  return v8::Intercepted::kYes;
}
}  // namespace

// Regression test for V8 bug 6627.
// Object.keys() must return enumerable keys only.
THREADED_TEST(EnumeratorsAndUnenumerableNamedProperties) {
  // The enumerator interceptor returns a list
  // of items which are filtered according to the
  // properties defined in the query interceptor.
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> obj = ObjectTemplate::New(isolate);
  obj->SetHandler(v8::NamedPropertyHandlerConfiguration(
      ConcatNamedPropertyGetter, nullptr, RestrictiveNamedQuery, nullptr,
      EnumCallbackWithNames));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            obj->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  ExpectInt32("Object.getOwnPropertyNames(obj).length", 3);
  ExpectString("Object.getOwnPropertyNames(obj)[0]", "foo");
  ExpectString("Object.getOwnPropertyNames(obj)[1]", "baz");
  ExpectString("Object.getOwnPropertyNames(obj)[2]", "10");

  ExpectTrue("Object.getOwnPropertyDescriptor(obj, 'foo').enumerable");
  ExpectFalse("Object.getOwnPropertyDescriptor(obj, 'baz').enumerable");

  ExpectInt32("Object.entries(obj).length", 1);
  ExpectString("Object.entries(obj)[0][0]", "foo");
  ExpectString("Object.entries(obj)[0][1]", "foofoo");

  ExpectInt32("Object.keys(obj).length", 1);
  ExpectString("Object.keys(obj)[0]", "foo");

  ExpectInt32("Object.values(obj).length", 1);
  ExpectString("Object.values(obj)[0]", "foofoo");
}

namespace {
v8::Intercepted QueryInterceptorForFoo(
    Local<Name> property, const v8::PropertyCallbackInfo<v8::Integer>& info) {
  // Don't intercept anything except "foo."
  if (!v8_str("foo")
           ->Equals(info.GetIsolate()->GetCurrentContext(), property)
           .FromJust()) {
    return v8::Intercepted::kNo;
  }
  // "foo" is enumerable.
  info.GetReturnValue().Set(v8::PropertyAttribute::None);
  return v8::Intercepted::kYes;
}
}  // namespace

// Test that calls to the query interceptor are independent of each
// other.
THREADED_TEST(EnumeratorsAndUnenumerableNamedPropertiesWithoutSet) {
  // The enumerator interceptor returns a list
  // of items which are filtered according to the
  // properties defined in the query interceptor.
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> obj = ObjectTemplate::New(isolate);
  obj->SetHandler(v8::NamedPropertyHandlerConfiguration(
      ConcatNamedPropertyGetter, nullptr, QueryInterceptorForFoo, nullptr,
      EnumCallbackWithNames));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            obj->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  ExpectInt32("Object.getOwnPropertyNames(obj).length", 3);
  ExpectString("Object.getOwnPropertyNames(obj)[0]", "foo");
  ExpectString("Object.getOwnPropertyNames(obj)[1]", "baz");
  ExpectString("Object.getOwnPropertyNames(obj)[2]", "10");

  ExpectTrue("Object.getOwnPropertyDescriptor(obj, 'foo').enumerable");
  ExpectInt32("Object.keys(obj).length", 1);
}

THREADED_TEST(EnumeratorsAndUnenumerableIndexedPropertiesArgumentsElements) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> obj = ObjectTemplate::New(isolate);
  obj->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      ConcatIndexedPropertyGetter, nullptr, RestrictiveIndexedQuery, nullptr,
      SloppyArgsIndexedPropertyEnumerator));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            obj->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  ExpectInt32("Object.getOwnPropertyNames(obj).length", 4);
  ExpectString("Object.getOwnPropertyNames(obj)[0]", "0");
  ExpectString("Object.getOwnPropertyNames(obj)[1]", "1");
  ExpectString("Object.getOwnPropertyNames(obj)[2]", "2");
  ExpectString("Object.getOwnPropertyNames(obj)[3]", "3");

  ExpectTrue("Object.getOwnPropertyDescriptor(obj, '2').enumerable");

  ExpectInt32("Object.entries(obj).length", 1);
  ExpectString("Object.entries(obj)[0][0]", "2");
  ExpectInt32("Object.entries(obj)[0][1]", 4);

  ExpectInt32("Object.keys(obj).length", 1);
  ExpectString("Object.keys(obj)[0]", "2");

  ExpectInt32("Object.values(obj).length", 1);
  ExpectInt32("Object.values(obj)[0]", 4);
}

THREADED_TEST(EnumeratorsAndUnenumerableIndexedProperties) {
  // The enumerator interceptor returns a list
  // of items which are filtered according to the
  // properties defined in the query interceptor.
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> obj = ObjectTemplate::New(isolate);
  obj->SetHandler(v8::IndexedPropertyHandlerConfiguration(
      ConcatIndexedPropertyGetter, nullptr, RestrictiveIndexedQuery, nullptr,
      EnumCallbackWithIndices));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            obj->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  ExpectInt32("Object.getOwnPropertyNames(obj).length", 3);
  ExpectString("Object.getOwnPropertyNames(obj)[0]", "10");
  ExpectString("Object.getOwnPropertyNames(obj)[1]", "12");
  ExpectString("Object.getOwnPropertyNames(obj)[2]", "14");

  ExpectFalse("Object.getOwnPropertyDescriptor(obj, '10').enumerable");
  ExpectTrue("Object.getOwnPropertyDescriptor(obj, '12').enumerable");

  ExpectInt32("Object.entries(obj).length", 1);
  ExpectString("Object.entries(obj)[0][0]", "12");
  ExpectInt32("Object.entries(obj)[0][1]", 24);

  ExpectInt32("Object.keys(obj).length", 1);
  ExpectString("Object.keys(obj)[0]", "12");

  ExpectInt32("Object.values(obj).length", 1);
  ExpectInt32("Object.values(obj)[0]", 24);
}

THREADED_TEST(EnumeratorsAndForIn) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::ObjectTemplate> obj = ObjectTemplate::New(isolate);
  obj->SetHandler(v8::NamedPropertyHandlerConfiguration(
      ConcatNamedPropertyGetter, nullptr, RestrictiveNamedQuery, nullptr,
      NamedEnum));
  LocalContext context;
  context->Global()
      ->Set(context.local(), v8_str("obj"),
            obj->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  ExpectInt32("Object.getOwnPropertyNames(obj).length", 3);
  ExpectString("Object.getOwnPropertyNames(obj)[0]", "foo");

  ExpectTrue("Object.getOwnPropertyDescriptor(obj, 'foo').enumerable");

  CompileRun(
      "let concat = '';"
      "for(var prop in obj) {"
      "  concat += `key:${prop}:value:${obj[prop]}`;"
      "}");

  // Check that for...in only iterates over enumerable properties.
  ExpectString("concat", "key:foo:value:foofoo");
}

namespace {

v8::Intercepted DatabaseGetter(Local<Name> name,
                               const v8::PropertyCallbackInfo<Value>& info) {
  auto context = info.GetIsolate()->GetCurrentContext();
  v8::MaybeLocal<Value> maybe_db =
      info.HolderV2()->GetRealNamedProperty(context, v8_str("db"));
  if (maybe_db.IsEmpty()) return v8::Intercepted::kNo;
  Local<v8::Object> db = maybe_db.ToLocalChecked().As<v8::Object>();
  if (!db->Has(context, name).FromJust()) return v8::Intercepted::kNo;

  // Side effects are allowed only when the property is present or throws.
  ApiTestFuzzer::Fuzz();
  info.GetReturnValue().Set(db->Get(context, name).ToLocalChecked());
  return v8::Intercepted::kYes;
}

v8::Intercepted DatabaseSetter(Local<Name> name, Local<Value> value,
                               const v8::PropertyCallbackInfo<void>& info) {
  auto context = info.GetIsolate()->GetCurrentContext();
  if (name->Equals(context, v8_str("db")).FromJust())
    return v8::Intercepted::kNo;

  // Side effects are allowed only when the property is present or throws.
  ApiTestFuzzer::Fuzz();
  Local<v8::Object> db = info.HolderV2()
                             ->GetRealNamedProperty(context, v8_str("db"))
                             .ToLocalChecked()
                             .As<v8::Object>();
  db->Set(context, name, value).FromJust();
  return v8::Intercepted::kYes;
}

}  // namespace


THREADED_TEST(NonMaskingInterceptorGlobalEvalRegression) {
  auto isolate = CcTest::isolate();
  v8::HandleScope handle_scope(isolate);
  LocalContext context;

  auto interceptor_templ = v8::ObjectTemplate::New(isolate);
  v8::NamedPropertyHandlerConfiguration conf(DatabaseGetter, DatabaseSetter);
  conf.flags = v8::PropertyHandlerFlags::kNonMasking;
  interceptor_templ->SetHandler(conf);

  context->Global()
      ->Set(context.local(), v8_str("intercepted_1"),
            interceptor_templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();
  context->Global()
      ->Set(context.local(), v8_str("intercepted_2"),
            interceptor_templ->NewInstance(context.local()).ToLocalChecked())
      .FromJust();

  // Init dbs.
  CompileRun(
      "intercepted_1.db = {};"
      "intercepted_2.db = {};");

  ExpectInt32(
      "var obj = intercepted_1;"
      "obj.x = 4;"
      "eval('obj.x');"
      "eval('obj.x');"
      "eval('obj.x');"
      "obj = intercepted_2;"
      "obj.x = 9;"
      "eval('obj.x');",
      9);
}

namespace {
v8::Intercepted CheckReceiver(Local<Name> name,
                              const v8::PropertyCallbackInfo<v8::Value>& info) {
  CHECK(info.This()->IsObject());
  return v8::Intercepted::kNo;
}
}  // namespace

TEST(Regress609134Interceptor) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  auto fun_templ = v8::FunctionTemplate::New(isolate);
  fun_templ->InstanceTemplate()->SetHandler(
      v8::NamedPropertyHandlerConfiguration(CheckReceiver));

  CHECK(env->Global()
            ->Set(env.local(), v8_str("Fun"),
                  fun_templ->GetFunction(env.local()).ToLocalChecked())
            .FromJust());

  CompileRun(
      "var f = new Fun();"
      "Number.prototype.__proto__ = f;"
      "var a = 42;"
      "for (var i = 0; i<3; i++) { a.foo; }");
}

namespace {

v8::Intercepted Regress42204611_Getter(
    Local<Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
  std::vector<std::string>* calls = reinterpret_cast<std::vector<std::string>*>(
      info.Data().As<v8::External>()->Value());

  calls->push_back("getter");
  return v8::Intercepted::kNo;
}
v8::Intercepted Regress42204611_Setter(
    Local<Name> name, Local<Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
  std::vector<std::string>* calls = reinterpret_cast<std::vector<std::string>*>(
      info.Data().As<v8::External>()->Value());

  calls->push_back("setter");
  return v8::Intercepted::kNo;
}
v8::Intercepted Regress42204611_Definer(
    Local<Name> name, const v8::PropertyDescriptor& descriptor,
    const v8::PropertyCallbackInfo<void>& info) {
  std::vector<std::string>* calls = reinterpret_cast<std::vector<std::string>*>(
      info.Data().As<v8::External>()->Value());

  calls->push_back("definer");
  return v8::Intercepted::kNo;
}

}  // namespace

// Regression test for crbug.com/42204611
THREADED_TEST(Regress42204611) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  std::vector<std::string> calls;
  Local<v8::External> calls_ext = v8::External::New(CcTest::isolate(), &calls);

  v8::Local<v8::ObjectTemplate> object_template =
      v8::ObjectTemplate::New(isolate);
  object_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
      Regress42204611_Getter, Regress42204611_Setter, nullptr, nullptr, nullptr,
      Regress42204611_Definer, calls_ext,
      static_cast<v8::PropertyHandlerFlags>(
          static_cast<int>(v8::PropertyHandlerFlags::kNonMasking) |
          static_cast<int>(v8::PropertyHandlerFlags::kHasNoSideEffect))));

  v8::Local<v8::Context> ctx =
      v8::Context::New(CcTest::isolate(), nullptr, object_template);

  {
    v8::Context::Scope scope(ctx);
    CompileRun(
        "Object.defineProperty(globalThis, 'key', {"
        "  value: 9, enumerable: true, configurable: true, writable: true"
        "})");
  }

  // We should intercept:
  //   1. The getter when getting the current property attributes,
  //   2. The definer when trying to intercept the define itself,
  //   3. The setter when applying the property descriptor.
  CHECK_EQ(calls, std::vector<std::string>({"getter", "definer", "setter"}));
}
