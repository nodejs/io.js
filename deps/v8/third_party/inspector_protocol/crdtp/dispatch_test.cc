// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "cbor.h"
#include "dispatch.h"
#include "error_support.h"
#include "frontend_channel.h"
#include "json.h"
#include "test_platform.h"

namespace v8_crdtp {
// =============================================================================
// DispatchResponse - Error status and chaining / fall through
// =============================================================================
TEST(DispatchResponseTest, OK) {
  EXPECT_EQ(DispatchCode::SUCCESS, DispatchResponse::Success().Code());
  EXPECT_TRUE(DispatchResponse::Success().IsSuccess());
}

TEST(DispatchResponseTest, ServerError) {
  DispatchResponse error = DispatchResponse::ServerError("Oops!");
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_EQ(DispatchCode::SERVER_ERROR, error.Code());
  EXPECT_EQ("Oops!", error.Message());
}

TEST(DispatchResponseTest, SessionNotFound) {
  DispatchResponse error = DispatchResponse::SessionNotFound("OMG!");
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_EQ(DispatchCode::SESSION_NOT_FOUND, error.Code());
  EXPECT_EQ("OMG!", error.Message());
}

TEST(DispatchResponseTest, InternalError) {
  DispatchResponse error = DispatchResponse::InternalError();
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_EQ(DispatchCode::INTERNAL_ERROR, error.Code());
  EXPECT_EQ("Internal error", error.Message());
}

TEST(DispatchResponseTest, InvalidParams) {
  DispatchResponse error = DispatchResponse::InvalidParams("too cool");
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_EQ(DispatchCode::INVALID_PARAMS, error.Code());
  EXPECT_EQ("too cool", error.Message());
}

TEST(DispatchResponseTest, FallThrough) {
  DispatchResponse error = DispatchResponse::FallThrough();
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_TRUE(error.IsFallThrough());
  EXPECT_EQ(DispatchCode::FALL_THROUGH, error.Code());
}

// =============================================================================
// Dispatchable - a shallow parser for CBOR encoded DevTools messages
// =============================================================================
TEST(DispatchableTest, MessageMustBeAnObject) {
  // Provide no input whatsoever.
  span<uint8_t> empty_span;
  Dispatchable empty(empty_span);
  EXPECT_FALSE(empty.ok());
  EXPECT_EQ(DispatchCode::INVALID_REQUEST, empty.DispatchError().Code());
  EXPECT_EQ("Message must be an object", empty.DispatchError().Message());
}

TEST(DispatchableTest, MessageMustHaveIntegerIdProperty) {
  // Construct an empty map inside of an envelope.
  std::vector<uint8_t> cbor;
  ASSERT_TRUE(json::ConvertJSONToCBOR(SpanFrom("{}"), &cbor).ok());
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_FALSE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchCode::INVALID_REQUEST, dispatchable.DispatchError().Code());
  EXPECT_EQ("Message must have integer 'id' property",
            dispatchable.DispatchError().Message());
}

TEST(DispatchableTest, MessageMustHaveIntegerIdProperty_IncorrectType) {
  // This time we set the id property, but fail to make it an int32.
  std::vector<uint8_t> cbor;
  ASSERT_TRUE(
      json::ConvertJSONToCBOR(SpanFrom("{\"id\":\"foo\"}"), &cbor).ok());
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_FALSE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchCode::INVALID_REQUEST, dispatchable.DispatchError().Code());
  EXPECT_EQ("Message must have integer 'id' property",
            dispatchable.DispatchError().Message());
}

TEST(DispatchableTest, MessageMustHaveStringMethodProperty) {
  // This time we set the id property, but not the method property.
  std::vector<uint8_t> cbor;
  ASSERT_TRUE(json::ConvertJSONToCBOR(SpanFrom("{\"id\":42}"), &cbor).ok());
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_TRUE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchCode::INVALID_REQUEST, dispatchable.DispatchError().Code());
  EXPECT_EQ("Message must have string 'method' property",
            dispatchable.DispatchError().Message());
}

TEST(DispatchableTest, MessageMustHaveStringMethodProperty_IncorrectType) {
  // This time we set the method property, but fail to make it a string.
  std::vector<uint8_t> cbor;
  ASSERT_TRUE(
      json::ConvertJSONToCBOR(SpanFrom("{\"id\":42,\"method\":42}"), &cbor)
          .ok());
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_TRUE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchCode::INVALID_REQUEST, dispatchable.DispatchError().Code());
  EXPECT_EQ("Message must have string 'method' property",
            dispatchable.DispatchError().Message());
}

