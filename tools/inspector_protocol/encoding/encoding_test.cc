// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoding.h"

#include <array>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "encoding_test_helper.h"

using testing::ElementsAreArray;

namespace v8_inspector_protocol_encoding {

class TestPlatform : public json::Platform {
  bool StrToD(const char* str, double* result) const override {
    // This is not thread-safe
    // (see https://en.cppreference.com/w/cpp/locale/setlocale)
    // but good enough for a unittest.
    const char* saved_locale = std::setlocale(LC_NUMERIC, nullptr);
    char* end;
    *result = std::strtod(str, &end);
    std::setlocale(LC_NUMERIC, saved_locale);
    if (errno == ERANGE) {
      // errno must be reset, e.g. see the example here:
      // https://en.cppreference.com/w/cpp/string/byte/strtof
      errno = 0;
      return false;
    }
    return end == str + strlen(str);
  }

  std::unique_ptr<char[]> DToStr(double value) const override {
    std::stringstream ss;
    ss.imbue(std::locale("C"));
    ss << value;
    std::string str = ss.str();
    std::unique_ptr<char[]> result(new char[str.size() + 1]);
    memcpy(result.get(), str.c_str(), str.size() + 1);
    return result;
  }
};

const json::Platform& GetTestPlatform() {
  static TestPlatform* platform = new TestPlatform;
  return *platform;
}

// =============================================================================
// span - sequence of bytes
// =============================================================================

template <typename T>
class SpanTest : public ::testing::Test {};

using TestTypes = ::testing::Types<uint8_t, uint16_t>;
TYPED_TEST_SUITE(SpanTest, TestTypes);

TYPED_TEST(SpanTest, Empty) {
  span<TypeParam> empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(0u, empty.size());
  EXPECT_EQ(0u, empty.size_bytes());
  EXPECT_EQ(empty.begin(), empty.end());
}

TYPED_TEST(SpanTest, SingleItem) {
  TypeParam single_item = 42;
  span<TypeParam> singular(&single_item, 1);
  EXPECT_FALSE(singular.empty());
  EXPECT_EQ(1u, singular.size());
  EXPECT_EQ(sizeof(TypeParam), singular.size_bytes());
  EXPECT_EQ(singular.begin() + 1, singular.end());
  EXPECT_EQ(42, singular[0]);
}

TYPED_TEST(SpanTest, FiveItems) {
  std::vector<TypeParam> test_input = {31, 32, 33, 34, 35};
  span<TypeParam> five_items(test_input.data(), 5);
  EXPECT_FALSE(five_items.empty());
  EXPECT_EQ(5u, five_items.size());
  EXPECT_EQ(sizeof(TypeParam) * 5, five_items.size_bytes());
  EXPECT_EQ(five_items.begin() + 5, five_items.end());
  EXPECT_EQ(31, five_items[0]);
  EXPECT_EQ(32, five_items[1]);
  EXPECT_EQ(33, five_items[2]);
  EXPECT_EQ(34, five_items[3]);
  EXPECT_EQ(35, five_items[4]);
  span<TypeParam> three_items = five_items.subspan(2);
  EXPECT_EQ(3u, three_items.size());
  EXPECT_EQ(33, three_items[0]);
  EXPECT_EQ(34, three_items[1]);
  EXPECT_EQ(35, three_items[2]);
  span<TypeParam> two_items = five_items.subspan(2, 2);
  EXPECT_EQ(2u, two_items.size());
  EXPECT_EQ(33, two_items[0]);
  EXPECT_EQ(34, two_items[1]);
}

TEST(SpanFromTest, FromConstCharAndLiteral) {
  // Testing this is useful because strlen(nullptr) is undefined.
  EXPECT_EQ(nullptr, SpanFrom(nullptr).data());
  EXPECT_EQ(0u, SpanFrom(nullptr).size());

  const char* kEmpty = "";
  EXPECT_EQ(kEmpty, reinterpret_cast<const char*>(SpanFrom(kEmpty).data()));
  EXPECT_EQ(0u, SpanFrom(kEmpty).size());

  const char* kFoo = "foo";
  EXPECT_EQ(kFoo, reinterpret_cast<const char*>(SpanFrom(kFoo).data()));
  EXPECT_EQ(3u, SpanFrom(kFoo).size());

  EXPECT_EQ(3u, SpanFrom("foo").size());
}

TEST(SpanComparisons, ByteWiseLexicographicalOrder) {
  // Compare the empty span.
  EXPECT_FALSE(SpanLessThan(span<uint8_t>(), span<uint8_t>()));
  EXPECT_TRUE(SpanEquals(span<uint8_t>(), span<uint8_t>()));

  // Compare message with itself.
  std::string msg = "Hello, world";
  EXPECT_FALSE(SpanLessThan(SpanFrom(msg), SpanFrom(msg)));
  EXPECT_TRUE(SpanEquals(SpanFrom(msg), SpanFrom(msg)));

  // Compare message and copy.
  EXPECT_FALSE(SpanLessThan(SpanFrom(msg), SpanFrom(std::string(msg))));
  EXPECT_TRUE(SpanEquals(SpanFrom(msg), SpanFrom(std::string(msg))));

  // Compare two messages. |lesser_msg| < |msg| because of the first
  // byte ('A' < 'H').
  std::string lesser_msg = "A lesser message.";
  EXPECT_TRUE(SpanLessThan(SpanFrom(lesser_msg), SpanFrom(msg)));
  EXPECT_FALSE(SpanLessThan(SpanFrom(msg), SpanFrom(lesser_msg)));
  EXPECT_FALSE(SpanEquals(SpanFrom(msg), SpanFrom(lesser_msg)));
}

// =============================================================================
// Status and Error codes
// =============================================================================

TEST(StatusTest, StatusToASCIIString) {
  Status ok_status;
  EXPECT_EQ("OK", ok_status.ToASCIIString());
  Status json_error(Error::JSON_PARSER_COLON_EXPECTED, 42);
  EXPECT_EQ("JSON: colon expected at position 42", json_error.ToASCIIString());
  Status cbor_error(Error::CBOR_TRAILING_JUNK, 21);
  EXPECT_EQ("CBOR: trailing junk at position 21", cbor_error.ToASCIIString());
}

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
  EXPECT_EQ(Error::CBOR_INVALID_INT32, tokenizer.Status().error);
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
    EXPECT_EQ(Error::CBOR_INVALID_INT32, tokenizer.Status().error);
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
    EXPECT_EQ(Error::CBOR_INVALID_STRING16, tokenizer.Status().error);
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
    EXPECT_EQ(Error::CBOR_INVALID_STRING8, tokenizer.Status().error);
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
  EXPECT_EQ(0, static_cast<int>(tokenizer.Status().error));
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
    EXPECT_EQ(Error::CBOR_INVALID_BINARY, tokenizer.Status().error);
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

// =============================================================================
// cbor::NewCBOREncoder - for encoding from a streaming parser
// =============================================================================

void EncodeUTF8ForTest(const std::string& key, std::vector<uint8_t>* out) {
  EncodeString8(SpanFrom(key), out);
}
TEST(JSONToCBOREncoderTest, SevenBitStrings) {
  // When a string can be represented as 7 bit ASCII, the encoder will use the
  // STRING (major Type 3) type, so the actual characters end up as bytes on the
  // wire.
  std::vector<uint8_t> encoded;
  Status status;
  std::unique_ptr<StreamingParserHandler> encoder =
      NewCBOREncoder(&encoded, &status);
  std::vector<uint16_t> utf16 = {'f', 'o', 'o'};
  encoder->HandleString16(span<uint16_t>(utf16.data(), utf16.size()));
  EXPECT_EQ(Error::OK, status.error);
  // Here we assert that indeed, seven bit strings are represented as
  // bytes on the wire, "foo" is just "foo".
  EXPECT_THAT(encoded,
              ElementsAreArray(std::array<uint8_t, 4>{
                  {/*major type 3*/ 3 << 5 | /*length*/ 3, 'f', 'o', 'o'}}));
}

TEST(JsonCborRoundtrip, EncodingDecoding) {
  // Hits all the cases except binary and error in StreamingParserHandler, first
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
  std::unique_ptr<StreamingParserHandler> encoder =
      NewCBOREncoder(&encoded, &status);
  span<uint8_t> ascii_in = SpanFrom(json);
  json::ParseJSON(GetTestPlatform(), ascii_in, encoder.get());
  std::vector<uint8_t> expected = {
      0xd8,            // envelope
      0x5a,            // byte string with 32 bit length
      0,    0, 0, 94,  // length is 94 bytes
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
  expected.push_back(0xd8);  // envelope
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
  std::unique_ptr<StreamingParserHandler> json_encoder =
      NewJSONEncoder(&GetTestPlatform(), &decoded, &status);
  ParseCBOR(span<uint8_t>(encoded.data(), encoded.size()), json_encoder.get());
  EXPECT_EQ(Error::OK, status.error);
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
    std::unique_ptr<StreamingParserHandler> encoder =
        NewCBOREncoder(&encoded, &status);
    span<uint8_t> ascii_in = SpanFrom(json);
    ParseJSON(GetTestPlatform(), ascii_in, encoder.get());
    std::string decoded;
    std::unique_ptr<StreamingParserHandler> json_writer =
        NewJSONEncoder(&GetTestPlatform(), &decoded, &status);
    ParseCBOR(span<uint8_t>(encoded.data(), encoded.size()), json_writer.get());
    EXPECT_EQ(Error::OK, status.error);
    EXPECT_EQ(json, decoded);
  }
}

TEST(JSONToCBOREncoderTest, HelloWorldBinary_WithTripToJson) {
  // The StreamingParserHandler::HandleBinary is a special case: The JSON parser
  // will never call this method, because JSON does not natively support the
  // binary type. So, we can't fully roundtrip. However, the other direction
  // works: binary will be rendered in JSON, as a base64 string. So, we make
  // calls to the encoder directly here, to construct a message, and one of
  // these calls is ::HandleBinary, to which we pass a "binary" string
  // containing "Hello, world.".
  std::vector<uint8_t> encoded;
  Status status;
  std::unique_ptr<StreamingParserHandler> encoder =
      NewCBOREncoder(&encoded, &status);
  encoder->HandleMapBegin();
  // Emit a key.
  std::vector<uint16_t> key = {'f', 'o', 'o'};
  encoder->HandleString16(SpanFrom(key));
  // Emit the binary payload, an arbitrary array of bytes that happens to
  // be the ascii message "Hello, world.".
  encoder->HandleBinary(SpanFrom(std::vector<uint8_t>{
      'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '.'}));
  encoder->HandleMapEnd();
  EXPECT_EQ(Error::OK, status.error);

  // Now drive the json writer via the CBOR decoder.
  std::string decoded;
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &decoded, &status);
  ParseCBOR(SpanFrom(encoded), json_writer.get());
  EXPECT_EQ(Error::OK, status.error);
  EXPECT_EQ(Status::npos(), status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(in.data(), in.size()), json_writer.get());
  EXPECT_EQ(Error::OK, status.error);
  EXPECT_EQ("{}", out);
}

TEST(ParseCBORTest, ParseCBORHelloWorld) {
  const uint8_t kPayloadLen = 27;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen};
  bytes.push_back(0xbf);                            // start indef length map.
  EncodeString8(SpanFrom("msg"), &bytes);           // key: msg
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::OK, status.error);
  EXPECT_EQ("{\"msg\":\"Hello, \\ud83c\\udf0e.\"}", out);
}

TEST(ParseCBORTest, UTF8IsSupportedInKeys) {
  const uint8_t kPayloadLen = 11;
  std::vector<uint8_t> bytes = {cbor::InitialByteForEnvelope(),
                                cbor::InitialByteFor32BitLengthByteString(),
                                0,
                                0,
                                0,
                                kPayloadLen};
  bytes.push_back(cbor::EncodeIndefiniteLengthMapStart());
  // Two UTF16 chars.
  EncodeString8(SpanFrom("🌎"), &bytes);
  // Can be encoded as a single UTF16 char.
  EncodeString8(SpanFrom("☾"), &bytes);
  bytes.push_back(cbor::EncodeStop());
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);

  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::OK, status.error);
  EXPECT_EQ("{\"\\ud83c\\udf0e\":\"\\u263e\"}", out);
}

