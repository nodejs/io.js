// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/debugging/internal/decode_rust_punycode.h"

#include <cstddef>
#include <cstring>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::Pointee;
using ::testing::ResultOf;
using ::testing::StrEq;

class DecodeRustPunycodeTest : public ::testing::Test {
 protected:
  void FillBufferWithNonzeroBytes() {
    // The choice of nonzero value to fill with is arbitrary.  The point is just
    // to fail tests if DecodeRustPunycode forgets to write the final NUL
    // character.
    std::memset(buffer_storage_, 0xab, sizeof(buffer_storage_));
  }

  DecodeRustPunycodeOptions WithAmpleSpace() {
    FillBufferWithNonzeroBytes();

    DecodeRustPunycodeOptions options;
    options.punycode_begin = punycode_.data();
    options.punycode_end = punycode_.data() + punycode_.size();
    options.out_begin = buffer_storage_;
    options.out_end = buffer_storage_ + sizeof(buffer_storage_);
    return options;
  }

  DecodeRustPunycodeOptions WithJustEnoughSpace() {
    FillBufferWithNonzeroBytes();

    const size_t begin_offset = sizeof(buffer_storage_) - plaintext_.size() - 1;
    DecodeRustPunycodeOptions options;
    options.punycode_begin = punycode_.data();
    options.punycode_end = punycode_.data() + punycode_.size();
    options.out_begin = buffer_storage_ + begin_offset;
    options.out_end = buffer_storage_ + sizeof(buffer_storage_);
    return options;
  }

  DecodeRustPunycodeOptions WithOneByteTooFew() {
    FillBufferWithNonzeroBytes();

    const size_t begin_offset = sizeof(buffer_storage_) - plaintext_.size();
    DecodeRustPunycodeOptions options;
    options.punycode_begin = punycode_.data();
    options.punycode_end = punycode_.data() + punycode_.size();
    options.out_begin = buffer_storage_ + begin_offset;
    options.out_end = buffer_storage_ + sizeof(buffer_storage_);
    return options;
  }

  // Matches a correct return value of DecodeRustPunycode when `golden` is the
  // expected plaintext output.
  auto PointsToTheNulAfter(const std::string& golden) {
    const size_t golden_size = golden.size();
    return AllOf(
        Pointee(Eq('\0')),
        ResultOf("preceding string body",
                 [golden_size](const char* p) { return p - golden_size; },
                 StrEq(golden)));
  }

  std::string punycode_;
  std::string plaintext_;
  char buffer_storage_[1024];
};

TEST_F(DecodeRustPunycodeTest, MapsEmptyToEmpty) {
  punycode_ = "";
  plaintext_ = "";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest,
       StripsTheTrailingDelimiterFromAPureRunOfBasicChars) {
  punycode_ = "foo_";
  plaintext_ = "foo";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, TreatsTheLastUnderscoreAsTheDelimiter) {
  punycode_ = "foo_bar_";
  plaintext_ = "foo_bar";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsALeadingUnderscoreIfNotTheDelimiter) {
  punycode_ = "_foo_";
  plaintext_ = "_foo";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, RejectsALeadingUnderscoreDelimiter) {
  punycode_ = "_foo";

  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, RejectsEmbeddedNul) {
  punycode_ = std::string("foo\0bar_", 8);

  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, RejectsAsciiCharsOtherThanIdentifierChars) {
  punycode_ = "foo\007_";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "foo-_";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "foo;_";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "foo\177_";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, RejectsRawNonAsciiChars) {
  punycode_ = "\x80";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "\x80_";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "\xff";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "\xff_";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, RecognizesU0080) {
  // a encodes 0, so the output is the smallest non-ASCII code point standing
  // alone.  (U+0080 PAD is not an identifier character, but DecodeRustPunycode
  // does not check whether non-ASCII characters could belong to an identifier.)
  punycode_ = "a";
  plaintext_ = "\xc2\x80";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, OneByteDeltaSequencesMustBeA) {
  // Because bias = 72 for the first code point, any digit but a/A is nonfinal
  // in one of the first two bytes of a delta sequence.
  punycode_ = "b";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "z";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "0";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "9";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsDeltaSequenceBA) {
  punycode_ = "ba";
  plaintext_ = "\xc2\x81";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsOtherDeltaSequencesWithSecondByteA) {
  punycode_ = "ca";
  plaintext_ = "\xc2\x82";
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "za";
  plaintext_ = "\xc2\x99";
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "0a";
  plaintext_ = "\xc2\x9a";
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "1a";
  plaintext_ = "\xc2\x9b";
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "9a";
  plaintext_ = "£";  // Pound sign, U+00A3
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
}

