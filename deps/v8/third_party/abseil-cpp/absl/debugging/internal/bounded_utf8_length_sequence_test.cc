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

#include "absl/debugging/internal/bounded_utf8_length_sequence.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
namespace {

TEST(BoundedUtf8LengthSequenceTest, RemembersAValueOfOneCorrectly) {
  BoundedUtf8LengthSequence<32> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 1), 0);
  EXPECT_EQ(seq.InsertAndReturnSumOfPredecessors(1, 1), 1);
}

TEST(BoundedUtf8LengthSequenceTest, RemembersAValueOfTwoCorrectly) {
  BoundedUtf8LengthSequence<32> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 2), 0);
  EXPECT_EQ(seq.InsertAndReturnSumOfPredecessors(1, 1), 2);
}

TEST(BoundedUtf8LengthSequenceTest, RemembersAValueOfThreeCorrectly) {
  BoundedUtf8LengthSequence<32> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 3), 0);
  EXPECT_EQ(seq.InsertAndReturnSumOfPredecessors(1, 1), 3);
}

TEST(BoundedUtf8LengthSequenceTest, RemembersAValueOfFourCorrectly) {
  BoundedUtf8LengthSequence<32> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 4), 0);
  EXPECT_EQ(seq.InsertAndReturnSumOfPredecessors(1, 1), 4);
}

TEST(BoundedUtf8LengthSequenceTest, RemembersSeveralAppendedValues) {
  BoundedUtf8LengthSequence<32> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 1), 0);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(1, 4), 1);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(2, 2), 5);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(3, 3), 7);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(4, 1), 10);
}

TEST(BoundedUtf8LengthSequenceTest, RemembersSeveralPrependedValues) {
  BoundedUtf8LengthSequence<32> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 4), 0);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 3), 0);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 2), 0);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 1), 0);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(4, 1), 10);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(3, 1), 6);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(2, 1), 3);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(1, 1), 1);
}

TEST(BoundedUtf8LengthSequenceTest, RepeatedInsertsShiftValuesOutTheRightEnd) {
  BoundedUtf8LengthSequence<32> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 2), 0);
  for (uint32_t i = 1; i < 31; ++i) {
    ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 1), 0)
        << "while moving the 2 into position " << i;
    ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(31, 1), 32)
        << "after moving the 2 into position " << i;
  }
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 1), 0)
      << "while moving the 2 into position 31";
  EXPECT_EQ(seq.InsertAndReturnSumOfPredecessors(31, 1), 31)
      << "after moving the 2 into position 31";
}

TEST(BoundedUtf8LengthSequenceTest, InsertsIntoWord1LeaveWord0Untouched) {
  BoundedUtf8LengthSequence<64> seq;
  for (uint32_t i = 0; i < 32; ++i) {
    ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(i, 2), 2 * i)
        << "at index " << i;
  }
  EXPECT_EQ(seq.InsertAndReturnSumOfPredecessors(32, 1), 64);
  EXPECT_EQ(seq.InsertAndReturnSumOfPredecessors(32, 1), 64);
}

TEST(BoundedUtf8LengthSequenceTest, InsertsIntoWord0ShiftValuesIntoWord1) {
  BoundedUtf8LengthSequence<64> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(29, 2), 29);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(30, 3), 31);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(31, 4), 34);

  // Pushing two 1's on the front moves the 3 and 4 into the high word.
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 1), 0);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 1), 0);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(34, 1), 31 + 2 + 3 + 4);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(32, 1), 31 + 2);
}

TEST(BoundedUtf8LengthSequenceTest, ValuesAreShiftedCorrectlyAmongThreeWords) {
  BoundedUtf8LengthSequence<96> seq;
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(31, 3), 31);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(63, 4), 62 + 3);

  // This insertion moves both the 3 and the 4 up a word.
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(0, 1), 0);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(65, 1), 63 + 3 + 4);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(64, 1), 63 + 3);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(33, 1), 32 + 3);
  ASSERT_EQ(seq.InsertAndReturnSumOfPredecessors(32, 1), 32);
}

}  // namespace
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
