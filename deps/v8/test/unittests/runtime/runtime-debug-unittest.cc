// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-exception.h"
#include "include/v8-local-handle.h"
#include "include/v8-object.h"
#include "include/v8-template.h"
#include "src/api/api.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/objects-inl.h"
#include "src/runtime/runtime.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8::internal {

using RuntimeTest = TestWithContext;

TEST_F(RuntimeTest, ReturnsPrototype) {
  TryCatch try_catch(isolate());

  Local<v8::Object> object = v8::Object::New(isolate());
  Handle<JSArray> i_result =
      Runtime::GetInternalProperties(i_isolate(), Utils::OpenHandle(*object))
          .ToHandleChecked();
  Local<Array> result = Utils::ToLocal(i_result);
  EXPECT_GE(result->Length(), 1u);

  char name_buffer[100];
  result->Get(context(), 0)
      .ToLocalChecked()
      .As<v8::String>()
      ->WriteUtf8(isolate(), name_buffer);
  EXPECT_EQ("[[Prototype]]", std::string(name_buffer));
}

bool AccessCheck(Local<v8::Context> accessing_context,
                 Local<v8::Object> accessed_object, Local<Value> data) {
  return false;
}

TEST_F(RuntimeTest, DoesNotReturnPrototypeWhenInacessible) {
  TryCatch try_catch(isolate());

  Local<ObjectTemplate> object_template = ObjectTemplate::New(isolate());
  object_template->SetAccessCheckCallback(AccessCheck);

  Local<v8::Object> object =
      object_template->NewInstance(context()).ToLocalChecked();
  Handle<JSArray> i_result =
      Runtime::GetInternalProperties(i_isolate(), Utils::OpenHandle(*object))
          .ToHandleChecked();
  Local<Array> result = Utils::ToLocal(i_result);
  EXPECT_EQ(0u, result->Length());
}

#if V8_ENABLE_WEBASSEMBLY
TEST_F(RuntimeTest, WasmTableWithoutInstance) {
  uint32_t initial = 1u;
  bool has_maximum = false;
  uint32_t maximum = std::numeric_limits<uint32_t>::max();
  Handle<WasmTableObject> table = WasmTableObject::New(
      i_isolate(), Handle<WasmInstanceObject>(), wasm::kWasmAnyRef, initial,
      has_maximum, maximum, i_isolate()->factory()->null_value());
  MaybeHandle<JSArray> result =
      Runtime::GetInternalProperties(i_isolate(), table);
  ASSERT_FALSE(result.is_null());
  // ["[[Prototype]]", <map>, "[[Entries]]", <entries>]
  ASSERT_EQ(4, result.ToHandleChecked()->elements()->length());
  Handle<Object> entries =
      Object::GetElement(i_isolate(), result.ToHandleChecked(), 3)
          .ToHandleChecked();
  EXPECT_EQ(1, JSArray::cast(*entries)->elements()->length());
}
#endif

}  // namespace v8::internal