TEST(ParseCBORTest, NoInputError) {
  std::vector<uint8_t> in = {};
  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(in.data(), in.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_NO_INPUT, status.error);
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, InvalidStartByteError) {
  // Here we test that some actual json, which usually starts with {,
  // is not considered CBOR. CBOR messages must start with 0x5a, the
  // envelope start byte.
  std::string json = "{\"msg\": \"Hello, world.\"}";
  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(SpanFrom(json), json_writer.get());
  EXPECT_EQ(Error::CBOR_INVALID_START_BYTE, status.error);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_UNEXPECTED_EOF_EXPECTED_VALUE, status.error);
  EXPECT_EQ(bytes.size(), status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_UNEXPECTED_EOF_IN_ARRAY, status.error);
  EXPECT_EQ(bytes.size(), status.pos);
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, UnexpectedEofInMapError) {
  constexpr uint8_t kPayloadLen = 1;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};  // The byte for starting a map.
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_UNEXPECTED_EOF_IN_MAP, status.error);
  EXPECT_EQ(7u, status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_INVALID_MAP_KEY, status.error);
  EXPECT_EQ(7u, status.pos);
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
    std::unique_ptr<StreamingParserHandler> json_writer =
        NewJSONEncoder(&GetTestPlatform(), &out, &status);
    ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
    EXPECT_EQ(Error::OK, status.error);
    EXPECT_EQ(Status::npos(), status.pos);
    EXPECT_EQ("{\"key\":{\"key\":{\"key\":\"innermost_value\"}}}", out);
  }
  {  // Depth 300: no stack limit exceeded.
    std::vector<uint8_t> bytes = MakeNestedCBOR(300);
    std::string out;
    Status status;
    std::unique_ptr<StreamingParserHandler> json_writer =
        NewJSONEncoder(&GetTestPlatform(), &out, &status);
    ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
    EXPECT_EQ(Error::OK, status.error);
    EXPECT_EQ(Status::npos(), status.pos);
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
    std::unique_ptr<StreamingParserHandler> json_writer =
        NewJSONEncoder(&GetTestPlatform(), &out, &status);
    ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
    EXPECT_EQ(Error::CBOR_STACK_LIMIT_EXCEEDED, status.error);
    EXPECT_EQ(opening_segment_size * 301, status.pos);
  }
  {  // Depth 320: still limit exceeded, and at the same pos as for 1001
    std::vector<uint8_t> bytes = MakeNestedCBOR(320);
    std::string out;
    Status status;
    std::unique_ptr<StreamingParserHandler> json_writer =
        NewJSONEncoder(&GetTestPlatform(), &out, &status);
    ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
    EXPECT_EQ(Error::CBOR_STACK_LIMIT_EXCEEDED, status.error);
    EXPECT_EQ(opening_segment_size * 301, status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_UNSUPPORTED_VALUE, status.error);
  EXPECT_EQ(error_pos, status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_INVALID_STRING16, status.error);
  EXPECT_EQ(error_pos, status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_INVALID_STRING8, status.error);
  EXPECT_EQ(error_pos, status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_INVALID_BINARY, status.error);
  EXPECT_EQ(error_pos, status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_INVALID_DOUBLE, status.error);
  EXPECT_EQ(error_pos, status.pos);
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
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_INVALID_INT32, status.error);
  EXPECT_EQ(error_pos, status.pos);
  EXPECT_EQ("", out);
}

