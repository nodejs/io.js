// Copyright 2021 The Abseil Authors
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

#include "absl/strings/cord_buffer.h"


#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/internal/cord_rep_test_util.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

using testing::Eq;
using testing::Ge;
using testing::Le;
using testing::Ne;

namespace absl {
ABSL_NAMESPACE_BEGIN

class CordBufferTestPeer {
 public:
  static cord_internal::CordRep* ConsumeValue(CordBuffer& buffer,
                                              absl::string_view& short_value) {
    return buffer.ConsumeValue(short_value);
  }
};

namespace {

using ::absl::cordrep_testing::CordToString;

constexpr size_t kInlinedSize = sizeof(CordBuffer) - 1;
constexpr size_t kDefaultLimit = CordBuffer::kDefaultLimit;
constexpr size_t kCustomLimit = CordBuffer::kCustomLimit;
constexpr size_t kMaxFlatSize = cord_internal::kMaxFlatSize;
constexpr size_t kMaxFlatLength = cord_internal::kMaxFlatLength;
constexpr size_t kFlatOverhead = cord_internal::kFlatOverhead;

constexpr size_t k8KiB = 8 << 10;
constexpr size_t k16KiB = 16 << 10;
constexpr size_t k64KiB = 64 << 10;
constexpr size_t k1MB = 1 << 20;

class CordBufferTest : public testing::TestWithParam<size_t> {};

INSTANTIATE_TEST_SUITE_P(MediumSize, CordBufferTest,
                         testing::Values(1, kInlinedSize - 1, kInlinedSize,
                                         kInlinedSize + 1, kDefaultLimit - 1,
                                         kDefaultLimit));

TEST_P(CordBufferTest, MaximumPayload) {
  EXPECT_THAT(CordBuffer::MaximumPayload(), Eq(kMaxFlatLength));
  EXPECT_THAT(CordBuffer::MaximumPayload(512), Eq(512 - kFlatOverhead));
  EXPECT_THAT(CordBuffer::MaximumPayload(k64KiB), Eq(k64KiB - kFlatOverhead));
  EXPECT_THAT(CordBuffer::MaximumPayload(k1MB), Eq(k64KiB - kFlatOverhead));
}

TEST(CordBufferTest, ConstructDefault) {
  CordBuffer buffer;
  EXPECT_THAT(buffer.capacity(), Eq(sizeof(CordBuffer) - 1));
  EXPECT_THAT(buffer.length(), Eq(0));
  EXPECT_THAT(buffer.data(), Ne(nullptr));
  EXPECT_THAT(buffer.available().data(), Eq(buffer.data()));
  EXPECT_THAT(buffer.available().size(), Eq(buffer.capacity()));
  memset(buffer.data(), 0xCD, buffer.capacity());
}

TEST(CordBufferTest, CreateSsoWithDefaultLimit) {
  CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(3);
  EXPECT_THAT(buffer.capacity(), Ge(3));
  EXPECT_THAT(buffer.capacity(), Le(sizeof(CordBuffer)));
  EXPECT_THAT(buffer.length(), Eq(0));
  memset(buffer.data(), 0xCD, buffer.capacity());

  memcpy(buffer.data(), "Abc", 3);
  buffer.SetLength(3);
  EXPECT_THAT(buffer.length(), Eq(3));
  absl::string_view short_value;
  EXPECT_THAT(CordBufferTestPeer::ConsumeValue(buffer, short_value),
              Eq(nullptr));
  EXPECT_THAT(absl::string_view(buffer.data(), 3), Eq("Abc"));
  EXPECT_THAT(short_value, Eq("Abc"));
}

TEST_P(CordBufferTest, Available) {
  const size_t requested = GetParam();
  CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(requested);
  EXPECT_THAT(buffer.available().data(), Eq(buffer.data()));
  EXPECT_THAT(buffer.available().size(), Eq(buffer.capacity()));

  buffer.SetLength(2);
  EXPECT_THAT(buffer.available().data(), Eq(buffer.data() + 2));
  EXPECT_THAT(buffer.available().size(), Eq(buffer.capacity() - 2));
}

TEST_P(CordBufferTest, IncreaseLengthBy) {
  const size_t requested = GetParam();
  CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(requested);
  buffer.IncreaseLengthBy(2);
  EXPECT_THAT(buffer.length(), Eq(2));
  buffer.IncreaseLengthBy(5);
  EXPECT_THAT(buffer.length(), Eq(7));
}

TEST_P(CordBufferTest, AvailableUpTo) {
  const size_t requested = GetParam();
  CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(requested);
  size_t expected_up_to = std::min<size_t>(3, buffer.capacity());
  EXPECT_THAT(buffer.available_up_to(3).data(), Eq(buffer.data()));
  EXPECT_THAT(buffer.available_up_to(3).size(), Eq(expected_up_to));

  buffer.SetLength(2);
  expected_up_to = std::min<size_t>(3, buffer.capacity() - 2);
  EXPECT_THAT(buffer.available_up_to(3).data(), Eq(buffer.data() + 2));
  EXPECT_THAT(buffer.available_up_to(3).size(), Eq(expected_up_to));
}

// Returns the maximum capacity for a given block_size and requested size.
size_t MaxCapacityFor(size_t block_size, size_t requested) {
  requested = (std::min)(requested, cord_internal::kMaxLargeFlatSize);
  // Maximum returned size is always capped at block_size - kFlatOverhead.
  return block_size - kFlatOverhead;
}

TEST_P(CordBufferTest, CreateWithDefaultLimit) {
  const size_t requested = GetParam();
  CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(requested);
  EXPECT_THAT(buffer.capacity(), Ge(requested));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(kMaxFlatSize, requested)));
  EXPECT_THAT(buffer.length(), Eq(0));

