// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cbor.h"

#include <array>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include "json.h"
#include "parser_handler.h"
#include "span.h"
#include "status.h"
#include "status_test_support.h"
#include "test_platform.h"

using testing::ElementsAreArray;
using testing::Eq;

namespace crdtp {
namespace cbor {
// =============================================================================
// Detecting CBOR content
// =============================================================================

TEST(IsCBORMessage, SomeSmokeTests) {
  std::vector<uint8_t> empty;
  EXPECT_FALSE(IsCBORMessage(SpanFrom(empty)));
  std::vector<uint8_t> hello = {'H', 'e', 'l', 'o', ' ', 't',
                                'h', 'e', 'r', 'e', '!'};
  EXPECT_FALSE(IsCBORMessage(SpanFrom(hello)));
  std::vector<uint8_t> example = {0xd8, 0x5a, 0, 0, 0, 0};
  EXPECT_TRUE(IsCBORMessage(SpanFrom(example)));
  std::vector<uint8_t> one = {0xd8, 0x5a, 0, 0, 0, 1, 1};
  EXPECT_TRUE(IsCBORMessage(SpanFrom(one)));
}

TEST(CheckCBORMessage, SmallestValidExample) {
  // The smallest example that we consider valid for this lightweight check is
  // an empty dictionary inside of an envelope.
  std::vector<uint8_t> empty_dict = {
      0xd8, 0x5a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), EncodeStop()};
  Status status = CheckCBORMessage(SpanFrom(empty_dict));
  EXPECT_THAT(status, StatusIsOk());
}

TEST(CheckCBORMessage, ValidCBORButNotValidMessage) {
  // The CBOR parser supports parsing values that aren't messages. E.g., this is
  // the encoded unsigned int 7 (CBOR really encodes it as a single byte with
  // value 7).
  std::vector<uint8_t> not_a_message = {7};

  // Show that the parser (happily) decodes it into JSON
  std::string json;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&json, &status);
  ParseCBOR(SpanFrom(not_a_message), json_writer.get());
  EXPECT_THAT(status, StatusIsOk());
  EXPECT_EQ("7", json);

  // ... but it's not a message.
  EXPECT_THAT(CheckCBORMessage(SpanFrom(not_a_message)),
              StatusIs(Error::CBOR_INVALID_START_BYTE, 0));
}

TEST(CheckCBORMessage, EmptyMessage) {
  std::vector<uint8_t> empty;
  Status status = CheckCBORMessage(SpanFrom(empty));
  EXPECT_THAT(status, StatusIs(Error::CBOR_UNEXPECTED_EOF_IN_ENVELOPE, 0));
}

TEST(CheckCBORMessage, InvalidStartByte) {
  // Here we test that some actual json, which usually starts with {, is not
  // considered CBOR. CBOR messages must start with 0xd8, 0x5a, the envelope
  // start bytes.
  Status status = CheckCBORMessage(SpanFrom("{\"msg\": \"Hello, world.\"}"));
  EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_START_BYTE, 0));
}

TEST(CheckCBORMessage, InvalidEnvelopes) {
  std::vector<uint8_t> bytes = {0xd8, 0x5a};
  EXPECT_THAT(CheckCBORMessage(SpanFrom(bytes)),
              StatusIs(Error::CBOR_UNEXPECTED_EOF_IN_ENVELOPE, 2));
  bytes = {0xd8, 0x5a, 0};
  EXPECT_THAT(CheckCBORMessage(SpanFrom(bytes)),
              StatusIs(Error::CBOR_UNEXPECTED_EOF_IN_ENVELOPE, 3));
  bytes = {0xd8, 0x5a, 0, 0};
  EXPECT_THAT(CheckCBORMessage(SpanFrom(bytes)),
              StatusIs(Error::CBOR_UNEXPECTED_EOF_IN_ENVELOPE, 4));
  bytes = {0xd8, 0x5a, 0, 0, 0};
  EXPECT_THAT(CheckCBORMessage(SpanFrom(bytes)),
              StatusIs(Error::CBOR_UNEXPECTED_EOF_IN_ENVELOPE, 5));
  bytes = {0xd8, 0x5a, 0, 0, 0, 0};
  EXPECT_THAT(CheckCBORMessage(SpanFrom(bytes)),
              StatusIs(Error::CBOR_MAP_OR_ARRAY_EXPECTED_IN_ENVELOPE, 6));
}

TEST(CheckCBORMessage, MapStartExpected) {
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, 1};
  EXPECT_THAT(CheckCBORMessage(SpanFrom(bytes)),
              StatusIs(Error::CBOR_ENVELOPE_CONTENTS_LENGTH_MISMATCH, 6));
}

// =============================================================================
// Encoding individual CBOR items
// cbor::CBORTokenizer - for parsing individual CBOR items
// =============================================================================

//
// EncodeInt32 / CBORTokenTag::INT32
//
TEST(EncodeDecodeInt32Test, Roundtrips23) {
  // This roundtrips the int32_t value 23 through the pair of EncodeInt32 /
  // CBORTokenizer; this is interesting since 23 is encoded as a single byte.
  std::vector<uint8_t> encoded;
  EncodeInt32(23, &encoded);
  // first three bits: major type = 0; remaining five bits: additional info =
  // value 23.
  EXPECT_THAT(encoded, ElementsAreArray(std::array<uint8_t, 1>{{23}}));

  // Reverse direction: decode with CBORTokenizer.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::INT32, tokenizer.TokenTag());
  EXPECT_EQ(23, tokenizer.GetInt32());
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeInt32Test, RoundtripsUint8) {
  // This roundtrips the int32_t value 42 through the pair of EncodeInt32 /
  // CBORTokenizer. This is different from Roundtrip23 because 42 is encoded
  // in an extra byte after the initial one.
  std::vector<uint8_t> encoded;
  EncodeInt32(42, &encoded);
  // first three bits: major type = 0;
  // remaining five bits: additional info = 24, indicating payload is uint8.
  EXPECT_THAT(encoded, ElementsAreArray(std::array<uint8_t, 2>{{24, 42}}));

  // Reverse direction: decode with CBORTokenizer.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::INT32, tokenizer.TokenTag());
  EXPECT_EQ(42, tokenizer.GetInt32());
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeInt32Test, RoundtripsUint16) {
  // 500 is encoded as a uint16 after the initial byte.
  std::vector<uint8_t> encoded;
  EncodeInt32(500, &encoded);
  // 1 for initial byte, 2 for uint16.
  EXPECT_EQ(3u, encoded.size());
  // first three bits: major type = 0;
  // remaining five bits: additional info = 25, indicating payload is uint16.
  EXPECT_EQ(25, encoded[0]);
  EXPECT_EQ(0x01, encoded[1]);
  EXPECT_EQ(0xf4, encoded[2]);

  // Reverse direction: decode with CBORTokenizer.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::INT32, tokenizer.TokenTag());
  EXPECT_EQ(500, tokenizer.GetInt32());
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeInt32Test, RoundtripsInt32Max) {
  // std::numeric_limits<int32_t> is encoded as a uint32 after the initial byte.
  std::vector<uint8_t> encoded;
  EncodeInt32(std::numeric_limits<int32_t>::max(), &encoded);
  // 1 for initial byte, 4 for the uint32.
  // first three bits: major type = 0;
  // remaining five bits: additional info = 26, indicating payload is uint32.
  EXPECT_THAT(
      encoded,
      ElementsAreArray(std::array<uint8_t, 5>{{26, 0x7f, 0xff, 0xff, 0xff}}));

  // Reverse direction: decode with CBORTokenizer.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::INT32, tokenizer.TokenTag());
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), tokenizer.GetInt32());
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeInt32Test, RoundtripsInt32Min) {
  // std::numeric_limits<int32_t> is encoded as a uint32 (4 unsigned bytes)
  // after the initial byte, which effectively carries the sign by
  // designating the token as NEGATIVE.
  std::vector<uint8_t> encoded;
  EncodeInt32(std::numeric_limits<int32_t>::min(), &encoded);
  // 1 for initial byte, 4 for the uint32.
  // first three bits: major type = 1;
  // remaining five bits: additional info = 26, indicating payload is uint32.
  EXPECT_THAT(encoded, ElementsAreArray(std::array<uint8_t, 5>{
                           {1 << 5 | 26, 0x7f, 0xff, 0xff, 0xff}}));

  // Reverse direction: decode with CBORTokenizer.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::INT32, tokenizer.TokenTag());
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), tokenizer.GetInt32());
  // It's nice to see how the min int32 value reads in hex:
  // That is, -1 minus the unsigned payload (0x7fffffff, see above).
  int32_t expected = -1 - 0x7fffffff;
  EXPECT_EQ(expected, tokenizer.GetInt32());
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeInt32Test, CantRoundtripUint32) {
  // 0xdeadbeef is a value which does not fit below
  // std::numerical_limits<int32_t>::max(), so we can't encode
  // it with EncodeInt32. However, CBOR does support this, so we
  // encode it here manually with the internal routine, just to observe
  // that it's considered an invalid int32 by CBORTokenizer.
  std::vector<uint8_t> encoded;
  internals::WriteTokenStart(MajorType::UNSIGNED, 0xdeadbeef, &encoded);
  // 1 for initial byte, 4 for the uint32.
  // first three bits: major type = 0;
  // remaining five bits: additional info = 26, indicating payload is uint32.
  EXPECT_THAT(
      encoded,
      ElementsAreArray(std::array<uint8_t, 5>{{26, 0xde, 0xad, 0xbe, 0xef}}));

  // Now try to decode; we treat this as an invalid INT32.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  // 0xdeadbeef is > std::numerical_limits<int32_t>::max().
  EXPECT_EQ(CBORTokenTag::ERROR_VALUE, tokenizer.TokenTag());
  EXPECT_THAT(tokenizer.Status(), StatusIs(Error::CBOR_INVALID_INT32, 0u));
}

