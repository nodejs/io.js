// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "json.h"

#include <array>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "cbor.h"
#include "parser_handler.h"
#include "span.h"
#include "status.h"
#include "status_test_support.h"
#include "test_platform.h"

namespace crdtp {
namespace json {
// =============================================================================
// json::NewJSONEncoder - for encoding streaming parser events as JSON
// =============================================================================

void WriteUTF8AsUTF16(ParserHandler* writer, const std::string& utf8) {
  writer->HandleString16(SpanFrom(UTF8ToUTF16(SpanFrom(utf8))));
}

TEST(JsonEncoder, OverlongEncodings) {
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);

  // We encode 0x7f, which is the DEL ascii character, as a 4 byte UTF8
  // sequence. This is called an overlong encoding, because only 1 byte
  // is needed to represent 0x7f as UTF8.
  std::vector<uint8_t> chars = {
      0xf0,  // Starts 4 byte utf8 sequence
      0x80,  // continuation byte
      0x81,  // continuation byte w/ payload bit 7 set to 1.
      0xbf,  // continuation byte w/ payload bits 0-6 set to 11111.
  };
  writer->HandleString8(SpanFrom(chars));
  EXPECT_EQ("\"\"", out);  // Empty string means that 0x7f was rejected (good).
}

TEST(JsonEncoder, NotAContinuationByte) {
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);

  // |world| encodes the globe as a 4 byte UTF8 sequence. So, naturally, it'll
  // have a start byte, followed by three continuation bytes.
  std::string world = "🌎";
  ASSERT_EQ(4u, world.size());
  ASSERT_EQ(world[1] & 0xc0, 0x80);  // checks for continuation byte
  ASSERT_EQ(world[2] & 0xc0, 0x80);
  ASSERT_EQ(world[3] & 0xc0, 0x80);

  // Now create a corrupted UTF8 string, starting with the first two bytes from
  // |world|, followed by an ASCII message. Upon encountering '!', our decoder
  // will realize that it's not a continuation byte; it'll skip to the end of
  // this UTF8 sequence and continue with the next character. In this case, the
  // 'H', of "Hello".
  std::vector<uint8_t> chars;
  chars.push_back(world[0]);
  chars.push_back(world[1]);
  chars.push_back('!');
  chars.push_back('?');
  chars.push_back('H');
  chars.push_back('e');
  chars.push_back('l');
  chars.push_back('l');
  chars.push_back('o');
  writer->HandleString8(SpanFrom(chars));
  EXPECT_EQ("\"Hello\"", out);  // "Hello" shows we restarted at 'H'.
}

TEST(JsonEncoder, EscapesLoneHighSurrogates) {
  // This tests that the JSON encoder escapes lone high surrogates, i.e.
  // invalid code points in the range from 0xD800 to 0xDBFF. In
  // unescaped form, these cannot be represented in well-formed UTF-8 or
  // UTF-16.
  std::vector<uint16_t> chars = {'a', 0xd800, 'b', 0xdada, 'c', 0xdbff, 'd'};
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleString16(span<uint16_t>(chars.data(), chars.size()));
  EXPECT_EQ("\"a\\ud800b\\udadac\\udbffd\"", out);
}

TEST(JsonEncoder, EscapesLoneLowSurrogates) {
  // This tests that the JSON encoder escapes lone low surrogates, i.e.
  // invalid code points in the range from 0xDC00 to 0xDFFF. In
  // unescaped form, these cannot be represented in well-formed UTF-8 or
  // UTF-16.
  std::vector<uint16_t> chars = {'a', 0xdc00, 'b', 0xdede, 'c', 0xdfff, 'd'};
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleString16(span<uint16_t>(chars.data(), chars.size()));
  EXPECT_EQ("\"a\\udc00b\\udedec\\udfffd\"", out);
}

TEST(JsonEncoder, EscapesFFFF) {
  // This tests that the JSON encoder will escape the UTF16 input 0xffff as
  // \uffff; useful to check this since it's an edge case.
  std::vector<uint16_t> chars = {'a', 'b', 'c', 0xffff, 'd'};
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleString16(span<uint16_t>(chars.data(), chars.size()));
  EXPECT_EQ("\"abc\\uffffd\"", out);
}

TEST(JsonEncoder, Passes0x7FString8) {
  std::vector<uint8_t> chars = {'a', 0x7f, 'b'};
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleString8(span<uint8_t>(chars.data(), chars.size()));
  EXPECT_EQ(
      "\"a\x7f"
      "b\"",
      out);
}

