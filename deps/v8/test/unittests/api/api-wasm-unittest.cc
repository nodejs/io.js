// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "include/v8-context.h"
#include "include/v8-function-callback.h"
#include "include/v8-local-handle.h"
#include "include/v8-object.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-promise.h"
#include "include/v8-wasm.h"
#include "src/api/api-inl.h"
#include "src/handles/global-handles.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-js.h"
#include "test/common/flag-utils.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

bool wasm_streaming_callback_got_called = false;
bool wasm_streaming_data_got_collected = false;

// The bytes of a minimal WebAssembly module.
const uint8_t kMinimalWasmModuleBytes[]{0x00, 0x61, 0x73, 0x6d,
                                        0x01, 0x00, 0x00, 0x00};

class ApiWasmTest : public TestWithIsolate {
 public:
  void TestWasmStreaming(WasmStreamingCallback callback,
                         Promise::PromiseState expected_state) {
    isolate()->SetWasmStreamingCallback(callback);
    HandleScope scope(isolate());

    Local<Context> context = Context::New(isolate());
    Context::Scope context_scope(context);
    // Call {WebAssembly.compileStreaming} with {null} as parameter. The
    // parameter is only really processed by the embedder, so for this test the
    // value is irrelevant.
    Local<Promise> promise =
        Local<Promise>::Cast(RunJS("WebAssembly.compileStreaming(null)"));
    EmptyMessageQueues();
    CHECK_EQ(expected_state, promise->State());
  }
};

void WasmStreamingTestFinalizer(const WeakCallbackInfo<void>& data) {
  CHECK(!wasm_streaming_data_got_collected);
  wasm_streaming_data_got_collected = true;
  i::GlobalHandles::Destroy(reinterpret_cast<i::Address*>(data.GetParameter()));
}

void WasmStreamingCallbackTestCallbackIsCalled(
    const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  CHECK(!wasm_streaming_callback_got_called);
  wasm_streaming_callback_got_called = true;

  i::Handle<i::Object> global_handle =
      reinterpret_cast<i::Isolate*>(info.GetIsolate())
          ->global_handles()
          ->Create(*Utils::OpenDirectHandle(*info.Data()));
  i::GlobalHandles::MakeWeak(global_handle.location(), global_handle.location(),
                             WasmStreamingTestFinalizer,
                             WeakCallbackType::kParameter);
}

void WasmStreamingCallbackTestFinishWithSuccess(
    const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  std::shared_ptr<WasmStreaming> streaming =
      WasmStreaming::Unpack(info.GetIsolate(), info.Data());
  streaming->OnBytesReceived(kMinimalWasmModuleBytes,
                             arraysize(kMinimalWasmModuleBytes));
  streaming->Finish();
}

void WasmStreamingCallbackTestFinishWithFailure(
    const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  std::shared_ptr<WasmStreaming> streaming =
      WasmStreaming::Unpack(info.GetIsolate(), info.Data());
  streaming->Finish();
}

void WasmStreamingCallbackTestAbortWithReject(
    const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  std::shared_ptr<WasmStreaming> streaming =
      WasmStreaming::Unpack(info.GetIsolate(), info.Data());
  streaming->Abort(Object::New(info.GetIsolate()));
}

void WasmStreamingCallbackTestAbortNoReject(
    const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  std::shared_ptr<WasmStreaming> streaming =
      WasmStreaming::Unpack(info.GetIsolate(), info.Data());
  streaming->Abort({});
}

void WasmStreamingCallbackTestOnBytesReceived(
    const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  std::shared_ptr<WasmStreaming> streaming =
      WasmStreaming::Unpack(info.GetIsolate(), info.Data());

  // The first bytes of the WebAssembly magic word.
  const uint8_t bytes[]{0x00, 0x61, 0x73};
  streaming->OnBytesReceived(bytes, arraysize(bytes));
}

void WasmStreamingMoreFunctionsCanBeSerializedCallback(
    const FunctionCallbackInfo<Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  std::shared_ptr<WasmStreaming> streaming =
      WasmStreaming::Unpack(info.GetIsolate(), info.Data());
  streaming->SetMoreFunctionsCanBeSerializedCallback([](CompiledWasmModule) {});
}

TEST_F(ApiWasmTest, WasmStreamingCallback) {
  TestWasmStreaming(WasmStreamingCallbackTestCallbackIsCalled,
                    Promise::kPending);
  CHECK(wasm_streaming_callback_got_called);
  InvokeMemoryReducingMajorGCs(i_isolate());
  CHECK(wasm_streaming_data_got_collected);
}

