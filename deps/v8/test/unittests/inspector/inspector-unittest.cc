// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "include/v8-inspector.h"
#include "include/v8-local-handle.h"
#include "include/v8-primitive.h"
#include "src/inspector/string-util.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/inspector/v8-inspector-session-impl.h"
#include "src/inspector/v8-runtime-agent-impl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

using v8_inspector::String16;
using v8_inspector::StringBuffer;
using v8_inspector::StringView;
using v8_inspector::toString16;
using v8_inspector::toStringView;
using v8_inspector::V8ContextInfo;
using v8_inspector::V8Inspector;
using v8_inspector::V8InspectorSession;

namespace v8 {
namespace internal {

using InspectorTest = TestWithContext;

namespace {

class NoopChannel : public V8Inspector::Channel {
 public:
  ~NoopChannel() override = default;
  void sendResponse(int callId,
                    std::unique_ptr<StringBuffer> message) override {}
  void sendNotification(std::unique_ptr<StringBuffer> message) override {}
  void flushProtocolNotifications() override {}
};

void WrapOnInterrupt(v8::Isolate* isolate, void* data) {
  const char* object_group = "";
  StringView object_group_view(reinterpret_cast<const uint8_t*>(object_group),
                               strlen(object_group));
  reinterpret_cast<V8InspectorSession*>(data)->wrapObject(
      isolate->GetCurrentContext(), v8::Null(isolate), object_group_view,
      false);
}

}  // namespace

TEST_F(InspectorTest, WrapInsideWrapOnInterrupt) {
  v8::Isolate* isolate = v8_isolate();
  v8::HandleScope handle_scope(isolate);

  v8_inspector::V8InspectorClient default_client;
  std::unique_ptr<V8Inspector> inspector =
      V8Inspector::create(isolate, &default_client);
  const char* name = "";
  StringView name_view(reinterpret_cast<const uint8_t*>(name), strlen(name));
  V8ContextInfo context_info(v8_context(), 1, name_view);
  inspector->contextCreated(context_info);

  NoopChannel channel;
  const char* state = "{}";
  StringView state_view(reinterpret_cast<const uint8_t*>(state), strlen(state));
  std::unique_ptr<V8InspectorSession> session = inspector->connect(
      1, &channel, state_view, v8_inspector::V8Inspector::kFullyTrusted);

  const char* object_group = "";
  StringView object_group_view(reinterpret_cast<const uint8_t*>(object_group),
                               strlen(object_group));
  isolate->RequestInterrupt(&WrapOnInterrupt, session.get());
  session->wrapObject(v8_context(), v8::Null(isolate), object_group_view,
                      false);
}

TEST_F(InspectorTest, BinaryFromBase64) {
  auto checkBinary = [](const v8_inspector::protocol::Binary& binary,
                        const std::vector<uint8_t>& values) {
    std::vector<uint8_t> binary_vector(binary.data(),
                                       binary.data() + binary.size());
    CHECK_EQ(binary_vector, values);
  };

  {
    bool success;
    auto binary = v8_inspector::protocol::Binary::fromBase64("", &success);
    CHECK(success);
    checkBinary(binary, {});
  }
  {
    bool success;
    auto binary = v8_inspector::protocol::Binary::fromBase64("YQ==", &success);
    CHECK(success);
    checkBinary(binary, {'a'});
  }
  {
    bool success;
    auto binary = v8_inspector::protocol::Binary::fromBase64("YWI=", &success);
    CHECK(success);
    checkBinary(binary, {'a', 'b'});
  }
  {
    bool success;
    auto binary = v8_inspector::protocol::Binary::fromBase64("YWJj", &success);
    CHECK(success);
    checkBinary(binary, {'a', 'b', 'c'});
  }
  {
    bool success;
    // Wrong input length:
    auto binary = v8_inspector::protocol::Binary::fromBase64("Y", &success);
    CHECK(!success);
  }
  {
    bool success;
    // Invalid space:
    auto binary = v8_inspector::protocol::Binary::fromBase64("=AAA", &success);
    CHECK(!success);
  }
  {
    bool success;
    // Invalid space in a non-final block of four:
    auto binary =
        v8_inspector::protocol::Binary::fromBase64("AAA=AAAA", &success);
    CHECK(!success);
  }
  {
    bool success;
    // Invalid invalid space in second to last position:
    auto binary = v8_inspector::protocol::Binary::fromBase64("AA=A", &success);
    CHECK(!success);
  }
  {
    bool success;
    // Invalid character:
    auto binary = v8_inspector::protocol::Binary::fromBase64(" ", &success);
    CHECK(!success);
  }
}

TEST_F(InspectorTest, BinaryToBase64) {
  uint8_t input[] = {'a', 'b', 'c'};
  {
    auto binary = v8_inspector::protocol::Binary::fromSpan(input, 0);
    v8_inspector::protocol::String base64 = binary.toBase64();
    CHECK_EQ(base64.utf8(), "");
  }
  {
    auto binary = v8_inspector::protocol::Binary::fromSpan(input, 1);
    v8_inspector::protocol::String base64 = binary.toBase64();
    CHECK_EQ(base64.utf8(), "YQ==");
  }
  {
    auto binary = v8_inspector::protocol::Binary::fromSpan(input, 2);
    v8_inspector::protocol::String base64 = binary.toBase64();
    CHECK_EQ(base64.utf8(), "YWI=");
  }
  {
    auto binary = v8_inspector::protocol::Binary::fromSpan(input, 3);
    v8_inspector::protocol::String base64 = binary.toBase64();
    CHECK_EQ(base64.utf8(), "YWJj");
  }
}

TEST_F(InspectorTest, BinaryBase64RoundTrip) {
  std::array<uint8_t, 256> values;
  for (uint16_t b = 0x0; b <= 0xFF; ++b) values[b] = b;
  auto binary =
      v8_inspector::protocol::Binary::fromSpan(values.data(), values.size());
  v8_inspector::protocol::String base64 = binary.toBase64();
  bool success = false;
  auto roundtrip_binary =
      v8_inspector::protocol::Binary::fromBase64(base64, &success);
  CHECK(success);
  CHECK_EQ(values.size(), roundtrip_binary.size());
  for (size_t i = 0; i < values.size(); ++i) {
    CHECK_EQ(values[i], roundtrip_binary.data()[i]);
  }
}

TEST_F(InspectorTest, NoInterruptOnGetAssociatedData) {
  v8::Isolate* isolate = v8_isolate();
  v8::HandleScope handle_scope(isolate);

  v8_inspector::V8InspectorClient default_client;
  std::unique_ptr<v8_inspector::V8InspectorImpl> inspector(
      new v8_inspector::V8InspectorImpl(isolate, &default_client));

  v8::Local<v8::Value> error = v8::Exception::Error(NewString("custom error"));
  v8::Local<v8::Name> key = NewString("key");
  v8::Local<v8::Value> value = NewString("value");
  inspector->associateExceptionData(v8_context(), error, key, value);

  struct InterruptRecorder {
    static void handler(v8::Isolate* isolate, void* data) {
      reinterpret_cast<InterruptRecorder*>(data)->WasInvoked = true;
    }

    bool WasInvoked = false;
  } recorder;

  isolate->RequestInterrupt(&InterruptRecorder::handler, &recorder);

  v8::Local<v8::Object> data =
      inspector->getAssociatedExceptionData(error).ToLocalChecked();
  CHECK(!recorder.WasInvoked);

  CHECK_EQ(data->Get(v8_context(), key).ToLocalChecked(), value);

  TryRunJS("0");
  CHECK(recorder.WasInvoked);
}

class TestChannel : public V8Inspector::Channel {
 public:
  ~TestChannel() override = default;
  void sendResponse(int callId,
                    std::unique_ptr<StringBuffer> message) override {
    CHECK_EQ(callId, 1);
    CHECK_NE(toString16(message->string()).find(expected_response_matcher_),
             String16::kNotFound);
  }
  void sendNotification(std::unique_ptr<StringBuffer> message) override {}
  void flushProtocolNotifications() override {}
  v8_inspector::String16 expected_response_matcher_;
};

TEST_F(InspectorTest, NoConsoleAPIForUntrustedClient) {
  v8::Isolate* isolate = v8_isolate();
  v8::HandleScope handle_scope(isolate);

  v8_inspector::V8InspectorClient default_client;
  std::unique_ptr<V8Inspector> inspector =
      V8Inspector::create(isolate, &default_client);
  V8ContextInfo context_info(v8_context(), 1, toStringView(""));
  inspector->contextCreated(context_info);

  TestChannel channel;
  const char kCommand[] = R"({
    "id": 1,
    "method": "Runtime.evaluate",
    "params": {
      "expression": "$0 || 42",
      "contextId": 1,
      "includeCommandLineAPI": true
    }
  })";
  std::unique_ptr<V8InspectorSession> trusted_session =
      inspector->connect(1, &channel, toStringView("{}"),
                         v8_inspector::V8Inspector::kFullyTrusted);
  channel.expected_response_matcher_ = R"("value":42)";
  trusted_session->dispatchProtocolMessage(toStringView(kCommand));