TEST(DispatchableTest, MessageMayHaveStringSessionIdProperty) {
  // This time, the session id is an int but it should be a string. Method and
  // call id are present.
  std::vector<uint8_t> cbor;
  ASSERT_TRUE(json::ConvertJSONToCBOR(
                  SpanFrom("{\"id\":42,\"method\":\"Foo.executeBar\","
                           "\"sessionId\":42"  // int32 is wrong type
                           "}"),
                  &cbor)
                  .ok());
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_TRUE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchCode::INVALID_REQUEST, dispatchable.DispatchError().Code());
  EXPECT_EQ("Message may have string 'sessionId' property",
            dispatchable.DispatchError().Message());
}

TEST(DispatchableTest, MessageMayHaveObjectParamsProperty) {
  // This time, we fail to use the correct type for the params property.
  std::vector<uint8_t> cbor;
  ASSERT_TRUE(json::ConvertJSONToCBOR(
                  SpanFrom("{\"id\":42,\"method\":\"Foo.executeBar\","
                           "\"params\":42"  // int32 is wrong type
                           "}"),
                  &cbor)
                  .ok());
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_TRUE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchCode::INVALID_REQUEST, dispatchable.DispatchError().Code());
  EXPECT_EQ("Message may have object 'params' property",
            dispatchable.DispatchError().Message());
}

TEST(DispatchableTest, MessageWithUnknownProperty) {
  // This time we set the 'unknown' property, so we are told what's allowed.
  std::vector<uint8_t> cbor;
  ASSERT_TRUE(
      json::ConvertJSONToCBOR(SpanFrom("{\"id\":42,\"unknown\":42}"), &cbor)
          .ok());
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_TRUE(dispatchable.HasCallId());
  EXPECT_EQ(DispatchCode::INVALID_REQUEST, dispatchable.DispatchError().Code());
  EXPECT_EQ(
      "Message has property other than 'id', 'method', 'sessionId', 'params'",
      dispatchable.DispatchError().Message());
}

TEST(DispatchableTest, DuplicateMapKey) {
  const std::array<std::string, 4> jsons = {
      {"{\"id\":42,\"id\":42}", "{\"params\":null,\"params\":null}",
       "{\"method\":\"foo\",\"method\":\"foo\"}",
       "{\"sessionId\":\"42\",\"sessionId\":\"42\"}"}};
  for (const std::string& json : jsons) {
    SCOPED_TRACE("json = " + json);
    std::vector<uint8_t> cbor;
    ASSERT_TRUE(json::ConvertJSONToCBOR(SpanFrom(json), &cbor).ok());
    Dispatchable dispatchable(SpanFrom(cbor));
    EXPECT_FALSE(dispatchable.ok());
    EXPECT_EQ(DispatchCode::PARSE_ERROR, dispatchable.DispatchError().Code());
    EXPECT_THAT(dispatchable.DispatchError().Message(),
                testing::StartsWith("CBOR: duplicate map key at position "));
  }
}

TEST(DispatchableTest, ValidMessageParsesOK_NoParams) {
  const std::array<std::string, 2> jsons = {
      {"{\"id\":42,\"method\":\"Foo.executeBar\",\"sessionId\":"
       "\"f421ssvaz4\"}",
       "{\"id\":42,\"method\":\"Foo.executeBar\",\"sessionId\":\"f421ssvaz4\","
       "\"params\":null}"}};
  for (const std::string& json : jsons) {
    SCOPED_TRACE("json = " + json);
    std::vector<uint8_t> cbor;
    ASSERT_TRUE(json::ConvertJSONToCBOR(SpanFrom(json), &cbor).ok());
    Dispatchable dispatchable(SpanFrom(cbor));
    EXPECT_TRUE(dispatchable.ok());
    EXPECT_TRUE(dispatchable.HasCallId());
    EXPECT_EQ(42, dispatchable.CallId());
    EXPECT_EQ("Foo.executeBar", std::string(dispatchable.Method().begin(),
                                            dispatchable.Method().end()));
    EXPECT_EQ("f421ssvaz4", std::string(dispatchable.SessionId().begin(),
                                        dispatchable.SessionId().end()));
    EXPECT_TRUE(dispatchable.Params().empty());
  }
}