TEST_F(ApiWasmTest, WasmStreamingOnBytesReceived) {
  TestWasmStreaming(WasmStreamingCallbackTestOnBytesReceived,
                    Promise::kPending);
}

TEST_F(ApiWasmTest, WasmStreamingFinishWithSuccess) {
  TestWasmStreaming(WasmStreamingCallbackTestFinishWithSuccess,
                    Promise::kFulfilled);
}

TEST_F(ApiWasmTest, WasmStreamingFinishWithFailure) {
  TestWasmStreaming(WasmStreamingCallbackTestFinishWithFailure,
                    Promise::kRejected);
}

TEST_F(ApiWasmTest, WasmStreamingAbortWithReject) {
  TestWasmStreaming(WasmStreamingCallbackTestAbortWithReject,
                    Promise::kRejected);
}

TEST_F(ApiWasmTest, WasmStreamingAbortWithoutReject) {
  TestWasmStreaming(WasmStreamingCallbackTestAbortNoReject, Promise::kPending);
}

TEST_F(ApiWasmTest, WasmCompileToWasmModuleObject) {
  Local<Context> context = Context::New(isolate());
  Context::Scope context_scope(context);
  auto maybe_module = WasmModuleObject::Compile(
      isolate(), {kMinimalWasmModuleBytes, arraysize(kMinimalWasmModuleBytes)});
  CHECK(!maybe_module.IsEmpty());
}

TEST_F(ApiWasmTest, WasmStreamingSetCallback) {
  TestWasmStreaming(WasmStreamingMoreFunctionsCanBeSerializedCallback,
                    Promise::kPending);
}

TEST_F(ApiWasmTest, WasmErrorIsSharedCrossOrigin) {
  Isolate::Scope iscope(isolate());
  HandleScope scope(isolate());
  Local<Context> context = Context::New(isolate());
  Context::Scope cscope(context);

  TryCatch try_catch(isolate());
  // A fairly minimal Wasm module that produces an error at runtime:
  // it returns {null} from an imported function that's typed to return
  // a non-null reference.
  const char* expected_message =
      "Uncaught TypeError: type incompatibility when transforming from/to JS";
  const char* src =
      "let raw = new Uint8Array(["
      "  0x00, 0x61, 0x73, 0x6d,  // wasm magic                            \n"
      "  0x01, 0x00, 0x00, 0x00,  // wasm version                          \n"

      "  0x01, 0x06,              // Type section, length 6                \n"
      "  0x01, 0x60,              // 1 type, kind: func                    \n"
      "  0x00, 0x01, 0x64, 0x6f,  // 0 params, 1 result: (ref extern)      \n"

      "  0x02, 0x07, 0x01,        // Import section, length 7, 1 import    \n"
      "  0x01, 0x6d, 0x01, 0x6e,  // 'm' 'n'                               \n"
      "  0x00, 0x00,              // kind: function $type0                 \n"

      "  0x03, 0x02,              // Function section, length 2            \n"
      "  0x01, 0x00,              // 1 function, $type0                    \n"

      "  0x07, 0x05, 0x01,        // Export section, length 5, 1 export    \n"
      "  0x01, 0x66, 0x00, 0x01,  // 'f': function #1                      \n"

      "  0x0a, 0x06, 0x01,        // Code section, length 6, 1 function    \n"
      "  0x04, 0x00,              // body size 4, 0 locals                 \n"
      "  0x10, 0x00, 0x0b,        // call $m.n; end                        \n"
      "]);                                                                 \n"

      "let mod = new WebAssembly.Module(raw.buffer);                       \n"
      "let instance = new WebAssembly.Instance(mod, {m: {n: () => null}}); \n"
      "instance.exports.f();";

  TryRunJS(src);
  EXPECT_TRUE(try_catch.HasCaught());
  Local<Message> message = try_catch.Message();
  CHECK_EQ(0, strcmp(*String::Utf8Value(isolate(), message->Get()),
                     expected_message));
  EXPECT_TRUE(message->IsSharedCrossOrigin());
}