TEST(EncodeDecodeInt32Test, DecodeErrorCases) {
  struct TestCase {
    std::vector<uint8_t> data;
    std::string msg;
  };
  std::vector<TestCase> tests{{
      TestCase{
          {24},
          "additional info = 24 would require 1 byte of payload (but it's 0)"},
      TestCase{{27, 0xaa, 0xbb, 0xcc},
               "additional info = 27 would require 8 bytes of payload (but "
               "it's 3)"},
      TestCase{{29}, "additional info = 29 isn't recognized"},
      TestCase{{1 << 5 | 27, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
               "Max UINT64 payload is outside the allowed range"},
      TestCase{{1 << 5 | 26, 0xff, 0xff, 0xff, 0xff},
               "Max UINT32 payload is outside the allowed range"},
      TestCase{{1 << 5 | 26, 0x80, 0x00, 0x00, 0x00},
               "UINT32 payload w/ high bit set is outside the allowed range"},
  }};
  for (const TestCase& test : tests) {
    SCOPED_TRACE(test.msg);
    CBORTokenizer tokenizer(SpanFrom(test.data));
    EXPECT_EQ(CBORTokenTag::ERROR_VALUE, tokenizer.TokenTag());
    EXPECT_THAT(tokenizer.Status(), StatusIs(Error::CBOR_INVALID_INT32, 0u));
  }
}

TEST(EncodeDecodeInt32Test, RoundtripsMinus24) {
  // This roundtrips the int32_t value -24 through the pair of EncodeInt32 /
  // CBORTokenizer; this is interesting since -24 is encoded as
  // a single byte as NEGATIVE, and it tests the specific encoding
  // (note how for unsigned the single byte covers values up to 23).
  // Additional examples are covered in RoundtripsAdditionalExamples.
  std::vector<uint8_t> encoded;
  EncodeInt32(-24, &encoded);
  // first three bits: major type = 1; remaining five bits: additional info =
  // value 23.
  EXPECT_THAT(encoded, ElementsAreArray(std::array<uint8_t, 1>{{1 << 5 | 23}}));

  // Reverse direction: decode with CBORTokenizer.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::INT32, tokenizer.TokenTag());
  EXPECT_EQ(-24, tokenizer.GetInt32());
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeInt32Test, RoundtripsAdditionalNegativeExamples) {
  std::vector<int32_t> examples = {-1,
                                   -10,
                                   -24,
                                   -25,
                                   -300,
                                   -30000,
                                   -300 * 1000,
                                   -1000 * 1000,
                                   -1000 * 1000 * 1000,
                                   std::numeric_limits<int32_t>::min()};
  for (int32_t example : examples) {
    SCOPED_TRACE(std::string("example ") + std::to_string(example));
    std::vector<uint8_t> encoded;
    EncodeInt32(example, &encoded);
    CBORTokenizer tokenizer(SpanFrom(encoded));
    EXPECT_EQ(CBORTokenTag::INT32, tokenizer.TokenTag());
    EXPECT_EQ(example, tokenizer.GetInt32());
    tokenizer.Next();
    EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
  }
}

//
// EncodeString16 / CBORTokenTag::STRING16
//
TEST(EncodeDecodeString16Test, RoundtripsEmpty) {
  // This roundtrips the empty utf16 string through the pair of EncodeString16 /
  // CBORTokenizer.
  std::vector<uint8_t> encoded;
  EncodeString16(span<uint16_t>(), &encoded);
  EXPECT_EQ(1u, encoded.size());
  // first three bits: major type = 2; remaining five bits: additional info =
  // size 0.
  EXPECT_EQ(2 << 5, encoded[0]);

  // Reverse direction: decode with CBORTokenizer.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::STRING16, tokenizer.TokenTag());
  span<uint8_t> decoded_string16_wirerep = tokenizer.GetString16WireRep();
  EXPECT_TRUE(decoded_string16_wirerep.empty());
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

// On the wire, we STRING16 is encoded as little endian (least
// significant byte first). The host may or may not be little endian,
// so this routine follows the advice in
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html.
std::vector<uint16_t> String16WireRepToHost(span<uint8_t> in) {
  // must be even number of bytes.
  CHECK_EQ(in.size() & 1, 0u);
  std::vector<uint16_t> host_out;
  for (size_t ii = 0; ii < in.size(); ii += 2)
    host_out.push_back(in[ii + 1] << 8 | in[ii]);
  return host_out;
}

TEST(EncodeDecodeString16Test, RoundtripsHelloWorld) {
  // This roundtrips the hello world message which is given here in utf16
  // characters. 0xd83c, 0xdf0e: UTF16 encoding for the "Earth Globe Americas"
  // character, 🌎.
  std::array<uint16_t, 10> msg{
      {'H', 'e', 'l', 'l', 'o', ',', ' ', 0xd83c, 0xdf0e, '.'}};
  std::vector<uint8_t> encoded;
  EncodeString16(span<uint16_t>(msg.data(), msg.size()), &encoded);
  // This will be encoded as BYTE_STRING of length 20, so the 20 is encoded in
  // the additional info part of the initial byte. Payload is two bytes for each
  // UTF16 character.
  uint8_t initial_byte = /*major type=*/2 << 5 | /*additional info=*/20;
  std::array<uint8_t, 21> encoded_expected = {
      {initial_byte, 'H', 0,   'e', 0,    'l',  0,    'l',  0,   'o', 0,
       ',',          0,   ' ', 0,   0x3c, 0xd8, 0x0e, 0xdf, '.', 0}};
  EXPECT_THAT(encoded, ElementsAreArray(encoded_expected));

  // Now decode to complete the roundtrip.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::STRING16, tokenizer.TokenTag());
  std::vector<uint16_t> decoded =
      String16WireRepToHost(tokenizer.GetString16WireRep());
  EXPECT_THAT(decoded, ElementsAreArray(msg));
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());

  // For bonus points, we look at the decoded message in UTF8 as well so we can
  // easily see it on the terminal screen.
  std::string utf8_decoded = UTF16ToUTF8(SpanFrom(decoded));
  EXPECT_EQ("Hello, 🌎.", utf8_decoded);
}

TEST(EncodeDecodeString16Test, Roundtrips500) {
  // We roundtrip a message that has 250 16 bit values. Each of these are just
  // set to their index. 250 is interesting because the cbor spec uses a
  // BYTE_STRING of length 500 for one of their examples of how to encode the
  // start of it (section 2.1) so it's easy for us to look at the first three
  // bytes closely.
  std::vector<uint16_t> two_fifty;
  for (uint16_t ii = 0; ii < 250; ++ii)
    two_fifty.push_back(ii);
  std::vector<uint8_t> encoded;
  EncodeString16(span<uint16_t>(two_fifty.data(), two_fifty.size()), &encoded);
  EXPECT_EQ(3u + 250u * 2, encoded.size());
  // Now check the first three bytes:
  // Major type: 2 (BYTE_STRING)
  // Additional information: 25, indicating size is represented by 2 bytes.
  // Bytes 1 and 2 encode 500 (0x01f4).
  EXPECT_EQ(2 << 5 | 25, encoded[0]);
  EXPECT_EQ(0x01, encoded[1]);
  EXPECT_EQ(0xf4, encoded[2]);

  // Now decode to complete the roundtrip.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::STRING16, tokenizer.TokenTag());
  std::vector<uint16_t> decoded =
      String16WireRepToHost(tokenizer.GetString16WireRep());
  EXPECT_THAT(decoded, ElementsAreArray(two_fifty));
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeString16Test, ErrorCases) {
  struct TestCase {
    std::vector<uint8_t> data;
    std::string msg;
  };
  std::vector<TestCase> tests{
      {TestCase{{2 << 5 | 1, 'a'},
                "length must be divisible by 2 (but it's 1)"},
       TestCase{{2 << 5 | 29}, "additional info = 29 isn't recognized"},
       TestCase{{2 << 5 | 9, 1, 2, 3, 4, 5, 6, 7, 8},
                "length (9) points just past the end of the test case"},
       TestCase{{2 << 5 | 27, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 'a', 'b', 'c'},
                "large length pointing past the end of the test case"}}};
  for (const TestCase& test : tests) {
    SCOPED_TRACE(test.msg);
    CBORTokenizer tokenizer(SpanFrom(test.data));
    EXPECT_EQ(CBORTokenTag::ERROR_VALUE, tokenizer.TokenTag());
    EXPECT_THAT(tokenizer.Status(), StatusIs(Error::CBOR_INVALID_STRING16, 0u));
  }
}

//
// EncodeString8 / CBORTokenTag::STRING8
//
TEST(EncodeDecodeString8Test, RoundtripsHelloWorld) {
  // This roundtrips the hello world message which is given here in utf8
  // characters. 🌎 is a four byte utf8 character.
  std::string utf8_msg = "Hello, 🌎.";
  std::vector<uint8_t> msg(utf8_msg.begin(), utf8_msg.end());
  std::vector<uint8_t> encoded;
  EncodeString8(SpanFrom(utf8_msg), &encoded);
  // This will be encoded as STRING of length 12, so the 12 is encoded in
  // the additional info part of the initial byte. Payload is one byte per
  // utf8 byte.
  uint8_t initial_byte = /*major type=*/3 << 5 | /*additional info=*/12;
  std::array<uint8_t, 13> encoded_expected = {{initial_byte, 'H', 'e', 'l', 'l',
                                               'o', ',', ' ', 0xF0, 0x9f, 0x8c,
                                               0x8e, '.'}};
  EXPECT_THAT(encoded, ElementsAreArray(encoded_expected));

  // Now decode to complete the roundtrip.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::STRING8, tokenizer.TokenTag());
  std::vector<uint8_t> decoded(tokenizer.GetString8().begin(),
                               tokenizer.GetString8().end());
  EXPECT_THAT(decoded, ElementsAreArray(msg));
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeString8Test, ErrorCases) {
  struct TestCase {
    std::vector<uint8_t> data;
    std::string msg;
  };
  std::vector<TestCase> tests{
      {TestCase{{3 << 5 | 29}, "additional info = 29 isn't recognized"},
       TestCase{{3 << 5 | 9, 1, 2, 3, 4, 5, 6, 7, 8},
                "length (9) points just past the end of the test case"},
       TestCase{{3 << 5 | 27, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 'a', 'b', 'c'},
                "large length pointing past the end of the test case"}}};
  for (const TestCase& test : tests) {
    SCOPED_TRACE(test.msg);
    CBORTokenizer tokenizer(SpanFrom(test.data));
    EXPECT_EQ(CBORTokenTag::ERROR_VALUE, tokenizer.TokenTag());
    EXPECT_THAT(tokenizer.Status(), StatusIs(Error::CBOR_INVALID_STRING8, 0u));
  }
}

TEST(EncodeFromLatin1Test, ConvertsToUTF8IfNeeded) {
  std::vector<std::pair<std::string, std::string>> examples = {
      {"Hello, world.", "Hello, world."},
      {"Above: \xDC"
       "ber",
       "Above: Über"},
      {"\xA5 500 are about \xA3 3.50; a y with umlaut is \xFF",
       "¥ 500 are about £ 3.50; a y with umlaut is ÿ"}};

  for (const auto& example : examples) {
    const std::string& latin1 = example.first;
    const std::string& expected_utf8 = example.second;
    std::vector<uint8_t> encoded;
    EncodeFromLatin1(SpanFrom(latin1), &encoded);
    CBORTokenizer tokenizer(SpanFrom(encoded));
    EXPECT_EQ(CBORTokenTag::STRING8, tokenizer.TokenTag());
    std::vector<uint8_t> decoded(tokenizer.GetString8().begin(),
                                 tokenizer.GetString8().end());
    std::string decoded_str(decoded.begin(), decoded.end());
    EXPECT_THAT(decoded_str, testing::Eq(expected_utf8));
  }
}

TEST(EncodeFromUTF16Test, ConvertsToUTF8IfEasy) {
  std::vector<uint16_t> ascii = {'e', 'a', 's', 'y'};
  std::vector<uint8_t> encoded;
  EncodeFromUTF16(span<uint16_t>(ascii.data(), ascii.size()), &encoded);

  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::STRING8, tokenizer.TokenTag());
  std::vector<uint8_t> decoded(tokenizer.GetString8().begin(),
                               tokenizer.GetString8().end());
  std::string decoded_str(decoded.begin(), decoded.end());
  EXPECT_THAT(decoded_str, testing::Eq("easy"));
}