TEST(DispatchableTest, ValidMessageParsesOK_WithParams) {
  std::vector<uint8_t> cbor;
  cbor::EnvelopeEncoder envelope;
  envelope.EncodeStart(&cbor);
  cbor.push_back(cbor::EncodeIndefiniteLengthMapStart());
  cbor::EncodeString8(SpanFrom("id"), &cbor);
  cbor::EncodeInt32(42, &cbor);
  cbor::EncodeString8(SpanFrom("method"), &cbor);
  cbor::EncodeString8(SpanFrom("Foo.executeBar"), &cbor);
  cbor::EncodeString8(SpanFrom("params"), &cbor);
  cbor::EnvelopeEncoder params_envelope;
  params_envelope.EncodeStart(&cbor);
  // The |Dispatchable| class does not parse into the "params" envelope,
  // so we can stick anything into there for the purpose of this test.
  // For convenience, we use a String8.
  cbor::EncodeString8(SpanFrom("params payload"), &cbor);
  params_envelope.EncodeStop(&cbor);
  cbor::EncodeString8(SpanFrom("sessionId"), &cbor);
  cbor::EncodeString8(SpanFrom("f421ssvaz4"), &cbor);
  cbor.push_back(cbor::EncodeStop());
  envelope.EncodeStop(&cbor);
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_TRUE(dispatchable.ok());
  EXPECT_TRUE(dispatchable.HasCallId());
  EXPECT_EQ(42, dispatchable.CallId());
  EXPECT_EQ("Foo.executeBar", std::string(dispatchable.Method().begin(),
                                          dispatchable.Method().end()));
  EXPECT_EQ("f421ssvaz4", std::string(dispatchable.SessionId().begin(),
                                      dispatchable.SessionId().end()));
  cbor::CBORTokenizer params_tokenizer(dispatchable.Params());
  ASSERT_EQ(cbor::CBORTokenTag::ENVELOPE, params_tokenizer.TokenTag());
  params_tokenizer.EnterEnvelope();
  ASSERT_EQ(cbor::CBORTokenTag::STRING8, params_tokenizer.TokenTag());
  EXPECT_EQ("params payload", std::string(params_tokenizer.GetString8().begin(),
                                          params_tokenizer.GetString8().end()));
}

TEST(DispatchableTest, FaultyCBORTrailingJunk) {
  // In addition to the higher level parsing errors, we also catch CBOR
  // structural corruption. E.g., in this case, the message would be
  // OK but has some extra trailing bytes.
  std::vector<uint8_t> cbor;
  cbor::EnvelopeEncoder envelope;
  envelope.EncodeStart(&cbor);
  cbor.push_back(cbor::EncodeIndefiniteLengthMapStart());
  cbor::EncodeString8(SpanFrom("id"), &cbor);
  cbor::EncodeInt32(42, &cbor);
  cbor::EncodeString8(SpanFrom("method"), &cbor);
  cbor::EncodeString8(SpanFrom("Foo.executeBar"), &cbor);
  cbor::EncodeString8(SpanFrom("sessionId"), &cbor);
  cbor::EncodeString8(SpanFrom("f421ssvaz4"), &cbor);
  cbor.push_back(cbor::EncodeStop());
  envelope.EncodeStop(&cbor);
  size_t trailing_junk_pos = cbor.size();
  cbor.push_back('t');
  cbor.push_back('r');
  cbor.push_back('a');
  cbor.push_back('i');
  cbor.push_back('l');
  Dispatchable dispatchable(SpanFrom(cbor));
  EXPECT_FALSE(dispatchable.ok());
  EXPECT_EQ(DispatchCode::PARSE_ERROR, dispatchable.DispatchError().Code());
  EXPECT_EQ(57u, trailing_junk_pos);
  EXPECT_EQ("CBOR: trailing junk at position 57",
            dispatchable.DispatchError().Message());
}

// =============================================================================
// Helpers for creating protocol cresponses and notifications.
// =============================================================================
TEST(CreateErrorResponseTest, SmokeTest) {
  auto serializable = CreateErrorResponse(
      42, DispatchResponse::InvalidParams("invalid params message"));
  std::string json;
  auto status =
      json::ConvertCBORToJSON(SpanFrom(serializable->Serialize()), &json);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(
      "{\"id\":42,\"error\":"
      "{\"code\":-32602,"
      "\"message\":\"invalid params message\"}}",
      json);
}

TEST(CreateErrorNotificationTest, SmokeTest) {
  auto serializable =
      CreateErrorNotification(DispatchResponse::InvalidRequest("oops!"));
  std::string json;
  auto status =
      json::ConvertCBORToJSON(SpanFrom(serializable->Serialize()), &json);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("{\"error\":{\"code\":-32600,\"message\":\"oops!\"}}", json);
}

TEST(CreateResponseTest, SmokeTest) {
  auto serializable = CreateResponse(42, nullptr);
  std::string json;
  auto status =
      json::ConvertCBORToJSON(SpanFrom(serializable->Serialize()), &json);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("{\"id\":42,\"result\":{}}", json);
}

TEST(CreateNotificationTest, SmokeTest) {
  auto serializable = CreateNotification("Foo.bar");
  std::string json;
  auto status =
      json::ConvertCBORToJSON(SpanFrom(serializable->Serialize()), &json);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ("{\"method\":\"Foo.bar\",\"params\":{}}", json);
}

// =============================================================================
// UberDispatcher - dispatches between domains (backends).
// =============================================================================
class TestChannel : public FrontendChannel {
 public:
  std::string JSON() const {
    std::string json;
    json::ConvertCBORToJSON(SpanFrom(cbor_), &json);
    return json;
  }