TEST(ParseCBORTest, TrailingJunk) {
  constexpr uint8_t kPayloadLen = 35;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  EncodeString8(SpanFrom("key"), &bytes);
  EncodeString8(SpanFrom("value"), &bytes);
  bytes.push_back(0xff);  // Up to here, it's a perfectly fine msg.
  size_t error_pos = bytes.size();
  EncodeString8(SpanFrom("trailing junk"), &bytes);

  internals::WriteTokenStart(MajorType::UNSIGNED,
                             std::numeric_limits<uint64_t>::max(), &bytes);
  EXPECT_EQ(kPayloadLen, bytes.size() - 6);
  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(span<uint8_t>(bytes.data(), bytes.size()), json_writer.get());
  EXPECT_EQ(Error::CBOR_TRAILING_JUNK, status.error);
  EXPECT_EQ(error_pos, status.pos);
  EXPECT_EQ("", out);
}

// =============================================================================
// cbor::AppendString8EntryToMap - for limited in-place editing of messages
// =============================================================================

template <typename T>
class AppendString8EntryToMapTest : public ::testing::Test {};

using ContainerTestTypes = ::testing::Types<std::vector<uint8_t>, std::string>;
TYPED_TEST_SUITE(AppendString8EntryToMapTest, ContainerTestTypes);