  std::unique_ptr<V8InspectorSession> untrusted_session = inspector->connect(
      1, &channel, toStringView("{}"), v8_inspector::V8Inspector::kUntrusted);
  channel.expected_response_matcher_ = R"("className":"ReferenceError")";
  untrusted_session->dispatchProtocolMessage(toStringView(kCommand));
}

TEST_F(InspectorTest, CanHandleMalformedCborMessage) {
  v8::Isolate* isolate = v8_isolate();
  v8::HandleScope handle_scope(isolate);

  v8_inspector::V8InspectorClient default_client;
  std::unique_ptr<V8Inspector> inspector =
      V8Inspector::create(isolate, &default_client);
  V8ContextInfo context_info(v8_context(), 1, toStringView(""));
  inspector->contextCreated(context_info);

  TestChannel channel;
  const unsigned char kCommand[] = {0xD8, 0x5A, 0x00, 0xBA, 0xDB, 0xEE, 0xF0};
  std::unique_ptr<V8InspectorSession> trusted_session =
      inspector->connect(1, &channel, toStringView("{}"),
                         v8_inspector::V8Inspector::kFullyTrusted);
  channel.expected_response_matcher_ = R"("value":42)";
  trusted_session->dispatchProtocolMessage(
      StringView(kCommand, sizeof(kCommand)));
}