TEST(JsonEncoder, Passes0x7FString16) {
  std::vector<uint16_t> chars16 = {'a', 0x7f, 'b'};
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleString16(span<uint16_t>(chars16.data(), chars16.size()));
  EXPECT_EQ(
      "\"a\x7f"
      "b\"",
      out);
}

TEST(JsonEncoder, IncompleteUtf8Sequence) {
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);

  writer->HandleArrayBegin();  // This emits [, which starts an array.

  {  // 🌎 takes four bytes to encode in UTF-8. We test with the first three;
    // This means we're trying to emit a string that consists solely of an
    // incomplete UTF-8 sequence. So the string in the JSON output is empty.
    std::string world_utf8 = "🌎";
    ASSERT_EQ(4u, world_utf8.size());
    std::vector<uint8_t> chars(world_utf8.begin(), world_utf8.begin() + 3);
    writer->HandleString8(SpanFrom(chars));
    EXPECT_EQ("[\"\"", out);  // Incomplete sequence rejected: empty string.
  }

  {  // This time, the incomplete sequence is at the end of the string.
    std::string msg = "Hello, \xF0\x9F\x8C";
    std::vector<uint8_t> chars(msg.begin(), msg.end());
    writer->HandleString8(SpanFrom(chars));
    EXPECT_EQ("[\"\",\"Hello, \"", out);  // Incomplete sequence dropped at end.
  }
}

TEST(JsonStdStringWriterTest, HelloWorld) {
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleMapBegin();
  WriteUTF8AsUTF16(writer.get(), "msg1");
  WriteUTF8AsUTF16(writer.get(), "Hello, 🌎.");
  std::string key = "msg1-as-utf8";
  std::string value = "Hello, 🌎.";
  writer->HandleString8(SpanFrom(key));
  writer->HandleString8(SpanFrom(value));
  WriteUTF8AsUTF16(writer.get(), "msg2");
  WriteUTF8AsUTF16(writer.get(), "\\\b\r\n\t\f\"");
  WriteUTF8AsUTF16(writer.get(), "nested");
  writer->HandleMapBegin();
  WriteUTF8AsUTF16(writer.get(), "double");
  writer->HandleDouble(3.1415);
  WriteUTF8AsUTF16(writer.get(), "int");
  writer->HandleInt32(-42);
  WriteUTF8AsUTF16(writer.get(), "bool");
  writer->HandleBool(false);
  WriteUTF8AsUTF16(writer.get(), "null");
  writer->HandleNull();
  writer->HandleMapEnd();
  WriteUTF8AsUTF16(writer.get(), "array");
  writer->HandleArrayBegin();
  writer->HandleInt32(1);
  writer->HandleInt32(2);
  writer->HandleInt32(3);
  writer->HandleArrayEnd();
  writer->HandleMapEnd();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(
      "{\"msg1\":\"Hello, \\ud83c\\udf0e.\","
      "\"msg1-as-utf8\":\"Hello, \\ud83c\\udf0e.\","
      "\"msg2\":\"\\\\\\b\\r\\n\\t\\f\\\"\","
      "\"nested\":{\"double\":3.1415,\"int\":-42,"
      "\"bool\":false,\"null\":null},\"array\":[1,2,3]}",
      out);
}

TEST(JsonStdStringWriterTest, ScalarsAreRenderedAsInt) {
  // Test that Number.MIN_SAFE_INTEGER / Number.MAX_SAFE_INTEGER from Javascript
  // are rendered as integers (no decimal point / rounding), even when we
  // encode them from double. Javascript's Number is an IEE754 double, so
  // it has 53 bits to represent integers.
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleMapBegin();

  writer->HandleString8(SpanFrom("Number.MIN_SAFE_INTEGER"));
  EXPECT_EQ(-0x1fffffffffffff, -9007199254740991);  // 53 bits for integers.
  writer->HandleDouble(-9007199254740991);          // Note HandleDouble here.

  writer->HandleString8(SpanFrom("Number.MAX_SAFE_INTEGER"));
  EXPECT_EQ(0x1fffffffffffff, 9007199254740991);  // 53 bits for integers.
  writer->HandleDouble(9007199254740991);         // Note HandleDouble here.

  writer->HandleMapEnd();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(
      "{\"Number.MIN_SAFE_INTEGER\":-9007199254740991,"
      "\"Number.MAX_SAFE_INTEGER\":9007199254740991}",
      out);
}