TEST_F(ApiWasmTest, WasmEnableDisableImportedStrings) {
  Local<Context> context_local = Context::New(isolate());
  Context::Scope context_scope(context_local);
  i::DirectHandle<i::NativeContext> context =
      v8::Utils::OpenDirectHandle(*context_local);
  // Test enabling/disabling via flag.
  {
    i::FlagScope<bool> flag_strings(
        &i::v8_flags.experimental_wasm_imported_strings, true);
    EXPECT_TRUE(i_isolate()->IsWasmImportedStringsEnabled(context));

    // When flag is on, callback return value has no effect.
    isolate()->SetWasmImportedStringsEnabledCallback([](auto) { return true; });
    EXPECT_TRUE(i_isolate()->IsWasmImportedStringsEnabled(context));
    EXPECT_TRUE(i::wasm::WasmEnabledFeatures::FromIsolate(i_isolate())
                    .has_imported_strings());
    isolate()->SetWasmImportedStringsEnabledCallback(
        [](auto) { return false; });
    EXPECT_TRUE(i_isolate()->IsWasmImportedStringsEnabled(context));
    EXPECT_TRUE(i::wasm::WasmEnabledFeatures::FromIsolate(i_isolate())
                    .has_imported_strings());
  }
  {
    i::FlagScope<bool> flag_strings(
        &i::v8_flags.experimental_wasm_imported_strings, false);
    EXPECT_FALSE(i_isolate()->IsWasmImportedStringsEnabled(context));

    // Test enabling/disabling via callback.
    isolate()->SetWasmImportedStringsEnabledCallback([](auto) { return true; });
    EXPECT_TRUE(i_isolate()->IsWasmImportedStringsEnabled(context));
    EXPECT_TRUE(i::wasm::WasmEnabledFeatures::FromIsolate(i_isolate())
                    .has_imported_strings());
    isolate()->SetWasmImportedStringsEnabledCallback(
        [](auto) { return false; });
    EXPECT_FALSE(i_isolate()->IsWasmImportedStringsEnabled(context));
    EXPECT_FALSE(i::wasm::WasmEnabledFeatures::FromIsolate(i_isolate())
                     .has_imported_strings());
  }
}

TEST_F(ApiWasmTest, WasmEnableDisableJSPI) {
  Local<Context> context_local = Context::New(isolate());
  Context::Scope context_scope(context_local);
  i::DirectHandle<i::NativeContext> context =
      v8::Utils::OpenDirectHandle(*context_local);
  // Test enabling/disabling via flag.
  {
    i::FlagScope<bool> flag_strings(&i::v8_flags.experimental_wasm_jspi, true);
    EXPECT_TRUE(i_isolate()->IsWasmJSPIRequested(context));
  }
  {
    i::FlagScope<bool> flag_strings(&i::v8_flags.experimental_wasm_jspi, false);
    EXPECT_FALSE(i_isolate()->IsWasmJSPIRequested(context));
  }
  // Test enabling/disabling via callback.
  isolate()->SetWasmJSPIEnabledCallback([](auto) { return true; });
  EXPECT_TRUE(i_isolate()->IsWasmJSPIRequested(context));
  isolate()->SetWasmJSPIEnabledCallback([](auto) { return false; });
  EXPECT_FALSE(i_isolate()->IsWasmJSPIRequested(context));
}

TEST_F(ApiWasmTest, WasmInstallJSPI) {
  Local<Context> context_local = Context::New(isolate());
  Context::Scope context_scope(context_local);
  i::DirectHandle<i::NativeContext> context =
      v8::Utils::OpenDirectHandle(*context_local);

  EXPECT_FALSE(i_isolate()->IsWasmJSPIEnabled(context));
  i::wasm::WasmEnabledFeatures features =
      i::wasm::WasmEnabledFeatures::FromIsolate(i_isolate());
  EXPECT_FALSE(features.has_jspi());

  // Test installing JSPI via flag.
  isolate()->SetWasmJSPIEnabledCallback([](auto) { return true; });

  EXPECT_TRUE(i_isolate()->IsWasmJSPIRequested(context));
  EXPECT_FALSE(i_isolate()->IsWasmJSPIEnabled(context));
  features = i::wasm::WasmEnabledFeatures::FromIsolate(i_isolate());
  EXPECT_FALSE(features.has_jspi());

  i::WasmJs::InstallConditionalFeatures(i_isolate(), context);

  EXPECT_TRUE(i_isolate()->IsWasmJSPIEnabled(context));
  features = i::wasm::WasmEnabledFeatures::FromIsolate(i_isolate());
  EXPECT_TRUE(features.has_jspi());
}

}  // namespace v8
