// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_VLQ_H_
#define V8_BASE_VLQ_H_

#include <limits>
#include <vector>

#include "src/base/memory.h"

namespace v8 {
namespace base {

static constexpr uint32_t kContinueShift = 7;
static constexpr uint32_t kContinueBit = 1 << kContinueShift;
static constexpr uint32_t kDataMask = kContinueBit - 1;

// Encodes an unsigned value using variable-length encoding and stores it using
// the passed process_byte function.
template <typename Function>
inline void VLQEncodeUnsigned(Function&& process_byte, uint32_t value) {
  // Write as many bytes as necessary to encode the value, with 7 bits of data
  // per byte (leaving space for one continuation bit).
  static constexpr uint32_t kDataBitsPerByte = kContinueShift;
  if (value < 1 << (kDataBitsPerByte)) goto write_one_byte;
  if (value < 1 << (2 * kDataBitsPerByte)) goto write_two_bytes;
  if (value < 1 << (3 * kDataBitsPerByte)) goto write_three_bytes;
  if (value < 1 << (4 * kDataBitsPerByte)) goto write_four_bytes;
  process_byte(value | kContinueBit);
  value >>= kContinueShift;
write_four_bytes:
  process_byte(value | kContinueBit);
  value >>= kContinueShift;
write_three_bytes:
  process_byte(value | kContinueBit);
  value >>= kContinueShift;
write_two_bytes:
  process_byte(value | kContinueBit);
  value >>= kContinueShift;
write_one_byte:
  // The last value written doesn't need a continuation bit.
  process_byte(value);
}

inline uint32_t VLQConvertToUnsigned(int32_t value) {
  // This wouldn't handle kMinInt correctly if it ever encountered it.
  DCHECK_NE(value, std::numeric_limits<int32_t>::min());
  bool is_negative = value < 0;
  // Encode sign in least significant bit.
  uint32_t bits = static_cast<uint32_t>((is_negative ? -value : value) << 1) |
                  static_cast<uint32_t>(is_negative);
  return bits;
}

// Encodes value using variable-length encoding and stores it using the passed
// process_byte function.
template <typename Function>
inline void VLQEncode(Function&& process_byte, int32_t value) {
  uint32_t bits = VLQConvertToUnsigned(value);
  VLQEncodeUnsigned(std::forward<Function>(process_byte), bits);
}

// Wrapper of VLQEncode for std::vector backed storage containers.
template <typename A>
inline void VLQEncode(std::vector<uint8_t, A>* data, int32_t value) {
  VLQEncode([data](uint8_t value) { data->push_back(value); }, value);
}

// Wrapper of VLQEncodeUnsigned for std::vector backed storage containers.
template <typename A>
inline void VLQEncodeUnsigned(std::vector<uint8_t, A>* data, uint32_t value) {
  VLQEncodeUnsigned([data](uint8_t value) { data->push_back(value); }, value);
}

// Decodes a variable-length encoded unsigned value from bytes returned by
// successive calls to the given function.
template <typename GetNextFunction>
inline typename std::enable_if<
    std::is_same<decltype(std::declval<GetNextFunction>()()), uint8_t>::value,
    uint32_t>::type
VLQDecodeUnsigned(GetNextFunction&& get_next) {
  uint8_t cur_byte = get_next();
  // Single byte fast path; no need to mask.
  if (cur_byte <= kDataMask) {
    return cur_byte;
  }
  uint32_t bits = cur_byte & kDataMask;
  for (int shift = kContinueShift; shift <= 32; shift += kContinueShift) {
    cur_byte = get_next();
    bits |= (cur_byte & kDataMask) << shift;
    if (cur_byte <= kDataMask) break;
  }
  return bits;
}

// Decodes a variable-length encoded unsigned value stored in contiguous memory
// starting at data_start + index, updating index to where the next encoded
// value starts.
inline uint32_t VLQDecodeUnsigned(const uint8_t* data_start, int* index) {
  return VLQDecodeUnsigned([&] { return data_start[(*index)++]; });
}

// Decodes a variable-length encoded value stored in contiguous memory starting
// at data_start + index, updating index to where the next encoded value starts.
inline int32_t VLQDecode(const uint8_t* data_start, int* index) {
  uint32_t bits = VLQDecodeUnsigned(data_start, index);
  bool is_negative = (bits & 1) == 1;
  int32_t result = bits >> 1;
  return is_negative ? -result : result;
}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_VLQ_H_
