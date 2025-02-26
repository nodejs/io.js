// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "include/v8-internal.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8::internal {

// Small Smis are 31 bit in size and used in compression scenarios or when
// explicitly enabled otherwise.
using SmallSmi = SmiTagging<4>;
// Large Smis are 32-bit in size and are used in uncompressed 64-bit builds by
// default when not explicitly opting for 31-bit Smis.
using LargeSmi = SmiTagging<8>;

constexpr int32_t kInt31Max = std::numeric_limits<int32_t>::max() / 2;
constexpr int32_t kInt31Min = std::numeric_limits<int32_t>::min() / 2;
constexpr uint32_t kInt31MaxAsUint = std::numeric_limits<int32_t>::max() / 2;

TEST(SmiTaggingTest, AssertCornerCases) {
  static_assert(SmallSmi::IsValidSmi(0));
  // int8_t, uint8_t
  static_assert(SmallSmi::IsValidSmi(std::numeric_limits<int8_t>::max()));
  static_assert(SmallSmi::IsValidSmi(std::numeric_limits<int8_t>::min()));
  static_assert(SmallSmi::IsValidSmi(std::numeric_limits<uint8_t>::max()));
  static_assert(SmallSmi::IsValidSmi(std::numeric_limits<uint8_t>::min()));
  // int16_t, uint16_t
  static_assert(SmallSmi::IsValidSmi(std::numeric_limits<int16_t>::max()));
  static_assert(SmallSmi::IsValidSmi(std::numeric_limits<int16_t>::min()));
  static_assert(SmallSmi::IsValidSmi(std::numeric_limits<uint16_t>::max()));
  static_assert(SmallSmi::IsValidSmi(std::numeric_limits<uint16_t>::min()));
  // int31_t, uint31_t
  static_assert(SmallSmi::IsValidSmi(kInt31Max));
  static_assert(SmallSmi::IsValidSmi(kInt31Min));
  static_assert(SmallSmi::IsValidSmi(kInt31MaxAsUint));
  static_assert(!SmallSmi::IsValidSmi(kInt31Max + 1));
  static_assert(!SmallSmi::IsValidSmi(kInt31Min - 1));
  static_assert(!SmallSmi::IsValidSmi(kInt31MaxAsUint + 1));
  // int32_t, uint32_t
  static_assert(!SmallSmi::IsValidSmi(std::numeric_limits<int32_t>::max()));
  static_assert(!SmallSmi::IsValidSmi(std::numeric_limits<int32_t>::min()));
  static_assert(!SmallSmi::IsValidSmi(std::numeric_limits<uint32_t>::max()));
  // int64_t, uint64_t
  static_assert(!SmallSmi::IsValidSmi(std::numeric_limits<int64_t>::max()));
  static_assert(!SmallSmi::IsValidSmi(std::numeric_limits<int64_t>::min()));
  static_assert(!SmallSmi::IsValidSmi(std::numeric_limits<uint64_t>::max()));

  static_assert(LargeSmi::IsValidSmi(0));
  // int8_t, uint8_t
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<int8_t>::max()));
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<int8_t>::min()));
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<uint8_t>::max()));
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<uint8_t>::min()));
  // int16_t, uint16_t
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<int16_t>::max()));
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<int16_t>::min()));
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<uint16_t>::max()));
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<uint16_t>::min()));
  // int31_t, uint31_t
  static_assert(LargeSmi::IsValidSmi(kInt31Max));
  static_assert(LargeSmi::IsValidSmi(kInt31Min));
  static_assert(LargeSmi::IsValidSmi(kInt31MaxAsUint));
  static_assert(LargeSmi::IsValidSmi(kInt31Max + 1));
  static_assert(LargeSmi::IsValidSmi(kInt31Min - 1));
  static_assert(LargeSmi::IsValidSmi(kInt31MaxAsUint + 1));
  // int32_t, uint32_t
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<int32_t>::max()));
  static_assert(LargeSmi::IsValidSmi(std::numeric_limits<int32_t>::min()));
  static_assert(!LargeSmi::IsValidSmi(std::numeric_limits<uint32_t>::max()));
  // int64_t, uint64_t
  static_assert(!LargeSmi::IsValidSmi(std::numeric_limits<int64_t>::max()));
  static_assert(!LargeSmi::IsValidSmi(std::numeric_limits<int64_t>::min()));
  static_assert(!LargeSmi::IsValidSmi(std::numeric_limits<uint64_t>::max()));
}

}  // namespace v8::internal