TYPED_TEST(AppendString8EntryToMapTest, AppendsEntrySuccessfully) {
  constexpr uint8_t kPayloadLen = 12;
  std::vector<uint8_t> bytes = {0xd8, 0x5a, 0, 0, 0, kPayloadLen,  // envelope
                                0xbf};                             // map start
  size_t pos_before_payload = bytes.size() - 1;
  EncodeString8(SpanFrom("key"), &bytes);
  EncodeString8(SpanFrom("value"), &bytes);
  bytes.push_back(0xff);  // A perfectly fine cbor message.
  EXPECT_EQ(kPayloadLen, bytes.size() - pos_before_payload);

  TypeParam msg(bytes.begin(), bytes.end());

  Status status =
      AppendString8EntryToCBORMap(SpanFrom("foo"), SpanFrom("bar"), &msg);
  EXPECT_EQ(Error::OK, status.error);
  EXPECT_EQ(Status::npos(), status.pos);
  std::string out;
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(SpanFrom(msg), json_writer.get());
  EXPECT_EQ("{\"key\":\"value\",\"foo\":\"bar\"}", out);
  EXPECT_EQ(Error::OK, status.error);
  EXPECT_EQ(Status::npos(), status.pos);
}

TYPED_TEST(AppendString8EntryToMapTest, AppendThreeEntries) {
  std::vector<uint8_t> encoded = {
      0xd8, 0x5a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), EncodeStop()};
  EXPECT_EQ(Error::OK, AppendString8EntryToCBORMap(SpanFrom("key"),
                                                   SpanFrom("value"), &encoded)
                           .error);
  EXPECT_EQ(Error::OK, AppendString8EntryToCBORMap(SpanFrom("key1"),
                                                   SpanFrom("value1"), &encoded)
                           .error);
  EXPECT_EQ(Error::OK, AppendString8EntryToCBORMap(SpanFrom("key2"),
                                                   SpanFrom("value2"), &encoded)
                           .error);
  TypeParam msg(encoded.begin(), encoded.end());
  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> json_writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  ParseCBOR(SpanFrom(msg), json_writer.get());
  EXPECT_EQ("{\"key\":\"value\",\"key1\":\"value1\",\"key2\":\"value2\"}", out);
  EXPECT_EQ(Error::OK, status.error);
  EXPECT_EQ(Status::npos(), status.pos);
}