TEST_F(InspectorTest, ApiCreatedTasksAreCleanedUp) {
  v8::Isolate* isolate = v8_isolate();
  v8::HandleScope handle_scope(isolate);

  v8_inspector::V8InspectorClient default_client;
  std::unique_ptr<v8_inspector::V8InspectorImpl> inspector =
      std::make_unique<v8_inspector::V8InspectorImpl>(isolate, &default_client);
  V8ContextInfo context_info(v8_context(), 1, toStringView(""));
  inspector->contextCreated(context_info);

  // Trigger V8Console creation.
  v8_inspector::V8Console* console = inspector->console();
  CHECK(console);

  {
    v8::HandleScope handle_scope(isolate);
    v8::MaybeLocal<v8::Value> result = TryRunJS(isolate, NewString(R"(
      globalThis['task'] = console.createTask('Task');
    )"));
    CHECK(!result.IsEmpty());

    // Run GC and check that the task is still here.
    InvokeMajorGC();
    CHECK_EQ(console->AllConsoleTasksForTest().size(), 1);
  }

  // Get rid of the task on the context, run GC and check we no longer have
  // the TaskInfo in the inspector.
  v8_context()->Global()->Delete(v8_context(), NewString("task")).Check();
  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        i_isolate()->heap());
    InvokeMajorGC();
  }
  CHECK_EQ(console->AllConsoleTasksForTest().size(), 0);
}