TEST(JsonStdStringWriterTest, RepresentingNonFiniteValuesAsNull) {
  // JSON can't represent +Infinity, -Infinity, or NaN.
  // So in practice it's mapped to null.
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleMapBegin();
  writer->HandleString8(SpanFrom("Infinity"));
  writer->HandleDouble(std::numeric_limits<double>::infinity());
  writer->HandleString8(SpanFrom("-Infinity"));
  writer->HandleDouble(-std::numeric_limits<double>::infinity());
  writer->HandleString8(SpanFrom("NaN"));
  writer->HandleDouble(std::numeric_limits<double>::quiet_NaN());
  writer->HandleMapEnd();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ("{\"Infinity\":null,\"-Infinity\":null,\"NaN\":null}", out);
}

TEST(JsonStdStringWriterTest, BinaryEncodedAsJsonString) {
  // The encoder emits binary submitted to ParserHandler::HandleBinary
  // as base64. The following three examples are taken from
  // https://en.wikipedia.org/wiki/Base64.
  {
    std::string out;
    Status status;
    std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
    writer->HandleBinary(SpanFrom(std::vector<uint8_t>({'M', 'a', 'n'})));
    EXPECT_TRUE(status.ok());
    EXPECT_EQ("\"TWFu\"", out);
  }
  {
    std::string out;
    Status status;
    std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
    writer->HandleBinary(SpanFrom(std::vector<uint8_t>({'M', 'a'})));
    EXPECT_TRUE(status.ok());
    EXPECT_EQ("\"TWE=\"", out);
  }
  {
    std::string out;
    Status status;
    std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
    writer->HandleBinary(SpanFrom(std::vector<uint8_t>({'M'})));
    EXPECT_TRUE(status.ok());
    EXPECT_EQ("\"TQ==\"", out);
  }
  {  // "Hello, world.", verified with base64decode.org.
    std::string out;
    Status status;
    std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
    writer->HandleBinary(SpanFrom(std::vector<uint8_t>(
        {'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '.'})));
    EXPECT_TRUE(status.ok());
    EXPECT_EQ("\"SGVsbG8sIHdvcmxkLg==\"", out);
  }
}

TEST(JsonStdStringWriterTest, HandlesErrors) {
  // When an error is sent via HandleError, it saves it in the provided
  // status and clears the output.
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleMapBegin();
  WriteUTF8AsUTF16(writer.get(), "msg1");
  writer->HandleError(Status{Error::JSON_PARSER_VALUE_EXPECTED, 42});
  EXPECT_THAT(status, StatusIs(Error::JSON_PARSER_VALUE_EXPECTED, 42u));
  EXPECT_EQ("", out);
}

TEST(JsonStdStringWriterTest, DoubleToString_LeadingZero) {
  // In JSON, .1 must be rendered as 0.1, and -.7 must be rendered as -0.7.
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> writer = NewJSONEncoder(&out, &status);
  writer->HandleArrayBegin();
  writer->HandleDouble(.1);
  writer->HandleDouble(-.7);
  writer->HandleArrayEnd();
  EXPECT_EQ("[0.1,-0.7]", out);
}

// =============================================================================
// json::ParseJSON - for receiving streaming parser events for JSON
// =============================================================================

class Log : public ParserHandler {
 public:
  void HandleMapBegin() override { log_ << "map begin\n"; }

  void HandleMapEnd() override { log_ << "map end\n"; }

  void HandleArrayBegin() override { log_ << "array begin\n"; }

  void HandleArrayEnd() override { log_ << "array end\n"; }

  void HandleString8(span<uint8_t> chars) override {
    log_ << "string8: " << std::string(chars.begin(), chars.end()) << "\n";
  }

  void HandleString16(span<uint16_t> chars) override {
    raw_log_string16_.emplace_back(chars.begin(), chars.end());
    log_ << "string16: " << UTF16ToUTF8(chars) << "\n";
  }

  void HandleBinary(span<uint8_t> bytes) override {
    // JSON doesn't have native support for arbitrary bytes, so our parser will
    // never call this.
    CHECK(false);
  }

  void HandleDouble(double value) override {
    log_ << "double: " << value << "\n";
  }

  void HandleInt32(int32_t value) override { log_ << "int: " << value << "\n"; }

  void HandleBool(bool value) override { log_ << "bool: " << value << "\n"; }

  void HandleNull() override { log_ << "null\n"; }

  void HandleError(Status status) override { status_ = status; }

  std::string str() const { return status_.ok() ? log_.str() : ""; }

  std::vector<std::vector<uint16_t>> raw_log_string16() const {
    return raw_log_string16_;
  }

  Status status() const { return status_; }

 private:
  std::ostringstream log_;
  std::vector<std::vector<uint16_t>> raw_log_string16_;
  Status status_;
};

class JsonParserTest : public ::testing::Test {
 protected:
  Log log_;
};

TEST_F(JsonParserTest, SimpleDictionary) {
  std::string json = "{\"foo\": 42}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: foo\n"
      "int: 42\n"
      "map end\n",
      log_.str());
}

TEST_F(JsonParserTest, UsAsciiDelCornerCase) {
  // DEL (0x7f) is a 7 bit US-ASCII character, and while it is a control
  // character according to Unicode, it's not considered a control
  // character in https://tools.ietf.org/html/rfc7159#section-7, so
  // it can be placed directly into the JSON string, without JSON escaping.
  std::string json = "{\"foo\": \"a\x7f\"}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: foo\n"
      "string16: a\x7f\n"
      "map end\n",
      log_.str());

  // We've seen an implementation of UTF16ToUTF8 which would replace the DEL
  // character with ' ', so this simple roundtrip tests the routines in
  // encoding_test_helper.h, to make test failures of the above easier to
  // diagnose.
  std::vector<uint16_t> utf16 = UTF8ToUTF16(SpanFrom(json));
  EXPECT_EQ(json, UTF16ToUTF8(SpanFrom(utf16)));
}

TEST_F(JsonParserTest, Whitespace) {
  std::string json = "\n  {\n\"msg\"\n: \v\"Hello, world.\"\t\r}\t";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: msg\n"
      "string16: Hello, world.\n"
      "map end\n",
      log_.str());
}

TEST_F(JsonParserTest, NestedDictionary) {
  std::string json = "{\"foo\": {\"bar\": {\"baz\": 1}, \"bar2\": 2}}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: foo\n"
      "map begin\n"
      "string16: bar\n"
      "map begin\n"
      "string16: baz\n"
      "int: 1\n"
      "map end\n"
      "string16: bar2\n"
      "int: 2\n"
      "map end\n"
      "map end\n",
      log_.str());
}

