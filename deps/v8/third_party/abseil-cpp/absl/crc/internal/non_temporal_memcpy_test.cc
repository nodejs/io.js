// Copyright 2022 The Abseil Authors
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

#include "absl/crc/internal/non_temporal_memcpy.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include "gtest/gtest.h"

namespace {

struct TestParam {
  size_t copy_size;
  uint32_t src_offset;
  uint32_t dst_offset;
};

class NonTemporalMemcpyTest : public testing::TestWithParam<TestParam> {
 protected:
  void SetUp() override {
    // Make buf_size multiple of 16 bytes.
    size_t buf_size = ((std::max(GetParam().src_offset, GetParam().dst_offset) +
                        GetParam().copy_size) +
                       15) /
                      16 * 16;
    a_.resize(buf_size);
    b_.resize(buf_size);
    for (size_t i = 0; i < buf_size; i++) {
      a_[i] = static_cast<uint8_t>(i % 256);
      b_[i] = ~a_[i];
    }
  }

  std::vector<uint8_t> a_, b_;
};

TEST_P(NonTemporalMemcpyTest, SSEEquality) {
  uint8_t *src = a_.data() + GetParam().src_offset;
  uint8_t *dst = b_.data() + GetParam().dst_offset;
  absl::crc_internal::non_temporal_store_memcpy(dst, src, GetParam().copy_size);
  for (size_t i = 0; i < GetParam().copy_size; i++) {
    EXPECT_EQ(src[i], dst[i]);
  }
}

TEST_P(NonTemporalMemcpyTest, AVXEquality) {
  uint8_t* src = a_.data() + GetParam().src_offset;
  uint8_t* dst = b_.data() + GetParam().dst_offset;

  absl::crc_internal::non_temporal_store_memcpy_avx(dst, src,
                                                    GetParam().copy_size);
  for (size_t i = 0; i < GetParam().copy_size; i++) {
    EXPECT_EQ(src[i], dst[i]);
  }
}

// 63B is smaller than one cacheline operation thus the non-temporal routine
// will not be called.
// 4352B is sufficient for testing 4092B data copy with room for offsets.
constexpr TestParam params[] = {
    {63, 0, 0},       {58, 5, 5},    {61, 2, 0},    {61, 0, 2},
    {58, 5, 2},       {4096, 0, 0},  {4096, 0, 1},  {4096, 0, 2},
    {4096, 0, 3},     {4096, 0, 4},  {4096, 0, 5},  {4096, 0, 6},
    {4096, 0, 7},     {4096, 0, 8},  {4096, 0, 9},  {4096, 0, 10},
    {4096, 0, 11},    {4096, 0, 12}, {4096, 0, 13}, {4096, 0, 14},
    {4096, 0, 15},    {4096, 7, 7},  {4096, 3, 0},  {4096, 1, 0},
    {4096, 9, 3},     {4096, 9, 11}, {8192, 0, 0},  {8192, 5, 2},
    {1024768, 7, 11}, {1, 0, 0},     {1, 0, 1},     {1, 1, 0},
    {1, 1, 1}};

INSTANTIATE_TEST_SUITE_P(ParameterizedNonTemporalMemcpyTest,
                         NonTemporalMemcpyTest, testing::ValuesIn(params));

}  // namespace