TYPED_TEST(AppendString8EntryToMapTest, MapStartExpected_Error) {
  std::vector<uint8_t> bytes = {
      0xd8, 0x5a, 0, 0, 0, 1, EncodeIndefiniteLengthArrayStart()};
  TypeParam msg(bytes.begin(), bytes.end());
  Status status =
      AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
  EXPECT_EQ(Error::CBOR_MAP_START_EXPECTED, status.error);
  EXPECT_EQ(6u, status.pos);
}

TYPED_TEST(AppendString8EntryToMapTest, MapStopExpected_Error) {
  std::vector<uint8_t> bytes = {
      0xd8, 0x5a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), 42};
  TypeParam msg(bytes.begin(), bytes.end());
  Status status =
      AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
  EXPECT_EQ(Error::CBOR_MAP_STOP_EXPECTED, status.error);
  EXPECT_EQ(7u, status.pos);
}

TYPED_TEST(AppendString8EntryToMapTest, InvalidEnvelope_Error) {
  {  // Second byte is wrong.
    std::vector<uint8_t> bytes = {
        0x5a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), EncodeStop(), 0};
    TypeParam msg(bytes.begin(), bytes.end());
    Status status =
        AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
    EXPECT_EQ(Error::CBOR_INVALID_ENVELOPE, status.error);
    EXPECT_EQ(0u, status.pos);
  }
  {  // Second byte is wrong.
    std::vector<uint8_t> bytes = {
        0xd8, 0x7a, 0, 0, 0, 2, EncodeIndefiniteLengthMapStart(), EncodeStop()};
    TypeParam msg(bytes.begin(), bytes.end());
    Status status =
        AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
    EXPECT_EQ(Error::CBOR_INVALID_ENVELOPE, status.error);
    EXPECT_EQ(0u, status.pos);
  }
  {  // Invalid envelope size example.
    std::vector<uint8_t> bytes = {
        0xd8, 0x5a, 0, 0, 0, 3, EncodeIndefiniteLengthMapStart(), EncodeStop(),
    };
    TypeParam msg(bytes.begin(), bytes.end());
    Status status =
        AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
    EXPECT_EQ(Error::CBOR_INVALID_ENVELOPE, status.error);
    EXPECT_EQ(0u, status.pos);
  }
  {  // Invalid envelope size example.
    std::vector<uint8_t> bytes = {
        0xd8, 0x5a, 0, 0, 0, 1, EncodeIndefiniteLengthMapStart(), EncodeStop(),
    };
    TypeParam msg(bytes.begin(), bytes.end());
    Status status =
        AppendString8EntryToCBORMap(SpanFrom("key"), SpanFrom("value"), &msg);
    EXPECT_EQ(Error::CBOR_INVALID_ENVELOPE, status.error);
    EXPECT_EQ(0u, status.pos);
  }
}
}  // namespace cbor

