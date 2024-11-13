// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-context.h"
#include "include/v8-function.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-template.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace {

using ObjectTest = TestWithContext;

void accessor_name_getter_callback(Local<Name>,
                                   const PropertyCallbackInfo<Value>&) {}

TEST_F(ObjectTest, SetAccessorWhenUnconfigurablePropAlreadyDefined) {
  TryCatch try_catch(isolate());

  Local<Object> global = context()->Global();
  Local<String> property_name = String::NewFromUtf8Literal(isolate(), "foo");

  PropertyDescriptor prop_desc;
  prop_desc.set_configurable(false);
  global->DefineProperty(context(), property_name, prop_desc).ToChecked();

  Maybe<bool> result = global->SetNativeDataProperty(
      context(), property_name, accessor_name_getter_callback);
  ASSERT_TRUE(result.IsJust());
  ASSERT_FALSE(result.FromJust());
  ASSERT_FALSE(try_catch.HasCaught());
}

using LapContextTest = TestWithIsolate;

TEST_F(LapContextTest, CurrentContextInLazyAccessorOnPrototype) {
  // The receiver object is created in |receiver_context|, but its prototype
  // object is created in |prototype_context|, and the property is accessed
  // from |caller_context|.
  Local<Context> receiver_context = Context::New(isolate());
  Local<Context> prototype_context = Context::New(isolate());
  Local<Context> caller_context = Context::New(isolate());

  static int call_count;  // The number of calls of the accessor callback.
  call_count = 0;

  Local<FunctionTemplate> function_template = FunctionTemplate::New(isolate());
  Local<Signature> signature = Signature::New(isolate(), function_template);
  Local<String> property_key =
      String::NewFromUtf8Literal(isolate(), "property");
  Local<FunctionTemplate> get_or_set = FunctionTemplate::New(
      isolate(),
      [](const FunctionCallbackInfo<Value>& info) {
        ++call_count;
        Local<Context> prototype_context = *reinterpret_cast<Local<Context>*>(
            info.Data().As<External>()->Value());
        EXPECT_EQ(prototype_context, info.GetIsolate()->GetCurrentContext());
      },
      External::New(isolate(), &prototype_context), signature);
  function_template->PrototypeTemplate()->SetAccessorProperty(
      property_key, get_or_set, get_or_set);

  // |object| is created in |receiver_context|, and |prototype| is created
  // in |prototype_context|.  And then, object.__proto__ = prototype.
  Local<Function> interface_for_receiver =
      function_template->GetFunction(receiver_context).ToLocalChecked();
  Local<Function> interface_for_prototype =
      function_template->GetFunction(prototype_context).ToLocalChecked();
  Local<String> prototype_key =
      String::NewFromUtf8Literal(isolate(), "prototype");
  Local<Object> prototype =
      interface_for_prototype->Get(caller_context, prototype_key)
          .ToLocalChecked()
          .As<Object>();
  Local<Object> object =
      interface_for_receiver->NewInstance(receiver_context).ToLocalChecked();
  object->SetPrototypeV2(caller_context, prototype).ToChecked();
  EXPECT_EQ(receiver_context, object->GetCreationContext().ToLocalChecked());
  EXPECT_EQ(prototype_context,
            prototype->GetCreationContext().ToLocalChecked());

  EXPECT_EQ(0, call_count);
  object->Get(caller_context, property_key).ToLocalChecked();
  EXPECT_EQ(1, call_count);
  object->Set(caller_context, property_key, Null(isolate())).ToChecked();
  EXPECT_EQ(2, call_count);

  // Test with a compiled version.
  Local<String> object_key = String::NewFromUtf8Literal(isolate(), "object");
  caller_context->Global()->Set(caller_context, object_key, object).ToChecked();
  const char script[] =
      "function f() { object.property; object.property = 0; } "
      "%PrepareFunctionForOptimization(f); "
      "f(); f(); "
      "%OptimizeFunctionOnNextCall(f); "
      "f();";
  Context::Scope scope(caller_context);
  internal::v8_flags.allow_natives_syntax = true;
  Script::Compile(caller_context, String::NewFromUtf8Literal(isolate(), script))
      .ToLocalChecked()
      ->Run(caller_context)
      .ToLocalChecked();
  EXPECT_EQ(8, call_count);
}

