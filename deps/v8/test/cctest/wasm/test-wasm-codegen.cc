// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests effects of (CSP) "unsafe-eval" and "wasm-eval" callback functions.
//
// Note: These tests are in a separate test file because the tests dynamically
// change the isolate in terms of allow_wasm_code_gen_callback.

#include "src/api/api-inl.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

// Possible values for callback pointers.
enum TestValue {
  kTestUsingNull,   // no callback.
  kTestUsingFalse,  // callback returning false.
  kTestUsingTrue,   // callbacl returning true.
};

constexpr int kNumTestValues = 3;

const char* TestValueName[kNumTestValues] = {"null", "false", "true"};

// Defined to simplify iterating over TestValues;
const TestValue AllTestValues[kNumTestValues] = {
    kTestUsingNull, kTestUsingFalse, kTestUsingTrue};

// This list holds the results of setting allow_wasm_code_gen_callback using
// TestValue's. The value in the list is true if code gen is
// allowed, and false otherwise.
const bool ExpectedResults[kNumTestValues] = {true, false, true};

bool TrueCallback(Local<v8::Context>, Local<v8::String>) { return true; }

bool FalseCallback(Local<v8::Context>, Local<v8::String>) { return false; }

using CallbackFn = bool (*)(Local<v8::Context>, Local<v8::String>);

// Defines the Callback to use for the corresponding TestValue.
CallbackFn Callback[kNumTestValues] = {nullptr, FalseCallback, TrueCallback};

void BuildTrivialModule(Zone* zone, ZoneBuffer* buffer) {
  WasmModuleBuilder* builder = zone->New<WasmModuleBuilder>(zone);
  builder->WriteTo(buffer);
}

bool TestModule(Isolate* isolate, v8::MemorySpan<const uint8_t> wire_bytes) {
  HandleScope scope(isolate);
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  v8::Local<v8::Context> context = Utils::ToLocal(isolate->native_context());

  // Get the "WebAssembly.Module" function.
  auto get_property = [context, v8_isolate](
                          v8::Local<v8::Object> obj,
                          const char* property_name) -> v8::Local<v8::Object> {
    auto name = v8::String::NewFromUtf8(v8_isolate, property_name,
                                        NewStringType::kInternalized)
                    .ToLocalChecked();
    return obj->Get(context, name).ToLocalChecked().As<v8::Object>();
  };
  auto wasm_class = get_property(context->Global(), "WebAssembly");
  auto module_class = get_property(wasm_class, "Module");

  // Create an arraybuffer with the wire bytes.
  v8::Local<v8::ArrayBuffer> buf =
      v8::ArrayBuffer::New(v8_isolate, wire_bytes.size());
  memcpy(static_cast<uint8_t*>(buf->GetBackingStore()->Data()),
         wire_bytes.data(), wire_bytes.size());

  // Now call the "WebAssembly.Module" function with the array buffer. Return
  // true if this succeeded, false otherwise.
  v8::TryCatch try_catch(v8_isolate);
  v8::Local<v8::Value> args[] = {buf};
  MaybeLocal<Value> module_object =
      module_class->CallAsConstructor(context, arraysize(args), args);

  CHECK_EQ(try_catch.HasCaught(), module_object.IsEmpty());
  return !module_object.IsEmpty();
}

}  // namespace

TEST(PropertiesOfCodegenCallbacks) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  ZoneBuffer buffer(&zone);
  BuildTrivialModule(&zone, &buffer);
  v8::MemorySpan<const uint8_t> wire_bytes = {buffer.begin(), buffer.size()};
  Isolate* isolate = CcTest::InitIsolateOnce();
  v8::Isolate* v8_isolate = CcTest::isolate();
  HandleScope scope(isolate);

  for (TestValue wasm_codegen : AllTestValues) {
    fprintf(stderr, "Test wasm_codegen = %s\n", TestValueName[wasm_codegen]);
    v8_isolate->SetAllowWasmCodeGenerationCallback(Callback[wasm_codegen]);
    bool found = TestModule(isolate, wire_bytes);
    bool expected = ExpectedResults[wasm_codegen];
    CHECK_EQ(expected, found);
    heap::InvokeMemoryReducingMajorGCs(isolate->heap());
  }
}

TEST(WasmModuleObjectCompileFailure) {
  const uint8_t wire_bytes_arr[] = {0xDE, 0xAD, 0xBE, 0xEF};
  v8::MemorySpan<const uint8_t> wire_bytes = {wire_bytes_arr,
                                              arraysize(wire_bytes_arr)};
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);
  CHECK(!TestModule(isolate, wire_bytes));
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