TEST_F(DecodeRustPunycodeTest, RejectsDeltaWhereTheSecondAndLastDigitIsNotA) {
  punycode_ = "bb";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "zz";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "00";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());

  punycode_ = "99";
  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsDeltasWithSecondByteBFollowedByA) {
  punycode_ = "bba";
  plaintext_ = "¤";  // U+00A4
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "cba";
  plaintext_ = "¥";  // U+00A5
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "zba";
  plaintext_ = "¼";  // U+00BC
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "0ba";
  plaintext_ = "½";  // U+00BD
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "1ba";
  plaintext_ = "¾";  // U+00BE
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));

  punycode_ = "9ba";
  plaintext_ = "Æ";  // U+00C6
  EXPECT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
}

// Tests beyond this point use characters allowed in identifiers, so you can
// prepend _RNvC1cu<decimal length><underscore if [0-9_] follows> to a test
// input and run it through another Rust demangler to verify that the
// corresponding golden output is correct.

TEST_F(DecodeRustPunycodeTest, AcceptsTwoByteCharAlone) {
  punycode_ = "0ca";
  plaintext_ = "à";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsTwoByteCharBeforeBasicChars) {
  punycode_ = "_la_mode_yya";
  plaintext_ = "à_la_mode";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsTwoByteCharAmidBasicChars) {
  punycode_ = "verre__vin_m4a";
  plaintext_ = "verre_à_vin";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsTwoByteCharAfterBasicChars) {
  punycode_ = "belt_3na";
  plaintext_ = "beltà";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsRepeatedTwoByteChar) {
  punycode_ = "0caaaa";
  plaintext_ = "àààà";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsNearbyTwoByteCharsInOrder) {
  punycode_ = "3camsuz";
  plaintext_ = "ãéïôù";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsNearbyTwoByteCharsOutOfOrder) {
  punycode_ = "3caltsx";
  plaintext_ = "ùéôãï";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsThreeByteCharAlone) {
  punycode_ = "fiq";
  plaintext_ = "中";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsRepeatedThreeByteChar) {
  punycode_ = "fiqaaaa";
  plaintext_ = "中中中中中";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsThreeByteCharsInOrder) {
  punycode_ = "fiq228c";
  plaintext_ = "中文";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsNearbyThreeByteCharsOutOfOrder) {
  punycode_ = "fiq128c";
  plaintext_ = "文中";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsFourByteCharAlone) {
  punycode_ = "uy7h";
  plaintext_ = "🂻";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsFourByteCharBeforeBasicChars) {
  punycode_ = "jack__uh63d";
  plaintext_ = "jack_🂻";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsFourByteCharAmidBasicChars) {
  punycode_ = "jack__of_hearts_ki37n";
  plaintext_ = "jack_🂻_of_hearts";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsFourByteCharAfterBasicChars) {
  punycode_ = "_of_hearts_kz45i";
  plaintext_ = "🂻_of_hearts";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsRepeatedFourByteChar) {
  punycode_ = "uy7haaaa";
  plaintext_ = "🂻🂻🂻🂻🂻";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsNearbyFourByteCharsInOrder) {
  punycode_ = "8x7hcjmf";
  plaintext_ = "🂦🂧🂪🂭🂮";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsNearbyFourByteCharsOutOfOrder) {
  punycode_ = "8x7hcild";
  plaintext_ = "🂮🂦🂭🂪🂧";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, AcceptsAMixtureOfByteLengths) {
  punycode_ = "3caltsx2079ivf8aiuy7cja3a6ak";
  plaintext_ = "ùéôãï中文🂮🂦🂭🂪🂧";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

TEST_F(DecodeRustPunycodeTest, RejectsOverlargeDeltas) {
  punycode_ = "123456789a";

  EXPECT_THAT(DecodeRustPunycode(WithAmpleSpace()), IsNull());
}

// Finally, we test on a few prose and poetry snippets as a defense in depth.
// If our artificial short test inputs did not exercise a bug that is tickled by
// patterns typical of real human writing, maybe real human writing will catch
// that.
//
// These test inputs are extracted from texts old enough to be out of copyright
// that probe a variety of ranges of code-point space.  All are longer than 32
// code points, so they exercise the carrying of seminibbles from one uint64_t
// to the next higher one in BoundedUtf8LengthSequence.

// The first three lines of the Old English epic _Beowulf_, mostly ASCII with a
// few archaic two-byte letters interspersed.
TEST_F(DecodeRustPunycodeTest, Beowulf) {
  punycode_ = "hwt_we_gardena_in_geardagum_"
              "eodcyninga_rym_gefrunon_"
              "hu_a_elingas_ellen_fremedon_hxg9c70do9alau";
  plaintext_ = "hwæt_we_gardena_in_geardagum_"
               "þeodcyninga_þrym_gefrunon_"
               "hu_ða_æþelingas_ellen_fremedon";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

// The whole of 過故人莊 by the 8th-century Chinese poet 孟浩然
// (Meng Haoran), exercising three-byte-character processing.
TEST_F(DecodeRustPunycodeTest, MengHaoran) {
  punycode_ = "gmq4ss0cfvao1e2wg8mcw8b0wkl9a7tt90a8riuvbk7t8kbv9a66ogofvzlf6"
              "3d01ybn1u28dyqi5q2cxyyxnk5d2gx1ks9ddvfm17bk6gbsd6wftrav60u4ta";
  plaintext_ = "故人具雞黍" "邀我至田家"
               "綠樹村邊合" "青山郭外斜"
               "開軒面場圃" "把酒話桑麻"
               "待到重陽日" "還來就菊花";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

// A poem of the 8th-century Japanese poet 山上憶良 (Yamanoue no Okura).
// Japanese mixes two-byte and three-byte characters: a good workout for codecs.
TEST_F(DecodeRustPunycodeTest, YamanoueNoOkura) {
  punycode_ = "48jdaa3a6ccpepjrsmlb0q4bwcdtid8fg6c0cai9822utqeruk3om0u4f2wbp0"
              "em23do0op23cc2ff70mb6tae8aq759gja";
  plaintext_ = "瓜食めば"
               "子ども思ほゆ"
               "栗食めば"
               "まして偲はゆ"
               "何処より"
               "来りしものそ"
               "眼交に"
               "もとな懸りて"
               "安眠し寝さぬ";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

// The first two lines of the Phoenician-language inscription on the sarcophagus
// of Eshmunazar II of Sidon, 6th century BCE.  Phoenician and many other
// archaic scripts are allocated in the Supplemental Multilingual Plane (U+10000
// through U+1FFFF) and thus exercise four-byte-character processing.
TEST_F(DecodeRustPunycodeTest, EshmunazarSarcophagus) {
  punycode_ = "wj9caaabaabbaaohcacxvhdc7bgxbccbdcjeacddcedcdlddbdbddcdbdcknfcee"
              "ifel8del2a7inq9fhcpxikms7a4a9ac9ataaa0g";
  plaintext_ = "𐤁𐤉𐤓𐤇𐤁𐤋𐤁𐤔𐤍𐤕𐤏𐤎𐤓"
               "𐤅𐤀𐤓𐤁𐤏𐤗𐤖𐤖𐤖𐤖𐤋𐤌𐤋𐤊𐤉𐤌𐤋𐤊"
               "𐤀𐤔𐤌𐤍𐤏𐤆𐤓𐤌𐤋𐤊𐤑𐤃𐤍𐤌"
               "𐤁𐤍𐤌𐤋𐤊𐤕𐤁𐤍𐤕𐤌𐤋𐤊𐤑𐤃𐤍𐤌"
               "𐤃𐤁𐤓𐤌𐤋𐤊𐤀𐤔𐤌𐤍𐤏𐤆𐤓𐤌𐤋𐤊"
               "𐤑𐤃𐤍𐤌𐤋𐤀𐤌𐤓𐤍𐤂𐤆𐤋𐤕";

  ASSERT_THAT(DecodeRustPunycode(WithAmpleSpace()),
              PointsToTheNulAfter(plaintext_));
  ASSERT_THAT(DecodeRustPunycode(WithJustEnoughSpace()),
              PointsToTheNulAfter(plaintext_));
  EXPECT_THAT(DecodeRustPunycode(WithOneByteTooFew()), IsNull());
}

}  // namespace
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
