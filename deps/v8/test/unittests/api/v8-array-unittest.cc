// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-container.h"
#include "include/v8-primitive.h"
#include "include/v8-value.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace {

using ArrayTest = TestWithContext;
using CbResult = v8::Array::CallbackResult;

TEST_F(ArrayTest, IterateEmpty) {
  HandleScope scope(isolate());
  Local<Array> array = Array::New(isolate());
  Array::IterationCallback unreachable_callback =
      [](uint32_t index, Local<Value> element, void* data) -> CbResult {
    UNREACHABLE();
  };
  CHECK(array->Iterate(context(), unreachable_callback, nullptr).IsJust());
}

TEST_F(ArrayTest, IterateOneElement) {
  HandleScope scope(isolate());
  Local<Array> smi_array = Array::New(isolate());
  Local<Array> double_array = Array::New(isolate());
  Local<Array> object_array = Array::New(isolate());
  Local<Array> dictionary_array = Array::New(isolate());
  struct Data {
    int sentinel;
    Local<Context> context;
    Isolate* isolate;
    int invocation_count = 0;
  };
  Data data{42, context(), isolate()};
  const Local<Value> kSmi = Number::New(isolate(), 333);
  const uint32_t kIndex = 3;

  CHECK(smi_array->Set(context(), kIndex, kSmi).FromJust());
  Array::IterationCallback smi_callback =
      [](uint32_t index, Local<Value> element, void* data) -> CbResult {
    Data* d = reinterpret_cast<Data*>(data);
    CHECK_EQ(42, d->sentinel);
    ++d->invocation_count;
    if (index != kIndex) {
      CHECK(element->IsUndefined());
      return CbResult::kContinue;
    }
    CHECK_EQ(333, element->NumberValue(d->context).FromJust());
    return CbResult::kContinue;
  };
  CHECK(smi_array->Iterate(context(), smi_callback, &data).IsJust());
  CHECK_EQ(kIndex + 1, data.invocation_count);

  const Local<Value> kDouble = Number::New(isolate(), 1.5);
  CHECK(double_array->Set(context(), kIndex, kDouble).FromJust());
  Array::IterationCallback double_callback =
      [](uint32_t index, Local<Value> element, void* data) -> CbResult {
    Data* d = reinterpret_cast<Data*>(data);
    CHECK_EQ(42, d->sentinel);
    if (index != kIndex) {
      CHECK(element->IsUndefined());
      return CbResult::kContinue;
    }
    CHECK_EQ(1.5, element->NumberValue(d->context).FromJust());
    return CbResult::kContinue;
  };
  CHECK(double_array->Iterate(context(), double_callback, &data).IsJust());

  // An "object" in the ElementsKind sense.
  const Local<Value> kObject = String::NewFromUtf8Literal(isolate(), "foo");
  CHECK(object_array->Set(context(), kIndex, kObject).FromJust());
  Array::IterationCallback object_callback =
      [](uint32_t index, Local<Value> element, void* data) -> CbResult {
    Data* d = reinterpret_cast<Data*>(data);
    CHECK_EQ(42, d->sentinel);
    if (index != kIndex) {
      CHECK(element->IsUndefined());
      return CbResult::kContinue;
    }
    CHECK_EQ(kIndex, index);
    Local<String> str = element->ToString(d->context).ToLocalChecked();
    CHECK_EQ(0, strcmp("foo", *String::Utf8Value(d->isolate, str)));
    return CbResult::kContinue;
  };
  CHECK(object_array->Iterate(context(), object_callback, &data).IsJust());

  Local<String> zero = String::NewFromUtf8Literal(isolate(), "0");
  CHECK(dictionary_array->DefineOwnProperty(context(), zero, kSmi, v8::ReadOnly)
            .FromJust());
  Array::IterationCallback dictionary_callback =
      [](uint32_t index, Local<Value> element, void* data) -> CbResult {
    Data* d = reinterpret_cast<Data*>(data);
    CHECK_EQ(42, d->sentinel);
    CHECK_EQ(0, index);
    CHECK_EQ(333, element->NumberValue(d->context).FromJust());
    return CbResult::kContinue;
  };
  CHECK(dictionary_array->Iterate(context(), dictionary_callback, &data)
            .IsJust());
}

static void GetElement(Local<Name> name,
                       const v8::PropertyCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  Isolate* isolate = info.GetIsolate();
  Local<String> zero_str = String::NewFromUtf8Literal(isolate, "0");
  Local<Value> zero_num = Number::New(isolate, 123);
  CHECK(name->Equals(isolate->GetCurrentContext(), zero_str).FromJust());
  info.GetReturnValue().Set(zero_num);
}

TEST_F(ArrayTest, IterateAccessorElements) {
  HandleScope scope(isolate());
  // {SetAccessor} doesn't automatically set the length.
  Local<Array> array = Array::New(isolate(), 1);
  struct Data {
    int sentinel;
    Local<Context> context;
    Isolate* isolate;
  };
  Data data{42, context(), isolate()};
  Local<String> zero = String::NewFromUtf8Literal(isolate(), "0");
  CHECK(array->SetNativeDataProperty(context(), zero, GetElement).FromJust());
  Array::IterationCallback callback = [](uint32_t index, Local<Value> element,
                                         void* data) -> CbResult {
    Data* d = reinterpret_cast<Data*>(data);
    CHECK_EQ(0, index);
    CHECK_EQ(123, element->NumberValue(d->context).FromJust());
    d->sentinel = 234;
    return CbResult::kContinue;
  };
  CHECK(array->Iterate(context(), callback, &data).IsJust());
  CHECK_EQ(234, data.sentinel);  // Callback has been called at least once.
}

TEST_F(ArrayTest, IterateEarlyTermination) {
  HandleScope scope(isolate());
  Local<Array> array = Array::New(isolate());
  const Local<Value> kValue = Number::New(isolate(), 333);
  CHECK(array->Set(context(), 0, kValue).FromJust());
  CHECK(array->Set(context(), 1, kValue).FromJust());
  CHECK(array->Set(context(), 2, kValue).FromJust());

  Array::IterationCallback exception_callback =
      [](uint32_t index, Local<Value> element, void* data) -> CbResult {
    CHECK_EQ(0, index);
    return CbResult::kException;
  };
  CHECK(array->Iterate(context(), exception_callback, nullptr).IsNothing());

  Array::IterationCallback break_callback =
      [](uint32_t index, Local<Value> element, void* data) -> CbResult {
    CHECK_EQ(0, index);
    return CbResult::kBreak;
  };
  CHECK(array->Iterate(context(), break_callback, nullptr).IsJust());
}

}  // namespace
}  // namespace v8
