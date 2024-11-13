// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using GlobalObjectTest = TestWithContext;

// This test fails if properties on the prototype of the global object appear
// as declared globals.
TEST_F(GlobalObjectTest, StrictUndeclaredGlobalVariable) {
  Local<String> var_name = NewString("x");
  TryCatch try_catch(isolate());
  Local<Object> proto = Object::New(isolate());
  Local<Object> global = context()->Global();
  proto->Set(context(), var_name, Number::New(isolate(), 100)).FromJust();
  global->SetPrototypeV2(context(), proto).FromJust();
  CHECK_EQ(global->GetPrototypeV2(), proto);
  CHECK(TryRunJS("\"use strict\"; x = 42;").IsEmpty());
  CHECK(try_catch.HasCaught());
  String::Utf8Value exception(isolate(), try_catch.Exception());
  CHECK_EQ(0, strcmp("ReferenceError: x is not defined", *exception));
}

TEST_F(GlobalObjectTest, KeysGlobalObject_Regress2764) {
  Local<Context> env1 = context();
  // Create second environment.
  Local<Context> env2 = Context::New(env1->GetIsolate());

  Local<Value> token = NewString("foo");

  // Set same security token for env1 and env2.
  env1->SetSecurityToken(token);
  env2->SetSecurityToken(token);

  // Create a reference to env2 global from env1 global.
  env1->Global()->Set(env1, NewString("global2"), env2->Global()).FromJust();
  // Set some global variables in global2
  env2->Global()->Set(env2, NewString("a"), NewString("a")).FromJust();
  env2->Global()->Set(env2, NewString("42"), NewString("42")).FromJust();

  // List all entries from global2.
  Local<Array> result;
  result = Local<Array>::Cast(RunJS("Object.keys(global2)"));
  CHECK_EQ(2u, result->Length());
  CHECK(NewString("42")
            ->Equals(env1, result->Get(env1, 0).ToLocalChecked())
            .FromJust());
  CHECK(NewString("a")
            ->Equals(env1, result->Get(env1, 1).ToLocalChecked())
            .FromJust());

  result = Local<Array>::Cast(RunJS("Object.getOwnPropertyNames(global2)"));
  CHECK_LT(2u, result->Length());
  // Check that all elements are in the property names
  CHECK(RunJS("-1 < Object.getOwnPropertyNames(global2).indexOf('42')")
            ->IsTrue());
  CHECK(
      RunJS("-1 < Object.getOwnPropertyNames(global2).indexOf('a')")->IsTrue());

  // Hold on to global from env2 and detach global from env2.
  env2->DetachGlobal();

  // List again all entries from the detached global2.
  result = Local<Array>::Cast(RunJS("Object.keys(global2)"));
  CHECK_EQ(0u, result->Length());
  result = Local<Array>::Cast(RunJS("Object.getOwnPropertyNames(global2)"));
  CHECK_EQ(0u, result->Length());
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
START_ALLOW_USE_DEPRECATED()

TEST_F(GlobalObjectTest, KeysGlobalObject_SetPrototype) {
  Local<Context> env1 = context();
  // Create second environment.
  Local<Context> env2 = Context::New(env1->GetIsolate());

  Local<Value> token = NewString("foo");

  // Set same security token for env1 and env2.
  env1->SetSecurityToken(token);
  env2->SetSecurityToken(token);

  // Create a reference to env2 global from env1 global.
  env1->Global()->SetPrototypeV2(env1, env2->Global()).FromJust();
  CHECK_EQ(env1->Global()->GetPrototypeV2(), env2->Global());
  CHECK_EQ(env1->Global()->GetPrototype().As<Object>()->GetPrototype(),
           env2->Global());

  // Set some global variables in global2
  env2->Global()->Set(env2, NewString("a"), NewString("a")).FromJust();
  env2->Global()->Set(env2, NewString("42"), NewString("42")).FromJust();

  // List all entries from global2.
  CHECK(RunJS("a == 'a'")->IsTrue());
}

// Allow usages of v8::Object::GetPrototype() for now.
// TODO(https://crbug.com/333672197): remove.
END_ALLOW_USE_DEPRECATED()

}  // namespace v8