TEST(EncodeFromUTF16Test, EncodesAsString16IfNeeded) {
  // Since this message contains non-ASCII characters, the routine is
  // forced to encode as UTF16. We see this below by checking that the
  // token tag is STRING16.
  std::vector<uint16_t> msg = {'H', 'e', 'l',    'l',    'o',
                               ',', ' ', 0xd83c, 0xdf0e, '.'};
  std::vector<uint8_t> encoded;
  EncodeFromUTF16(span<uint16_t>(msg.data(), msg.size()), &encoded);

  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::STRING16, tokenizer.TokenTag());
  std::vector<uint16_t> decoded =
      String16WireRepToHost(tokenizer.GetString16WireRep());
  std::string utf8_decoded = UTF16ToUTF8(SpanFrom(decoded));
  EXPECT_EQ("Hello, 🌎.", utf8_decoded);
}

//
// EncodeBinary / CBORTokenTag::BINARY
//
TEST(EncodeDecodeBinaryTest, RoundtripsHelloWorld) {
  std::vector<uint8_t> binary = {'H', 'e', 'l', 'l', 'o', ',', ' ',
                                 'w', 'o', 'r', 'l', 'd', '.'};
  std::vector<uint8_t> encoded;
  EncodeBinary(span<uint8_t>(binary.data(), binary.size()), &encoded);
  // So, on the wire we see that the binary blob travels unmodified.
  EXPECT_THAT(
      encoded,
      ElementsAreArray(std::array<uint8_t, 15>{
          {(6 << 5 | 22),  // tag 22 indicating base64 interpretation in JSON
           (2 << 5 | 13),  // BYTE_STRING (type 2) of length 13
           'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '.'}}));
  std::vector<uint8_t> decoded;
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::BINARY, tokenizer.TokenTag());
  EXPECT_THAT(tokenizer.Status(), StatusIsOk());
  decoded = std::vector<uint8_t>(tokenizer.GetBinary().begin(),
                                 tokenizer.GetBinary().end());
  EXPECT_THAT(decoded, ElementsAreArray(binary));
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeBinaryTest, ErrorCases) {
  struct TestCase {
    std::vector<uint8_t> data;
    std::string msg;
  };
  std::vector<TestCase> tests{{TestCase{
      {6 << 5 | 22,  // tag 22 indicating base64 interpretation in JSON
       2 << 5 | 27,  // BYTE_STRING (type 2), followed by 8 bytes length
       0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      "large length pointing past the end of the test case"}}};
  for (const TestCase& test : tests) {
    SCOPED_TRACE(test.msg);
    CBORTokenizer tokenizer(SpanFrom(test.data));
    EXPECT_EQ(CBORTokenTag::ERROR_VALUE, tokenizer.TokenTag());
    EXPECT_THAT(tokenizer.Status(), StatusIs(Error::CBOR_INVALID_BINARY, 0u));
  }
}

//
// EncodeDouble / CBORTokenTag::DOUBLE
//
TEST(EncodeDecodeDoubleTest, RoundtripsWikipediaExample) {
  // https://en.wikipedia.org/wiki/Double-precision_floating-point_format
  // provides the example of a hex representation 3FD5 5555 5555 5555, which
  // approximates 1/3.

  const double kOriginalValue = 1.0 / 3;
  std::vector<uint8_t> encoded;
  EncodeDouble(kOriginalValue, &encoded);
  // first three bits: major type = 7; remaining five bits: additional info =
  // value 27. This is followed by 8 bytes of payload (which match Wikipedia).
  EXPECT_THAT(
      encoded,
      ElementsAreArray(std::array<uint8_t, 9>{
          {7 << 5 | 27, 0x3f, 0xd5, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55}}));

  // Reverse direction: decode and compare with original value.
  CBORTokenizer tokenizer(SpanFrom(encoded));
  EXPECT_EQ(CBORTokenTag::DOUBLE, tokenizer.TokenTag());
  EXPECT_THAT(tokenizer.GetDouble(), testing::DoubleEq(kOriginalValue));
  tokenizer.Next();
  EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

TEST(EncodeDecodeDoubleTest, RoundtripsAdditionalExamples) {
  std::vector<double> examples = {0.0,
                                  1.0,
                                  -1.0,
                                  3.1415,
                                  std::numeric_limits<double>::min(),
                                  std::numeric_limits<double>::max(),
                                  std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN()};
  for (double example : examples) {
    SCOPED_TRACE(std::string("example ") + std::to_string(example));
    std::vector<uint8_t> encoded;
    EncodeDouble(example, &encoded);
    CBORTokenizer tokenizer(SpanFrom(encoded));
    EXPECT_EQ(CBORTokenTag::DOUBLE, tokenizer.TokenTag());
    if (std::isnan(example))
      EXPECT_TRUE(std::isnan(tokenizer.GetDouble()));
    else
      EXPECT_THAT(tokenizer.GetDouble(), testing::DoubleEq(example));
    tokenizer.Next();
    EXPECT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
  }
}

TEST(EncodeDecodeEnvelopesTest, MessageWithNestingAndEnvelopeContentsAccess) {
  // This encodes and decodes the following message, which has some nesting
  // and therefore envelopes.
  //  { "inner": { "foo" : "bar" } }
  // The decoding is done with the Tokenizer,
  // and we test both ::GetEnvelopeContents and GetEnvelope here.
  std::vector<uint8_t> message;
  EnvelopeEncoder envelope;
  envelope.EncodeStart(&message);
  size_t pos_after_header = message.size();
  message.push_back(EncodeIndefiniteLengthMapStart());
  EncodeString8(SpanFrom("inner"), &message);
  size_t pos_inside_inner = message.size();
  EnvelopeEncoder inner_envelope;
  inner_envelope.EncodeStart(&message);
  size_t pos_inside_inner_contents = message.size();
  message.push_back(EncodeIndefiniteLengthMapStart());
  EncodeString8(SpanFrom("foo"), &message);
  EncodeString8(SpanFrom("bar"), &message);
  message.push_back(EncodeStop());
  size_t pos_after_inner = message.size();
  inner_envelope.EncodeStop(&message);
  message.push_back(EncodeStop());
  envelope.EncodeStop(&message);

  CBORTokenizer tokenizer(SpanFrom(message));
  ASSERT_EQ(CBORTokenTag::ENVELOPE, tokenizer.TokenTag());
  EXPECT_EQ(message.size(), tokenizer.GetEnvelope().size());
  EXPECT_EQ(message.data(), tokenizer.GetEnvelope().data());
  EXPECT_EQ(message.data() + pos_after_header,
            tokenizer.GetEnvelopeContents().data());
  EXPECT_EQ(message.size() - pos_after_header,
            tokenizer.GetEnvelopeContents().size());
  tokenizer.EnterEnvelope();
  ASSERT_EQ(CBORTokenTag::MAP_START, tokenizer.TokenTag());
  tokenizer.Next();
  ASSERT_EQ(CBORTokenTag::STRING8, tokenizer.TokenTag());
  EXPECT_EQ("inner", std::string(tokenizer.GetString8().begin(),
                                 tokenizer.GetString8().end()));
  tokenizer.Next();
  ASSERT_EQ(CBORTokenTag::ENVELOPE, tokenizer.TokenTag());
  EXPECT_EQ(message.data() + pos_inside_inner, tokenizer.GetEnvelope().data());
  EXPECT_EQ(pos_after_inner - pos_inside_inner, tokenizer.GetEnvelope().size());
  EXPECT_EQ(message.data() + pos_inside_inner_contents,
            tokenizer.GetEnvelopeContents().data());
  EXPECT_EQ(pos_after_inner - pos_inside_inner_contents,
            tokenizer.GetEnvelopeContents().size());
  tokenizer.EnterEnvelope();
  ASSERT_EQ(CBORTokenTag::MAP_START, tokenizer.TokenTag());
  tokenizer.Next();
  ASSERT_EQ(CBORTokenTag::STRING8, tokenizer.TokenTag());
  EXPECT_EQ("foo", std::string(tokenizer.GetString8().begin(),
                               tokenizer.GetString8().end()));
  tokenizer.Next();
  ASSERT_EQ(CBORTokenTag::STRING8, tokenizer.TokenTag());
  EXPECT_EQ("bar", std::string(tokenizer.GetString8().begin(),
                               tokenizer.GetString8().end()));
  tokenizer.Next();
  ASSERT_EQ(CBORTokenTag::STOP, tokenizer.TokenTag());
  tokenizer.Next();
  ASSERT_EQ(CBORTokenTag::STOP, tokenizer.TokenTag());
  tokenizer.Next();
  ASSERT_EQ(CBORTokenTag::DONE, tokenizer.TokenTag());
}

// =============================================================================
// cbor::NewCBOREncoder - for encoding from a streaming parser
// =============================================================================

TEST(JSONToCBOREncoderTest, SevenBitStrings) {
  // When a string can be represented as 7 bit ASCII, the encoder will use the
  // STRING (major Type 3) type, so the actual characters end up as bytes on the
  // wire.
  std::vector<uint8_t> encoded;
  Status status;
  std::unique_ptr<ParserHandler> encoder = NewCBOREncoder(&encoded, &status);
  std::vector<uint16_t> utf16 = {'f', 'o', 'o'};
  encoder->HandleString16(span<uint16_t>(utf16.data(), utf16.size()));
  EXPECT_THAT(status, StatusIsOk());
  // Here we assert that indeed, seven bit strings are represented as
  // bytes on the wire, "foo" is just "foo".
  EXPECT_THAT(encoded,
              ElementsAreArray(std::array<uint8_t, 4>{
                  {/*major type 3*/ 3 << 5 | /*length*/ 3, 'f', 'o', 'o'}}));
}

TEST(JsonCborRoundtrip, EncodingDecoding) {
  // Hits all the cases except binary and error in ParserHandler, first
  // parsing a JSON message into CBOR, then parsing it back from CBOR into JSON.
  std::string json =
      "{"
      "\"string\":\"Hello, \\ud83c\\udf0e.\","
      "\"double\":3.1415,"
      "\"int\":1,"
      "\"negative int\":-1,"
      "\"bool\":true,"
      "\"null\":null,"
      "\"array\":[1,2,3]"
      "}";
  std::vector<uint8_t> encoded;
  Status status;
  std::unique_ptr<ParserHandler> encoder = NewCBOREncoder(&encoded, &status);
  span<uint8_t> ascii_in = SpanFrom(json);
  json::ParseJSON(ascii_in, encoder.get());
  std::vector<uint8_t> expected = {
      0xd8, 0x18,         // envelope
      0x5a,               // byte string with 32 bit length
      0,    0,    0, 95,  // length is 95 bytes
  };
  expected.push_back(0xbf);  // indef length map start
  EncodeString8(SpanFrom("string"), &expected);
  // This is followed by the encoded string for "Hello, 🌎."
  // So, it's the same bytes that we tested above in
  // EncodeDecodeString16Test.RoundtripsHelloWorld.
  expected.push_back(/*major type=*/2 << 5 | /*additional info=*/20);
  for (uint8_t ch : std::array<uint8_t, 20>{
           {'H', 0, 'e', 0, 'l',  0,    'l',  0,    'o', 0,
            ',', 0, ' ', 0, 0x3c, 0xd8, 0x0e, 0xdf, '.', 0}})
    expected.push_back(ch);
  EncodeString8(SpanFrom("double"), &expected);
  EncodeDouble(3.1415, &expected);
  EncodeString8(SpanFrom("int"), &expected);
  EncodeInt32(1, &expected);
  EncodeString8(SpanFrom("negative int"), &expected);
  EncodeInt32(-1, &expected);
  EncodeString8(SpanFrom("bool"), &expected);
  expected.push_back(7 << 5 | 21);  // RFC 7049 Section 2.3, Table 2: true
  EncodeString8(SpanFrom("null"), &expected);
  expected.push_back(7 << 5 | 22);  // RFC 7049 Section 2.3, Table 2: null
  EncodeString8(SpanFrom("array"), &expected);
  expected.push_back(0xd8);  // envelope (tag first byte)
  expected.push_back(0x18);  // envelope (tag second byte)
  expected.push_back(0x5a);  // byte string with 32 bit length
  // the length is 5 bytes (that's up to end indef length array below).
  for (uint8_t ch : std::array<uint8_t, 4>{{0, 0, 0, 5}})
    expected.push_back(ch);
  expected.push_back(0x9f);  // RFC 7049 Section 2.2.1, indef length array start
  expected.push_back(1);     // Three UNSIGNED values (easy since Major Type 0)
  expected.push_back(2);
  expected.push_back(3);
  expected.push_back(0xff);  // End indef length array
  expected.push_back(0xff);  // End indef length map
  EXPECT_TRUE(status.ok());
  EXPECT_THAT(encoded, ElementsAreArray(expected));

  // And now we roundtrip, decoding the message we just encoded.
  std::string decoded;
  std::unique_ptr<ParserHandler> json_encoder =
      json::NewJSONEncoder(&decoded, &status);
  ParseCBOR(span<uint8_t>(encoded.data(), encoded.size()), json_encoder.get());
  EXPECT_THAT(status, StatusIsOk());
  EXPECT_EQ(json, decoded);
}

TEST(JsonCborRoundtrip, MoreRoundtripExamples) {
  std::vector<std::string> examples = {
      // Tests that after closing a nested objects, additional key/value pairs
      // are considered.
      "{\"foo\":{\"bar\":1},\"baz\":2}", "{\"foo\":[1,2,3],\"baz\":2}"};
  for (const std::string& json : examples) {
    SCOPED_TRACE(std::string("example: ") + json);
    std::vector<uint8_t> encoded;
    Status status;
    std::unique_ptr<ParserHandler> encoder = NewCBOREncoder(&encoded, &status);
    span<uint8_t> ascii_in = SpanFrom(json);
    json::ParseJSON(ascii_in, encoder.get());
    std::string decoded;
    std::unique_ptr<ParserHandler> json_writer =
        json::NewJSONEncoder(&decoded, &status);
    ParseCBOR(span<uint8_t>(encoded.data(), encoded.size()), json_writer.get());
    EXPECT_THAT(status, StatusIsOk());
    EXPECT_EQ(json, decoded);
  }
}

TEST(JSONToCBOREncoderTest, HelloWorldBinary_WithTripToJson) {
  // The ParserHandler::HandleBinary is a special case: The JSON parser
  // will never call this method, because JSON does not natively support the
  // binary type. So, we can't fully roundtrip. However, the other direction
  // works: binary will be rendered in JSON, as a base64 string. So, we make
  // calls to the encoder directly here, to construct a message, and one of
  // these calls is ::HandleBinary, to which we pass a "binary" string
  // containing "Hello, world.".
  std::vector<uint8_t> encoded;
  Status status;
  std::unique_ptr<ParserHandler> encoder = NewCBOREncoder(&encoded, &status);
  encoder->HandleMapBegin();
  // Emit a key.
  std::vector<uint16_t> key = {'f', 'o', 'o'};
  encoder->HandleString16(SpanFrom(key));
  // Emit the binary payload, an arbitrary array of bytes that happens to
  // be the ascii message "Hello, world.".
  encoder->HandleBinary(SpanFrom(std::vector<uint8_t>{
      'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '.'}));
  encoder->HandleMapEnd();
  EXPECT_THAT(status, StatusIsOk());

  // Now drive the json writer via the CBOR decoder.
  std::string decoded;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&decoded, &status);
  ParseCBOR(SpanFrom(encoded), json_writer.get());
  EXPECT_THAT(status, StatusIsOk());
  // "Hello, world." in base64 is "SGVsbG8sIHdvcmxkLg==".
  EXPECT_EQ("{\"foo\":\"SGVsbG8sIHdvcmxkLg==\"}", decoded);
}

// =============================================================================
// cbor::ParseCBOR - for receiving streaming parser events for CBOR messages
// =============================================================================

TEST(ParseCBORTest, ParseEmptyCBORMessage) {
  // An envelope starting with 0xd8, 0x5a, with the byte length
  // of 2, containing a map that's empty (0xbf for map
  // start, and 0xff for map end).
  std::vector<uint8_t> in = {0xd8, 0x5a, 0, 0, 0, 2, 0xbf, 0xff};
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(in.data(), in.size()), json_writer.get());
  EXPECT_THAT(status, StatusIsOk());
  EXPECT_EQ("{}", out);
}

TEST(ParseCBORTest, ParseCBORHelloWorld) {
  const uint8_t kPayloadLen = 27;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen};
  bytes.push_back(0xbf);                   // start indef length map.
  EncodeString8(SpanFrom("msg"), &bytes);  // key: msg
  // Now write the value, the familiar "Hello, 🌎." where the globe is expressed
  // as two utf16 chars.
  bytes.push_back(/*major type=*/2 << 5 | /*additional info=*/20);
  for (uint8_t ch : std::array<uint8_t, 20>{
           {'H', 0, 'e', 0, 'l',  0,    'l',  0,    'o', 0,
            ',', 0, ' ', 0, 0x3c, 0xd8, 0x0e, 0xdf, '.', 0}})
    bytes.push_back(ch);
  bytes.push_back(0xff);  // stop byte
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);

  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIsOk());
  EXPECT_EQ("{\"msg\":\"Hello, \\ud83c\\udf0e.\"}", out);
}

TEST(ParseCBORTest, UTF8IsSupportedInKeys) {
  const uint8_t kPayloadLen = 11;
  std::vector<uint8_t> bytes = {0xd8, 0x5a,  // envelope
                                0,    0,    0, kPayloadLen};
  bytes.push_back(cbor::EncodeIndefiniteLengthMapStart());
  // Two UTF16 chars.
  EncodeString8(SpanFrom("🌎"), &bytes);
  // Can be encoded as a single UTF16 char.
  EncodeString8(SpanFrom("☾"), &bytes);
  bytes.push_back(cbor::EncodeStop());
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);

  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIsOk());
  EXPECT_EQ("{\"\\ud83c\\udf0e\":\"\\u263e\"}", out);
}

TEST(ParseCBORTest, NoInputError) {
  std::vector<uint8_t> in = {};
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(in.data(), in.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_UNEXPECTED_EOF_IN_ENVELOPE, 0u));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, UnexpectedEofExpectedValueError) {
  constexpr uint8_t kPayloadLen = 5;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  // A key; so value would be next.
  EncodeString8(SpanFrom("key"), &bytes);
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_UNEXPECTED_EOF_EXPECTED_VALUE,
                               bytes.size()));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, UnexpectedEofInArrayError) {
  constexpr uint8_t kPayloadLen = 8;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};  // The byte for starting a map.
  // A key; so value would be next.
  EncodeString8(SpanFrom("array"), &bytes);
  bytes.push_back(0x9f);  // byte for indefinite length array start.
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status,
              StatusIs(Error::CBOR_UNEXPECTED_EOF_IN_ARRAY, bytes.size()));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, UnexpectedEofInMapError) {
  constexpr uint8_t kPayloadLen = 1;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};  // The byte for starting a map.
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_UNEXPECTED_EOF_IN_MAP, 7u));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, EnvelopeEncodingLegacy) {
  constexpr uint8_t kPayloadLen = 8;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen};  // envelope
  bytes.push_back(cbor::EncodeIndefiniteLengthMapStart());
  EncodeString8(SpanFrom("foo"), &bytes);
  EncodeInt32(42, &bytes);
  bytes.emplace_back(EncodeStop());
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIsOk());
  EXPECT_EQ(out, "{\"foo\":42}");
}