  memset(buffer.data(), 0xCD, buffer.capacity());

  std::string data(requested - 1, 'x');
  memcpy(buffer.data(), data.c_str(), requested);
  buffer.SetLength(requested);

  EXPECT_THAT(buffer.length(), Eq(requested));
  EXPECT_THAT(absl::string_view(buffer.data()), Eq(data));
}

TEST(CordBufferTest, CreateWithDefaultLimitAskingFor2GB) {
  constexpr size_t k2GiB = 1U << 31;
  CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(k2GiB);
  // Expect to never be awarded more than a reasonable memory size, even in
  // cases where a (debug) memory allocator may grant us somewhat more memory
  // than `kDefaultLimit` which should be no more than `2 * kDefaultLimit`
  EXPECT_THAT(buffer.capacity(), Le(2 * CordBuffer::kDefaultLimit));
  EXPECT_THAT(buffer.length(), Eq(0));
  EXPECT_THAT(buffer.data(), Ne(nullptr));
  memset(buffer.data(), 0xCD, buffer.capacity());
}

TEST_P(CordBufferTest, MoveConstruct) {
  const size_t requested = GetParam();
  CordBuffer from = CordBuffer::CreateWithDefaultLimit(requested);
  const size_t capacity = from.capacity();
  memcpy(from.data(), "Abc", 4);
  from.SetLength(4);

  CordBuffer to(std::move(from));
  EXPECT_THAT(to.capacity(), Eq(capacity));
  EXPECT_THAT(to.length(), Eq(4));
  EXPECT_THAT(absl::string_view(to.data()), Eq("Abc"));

  EXPECT_THAT(from.length(), Eq(0));  // NOLINT
}

TEST_P(CordBufferTest, MoveAssign) {
  const size_t requested = GetParam();
  CordBuffer from = CordBuffer::CreateWithDefaultLimit(requested);
  const size_t capacity = from.capacity();
  memcpy(from.data(), "Abc", 4);
  from.SetLength(4);

  CordBuffer to;
  to = std::move(from);
  EXPECT_THAT(to.capacity(), Eq(capacity));
  EXPECT_THAT(to.length(), Eq(4));
  EXPECT_THAT(absl::string_view(to.data()), Eq("Abc"));

  EXPECT_THAT(from.length(), Eq(0));  // NOLINT
}

TEST_P(CordBufferTest, ConsumeValue) {
  const size_t requested = GetParam();
  CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(requested);
  memcpy(buffer.data(), "Abc", 4);
  buffer.SetLength(3);

  absl::string_view short_value;
  if (cord_internal::CordRep* rep =
          CordBufferTestPeer::ConsumeValue(buffer, short_value)) {
    EXPECT_THAT(CordToString(rep), Eq("Abc"));
    cord_internal::CordRep::Unref(rep);
  } else {
    EXPECT_THAT(short_value, Eq("Abc"));
  }
  EXPECT_THAT(buffer.length(), Eq(0));
}

TEST_P(CordBufferTest, CreateWithCustomLimitWithinDefaultLimit) {
  const size_t requested = GetParam();
  CordBuffer buffer =
      CordBuffer::CreateWithCustomLimit(kMaxFlatSize, requested);
  EXPECT_THAT(buffer.capacity(), Ge(requested));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(kMaxFlatSize, requested)));
  EXPECT_THAT(buffer.length(), Eq(0));

  memset(buffer.data(), 0xCD, buffer.capacity());

  std::string data(requested - 1, 'x');
  memcpy(buffer.data(), data.c_str(), requested);
  buffer.SetLength(requested);

  EXPECT_THAT(buffer.length(), Eq(requested));
  EXPECT_THAT(absl::string_view(buffer.data()), Eq(data));
}

