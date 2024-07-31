// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/internal/charconv_parse.h"

#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"

using absl::chars_format;
using absl::strings_internal::FloatType;
using absl::strings_internal::ParsedFloat;
using absl::strings_internal::ParseFloat;

namespace {

// Check that a given string input is parsed to the expected mantissa and
// exponent.
//
// Input string `s` must contain a '$' character.  It marks the end of the
// characters that should be consumed by the match.  It is stripped from the
// input to ParseFloat.
//
// If input string `s` contains '[' and ']' characters, these mark the region
// of characters that should be marked as the "subrange".  For NaNs, this is
// the location of the extended NaN string.  For numbers, this is the location
// of the full, over-large mantissa.
template <int base>
void ExpectParsedFloat(std::string s, absl::chars_format format_flags,
                       FloatType expected_type, uint64_t expected_mantissa,
                       int expected_exponent,
                       int expected_literal_exponent = -999) {
  SCOPED_TRACE(s);

  int begin_subrange = -1;
  int end_subrange = -1;
  // If s contains '[' and ']', then strip these characters and set the subrange
  // indices appropriately.
  std::string::size_type open_bracket_pos = s.find('[');
  if (open_bracket_pos != std::string::npos) {
    begin_subrange = static_cast<int>(open_bracket_pos);
    s.replace(open_bracket_pos, 1, "");
    std::string::size_type close_bracket_pos = s.find(']');
    CHECK_NE(close_bracket_pos, absl::string_view::npos)
        << "Test input contains [ without matching ]";
    end_subrange = static_cast<int>(close_bracket_pos);
    s.replace(close_bracket_pos, 1, "");
  }
  const std::string::size_type expected_characters_matched = s.find('$');
  CHECK_NE(expected_characters_matched, std::string::npos)
      << "Input string must contain $";
  s.replace(expected_characters_matched, 1, "");

  ParsedFloat parsed =
      ParseFloat<base>(s.data(), s.data() + s.size(), format_flags);

  EXPECT_NE(parsed.end, nullptr);
  if (parsed.end == nullptr) {
    return;  // The following tests are not useful if we fully failed to parse
  }
  EXPECT_EQ(parsed.type, expected_type);
  if (begin_subrange == -1) {
    EXPECT_EQ(parsed.subrange_begin, nullptr);
    EXPECT_EQ(parsed.subrange_end, nullptr);
  } else {
    EXPECT_EQ(parsed.subrange_begin, s.data() + begin_subrange);
    EXPECT_EQ(parsed.subrange_end, s.data() + end_subrange);
  }
  if (parsed.type == FloatType::kNumber) {
    EXPECT_EQ(parsed.mantissa, expected_mantissa);
    EXPECT_EQ(parsed.exponent, expected_exponent);
    if (expected_literal_exponent != -999) {
      EXPECT_EQ(parsed.literal_exponent, expected_literal_exponent);
    }
  }
  auto characters_matched = static_cast<int>(parsed.end - s.data());
  EXPECT_EQ(characters_matched, expected_characters_matched);
}

// Check that a given string input is parsed to the expected mantissa and
// exponent.
//
// Input string `s` must contain a '$' character.  It marks the end of the
// characters that were consumed by the match.
template <int base>
void ExpectNumber(std::string s, absl::chars_format format_flags,
                  uint64_t expected_mantissa, int expected_exponent,
                  int expected_literal_exponent = -999) {
  ExpectParsedFloat<base>(std::move(s), format_flags, FloatType::kNumber,
                          expected_mantissa, expected_exponent,
                          expected_literal_exponent);
}

// Check that a given string input is parsed to the given special value.
//
// This tests against both number bases, since infinities and NaNs have
// identical representations in both modes.
void ExpectSpecial(const std::string& s, absl::chars_format format_flags,
                   FloatType type) {
  ExpectParsedFloat<10>(s, format_flags, type, 0, 0);
  ExpectParsedFloat<16>(s, format_flags, type, 0, 0);
}

// Check that a given input string is not matched by Float.
template <int base>
void ExpectFailedParse(absl::string_view s, absl::chars_format format_flags) {
  ParsedFloat parsed =
      ParseFloat<base>(s.data(), s.data() + s.size(), format_flags);
  EXPECT_EQ(parsed.end, nullptr);
}

TEST(ParseFloat, SimpleValue) {
  // Test that various forms of floating point numbers all parse correctly.
  ExpectNumber<10>("1.23456789e5$", chars_format::general, 123456789, -3);
  ExpectNumber<10>("1.23456789e+5$", chars_format::general, 123456789, -3);
  ExpectNumber<10>("1.23456789E5$", chars_format::general, 123456789, -3);
  ExpectNumber<10>("1.23456789e05$", chars_format::general, 123456789, -3);
  ExpectNumber<10>("123.456789e3$", chars_format::general, 123456789, -3);
  ExpectNumber<10>("0.000123456789e9$", chars_format::general, 123456789, -3);
  ExpectNumber<10>("123456.789$", chars_format::general, 123456789, -3);
  ExpectNumber<10>("123456789e-3$", chars_format::general, 123456789, -3);

  ExpectNumber<16>("1.234abcdefp28$", chars_format::general, 0x1234abcdef, -8);
  ExpectNumber<16>("1.234abcdefp+28$", chars_format::general, 0x1234abcdef, -8);
  ExpectNumber<16>("1.234ABCDEFp28$", chars_format::general, 0x1234abcdef, -8);
  ExpectNumber<16>("1.234AbCdEfP0028$", chars_format::general, 0x1234abcdef,
                   -8);
  ExpectNumber<16>("123.4abcdefp20$", chars_format::general, 0x1234abcdef, -8);
  ExpectNumber<16>("0.0001234abcdefp44$", chars_format::general, 0x1234abcdef,
                   -8);
  ExpectNumber<16>("1234abcd.ef$", chars_format::general, 0x1234abcdef, -8);
  ExpectNumber<16>("1234abcdefp-8$", chars_format::general, 0x1234abcdef, -8);

  // ExpectNumber does not attempt to drop trailing zeroes.
  ExpectNumber<10>("0001.2345678900e005$", chars_format::general, 12345678900,
                   -5);
  ExpectNumber<16>("0001.234abcdef000p28$", chars_format::general,
                   0x1234abcdef000, -20);

  // Ensure non-matching characters after a number are ignored, even when they
  // look like potentially matching characters.
  ExpectNumber<10>("1.23456789e5$   ", chars_format::general, 123456789, -3);
  ExpectNumber<10>("1.23456789e5$e5e5", chars_format::general, 123456789, -3);
  ExpectNumber<10>("1.23456789e5$.25", chars_format::general, 123456789, -3);
  ExpectNumber<10>("1.23456789e5$-", chars_format::general, 123456789, -3);
  ExpectNumber<10>("1.23456789e5$PUPPERS!!!", chars_format::general, 123456789,
                   -3);
  ExpectNumber<10>("123456.789$efghij", chars_format::general, 123456789, -3);
  ExpectNumber<10>("123456.789$e", chars_format::general, 123456789, -3);
  ExpectNumber<10>("123456.789$p5", chars_format::general, 123456789, -3);
  ExpectNumber<10>("123456.789$.10", chars_format::general, 123456789, -3);

  ExpectNumber<16>("1.234abcdefp28$   ", chars_format::general, 0x1234abcdef,
                   -8);
  ExpectNumber<16>("1.234abcdefp28$p28", chars_format::general, 0x1234abcdef,
                   -8);
  ExpectNumber<16>("1.234abcdefp28$.125", chars_format::general, 0x1234abcdef,
                   -8);
  ExpectNumber<16>("1.234abcdefp28$-", chars_format::general, 0x1234abcdef, -8);
  ExpectNumber<16>("1.234abcdefp28$KITTEHS!!!", chars_format::general,
                   0x1234abcdef, -8);
  ExpectNumber<16>("1234abcd.ef$ghijk", chars_format::general, 0x1234abcdef,
                   -8);
  ExpectNumber<16>("1234abcd.ef$p", chars_format::general, 0x1234abcdef, -8);
  ExpectNumber<16>("1234abcd.ef$.10", chars_format::general, 0x1234abcdef, -8);

  // Ensure we can read a full resolution mantissa without overflow.
  ExpectNumber<10>("9999999999999999999$", chars_format::general,
                   9999999999999999999u, 0);
  ExpectNumber<16>("fffffffffffffff$", chars_format::general,
                   0xfffffffffffffffu, 0);

  // Check that zero is consistently read.
  ExpectNumber<10>("0$", chars_format::general, 0, 0);
  ExpectNumber<16>("0$", chars_format::general, 0, 0);
  ExpectNumber<10>("000000000000000000000000000000000000000$",
                   chars_format::general, 0, 0);
  ExpectNumber<16>("000000000000000000000000000000000000000$",
                   chars_format::general, 0, 0);
  ExpectNumber<10>("0000000000000000000000.000000000000000000$",
                   chars_format::general, 0, 0);
  ExpectNumber<16>("0000000000000000000000.000000000000000000$",
                   chars_format::general, 0, 0);
  ExpectNumber<10>("0.00000000000000000000000000000000e123456$",
                   chars_format::general, 0, 0);
  ExpectNumber<16>("0.00000000000000000000000000000000p123456$",
                   chars_format::general, 0, 0);
}

TEST(ParseFloat, LargeDecimalMantissa) {
  // After 19 significant decimal digits in the mantissa, ParsedFloat will
  // truncate additional digits.  We need to test that:
  //   1) the truncation to 19 digits happens
  //   2) the returned exponent reflects the dropped significant digits
  //   3) a correct literal_exponent is set
  //
  // If and only if a significant digit is found after 19 digits, then the
  // entirety of the mantissa in case the exact value is needed to make a
  // rounding decision.  The [ and ] characters below denote where such a
  // subregion was marked by by ParseFloat.  They are not part of the input.

  // Mark a capture group only if a dropped digit is significant (nonzero).
  ExpectNumber<10>("100000000000000000000000000$", chars_format::general,
                   1000000000000000000,
                   /* adjusted exponent */ 8);

  ExpectNumber<10>("123456789123456789100000000$", chars_format::general,
                   1234567891234567891,
                   /* adjusted exponent */ 8);

  ExpectNumber<10>("[123456789123456789123456789]$", chars_format::general,
                   1234567891234567891,
                   /* adjusted exponent */ 8,
                   /* literal exponent */ 0);

  ExpectNumber<10>("[123456789123456789100000009]$", chars_format::general,
                   1234567891234567891,
                   /* adjusted exponent */ 8,
                   /* literal exponent */ 0);

  ExpectNumber<10>("[123456789123456789120000000]$", chars_format::general,
                   1234567891234567891,
                   /* adjusted exponent */ 8,
                   /* literal exponent */ 0);

  // Leading zeroes should not count towards the 19 significant digit limit
  ExpectNumber<10>("[00000000123456789123456789123456789]$",
                   chars_format::general, 1234567891234567891,
                   /* adjusted exponent */ 8,
                   /* literal exponent */ 0);

  ExpectNumber<10>("00000000123456789123456789100000000$",
                   chars_format::general, 1234567891234567891,
                   /* adjusted exponent */ 8);

  // Truncated digits after the decimal point should not cause a further
  // exponent adjustment.
  ExpectNumber<10>("1.234567891234567891e123$", chars_format::general,
                   1234567891234567891, 105);
  ExpectNumber<10>("[1.23456789123456789123456789]e123$", chars_format::general,
                   1234567891234567891,
                   /* adjusted exponent */ 105,
                   /* literal exponent */ 123);

  // Ensure we truncate, and not round.  (The from_chars algorithm we use
  // depends on our guess missing low, if it misses, so we need the rounding
  // error to be downward.)
  ExpectNumber<10>("[1999999999999999999999]$", chars_format::general,
                   1999999999999999999,
                   /* adjusted exponent */ 3,
                   /* literal exponent */ 0);
}

TEST(ParseFloat, LargeHexadecimalMantissa) {
  // After 15 significant hex digits in the mantissa, ParsedFloat will treat
  // additional digits as sticky,  We need to test that:
  //   1) The truncation to 15 digits happens
  //   2) The returned exponent reflects the dropped significant digits
  //   3) If a nonzero digit is dropped, the low bit of mantissa is set.

  ExpectNumber<16>("123456789abcdef123456789abcdef$", chars_format::general,
                   0x123456789abcdef, 60);

  // Leading zeroes should not count towards the 15 significant digit limit
  ExpectNumber<16>("000000123456789abcdef123456789abcdef$",
                   chars_format::general, 0x123456789abcdef, 60);

  // Truncated digits after the radix point should not cause a further
  // exponent adjustment.
  ExpectNumber<16>("1.23456789abcdefp100$", chars_format::general,
                   0x123456789abcdef, 44);
  ExpectNumber<16>("1.23456789abcdef123456789abcdefp100$",
                   chars_format::general, 0x123456789abcdef, 44);

  // test sticky digit behavior.  The low bit should be set iff any dropped
  // digit is nonzero.
  ExpectNumber<16>("123456789abcdee123456789abcdee$", chars_format::general,
                   0x123456789abcdef, 60);
  ExpectNumber<16>("123456789abcdee000000000000001$", chars_format::general,
                   0x123456789abcdef, 60);
  ExpectNumber<16>("123456789abcdee000000000000000$", chars_format::general,
                   0x123456789abcdee, 60);
}

TEST(ParseFloat, ScientificVsFixed) {
  // In fixed mode, an exponent is never matched (but the remainder of the
  // number will be matched.)
  ExpectNumber<10>("1.23456789$e5", chars_format::fixed, 123456789, -8);
  ExpectNumber<10>("123456.789$", chars_format::fixed, 123456789, -3);
  ExpectNumber<16>("1.234abcdef$p28", chars_format::fixed, 0x1234abcdef, -36);
  ExpectNumber<16>("1234abcd.ef$", chars_format::fixed, 0x1234abcdef, -8);

  // In scientific mode, numbers don't match *unless* they have an exponent.
  ExpectNumber<10>("1.23456789e5$", chars_format::scientific, 123456789, -3);
  ExpectFailedParse<10>("-123456.789$", chars_format::scientific);
  ExpectNumber<16>("1.234abcdefp28$", chars_format::scientific, 0x1234abcdef,
                   -8);
  ExpectFailedParse<16>("1234abcd.ef$", chars_format::scientific);
}

TEST(ParseFloat, Infinity) {
  ExpectFailedParse<10>("in", chars_format::general);
  ExpectFailedParse<16>("in", chars_format::general);
  ExpectFailedParse<10>("inx", chars_format::general);
  ExpectFailedParse<16>("inx", chars_format::general);
  ExpectSpecial("inf$", chars_format::general, FloatType::kInfinity);
  ExpectSpecial("Inf$", chars_format::general, FloatType::kInfinity);
  ExpectSpecial("INF$", chars_format::general, FloatType::kInfinity);
  ExpectSpecial("inf$inite", chars_format::general, FloatType::kInfinity);
  ExpectSpecial("iNfInItY$", chars_format::general, FloatType::kInfinity);
  ExpectSpecial("infinity$!!!", chars_format::general, FloatType::kInfinity);
}

TEST(ParseFloat, NaN) {
  ExpectFailedParse<10>("na", chars_format::general);
  ExpectFailedParse<16>("na", chars_format::general);
  ExpectFailedParse<10>("nah", chars_format::general);
  ExpectFailedParse<16>("nah", chars_format::general);
  ExpectSpecial("nan$", chars_format::general, FloatType::kNan);
  ExpectSpecial("NaN$", chars_format::general, FloatType::kNan);
  ExpectSpecial("nAn$", chars_format::general, FloatType::kNan);
  ExpectSpecial("NAN$", chars_format::general, FloatType::kNan);
  ExpectSpecial("NaN$aNaNaNaNaBatman!", chars_format::general, FloatType::kNan);

  // A parenthesized sequence of the characters [a-zA-Z0-9_] is allowed to
  // appear after an NaN.  Check that this is allowed, and that the correct
  // characters are grouped.
  //
  // (The characters [ and ] in the pattern below delimit the expected matched
  // subgroup; they are not part of the input passed to ParseFloat.)
  ExpectSpecial("nan([0xabcdef])$", chars_format::general, FloatType::kNan);
  ExpectSpecial("nan([0xabcdef])$...", chars_format::general, FloatType::kNan);
  ExpectSpecial("nan([0xabcdef])$)...", chars_format::general, FloatType::kNan);
  ExpectSpecial("nan([])$", chars_format::general, FloatType::kNan);
  ExpectSpecial("nan([aAzZ09_])$", chars_format::general, FloatType::kNan);
  // If the subgroup contains illegal characters, don't match it at all.
  ExpectSpecial("nan$(bad-char)", chars_format::general, FloatType::kNan);
  // Also cope with a missing close paren.
  ExpectSpecial("nan$(0xabcdef", chars_format::general, FloatType::kNan);
}

}  // namespace