TEST(ParseCBORTest, EnvelopeEncodingBySpec) {
  constexpr uint8_t kPayloadLen = 8;
  std::vector<uint8_t> bytes = {0xd8, 0x18, 0x5a,       0,
                                0,    0,    kPayloadLen};  // envelope
  bytes.push_back(cbor::EncodeIndefiniteLengthMapStart());
  EncodeString8(SpanFrom("foo"), &bytes);
  EncodeInt32(42, &bytes);
  bytes.emplace_back(EncodeStop());
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIsOk());
  EXPECT_EQ(out, "{\"foo\":42}");
}

TEST(ParseCBORTest, NoEmptyEnvelopesAllowed) {
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, 0};  // envelope
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_MAP_OR_ARRAY_EXPECTED_IN_ENVELOPE,
                               bytes.size()));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, OnlyMapsAndArraysSupportedInsideEnvelopes) {
  // The top level is a map with key "foo", and the value
  // is an envelope that contains just a number (1). We don't
  // allow numbers to be contained in an envelope though, only
  // maps and arrays.
  constexpr uint8_t kPayloadLen = 8;
  std::vector<uint8_t> bytes = {0xd8,
                                0x5a,
                                0,
                                0,
                                0,
                                kPayloadLen,  // envelope
                                EncodeIndefiniteLengthMapStart()};
  EncodeString8(SpanFrom("foo"), &bytes);
  for (uint8_t byte : {0xd8, 0x5a, 0, 0, 0, /*payload_len*/ 1})
    bytes.emplace_back(byte);
  size_t error_pos = bytes.size();
  bytes.push_back(1);  // Envelope contents / payload = number 1.
  bytes.emplace_back(EncodeStop());

  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_MAP_OR_ARRAY_EXPECTED_IN_ENVELOPE,
                               error_pos));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, InvalidMapKeyError) {
  constexpr uint8_t kPayloadLen = 2;
  std::vector<uint8_t> bytes = {0xd8,       0x5a, 0,
                                0,          0,    kPayloadLen,  // envelope
                                0xbf,                           // map start
                                7 << 5 | 22};  // null (not a valid map key)
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_MAP_KEY, 7u));
  EXPECT_EQ("", out);
}