 private:
  void SendProtocolResponse(int call_id,
                            std::unique_ptr<Serializable> message) override {
    cbor_ = message->Serialize();
  }

  void SendProtocolNotification(
      std::unique_ptr<Serializable> message) override {
    cbor_ = message->Serialize();
  }

  void FallThrough(int call_id,
                   span<uint8_t> method,
                   span<uint8_t> message) override {}

  void FlushProtocolNotifications() override {}

  std::vector<uint8_t> cbor_;
};

TEST(UberDispatcherTest, MethodNotFound) {
  // No domain dispatchers are registered, so unsuprisingly, we'll get a method
  // not found error and can see that DispatchResult::MethodFound() yields
  // false.
  TestChannel channel;
  UberDispatcher dispatcher(&channel);
  std::vector<uint8_t> message;
  json::ConvertJSONToCBOR(SpanFrom("{\"id\":42,\"method\":\"Foo.bar\"}"),
                          &message);
  Dispatchable dispatchable(SpanFrom(message));
  ASSERT_TRUE(dispatchable.ok());
  UberDispatcher::DispatchResult dispatched = dispatcher.Dispatch(dispatchable);
  EXPECT_FALSE(dispatched.MethodFound());
  dispatched.Run();
  EXPECT_EQ(
      "{\"id\":42,\"error\":"
      "{\"code\":-32601,\"message\":\"'Foo.bar' wasn't found\"}}",
      channel.JSON());
}

// A domain dispatcher which captured dispatched and executed commands in fields
// for testing.
class TestDomain : public DomainDispatcher {
 public:
  explicit TestDomain(FrontendChannel* channel) : DomainDispatcher(channel) {}

  std::function<void(const Dispatchable&)> Dispatch(
      span<uint8_t> command_name) override {
    dispatched_commands_.push_back(
        std::string(command_name.begin(), command_name.end()));
    return [this](const Dispatchable& dispatchable) {
      executed_commands_.push_back(dispatchable.CallId());
    };
  }

  // Command names of the dispatched commands.
  std::vector<std::string> DispatchedCommands() const {
    return dispatched_commands_;
  }

  // Call ids of the executed commands.
  std::vector<int32_t> ExecutedCommands() const { return executed_commands_; }

 private:
  std::vector<std::string> dispatched_commands_;
  std::vector<int32_t> executed_commands_;
};

TEST(UberDispatcherTest, DispatchingToDomainWithRedirects) {
  // This time, we register two domain dispatchers (Foo and Bar) and issue one
  // command 'Foo.execute' which executes on Foo and one command 'Foo.redirect'
  // which executes as 'Bar.redirected'.
  TestChannel channel;
  UberDispatcher dispatcher(&channel);
  auto foo_dispatcher = std::make_unique<TestDomain>(&channel);
  TestDomain* foo = foo_dispatcher.get();
  auto bar_dispatcher = std::make_unique<TestDomain>(&channel);
  TestDomain* bar = bar_dispatcher.get();

  dispatcher.WireBackend(
      SpanFrom("Foo"), {{SpanFrom("Foo.redirect"), SpanFrom("Bar.redirected")}},
      std::move(foo_dispatcher));
  dispatcher.WireBackend(SpanFrom("Bar"), {}, std::move(bar_dispatcher));

  {
    std::vector<uint8_t> message;
    json::ConvertJSONToCBOR(SpanFrom("{\"id\":42,\"method\":\"Foo.execute\"}"),
                            &message);
    Dispatchable dispatchable(SpanFrom(message));
    ASSERT_TRUE(dispatchable.ok());
    UberDispatcher::DispatchResult dispatched =
        dispatcher.Dispatch(dispatchable);
    EXPECT_TRUE(dispatched.MethodFound());
    dispatched.Run();
  }
  {
    std::vector<uint8_t> message;
    json::ConvertJSONToCBOR(SpanFrom("{\"id\":43,\"method\":\"Foo.redirect\"}"),
                            &message);
    Dispatchable dispatchable(SpanFrom(message));
    ASSERT_TRUE(dispatchable.ok());
    UberDispatcher::DispatchResult dispatched =
        dispatcher.Dispatch(dispatchable);
    EXPECT_TRUE(dispatched.MethodFound());
    dispatched.Run();
  }
  EXPECT_THAT(foo->DispatchedCommands(), testing::ElementsAre("execute"));
  EXPECT_THAT(foo->ExecutedCommands(), testing::ElementsAre(42));
  EXPECT_THAT(bar->DispatchedCommands(), testing::ElementsAre("redirected"));
  EXPECT_THAT(bar->ExecutedCommands(), testing::ElementsAre(43));
}
}  // namespace v8_crdtp