TEST_F(InspectorTest, Evaluate) {
  v8::Isolate* isolate = v8_isolate();
  v8::HandleScope handle_scope(isolate);

  v8_inspector::V8InspectorClient default_client;
  std::unique_ptr<V8Inspector> inspector =
      V8Inspector::create(isolate, &default_client);
  V8ContextInfo context_info(v8_context(), 1, toStringView(""));
  inspector->contextCreated(context_info);

  TestChannel channel;
  std::unique_ptr<V8InspectorSession> trusted_session =
      inspector->connect(1, &channel, toStringView("{}"),
                         v8_inspector::V8Inspector::kFullyTrusted);

  {
    auto result =
        trusted_session->evaluate(v8_context(), toStringView("21 + 21"));
    CHECK_EQ(
        result.type,
        v8_inspector::V8InspectorSession::EvaluateResult::ResultType::kSuccess);
    CHECK_EQ(result.value->IntegerValue(v8_context()).FromJust(), 42);
  }
  {
    auto result = trusted_session->evaluate(
        v8_context(), toStringView("throw new Error('foo')"));
    CHECK_EQ(result.type, v8_inspector::V8InspectorSession::EvaluateResult::
                              ResultType::kException);
    CHECK(result.value->IsNativeError());
  }
  {
    // Unknown context.
    v8::Local<v8::Context> ctx = v8::Context::New(v8_isolate());
    auto result = trusted_session->evaluate(ctx, toStringView("21 + 21"));
    CHECK_EQ(
        result.type,
        v8_inspector::V8InspectorSession::EvaluateResult::ResultType::kNotRun);
  }
  {
    // CommandLine API
    auto result = trusted_session->evaluate(v8_context(),
                                            toStringView("debug(console.log)"),
                                            /*includeCommandLineAPI=*/true);
    CHECK_EQ(
        result.type,
        v8_inspector::V8InspectorSession::EvaluateResult::ResultType::kSuccess);
    CHECK(result.value->IsUndefined());
  }
}

// Regression test for crbug.com/323813642.
TEST_F(InspectorTest, NoInterruptWhileBuildingConsoleMessages) {
  v8::Isolate* isolate = v8_isolate();
  v8::HandleScope handle_scope(isolate);

  v8_inspector::V8InspectorClient default_client;
  std::unique_ptr<v8_inspector::V8InspectorImpl> inspector(
      new v8_inspector::V8InspectorImpl(isolate, &default_client));
  V8ContextInfo context_info(v8_context(), 1, toStringView(""));
  inspector->contextCreated(context_info);

  TestChannel channel;
  std::unique_ptr<V8InspectorSession> session = inspector->connect(
      1, &channel, toStringView("{}"), v8_inspector::V8Inspector::kFullyTrusted,
      v8_inspector::V8Inspector::kNotWaitingForDebugger);
  reinterpret_cast<v8_inspector::V8InspectorSessionImpl*>(session.get())
      ->runtimeAgent()
      ->enable();

  struct InterruptRecorder {
    static void handler(v8::Isolate* isolate, void* data) {
      reinterpret_cast<InterruptRecorder*>(data)->WasInvoked = true;
    }

    bool WasInvoked = false;
  } recorder;

  isolate->RequestInterrupt(&InterruptRecorder::handler, &recorder);

  v8::Local<v8::Value> error = v8::Exception::Error(NewString("custom error"));
  inspector->exceptionThrown(v8_context(), toStringView("message"), error,
                             toStringView("detailed message"),
                             toStringView("https://example.com/script.js"), 42,
                             21, std::unique_ptr<v8_inspector::V8StackTrace>(),
                             0);

  CHECK(!recorder.WasInvoked);

  TryRunJS("0");
  CHECK(recorder.WasInvoked);
}

}  // namespace internal
}  // namespace v8