std::vector<uint8_t> MakeNestedCBOR(int depth) {
  std::vector<uint8_t> bytes;
  std::vector<EnvelopeEncoder> envelopes;
  for (int ii = 0; ii < depth; ++ii) {
    envelopes.emplace_back();
    envelopes.back().EncodeStart(&bytes);
    bytes.push_back(0xbf);  // indef length map start
    EncodeString8(SpanFrom("key"), &bytes);
  }
  EncodeString8(SpanFrom("innermost_value"), &bytes);
  for (int ii = 0; ii < depth; ++ii) {
    bytes.push_back(0xff);  // stop byte, finishes map.
    envelopes.back().EncodeStop(&bytes);
    envelopes.pop_back();
  }
  return bytes;
}

TEST(ParseCBORTest, StackLimitExceededError) {
  {  // Depth 3: no stack limit exceeded error and is easy to inspect.
    std::vector<uint8_t> bytes = MakeNestedCBOR(3);
    std::string out;
    Status status;
    std::unique_ptr<ParserHandler> json_writer =
        json::NewJSONEncoder(&out, &status);
    ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
    EXPECT_THAT(status, StatusIsOk());
    EXPECT_EQ("{\"key\":{\"key\":{\"key\":\"innermost_value\"}}}", out);
  }
  {  // Depth 300: no stack limit exceeded.
    std::vector<uint8_t> bytes = MakeNestedCBOR(300);
    std::string out;
    Status status;
    std::unique_ptr<ParserHandler> json_writer =
        json::NewJSONEncoder(&out, &status);
    ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
    EXPECT_THAT(status, StatusIsOk());
  }

  // We just want to know the length of one opening map so we can compute
  // where the error is encountered. So we look at a small example and find
  // the second envelope start.
  std::vector<uint8_t> small_example = MakeNestedCBOR(3);
  size_t opening_segment_size = 1;  // Start after the first envelope start.
  while (opening_segment_size < small_example.size() &&
         small_example[opening_segment_size] != 0xd8)
    opening_segment_size++;

  {  // Depth 301: limit exceeded.
    std::vector<uint8_t> bytes = MakeNestedCBOR(301);
    std::string out;
    Status status;
    std::unique_ptr<ParserHandler> json_writer =
        json::NewJSONEncoder(&out, &status);
    ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
    EXPECT_THAT(status, StatusIs(Error::CBOR_STACK_LIMIT_EXCEEDED,
                                 opening_segment_size * 301));
  }
  {  // Depth 320: still limit exceeded, and at the same pos as for 1001
    std::vector<uint8_t> bytes = MakeNestedCBOR(320);
    std::string out;
    Status status;
    std::unique_ptr<ParserHandler> json_writer =
        json::NewJSONEncoder(&out, &status);
    ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
    EXPECT_THAT(status, StatusIs(Error::CBOR_STACK_LIMIT_EXCEEDED,
                                 opening_segment_size * 301));
  }
}