namespace json {

// =============================================================================
// json::NewJSONEncoder - for encoding streaming parser events as JSON
// =============================================================================

void WriteUTF8AsUTF16(StreamingParserHandler* writer, const std::string& utf8) {
  writer->HandleString16(SpanFrom(UTF8ToUTF16(SpanFrom(utf8))));
}

TEST(JsonEncoder, OverlongEncodings) {
  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);

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

TEST(JsonStdStringWriterTest, HelloWorld) {
  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
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

TEST(JsonStdStringWriterTest, RepresentingNonFiniteValuesAsNull) {
  // JSON can't represent +Infinity, -Infinity, or NaN.
  // So in practice it's mapped to null.
  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
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
  // The encoder emits binary submitted to StreamingParserHandler::HandleBinary
  // as base64. The following three examples are taken from
  // https://en.wikipedia.org/wiki/Base64.
  {
    std::string out;
    Status status;
    std::unique_ptr<StreamingParserHandler> writer =
        NewJSONEncoder(&GetTestPlatform(), &out, &status);
    writer->HandleBinary(SpanFrom(std::vector<uint8_t>({'M', 'a', 'n'})));
    EXPECT_TRUE(status.ok());
    EXPECT_EQ("\"TWFu\"", out);
  }
  {
    std::string out;
    Status status;
    std::unique_ptr<StreamingParserHandler> writer =
        NewJSONEncoder(&GetTestPlatform(), &out, &status);
    writer->HandleBinary(SpanFrom(std::vector<uint8_t>({'M', 'a'})));
    EXPECT_TRUE(status.ok());
    EXPECT_EQ("\"TWE=\"", out);
  }
  {
    std::string out;
    Status status;
    std::unique_ptr<StreamingParserHandler> writer =
        NewJSONEncoder(&GetTestPlatform(), &out, &status);
    writer->HandleBinary(SpanFrom(std::vector<uint8_t>({'M'})));
    EXPECT_TRUE(status.ok());
    EXPECT_EQ("\"TQ==\"", out);
  }
  {  // "Hello, world.", verified with base64decode.org.
    std::string out;
    Status status;
    std::unique_ptr<StreamingParserHandler> writer =
        NewJSONEncoder(&GetTestPlatform(), &out, &status);
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
  std::unique_ptr<StreamingParserHandler> writer =
      NewJSONEncoder(&GetTestPlatform(), &out, &status);
  writer->HandleMapBegin();
  WriteUTF8AsUTF16(writer.get(), "msg1");
  writer->HandleError(Status{Error::JSON_PARSER_VALUE_EXPECTED, 42});
  EXPECT_EQ(Error::JSON_PARSER_VALUE_EXPECTED, status.error);
  EXPECT_EQ(42u, status.pos);
  EXPECT_EQ("", out);
}

// We'd use Gmock but unfortunately it only handles copyable return types.
class MockPlatform : public Platform {
 public:
  // Not implemented.
  bool StrToD(const char* str, double* result) const override { return false; }

  // A map with pre-registered responses for DToSTr.
  std::map<double, std::string> dtostr_responses_;

  std::unique_ptr<char[]> DToStr(double value) const override {
    auto it = dtostr_responses_.find(value);
    CHECK(it != dtostr_responses_.end());
    const std::string& str = it->second;
    std::unique_ptr<char[]> response(new char[str.size() + 1]);
    memcpy(response.get(), str.c_str(), str.size() + 1);
    return response;
  }
};

TEST(JsonStdStringWriterTest, DoubleToString) {
  // This "broken" platform responds without the leading 0 before the
  // decimal dot, so it'd be invalid JSON.
  MockPlatform platform;
  platform.dtostr_responses_[.1] = ".1";
  platform.dtostr_responses_[-.7] = "-.7";

  std::string out;
  Status status;
  std::unique_ptr<StreamingParserHandler> writer =
      NewJSONEncoder(&platform, &out, &status);
  writer->HandleArrayBegin();
  writer->HandleDouble(.1);
  writer->HandleDouble(-.7);
  writer->HandleArrayEnd();
  EXPECT_EQ("[0.1,-0.7]", out);
}

// =============================================================================
// json::ParseJSON - for receiving streaming parser events for JSON
// =============================================================================

class Log : public StreamingParserHandler {
 public:
  void HandleMapBegin() override { log_ << "map begin\n"; }

  void HandleMapEnd() override { log_ << "map end\n"; }

  void HandleArrayBegin() override { log_ << "array begin\n"; }

  void HandleArrayEnd() override { log_ << "array end\n"; }

  void HandleString8(span<uint8_t> chars) override {
    log_ << "string8: " << std::string(chars.begin(), chars.end()) << "\n";
  }

  void HandleString16(span<uint16_t> chars) override {
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

  Status status() const { return status_; }

 private:
  std::ostringstream log_;
  Status status_;
};

class JsonParserTest : public ::testing::Test {
 protected:
  Log log_;
};

TEST_F(JsonParserTest, SimpleDictionary) {
  std::string json = "{\"foo\": 42}";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
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
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
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
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
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
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
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
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
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
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
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
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_TRUE(log_.status().ok());
  EXPECT_EQ(
      "map begin\n"
      "string16: space\n"
      "string16: 🌎 🌙.\n"
      "map end\n",
      log_.str());
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
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
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
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_UNPROCESSED_INPUT_REMAINS, log_.status().error);
  EXPECT_EQ(junk_idx, log_.status().pos);
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
  ParseJSON(GetTestPlatform(), SpanFrom(json_3), &log_);
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
  ParseJSON(GetTestPlatform(),
            span<uint8_t>(reinterpret_cast<const uint8_t*>(json_limit.data()),
                          json_limit.size()),
            &log_);
  EXPECT_TRUE(log_.status().ok());
}

TEST_F(JsonParserTest, StackLimitExceededError_AboveLimit) {
  // Now with kStackLimit + 1 (301) - it exceeds in the innermost instance.
  std::string exceeded = MakeNestedJson(301);
  ParseJSON(GetTestPlatform(), SpanFrom(exceeded), &log_);
  EXPECT_EQ(Error::JSON_PARSER_STACK_LIMIT_EXCEEDED, log_.status().error);
  EXPECT_EQ(strlen("{\"foo\":") * 301, log_.status().pos);
}

TEST_F(JsonParserTest, StackLimitExceededError_WayAboveLimit) {
  // Now way past the limit. Still, the point of exceeding is 301.
  std::string far_out = MakeNestedJson(320);
  ParseJSON(GetTestPlatform(), SpanFrom(far_out), &log_);
  EXPECT_EQ(Error::JSON_PARSER_STACK_LIMIT_EXCEEDED, log_.status().error);
  EXPECT_EQ(strlen("{\"foo\":") * 301, log_.status().pos);
}

TEST_F(JsonParserTest, NoInputError) {
  std::string json = "";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_NO_INPUT, log_.status().error);
  EXPECT_EQ(0u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, InvalidTokenError) {
  std::string json = "|";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_INVALID_TOKEN, log_.status().error);
  EXPECT_EQ(0u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, InvalidNumberError) {
  // Mantissa exceeds max (the constant used here is int64_t max).
  std::string json = "1E9223372036854775807";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_INVALID_NUMBER, log_.status().error);
  EXPECT_EQ(0u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, InvalidStringError) {
  // \x22 is an unsupported escape sequence
  std::string json = "\"foo\\x22\"";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_INVALID_STRING, log_.status().error);
  EXPECT_EQ(0u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, UnexpectedArrayEndError) {
  std::string json = "[1,2,]";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_UNEXPECTED_ARRAY_END, log_.status().error);
  EXPECT_EQ(5u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, CommaOrArrayEndExpectedError) {
  std::string json = "[1,2 2";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_COMMA_OR_ARRAY_END_EXPECTED,
            log_.status().error);
  EXPECT_EQ(5u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, StringLiteralExpectedError) {
  // There's an error because the key bar, a string, is not terminated.
  std::string json = "{\"foo\": 3.1415, \"bar: 31415e-4}";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_STRING_LITERAL_EXPECTED, log_.status().error);
  EXPECT_EQ(16u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, ColonExpectedError) {
  std::string json = "{\"foo\", 42}";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_COLON_EXPECTED, log_.status().error);
  EXPECT_EQ(6u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, UnexpectedMapEndError) {
  std::string json = "{\"foo\": 42, }";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_UNEXPECTED_MAP_END, log_.status().error);
  EXPECT_EQ(12u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, CommaOrMapEndExpectedError) {
  // The second separator should be a comma.
  std::string json = "{\"foo\": 3.1415: \"bar\": 0}";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_COMMA_OR_MAP_END_EXPECTED, log_.status().error);
  EXPECT_EQ(14u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

TEST_F(JsonParserTest, ValueExpectedError) {
  std::string json = "}";
  ParseJSON(GetTestPlatform(), SpanFrom(json), &log_);
  EXPECT_EQ(Error::JSON_PARSER_VALUE_EXPECTED, log_.status().error);
  EXPECT_EQ(0u, log_.status().pos);
  EXPECT_EQ("", log_.str());
}

template <typename T>
class ConvertJSONToCBORTest : public ::testing::Test {};

using ContainerTestTypes = ::testing::Types<std::vector<uint8_t>, std::string>;
TYPED_TEST_SUITE(ConvertJSONToCBORTest, ContainerTestTypes);

TYPED_TEST(ConvertJSONToCBORTest, RoundTripValidJson) {
  std::string json_in = "{\"msg\":\"Hello, world.\",\"lst\":[1,2,3]}";
  TypeParam json(json_in.begin(), json_in.end());
  TypeParam cbor;
  {
    Status status = ConvertJSONToCBOR(GetTestPlatform(), SpanFrom(json), &cbor);
    EXPECT_EQ(Error::OK, status.error);
    EXPECT_EQ(Status::npos(), status.pos);
  }
  TypeParam roundtrip_json;
  {
    Status status =
        ConvertCBORToJSON(GetTestPlatform(), SpanFrom(cbor), &roundtrip_json);
    EXPECT_EQ(Error::OK, status.error);
    EXPECT_EQ(Status::npos(), status.pos);
  }
  EXPECT_EQ(json, roundtrip_json);
}

TYPED_TEST(ConvertJSONToCBORTest, RoundTripValidJson16) {
  std::vector<uint16_t> json16 = {
      '{', '"', 'm', 's',    'g',    '"', ':', '"', 'H', 'e', 'l', 'l',
      'o', ',', ' ', 0xd83c, 0xdf0e, '.', '"', ',', '"', 'l', 's', 't',
      '"', ':', '[', '1',    ',',    '2', ',', '3', ']', '}'};
  TypeParam cbor;
  {
    Status status = ConvertJSONToCBOR(
        GetTestPlatform(), span<uint16_t>(json16.data(), json16.size()), &cbor);
    EXPECT_EQ(Error::OK, status.error);
    EXPECT_EQ(Status::npos(), status.pos);
  }
  TypeParam roundtrip_json;
  {
    Status status =
        ConvertCBORToJSON(GetTestPlatform(), SpanFrom(cbor), &roundtrip_json);
    EXPECT_EQ(Error::OK, status.error);
    EXPECT_EQ(Status::npos(), status.pos);
  }
  std::string json = "{\"msg\":\"Hello, \\ud83c\\udf0e.\",\"lst\":[1,2,3]}";
  TypeParam expected_json(json.begin(), json.end());
  EXPECT_EQ(expected_json, roundtrip_json);
}
}  // namespace json
}  // namespace v8_inspector_protocol_encoding
