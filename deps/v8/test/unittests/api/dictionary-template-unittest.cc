// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-template.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

using DictionaryTemplateTest = v8::TestWithContext;

namespace v8 {

namespace {

v8::Local<v8::String> v8_str(v8::Isolate* isolate, const char* x) {
  return v8::String::NewFromUtf8(isolate, x).ToLocalChecked();
}

v8::Local<v8::Integer> v8_int(v8::Isolate* isolate, int x) {
  return v8::Integer::New(isolate, x);
}

}  // namespace

TEST_F(DictionaryTemplateTest, SetPropertiesAndInstantiateWithoutValues) {
  HandleScope scope(isolate());
  constexpr std::string_view property_names[] = {"a", "b"};
  Local<DictionaryTemplate> tpl =
      DictionaryTemplate::New(isolate(), property_names);

  MaybeLocal<Value> values[2];
  Local<Object> instance = tpl->NewInstance(context(), values);
  EXPECT_FALSE(instance.IsEmpty());
  EXPECT_FALSE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "a")).ToChecked());
  EXPECT_FALSE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "b")).ToChecked());
}

TEST_F(DictionaryTemplateTest, SetPropertiesAndInstantiateWithSomeValues) {
  HandleScope scope(isolate());
  constexpr std::string_view property_names[] = {"a", "b"};
  Local<DictionaryTemplate> tpl =
      DictionaryTemplate::New(isolate(), property_names);

  MaybeLocal<Value> values[2] = {{}, v8_str(isolate(), "b_value")};
  Local<Object> instance = tpl->NewInstance(context(), values);
  EXPECT_FALSE(instance.IsEmpty());
  EXPECT_FALSE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "a")).ToChecked());
  EXPECT_TRUE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "b")).ToChecked());
}

TEST_F(DictionaryTemplateTest, SetPropertiesAndInstantiateWithAllValues) {
  HandleScope scope(isolate());
  constexpr std::string_view property_names[] = {"a", "b"};
  Local<DictionaryTemplate> tpl =
      DictionaryTemplate::New(isolate(), property_names);

  MaybeLocal<Value> values[2] = {v8_str(isolate(), "a_value"),
                                 v8_str(isolate(), "b_value")};
  Local<Object> instance = tpl->NewInstance(context(), values);
  EXPECT_FALSE(instance.IsEmpty());
  EXPECT_TRUE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "a")).ToChecked());
  EXPECT_TRUE(
      instance->HasOwnProperty(context(), v8_str(isolate(), "b")).ToChecked());
}

TEST_F(DictionaryTemplateTest,
       TestPropertyTransitionWithDifferentRepresentation) {
  HandleScope scope(isolate());

  constexpr std::string_view property_names[] = {"q", "a"};
  Local<DictionaryTemplate> tpl =
      DictionaryTemplate::New(isolate(), property_names);

  constexpr int32_t kBoxedInt = 1 << 31;
  MaybeLocal<Value> values[2] = {{}, v8_int(isolate(), kBoxedInt)};
  Local<Object> instance1 = tpl->NewInstance(context(), values);
  auto value1 =
      instance1->Get(context(), v8_str(isolate(), "a")).ToLocalChecked();
  EXPECT_EQ(Int32::Cast(*value1)->Value(), kBoxedInt);

  // Now transition from a boxed int to a SMI.
  constexpr int32_t kSmi = 42;
  values[1] = v8_int(isolate(), kSmi);
  Local<Object> instance2 = tpl->NewInstance(context(), values);

  auto value2 =
      instance2->Get(context(), v8_str(isolate(), "a")).ToLocalChecked();
  EXPECT_EQ(Int32::Cast(*value2)->Value(), kSmi);

  // Now from SMI back to a boxed int again, just in case.
  values[1] = v8_int(isolate(), kBoxedInt);
  Local<Object> instance3 = tpl->NewInstance(context(), values);

  auto value3 =
      instance3->Get(context(), v8_str(isolate(), "a")).ToLocalChecked();
  EXPECT_EQ(Int32::Cast(*value3)->Value(), kBoxedInt);
}

TEST_F(DictionaryTemplateTest, PrototypeContext) {
  HandleScope handle_scope(isolate());

  constexpr std::string_view property_names[] = {"a", "b"};
  Local<DictionaryTemplate> tpl =
      DictionaryTemplate::New(isolate(), property_names);

  MaybeLocal<Value> fast_values[2] = {v8_str(isolate(), "a_value"),
                                      v8_str(isolate(), "b_value")};

  MaybeLocal<Value> slow_values[2] = {{}, v8_str(isolate(), "b_value")};

  Local<Object> instance1 = tpl->NewInstance(context(), fast_values);
  Local<Object> object1 = v8::Object::New(isolate());

  Local<Object> instance2, instance3;
  Local<Object> object2;
  {
    Local<Context> context2 = Context::New(isolate());
    v8::Context::Scope scope(context2);
    instance2 = tpl->NewInstance(context2, fast_values);
    instance3 = tpl->NewInstance(context2, slow_values);
    object2 = v8::Object::New(isolate());
  }

  EXPECT_TRUE(instance1->GetPrototypeV2() == object1->GetPrototypeV2());
  EXPECT_TRUE(instance2->GetPrototypeV2() == object2->GetPrototypeV2());

  EXPECT_FALSE(instance1->GetPrototypeV2() == instance2->GetPrototypeV2());
  EXPECT_TRUE(instance2->GetPrototypeV2() == instance3->GetPrototypeV2());
}

}  // namespace v8