TEST(CordLargeBufferTest, CreateAtOrBelowDefaultLimit) {
  CordBuffer buffer = CordBuffer::CreateWithCustomLimit(k64KiB, kDefaultLimit);
  EXPECT_THAT(buffer.capacity(), Ge(kDefaultLimit));
  EXPECT_THAT(buffer.capacity(),
              Le(MaxCapacityFor(kMaxFlatSize, kDefaultLimit)));

  buffer = CordBuffer::CreateWithCustomLimit(k64KiB, 3178);
  EXPECT_THAT(buffer.capacity(), Ge(3178));
}

TEST(CordLargeBufferTest, CreateWithCustomLimit) {
  ASSERT_THAT((kMaxFlatSize & (kMaxFlatSize - 1)) == 0, "Must be power of 2");

  for (size_t size = kMaxFlatSize; size <= kCustomLimit; size *= 2) {
    CordBuffer buffer = CordBuffer::CreateWithCustomLimit(size, size);
    size_t expected = size - kFlatOverhead;
    ASSERT_THAT(buffer.capacity(), Ge(expected));
    EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(size, expected)));
  }
}

TEST(CordLargeBufferTest, CreateWithTooLargeLimit) {
  CordBuffer buffer = CordBuffer::CreateWithCustomLimit(k64KiB, k1MB);
  ASSERT_THAT(buffer.capacity(), Ge(k64KiB - kFlatOverhead));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(k64KiB, k1MB)));
}

TEST(CordLargeBufferTest, CreateWithHugeValueForOverFlowHardening) {
  for (size_t dist_from_max = 0; dist_from_max <= 32; ++dist_from_max) {
    size_t capacity = std::numeric_limits<size_t>::max() - dist_from_max;

    CordBuffer buffer = CordBuffer::CreateWithDefaultLimit(capacity);
    ASSERT_THAT(buffer.capacity(), Ge(kDefaultLimit));
    EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(kMaxFlatSize, capacity)));

    for (size_t limit = kMaxFlatSize; limit <= kCustomLimit; limit *= 2) {
      CordBuffer buffer = CordBuffer::CreateWithCustomLimit(limit, capacity);
      ASSERT_THAT(buffer.capacity(), Ge(limit - kFlatOverhead));
      EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(limit, capacity)));
    }
  }
}

TEST(CordLargeBufferTest, CreateWithSmallLimit) {
  CordBuffer buffer = CordBuffer::CreateWithCustomLimit(512, 1024);
  ASSERT_THAT(buffer.capacity(), Ge(512 - kFlatOverhead));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(512, 1024)));

  // Ask for precise block size, should return size - kOverhead
  buffer = CordBuffer::CreateWithCustomLimit(512, 512);
  ASSERT_THAT(buffer.capacity(), Ge(512 - kFlatOverhead));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(512, 512)));

  // Corner case: 511 < block_size, but 511 + kOverhead is above
  buffer = CordBuffer::CreateWithCustomLimit(512, 511);
  ASSERT_THAT(buffer.capacity(), Ge(512 - kFlatOverhead));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(512, 511)));

  // Corner case: 498 + kOverhead < block_size
  buffer = CordBuffer::CreateWithCustomLimit(512, 498);
  ASSERT_THAT(buffer.capacity(), Ge(512 - kFlatOverhead));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(512, 498)));
}

TEST(CordLargeBufferTest, CreateWasteFull) {
  // 15 KiB gets rounded down to next pow2 value.
  const size_t requested = (15 << 10);
  CordBuffer buffer = CordBuffer::CreateWithCustomLimit(k16KiB, requested);
  ASSERT_THAT(buffer.capacity(), Ge(k8KiB - kFlatOverhead));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(k8KiB, requested)));
}

TEST(CordLargeBufferTest, CreateSmallSlop) {
  const size_t requested = k16KiB - 2 * kFlatOverhead;
  CordBuffer buffer = CordBuffer::CreateWithCustomLimit(k16KiB, requested);
  ASSERT_THAT(buffer.capacity(), Ge(k16KiB - kFlatOverhead));
  EXPECT_THAT(buffer.capacity(), Le(MaxCapacityFor(k16KiB, requested)));
}

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
