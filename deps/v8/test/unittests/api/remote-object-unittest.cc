// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-context.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-template.h"
#include "src/api/api-inl.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace remote_object_unittest {

using RemoteObjectTest = TestWithIsolate;

namespace {

bool AccessCheck(Local<Context> accessing_context,
                 Local<Object> accessed_object, Local<Value> data) {
  return false;
}

v8::Intercepted NamedGetter(Local<Name> property,
                            const PropertyCallbackInfo<Value>& info) {
  return v8::Intercepted::kNo;
}

void Constructor(const FunctionCallbackInfo<Value>& info) {
  ASSERT_TRUE(info.IsConstructCall());
}

}  // namespace

TEST_F(RemoteObjectTest, CreationContextOfRemoteContext) {
  Local<ObjectTemplate> global_template = ObjectTemplate::New(isolate());
  global_template->SetAccessCheckCallbackAndHandler(
      AccessCheck, NamedPropertyHandlerConfiguration(NamedGetter),
      IndexedPropertyHandlerConfiguration());

  Local<Object> remote_context =
      Context::NewRemoteContext(isolate(), global_template).ToLocalChecked();
  EXPECT_TRUE(remote_context->GetCreationContext(isolate()).IsEmpty());
}

TEST_F(RemoteObjectTest, CreationContextOfRemoteObject) {
  Local<FunctionTemplate> constructor_template =
      FunctionTemplate::New(isolate(), Constructor);
  constructor_template->InstanceTemplate()->SetAccessCheckCallbackAndHandler(
      AccessCheck, NamedPropertyHandlerConfiguration(NamedGetter),
      IndexedPropertyHandlerConfiguration());

  Local<Object> remote_object =
      constructor_template->NewRemoteInstance().ToLocalChecked();
  EXPECT_TRUE(remote_object->GetCreationContext(isolate()).IsEmpty());
}

TEST_F(RemoteObjectTest, RemoteContextInstanceChecks) {
  Local<FunctionTemplate> parent_template =
      FunctionTemplate::New(isolate(), Constructor);

  Local<FunctionTemplate> constructor_template =
      FunctionTemplate::New(isolate(), Constructor);
  constructor_template->InstanceTemplate()->SetAccessCheckCallbackAndHandler(
      AccessCheck, NamedPropertyHandlerConfiguration(NamedGetter),
      IndexedPropertyHandlerConfiguration());
  constructor_template->Inherit(parent_template);

  Local<Object> remote_context =
      Context::NewRemoteContext(isolate(),
                                constructor_template->InstanceTemplate())
          .ToLocalChecked();
  EXPECT_TRUE(parent_template->HasInstance(remote_context));
  EXPECT_TRUE(constructor_template->HasInstance(remote_context));
}

TEST_F(RemoteObjectTest, TypeOfRemoteContext) {
  Local<ObjectTemplate> global_template = ObjectTemplate::New(isolate());
  global_template->SetAccessCheckCallbackAndHandler(
      AccessCheck, NamedPropertyHandlerConfiguration(NamedGetter),
      IndexedPropertyHandlerConfiguration());

  Local<Object> remote_context =
      Context::NewRemoteContext(isolate(), global_template).ToLocalChecked();
  String::Utf8Value result(isolate(), remote_context->TypeOf(isolate()));
  EXPECT_STREQ("object", *result);
}

TEST_F(RemoteObjectTest, TypeOfRemoteObject) {
  Local<FunctionTemplate> constructor_template =
      FunctionTemplate::New(isolate(), Constructor);
  constructor_template->InstanceTemplate()->SetAccessCheckCallbackAndHandler(
      AccessCheck, NamedPropertyHandlerConfiguration(NamedGetter),
      IndexedPropertyHandlerConfiguration());

  Local<Object> remote_object =
      constructor_template->NewRemoteInstance().ToLocalChecked();
  String::Utf8Value result(isolate(), remote_object->TypeOf(isolate()));
  EXPECT_STREQ("object", *result);
}

}  // namespace remote_object_unittest
}  // namespace v8