TEST_F(LapContextTest, CurrentContextInLazyAccessorOnPlatformObject) {
  Local<Context> receiver_context = Context::New(isolate());
  Local<Context> caller_context = Context::New(isolate());

  static int call_count;  // The number of calls of the accessor callback.
  call_count = 0;

  Local<FunctionTemplate> function_template = FunctionTemplate::New(isolate());
  Local<Signature> signature = Signature::New(isolate(), function_template);
  Local<String> property_key =
      String::NewFromUtf8Literal(isolate(), "property");
  Local<FunctionTemplate> get_or_set = FunctionTemplate::New(
      isolate(),
      [](const FunctionCallbackInfo<Value>& info) {
        ++call_count;
        Local<Context> receiver_context = *reinterpret_cast<Local<Context>*>(
            info.Data().As<External>()->Value());
        EXPECT_EQ(receiver_context, info.GetIsolate()->GetCurrentContext());
      },
      External::New(isolate(), &receiver_context), signature);
  function_template->InstanceTemplate()->SetAccessorProperty(
      property_key, get_or_set, get_or_set);

  Local<Function> interface =
      function_template->GetFunction(receiver_context).ToLocalChecked();
  Local<Object> object =
      interface->NewInstance(receiver_context).ToLocalChecked();

  EXPECT_EQ(0, call_count);
  object->Get(caller_context, property_key).ToLocalChecked();
  EXPECT_EQ(1, call_count);
  object->Set(caller_context, property_key, Null(isolate())).ToChecked();
  EXPECT_EQ(2, call_count);

  // Test with a compiled version.
  Local<String> object_key = String::NewFromUtf8Literal(isolate(), "object");
  caller_context->Global()->Set(caller_context, object_key, object).ToChecked();
  const char script[] =
      "function f() { object.property; object.property = 0; } "
      "%PrepareFunctionForOptimization(f);"
      "f(); f(); "
      "%OptimizeFunctionOnNextCall(f); "
      "f();";
  Context::Scope scope(caller_context);
  internal::v8_flags.allow_natives_syntax = true;
  Script::Compile(caller_context, String::NewFromUtf8Literal(isolate(), script))
      .ToLocalChecked()
      ->Run(caller_context)
      .ToLocalChecked();
  EXPECT_EQ(8, call_count);
}

TEST_F(LapContextTest, CurrentContextInLazyAccessorOnInterface) {
  Local<Context> interface_context = Context::New(isolate());
  Local<Context> caller_context = Context::New(isolate());

  static int call_count;  // The number of calls of the accessor callback.
  call_count = 0;

  Local<FunctionTemplate> function_template = FunctionTemplate::New(isolate());
  Local<String> property_key =
      String::NewFromUtf8Literal(isolate(), "property");
  Local<FunctionTemplate> get_or_set = FunctionTemplate::New(
      isolate(),
      [](const FunctionCallbackInfo<Value>& info) {
        ++call_count;
        Local<Context> interface_context = *reinterpret_cast<Local<Context>*>(
            info.Data().As<External>()->Value());
        EXPECT_EQ(interface_context, info.GetIsolate()->GetCurrentContext());
      },
      External::New(isolate(), &interface_context), Local<Signature>());
  function_template->SetAccessorProperty(property_key, get_or_set, get_or_set);

  Local<Function> interface =
      function_template->GetFunction(interface_context).ToLocalChecked();

  EXPECT_EQ(0, call_count);
  interface->Get(caller_context, property_key).ToLocalChecked();
  EXPECT_EQ(1, call_count);
  interface->Set(caller_context, property_key, Null(isolate())).ToChecked();
  EXPECT_EQ(2, call_count);

  // Test with a compiled version.
  Local<String> interface_key =
      String::NewFromUtf8Literal(isolate(), "Interface");
  caller_context->Global()
      ->Set(caller_context, interface_key, interface)
      .ToChecked();
  const char script[] =
      "function f() { Interface.property; Interface.property = 0; } "
      "%PrepareFunctionForOptimization(f);"
      "f(); f(); "
      "%OptimizeFunctionOnNextCall(f); "
      "f();";
  Context::Scope scope(caller_context);
  internal::v8_flags.allow_natives_syntax = true;
  Script::Compile(caller_context, String::NewFromUtf8Literal(isolate(), script))
      .ToLocalChecked()
      ->Run(caller_context)
      .ToLocalChecked();
  EXPECT_EQ(8, call_count);
}

}  // namespace
}  // namespace v8
