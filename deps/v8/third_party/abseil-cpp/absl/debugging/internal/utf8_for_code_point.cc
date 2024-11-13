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

#include "absl/debugging/internal/utf8_for_code_point.h"

#include <cstdint>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
namespace {

// UTF-8 encoding bounds.
constexpr uint32_t kMinSurrogate = 0xd800, kMaxSurrogate = 0xdfff;
constexpr uint32_t kMax1ByteCodePoint = 0x7f;
constexpr uint32_t kMax2ByteCodePoint = 0x7ff;
constexpr uint32_t kMax3ByteCodePoint = 0xffff;
constexpr uint32_t kMaxCodePoint = 0x10ffff;

}  // namespace

Utf8ForCodePoint::Utf8ForCodePoint(uint64_t code_point) {
  if (code_point <= kMax1ByteCodePoint) {
    length = 1;
    bytes[0] = static_cast<char>(code_point);
    return;
  }

  if (code_point <= kMax2ByteCodePoint) {
    length = 2;
    bytes[0] = static_cast<char>(0xc0 | (code_point >> 6));
    bytes[1] = static_cast<char>(0x80 | (code_point & 0x3f));
    return;
  }

  if (kMinSurrogate <= code_point && code_point <= kMaxSurrogate) return;

  if (code_point <= kMax3ByteCodePoint) {
    length = 3;
    bytes[0] = static_cast<char>(0xe0 | (code_point >> 12));
    bytes[1] = static_cast<char>(0x80 | ((code_point >> 6) & 0x3f));
    bytes[2] = static_cast<char>(0x80 | (code_point & 0x3f));
    return;
  }

  if (code_point > kMaxCodePoint) return;

  length = 4;
  bytes[0] = static_cast<char>(0xf0 | (code_point >> 18));
  bytes[1] = static_cast<char>(0x80 | ((code_point >> 12) & 0x3f));
  bytes[2] = static_cast<char>(0x80 | ((code_point >> 6) & 0x3f));
  bytes[3] = static_cast<char>(0x80 | (code_point & 0x3f));
}

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