TEST(ParseCBORTest, UnsupportedValueError) {
  constexpr uint8_t kPayloadLen = 6;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  EncodeString8(SpanFrom("key"), &bytes);
  size_t error_pos = bytes.size();
  bytes.push_back(6 << 5 | 5);  // tags aren't supported yet.
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);

  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_UNSUPPORTED_VALUE, error_pos));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, InvalidString16Error) {
  constexpr uint8_t kPayloadLen = 11;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  EncodeString8(SpanFrom("key"), &bytes);
  size_t error_pos = bytes.size();
  // a BYTE_STRING of length 5 as value; since we interpret these as string16,
  // it's going to be invalid as each character would need two bytes, but
  // 5 isn't divisible by 2.
  bytes.push_back(2 << 5 | 5);
  for (int ii = 0; ii < 5; ++ii)
    bytes.push_back(' ');
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_STRING16, error_pos));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, InvalidString8Error) {
  constexpr uint8_t kPayloadLen = 6;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  EncodeString8(SpanFrom("key"), &bytes);
  size_t error_pos = bytes.size();
  // a STRING of length 5 as value, but we're at the end of the bytes array
  // so it can't be decoded successfully.
  bytes.push_back(3 << 5 | 5);
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_STRING8, error_pos));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, InvalidBinaryError) {
  constexpr uint8_t kPayloadLen = 9;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  EncodeString8(SpanFrom("key"), &bytes);
  size_t error_pos = bytes.size();
  bytes.push_back(6 << 5 | 22);  // base64 hint for JSON; indicates binary
  bytes.push_back(2 << 5 | 10);  // BYTE_STRING (major type 2) of length 10
  // Just two garbage bytes, not enough for the binary.
  bytes.push_back(0x31);
  bytes.push_back(0x23);
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_BINARY, error_pos));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, InvalidDoubleError) {
  constexpr uint8_t kPayloadLen = 8;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  EncodeString8(SpanFrom("key"), &bytes);
  size_t error_pos = bytes.size();
  bytes.push_back(7 << 5 | 27);  // initial byte for double
  // Just two garbage bytes, not enough to represent an actual double.
  bytes.push_back(0x31);
  bytes.push_back(0x23);
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_DOUBLE, error_pos));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, InvalidSignedError) {
  constexpr uint8_t kPayloadLen = 14;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  EncodeString8(SpanFrom("key"), &bytes);
  size_t error_pos = bytes.size();
  // uint64_t max is a perfectly fine value to encode as CBOR unsigned,
  // but we don't support this since we only cover the int32_t range.
  internals::WriteTokenStart(MajorType::UNSIGNED,
                             std::numeric_limits<uint64_t>::max(), &bytes);
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_INT32, error_pos));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, TrailingJunk) {
  constexpr uint8_t kPayloadLen = 12;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  EncodeString8(SpanFrom("key"), &bytes);
  EncodeString8(SpanFrom("value"), &bytes);
  bytes.push_back(0xff);  // Up to here, it's a perfectly fine msg.
  ASSERT_EQ(kPayloadLen, bytes.size() - 6);
  size_t error_pos = bytes.size();
  // Now write some trailing junk after the message.
  EncodeString8(SpanFrom("trailing junk"), &bytes);
  internals::WriteTokenStart(MajorType::UNSIGNED,
                             std::numeric_limits<uint64_t>::max(), &bytes);
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_TRAILING_JUNK, error_pos));
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, EnvelopeContentsLengthMismatch) {
  constexpr uint8_t kPartialPayloadLen = 5;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0,
                                0,    0,    kPartialPayloadLen,  // envelope
                                0xbf};                           // map start
  EncodeString8(SpanFrom("key"), &bytes);
  // kPartialPayloadLen would need to indicate the length of the entire map,
  // all the way past the 0xff map stop character. Instead, it only covers
  // a portion of the map.
  EXPECT_EQ(bytes.size() - 6, kPartialPayloadLen);
  EncodeString8(SpanFrom("value"), &bytes);
  bytes.push_back(0xff);  // map stop

  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_THAT(status, StatusIs(Error::CBOR_ENVELOPE_CONTENTS_LENGTH_MISMATCH,
                               bytes.size()));
  EXPECT_EQ("", out);
}

