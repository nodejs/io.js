// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-exception.h"
#include "include/v8-local-handle.h"
#include "include/v8-object.h"
#include "include/v8-primitive.h"
#include "include/v8-template.h"
#include "src/api/api.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace debug {
namespace {

using DebugPropertyIteratorTest = TestWithContext;

TEST_F(DebugPropertyIteratorTest, WalksPrototypeChain) {
  TryCatch try_catch(isolate());

  Local<Object> object = Object::New(isolate());

  ASSERT_TRUE(object
                  ->CreateDataProperty(
                      context(),
                      String::NewFromUtf8Literal(isolate(), "own_property"),
                      Number::New(isolate(), 42))
                  .FromMaybe(false));

  Local<Object> prototype = Object::New(isolate());
  ASSERT_TRUE(object->SetPrototypeV2(context(), prototype).FromMaybe(false));
  ASSERT_TRUE(prototype
                  ->CreateDataProperty(context(),
                                       String::NewFromUtf8Literal(
                                           isolate(), "prototype_property"),
                                       Number::New(isolate(), 21))
                  .FromMaybe(false));

  auto iterator = PropertyIterator::Create(context(), object);
  ASSERT_NE(iterator, nullptr);
  ASSERT_FALSE(iterator->Done());
  EXPECT_TRUE(iterator->is_own());
  char name_buffer[100];
  iterator->name().As<v8::String>()->WriteUtf8(isolate(), name_buffer);
  EXPECT_EQ("own_property", std::string(name_buffer));
  ASSERT_TRUE(iterator->Advance().FromMaybe(false));

  ASSERT_FALSE(iterator->Done());
  EXPECT_TRUE(iterator->is_own());
  iterator->name().As<v8::String>()->WriteUtf8(isolate(), name_buffer);
  EXPECT_EQ("own_property", std::string(name_buffer));
  ASSERT_TRUE(iterator->Advance().FromMaybe(false));

  ASSERT_FALSE(iterator->Done());
  EXPECT_FALSE(iterator->is_own());
  iterator->name().As<v8::String>()->WriteUtf8(isolate(), name_buffer);
  EXPECT_EQ("prototype_property", std::string(name_buffer));
  ASSERT_TRUE(iterator->Advance().FromMaybe(false));

  ASSERT_FALSE(iterator->Done());
}

bool may_access = true;

bool AccessCheck(Local<Context> accessing_context,
                 Local<Object> accessed_object, Local<Value> data) {
  return may_access;
}

TEST_F(DebugPropertyIteratorTest, DoestWalksPrototypeChainIfInaccesible) {
  TryCatch try_catch(isolate());

  Local<ObjectTemplate> object_template = ObjectTemplate::New(isolate());
  object_template->SetAccessCheckCallback(AccessCheck);

  Local<Object> object =
      object_template->NewInstance(context()).ToLocalChecked();
  ASSERT_TRUE(object
                  ->CreateDataProperty(
                      context(),
                      String::NewFromUtf8Literal(isolate(), "own_property"),
                      Number::New(isolate(), 42))
                  .FromMaybe(false));

  auto iterator = PropertyIterator::Create(context(), object);
  may_access = false;
  ASSERT_NE(iterator, nullptr);
  ASSERT_FALSE(iterator->Done());
  EXPECT_TRUE(iterator->is_own());
  char name_buffer[100];
  iterator->name().As<v8::String>()->WriteUtf8(isolate(), name_buffer);
  EXPECT_EQ("own_property", std::string(name_buffer));
  ASSERT_TRUE(iterator->Advance().FromMaybe(false));

  ASSERT_TRUE(iterator->Done());
}

TEST_F(DebugPropertyIteratorTest, SkipsIndicesOnArrays) {
  TryCatch try_catch(isolate());

  Local<Value> elements[2] = {
      Number::New(isolate(), 21),
      Number::New(isolate(), 42),
  };
  auto array = Array::New(isolate(), elements, arraysize(elements));

  auto iterator = PropertyIterator::Create(context(), array, true);
  while (!iterator->Done()) {
    ASSERT_FALSE(iterator->is_array_index());
    ASSERT_TRUE(iterator->Advance().FromMaybe(false));
  }
}

TEST_F(DebugPropertyIteratorTest, SkipsIndicesOnObjects) {
  TryCatch try_catch(isolate());

  Local<Name> names[2] = {
      String::NewFromUtf8Literal(isolate(), "42"),
      String::NewFromUtf8Literal(isolate(), "x"),
  };
  Local<Value> values[arraysize(names)] = {
      Number::New(isolate(), 42),
      Number::New(isolate(), 21),
  };
  Local<Object> object =
      Object::New(isolate(), Null(isolate()), names, values, arraysize(names));

  auto iterator = PropertyIterator::Create(context(), object, true);
  while (!iterator->Done()) {
    ASSERT_FALSE(iterator->is_array_index());
    ASSERT_TRUE(iterator->Advance().FromMaybe(false));
  }
}

TEST_F(DebugPropertyIteratorTest, SkipsIndicesOnTypedArrays) {
  TryCatch try_catch(isolate());

  auto buffer = ArrayBuffer::New(isolate(), 1024 * 1024);
  auto array = Uint8Array::New(buffer, 0, 1024 * 1024);

  auto iterator = PropertyIterator::Create(context(), array, true);
  while (!iterator->Done()) {
    ASSERT_FALSE(iterator->is_array_index());
    ASSERT_TRUE(iterator->Advance().FromMaybe(false));
  }
}

#if V8_CAN_CREATE_SHARED_HEAP_BOOL

using SharedObjectDebugPropertyIteratorTest = TestJSSharedMemoryWithContext;

TEST_F(SharedObjectDebugPropertyIteratorTest, SharedStruct) {
  TryCatch try_catch(isolate());

  const char source_text[] =
      "let S = new SharedStructType(['field', 'another_field']);"
      "new S();";

  auto shared_struct =
      RunJS(context(), source_text)->ToObject(context()).ToLocalChecked();
  auto iterator = PropertyIterator::Create(context(), shared_struct);

  ASSERT_NE(iterator, nullptr);
  ASSERT_FALSE(iterator->Done());
  EXPECT_TRUE(iterator->is_own());
  char name_buffer[64];
  iterator->name().As<v8::String>()->WriteUtf8(isolate(), name_buffer);
  EXPECT_EQ("field", std::string(name_buffer));
  ASSERT_TRUE(iterator->Advance().FromMaybe(false));

  ASSERT_FALSE(iterator->Done());
  EXPECT_TRUE(iterator->is_own());
  iterator->name().As<v8::String>()->WriteUtf8(isolate(), name_buffer);
  EXPECT_EQ("another_field", std::string(name_buffer));
  ASSERT_TRUE(iterator->Advance().FromMaybe(false));

  ASSERT_FALSE(iterator->Done());
}

#endif  // V8_CAN_CREATE_SHARED_HEAP_BOOL

}  // namespace
}  // namespace debug
}  // namespace v8
