// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/vlq.h"

#include <cmath>
#include <limits>

#include "src/base/memory.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace base {

int ExpectedBytesUsed(int64_t value, bool is_signed) {
  uint64_t bits = value;
  if (is_signed) {
    bits = (value < 0 ? -value : value) << 1;
  }
  int num_bits = 0;
  while (bits != 0) {
    num_bits++;
    bits >>= 1;
  }
  return std::max(1, static_cast<int>(ceil(static_cast<float>(num_bits) / 7)));
}

void TestVLQUnsignedEquals(uint32_t value) {
  std::vector<uint8_t> buffer;
  VLQEncodeUnsigned(&buffer, value);
  uint8_t* data_start = buffer.data();
  int index = 0;
  int expected_bytes_used = ExpectedBytesUsed(value, false);
  EXPECT_EQ(buffer.size(), static_cast<size_t>(expected_bytes_used));
  EXPECT_EQ(value, VLQDecodeUnsigned(data_start, &index));
  EXPECT_EQ(index, expected_bytes_used);
}

void TestVLQEquals(int32_t value) {
  std::vector<uint8_t> buffer;
  VLQEncode(&buffer, value);
  uint8_t* data_start = buffer.data();
  int index = 0;
  int expected_bytes_used = ExpectedBytesUsed(value, true);
  EXPECT_EQ(buffer.size(), static_cast<size_t>(expected_bytes_used));
  EXPECT_EQ(value, VLQDecode(data_start, &index));
  EXPECT_EQ(index, expected_bytes_used);
}

TEST(VLQ, Unsigned) {
  TestVLQUnsignedEquals(0);
  TestVLQUnsignedEquals(1);
  TestVLQUnsignedEquals(63);
  TestVLQUnsignedEquals(64);
  TestVLQUnsignedEquals(127);
  TestVLQUnsignedEquals(255);
  TestVLQUnsignedEquals(256);
}

TEST(VLQ, Positive) {
  TestVLQEquals(0);
  TestVLQEquals(1);
  TestVLQEquals(63);
  TestVLQEquals(64);
  TestVLQEquals(127);
  TestVLQEquals(255);
  TestVLQEquals(256);
}

TEST(VLQ, Negative) {
  TestVLQEquals(-1);
  TestVLQEquals(-63);
  TestVLQEquals(-64);
  TestVLQEquals(-127);
  TestVLQEquals(-255);
  TestVLQEquals(-256);
}

TEST(VLQ, LimitsUnsigned) {
  TestVLQEquals(std::numeric_limits<uint8_t>::max());
  TestVLQEquals(std::numeric_limits<uint8_t>::max() - 1);
  TestVLQEquals(std::numeric_limits<uint8_t>::max() + 1);
  TestVLQEquals(std::numeric_limits<uint16_t>::max());
  TestVLQEquals(std::numeric_limits<uint16_t>::max() - 1);
  TestVLQEquals(std::numeric_limits<uint16_t>::max() + 1);
  TestVLQEquals(std::numeric_limits<uint32_t>::max());
  TestVLQEquals(std::numeric_limits<uint32_t>::max() - 1);
}

TEST(VLQ, LimitsSigned) {
  TestVLQEquals(std::numeric_limits<int8_t>::max());
  TestVLQEquals(std::numeric_limits<int8_t>::max() - 1);
  TestVLQEquals(std::numeric_limits<int8_t>::max() + 1);
  TestVLQEquals(std::numeric_limits<int16_t>::max());
  TestVLQEquals(std::numeric_limits<int16_t>::max() - 1);
  TestVLQEquals(std::numeric_limits<int16_t>::max() + 1);
  TestVLQEquals(std::numeric_limits<int32_t>::max());
  TestVLQEquals(std::numeric_limits<int32_t>::max() - 1);
  TestVLQEquals(std::numeric_limits<int8_t>::min());
  TestVLQEquals(std::numeric_limits<int8_t>::min() - 1);
  TestVLQEquals(std::numeric_limits<int8_t>::min() + 1);
  TestVLQEquals(std::numeric_limits<int16_t>::min());
  TestVLQEquals(std::numeric_limits<int16_t>::min() - 1);
  TestVLQEquals(std::numeric_limits<int16_t>::min() + 1);
  // int32_t::min() is not supported.
  TestVLQEquals(std::numeric_limits<int32_t>::min() + 1);
}

TEST(VLQ, Random) {
  static constexpr int RANDOM_RUNS = 50;

  base::RandomNumberGenerator rng(GTEST_FLAG_GET(random_seed));
  for (int i = 0; i < RANDOM_RUNS; ++i) {
    TestVLQUnsignedEquals(rng.NextInt(std::numeric_limits<int32_t>::max()));
  }
  for (int i = 0; i < RANDOM_RUNS; ++i) {
    TestVLQEquals(rng.NextInt());
  }
}
}  // namespace base
}  // namespace v8