// =============================================================================
// cbor::EnvelopeHeader - for parsing envelope headers
// =============================================================================
// Note most of converage for this is historically on a higher level of
// ParseCBOR(). This provides just a few essnetial scenarios for now.

template <typename T>
class EnvelopeHeaderTest : public ::testing::Test {};

TEST(EnvelopeHeaderTest, EnvelopeStartLegacy) {
  std::vector<uint8_t> bytes = {0xd8,             // Tag start
                                0x5a,             // Byte string, 4 bytes length
                                0,    0,   0, 2,  // Length
                                0xbf, 0xff};      // map start / map end
  auto result = EnvelopeHeader::Parse(SpanFrom(bytes));
  ASSERT_THAT(result.status(), StatusIsOk());
  EXPECT_THAT((*result).header_size(), Eq(6u));
  EXPECT_THAT((*result).content_size(), Eq(2u));
  EXPECT_THAT((*result).outer_size(), Eq(8u));
}

TEST(EnvelopeHeaderTest, EnvelopeStartSpecCompliant) {
  std::vector<uint8_t> bytes = {0xd8,             // Tag start
                                0x18,             // Tag type (CBOR)
                                0x5a,             // Byte string, 4 bytes length
                                0,    0,   0, 2,  // Length
                                0xbf, 0xff};      // map start / map end
  auto result = EnvelopeHeader::Parse(SpanFrom(bytes));
  ASSERT_THAT(result.status(), StatusIsOk());
  EXPECT_THAT((*result).header_size(), Eq(7u));
  EXPECT_THAT((*result).content_size(), Eq(2u));
  EXPECT_THAT((*result).outer_size(), Eq(9u));
}