TEST_F(JsonParserTest, Doubles) {
  std::string json = "{\"foo\": 3.1415, \"bar\": 31415e-4}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: foo\n"
      "double: 3.1415\n"
      "string16: bar\n"
      "double: 3.1415\n"
      "map end\n",
      log_.str());
}

TEST_F(JsonParserTest, Unicode) {
  // Globe character. 0xF0 0x9F 0x8C 0x8E in utf8, 0xD83C 0xDF0E in utf16.
  std::string json = "{\"msg\": \"Hello, \\uD83C\\uDF0E.\"}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: msg\n"
      "string16: Hello, 🌎.\n"
      "map end\n",
      log_.str());
}

TEST_F(JsonParserTest, Unicode_ParseUtf16) {
  // Globe character. utf8: 0xF0 0x9F 0x8C 0x8E; utf16: 0xD83C 0xDF0E.
  // Crescent moon character. utf8: 0xF0 0x9F 0x8C 0x99; utf16: 0xD83C 0xDF19.

  // We provide the moon with json escape, but the earth as utf16 input.
  // Either way they arrive as utf8 (after decoding in log_.str()).
  std::vector<uint16_t> json =
      UTF8ToUTF16(SpanFrom("{\"space\": \"🌎 \\uD83C\\uDF19.\"}"));
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: space\n"
      "string16: 🌎 🌙.\n"
      "map end\n",
      log_.str());
}

TEST_F(JsonParserTest, Unicode_ParseUtf16_SingleEscapeUpToFFFF) {
  // 0xFFFF is the max codepoint that can be represented as a single \u escape.
  // One way to write this is \uffff, another way is to encode it as a 3 byte
  // UTF-8 sequence (0xef 0xbf 0xbf). Both are equivalent.

  // Example with both ways of encoding code point 0xFFFF in a JSON string.
  std::string json = "{\"escape\": \"\xef\xbf\xbf or \\uffff\"}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());

  // Shows both inputs result in equivalent output once converted to UTF-8.
  EXPECT_EQ(
      "map begin\n"
      "string16: escape\n"
      "string16: \xEF\xBF\xBF or \xEF\xBF\xBF\n"
      "map end\n",
      log_.str());

  // Make an even stronger assertion: The parser represents \xffff as a single
  // UTF-16 char.
  ASSERT_EQ(2u, log_.raw_log_string16().size());
  std::vector<uint16_t> expected = {0xffff, ' ', 'o', 'r', ' ', 0xffff};
  EXPECT_EQ(expected, log_.raw_log_string16()[1]);
}

TEST_F(JsonParserTest, Unicode_ParseUtf8) {
  // Used below:
  // гласность - example for 2 byte utf8, Russian word "glasnost"
  // 屋 - example for 3 byte utf8, Chinese word for "house"
  // 🌎 - example for 4 byte utf8: 0xF0 0x9F 0x8C 0x8E; utf16: 0xD83C 0xDF0E.
  // 🌙 - example for escapes: utf8: 0xF0 0x9F 0x8C 0x99; utf16: 0xD83C 0xDF19.

  // We provide the moon with json escape, but the earth as utf8 input.
  // Either way they arrive as utf8 (after decoding in log_.str()).
  std::string json =
      "{"
      "\"escapes\": \"\\uD83C\\uDF19\","
      "\"2 byte\":\"гласность\","
      "\"3 byte\":\"屋\","
      "\"4 byte\":\"🌎\""
      "}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: escapes\n"
      "string16: 🌙\n"
      "string16: 2 byte\n"
      "string16: гласность\n"
      "string16: 3 byte\n"
      "string16: 屋\n"
      "string16: 4 byte\n"
      "string16: 🌎\n"
      "map end\n",
      log_.str());
}

TEST_F(JsonParserTest, UnprocessedInputRemainsError) {
  // Trailing junk after the valid JSON.
  std::string json = "{\"foo\": 3.1415} junk";
  size_t junk_idx = json.find("junk");
  EXPECT_NE(junk_idx, std::string::npos);
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(),
              StatusIs(Error::JSON_PARSER_UNPROCESSED_INPUT_REMAINS, junk_idx));
  EXPECT_EQ("", log_.str());
}

std::string MakeNestedJson(int depth) {
  std::string json;
  for (int ii = 0; ii < depth; ++ii)
    json += "{\"foo\":";
  json += "42";
  for (int ii = 0; ii < depth; ++ii)
    json += "}";
  return json;
}

TEST_F(JsonParserTest, StackLimitExceededError_BelowLimit) {
  // kStackLimit is 300 (see json_parser.cc). First let's
  // try with a small nested example.
  std::string json_3 = MakeNestedJson(3);
  ParseJSON(SpanFrom(json_3), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: foo\n"
      "map begin\n"
      "string16: foo\n"
      "map begin\n"
      "string16: foo\n"
      "int: 42\n"
      "map end\n"
      "map end\n"
      "map end\n",
      log_.str());
}

TEST_F(JsonParserTest, StackLimitExceededError_AtLimit) {
  // Now with kStackLimit (300).
  std::string json_limit = MakeNestedJson(300);
  ParseJSON(span<uint8_t>(reinterpret_cast<const uint8_t*>(json_limit.data()),
                          json_limit.size()),
            &log_);
  EXPECT_THAT(log_.status(), StatusIsOk());
}

TEST_F(JsonParserTest, StackLimitExceededError_AboveLimit) {
  // Now with kStackLimit + 1 (301) - it exceeds in the innermost instance.
  std::string exceeded = MakeNestedJson(301);
  ParseJSON(SpanFrom(exceeded), &log_);
  EXPECT_THAT(log_.status(), StatusIs(Error::JSON_PARSER_STACK_LIMIT_EXCEEDED,
                                      strlen("{\"foo\":") * 301));
}

TEST_F(JsonParserTest, StackLimitExceededError_WayAboveLimit) {
  // Now way past the limit. Still, the point of exceeding is 301.
  std::string far_out = MakeNestedJson(320);
  ParseJSON(SpanFrom(far_out), &log_);
  EXPECT_THAT(log_.status(), StatusIs(Error::JSON_PARSER_STACK_LIMIT_EXCEEDED,
                                      strlen("{\"foo\":") * 301));
}