TEST(EnvelopeHeaderTest, EnvelopeStartShortLen) {
  std::vector<uint8_t> bytes = {0xd8,         // Tag start
                                0x18,         // Tag type (CBOR)
                                0x58,         // Byte string, 1 byte length
                                2,            // Length
                                0xbf, 0xff};  // map start / map end
  auto result = EnvelopeHeader::Parse(SpanFrom(bytes));
  ASSERT_THAT(result.status(), StatusIsOk());
  EXPECT_THAT((*result).header_size(), Eq(4u));
  EXPECT_THAT((*result).content_size(), Eq(2u));
  EXPECT_THAT((*result).outer_size(), Eq(6u));
}

TEST(EnvelopeHeaderTest, ParseFragment) {
  std::vector<uint8_t> bytes = {0xd8,  // Tag start
                                0x18,  // Tag type (CBOR)
                                0x5a,  // Byte string, 4 bytes length
                                0,    0, 0, 20, 0xbf};  // map start
  auto result = EnvelopeHeader::ParseFromFragment(SpanFrom(bytes));
  ASSERT_THAT(result.status(), StatusIsOk());
  EXPECT_THAT((*result).header_size(), Eq(7u));
  EXPECT_THAT((*result).content_size(), Eq(20u));
  EXPECT_THAT((*result).outer_size(), Eq(27u));

  result = EnvelopeHeader::Parse(SpanFrom(bytes));
  ASSERT_THAT(result.status(),
              StatusIs(Error::CBOR_ENVELOPE_CONTENTS_LENGTH_MISMATCH, 8));
}

// =============================================================================
// cbor::AppendString8EntryToMap - for limited in-place editing of messages
// =============================================================================

template <typename T>
class AppendString8EntryToMapTest : public ::testing::Test {};

using ContainerTestTypes = ::testing::Types<std::vector<uint8_t>, std::string>;
TYPED_TEST_SUITE(AppendString8EntryToMapTest, ContainerTestTypes);

TEST(AppendString8EntryToMapTest, AppendsEntrySuccessfully) {
  constexpr uint8_t kPayloadLen = 12;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  size_t pos_before_payload = bytes.size() - 1;
  EncodeString8(SpanFrom("key"), &bytes);
  EncodeString8(SpanFrom("value"), &bytes);
  bytes.push_back(0xff);  // A perfectly fine cbor message.
  EXPECT_EQ(kPayloadLen, bytes.size() - pos_before_payload);

  std::vector<uint8_t> msg(bytes.begin(), bytes.end());

  Status status =
      AppendString8EntryToCBORMap(SpanFrom("foo"), SpanFrom("bar"), &msg);
  EXPECT_THAT(status, StatusIsOk());
  std::string out;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(SpanFrom(msg), json_writer.get());
  EXPECT_EQ("{\"key\":\"value\",\"foo\":\"bar\"}", out);
  EXPECT_THAT(status, StatusIsOk());
}

TYPED_TEST(AppendString8EntryToMapTest, AppendThreeEntries) {
  std::vector<uint8_t> encoded = {
      0xd8, 0x5a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), EncodeStop()};
  EXPECT_THAT(
      AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &encoded),
      StatusIsOk());
  EXPECT_THAT(AppendString8EntryToCBORMap(SpanFrom("key1"), SpanFrom("value1"),
                                          &encoded),
              StatusIsOk());
  EXPECT_THAT(AppendString8EntryToCBORMap(SpanFrom("key2"), SpanFrom("value2"),
                                          &encoded),
              StatusIsOk());
  TypeParam msg(encoded.begin(), encoded.end());
  std::string out;
  Status status;
  std::unique_ptr<ParserHandler> json_writer =
      json::NewJSONEncoder(&out, &status);
  ParseCBOR(SpanFrom(msg), json_writer.get());
  EXPECT_EQ("{\"key\":\"value\",\"key1\":\"value1\",\"key2\":\"value2\"}", out);
  EXPECT_THAT(status, StatusIsOk());
}

TEST(AppendString8EntryToMapTest, MapStartExpected_Error) {
  std::vector<uint8_t> msg = {
      0xd8, 0x5a, 0, 0, 0, 1, EncodeIndefiniteLengthArrayStart()};
  Status status =
      AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
  EXPECT_THAT(status, StatusIs(Error::CBOR_MAP_START_EXPECTED, 6u));
}

TEST(AppendString8EntryToMapTest, MapStopExpected_Error) {
  std::vector<uint8_t> msg = {
      0xd8, 0x5a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), 42};
  Status status =
      AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
  EXPECT_THAT(status, StatusIs(Error::CBOR_MAP_STOP_EXPECTED, 7u));
}

TEST(AppendString8EntryToMapTest, InvalidEnvelope_Error) {
  {  // Second byte is wrong.
    std::vector<uint8_t> msg = {
        0x5a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), EncodeStop(), 0};
    Status status =
        AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
    EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_ENVELOPE, 0u));
  }
  {  // Second byte is wrong.
    std::vector<uint8_t> msg = {
        0xd8, 0x7a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), EncodeStop()};
    Status status =
        AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
    EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_ENVELOPE, 1u));
  }
  {  // Invalid envelope size example.
    std::vector<uint8_t> msg = {
        0xd8, 0x5a, 0, 0, 0, 3, EncodeIndefiniteLengthMapStart(), EncodeStop(),
    };
    Status status =
        AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
    EXPECT_THAT(status,
                StatusIs(Error::CBOR_ENVELOPE_CONTENTS_LENGTH_MISMATCH, 8u));
  }
  {  // Invalid envelope size example.
    std::vector<uint8_t> msg = {
        0xd8, 0x5a, 0, 0, 0, 1, EncodeIndefiniteLengthMapStart(), EncodeStop(),
    };
    Status status =
        AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
    EXPECT_THAT(status, StatusIs(Error::CBOR_INVALID_ENVELOPE, 0));
  }
}
}  // namespace cbor
}  // namespace crdtp