TEST_F(JsonParserTest, NoInputError) {
  std::string json = "";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(), StatusIs(Error::JSON_PARSER_NO_INPUT, 0u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, InvalidTokenError) {
  std::string json = "|";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(), StatusIs(Error::JSON_PARSER_INVALID_TOKEN, 0u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, InvalidNumberError) {
  // Mantissa exceeds max (the constant used here is int64_t max).
  std::string json = "1E9223372036854775807";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(), StatusIs(Error::JSON_PARSER_INVALID_NUMBER, 0u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, InvalidStringError) {
  // \x22 is an unsupported escape sequence
  std::string json = "\"foo\\x22\"";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(), StatusIs(Error::JSON_PARSER_INVALID_STRING, 0u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, UnexpectedArrayEndError) {
  std::string json = "[1,2,]";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(),
              StatusIs(Error::JSON_PARSER_UNEXPECTED_ARRAY_END, 5u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, CommaOrArrayEndExpectedError) {
  std::string json = "[1,2 2";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(),
              StatusIs(Error::JSON_PARSER_COMMA_OR_ARRAY_END_EXPECTED, 5u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, StringLiteralExpectedError) {
  // There's an error because the key bar, a string, is not terminated.
  std::string json = "{\"foo\": 3.1415, \"bar: 31415e-4}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(),
              StatusIs(Error::JSON_PARSER_STRING_LITERAL_EXPECTED, 16u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, ColonExpectedError) {
  std::string json = "{\"foo\", 42}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(), StatusIs(Error::JSON_PARSER_COLON_EXPECTED, 6u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, UnexpectedMapEndError) {
  std::string json = "{\"foo\": 42, }";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(),
              StatusIs(Error::JSON_PARSER_UNEXPECTED_MAP_END, 12u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, CommaOrMapEndExpectedError) {
  // The second separator should be a comma.
  std::string json = "{\"foo\": 3.1415: \"bar\": 0}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(),
              StatusIs(Error::JSON_PARSER_COMMA_OR_MAP_END_EXPECTED, 14u));
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, ValueExpectedError) {
  std::string json = "}";
  ParseJSON(SpanFrom(json), &log_);
  EXPECT_THAT(log_.status(), StatusIs(Error::JSON_PARSER_VALUE_EXPECTED, 0u));
  EXPECT_EQ("", log_.str());
}

template <typename T>
class ConvertJSONToCBORTest : public ::testing::Test {};

using ContainerTestTypes = ::testing::Types<std::vector<uint8_t>, std::string>;
TYPED_TEST_SUITE(ConvertJSONToCBORTest, ContainerTestTypes);

TYPED_TEST(ConvertJSONToCBORTest, RoundTripValidJson) {
  for (const std::string& json_in : {
           "{\"msg\":\"Hello, world.\",\"lst\":[1,2,3]}",
           "3.1415",
           "false",
           "true",
           "\"Hello, world.\"",
           "[1,2,3]",
           "[]",
       }) {
    SCOPED_TRACE(json_in);
    TypeParam json(json_in.begin(), json_in.end());
    std::vector<uint8_t> cbor;
    {
      Status status = ConvertJSONToCBOR(SpanFrom(json), &cbor);
      EXPECT_THAT(status, StatusIsOk());
    }
    TypeParam roundtrip_json;
    {
      Status status = ConvertCBORToJSON(SpanFrom(cbor), &roundtrip_json);
      EXPECT_THAT(status, StatusIsOk());
    }
    EXPECT_EQ(json, roundtrip_json);
  }
}

TYPED_TEST(ConvertJSONToCBORTest, RoundTripValidJson16) {
  std::vector<uint16_t> json16 = {
      '{', '"', 'm', 's',    'g',    '"', ':', '"', 'H', 'e', 'l', 'l',
      'o', ',', ' ', 0xd83c, 0xdf0e, '.', '"', ',', '"', 'l', 's', 't',
      '"', ':', '[', '1',    ',',    '2', ',', '3', ']', '}'};
  std::vector<uint8_t> cbor;
  {
    Status status =
        ConvertJSONToCBOR(span<uint16_t>(json16.data(), json16.size()), &cbor);
    EXPECT_THAT(status, StatusIsOk());
  }
  TypeParam roundtrip_json;
  {
    Status status = ConvertCBORToJSON(SpanFrom(cbor), &roundtrip_json);
    EXPECT_THAT(status, StatusIsOk());
  }
  std::string json = "{\"msg\":\"Hello, \\ud83c\\udf0e.\",\"lst\":[1,2,3]}";
  TypeParam expected_json(json.begin(), json.end());
  EXPECT_EQ(expected_json, roundtrip_json);
}
}  // namespace json
}  // namespace crdtp
