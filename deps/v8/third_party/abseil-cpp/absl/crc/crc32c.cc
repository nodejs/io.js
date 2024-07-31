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

#include "absl/crc/crc32c.h"

#include <cstdint>

#include "absl/crc/internal/crc.h"
#include "absl/crc/internal/crc32c.h"
#include "absl/crc/internal/crc_memcpy.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace {

const crc_internal::CRC* CrcEngine() {
  static const crc_internal::CRC* engine = crc_internal::CRC::Crc32c();
  return engine;
}

constexpr uint32_t kCRC32Xor = 0xffffffffU;

}  // namespace

namespace crc_internal {

crc32c_t UnextendCrc32cByZeroes(crc32c_t initial_crc, size_t length) {
  uint32_t crc = static_cast<uint32_t>(initial_crc) ^ kCRC32Xor;
  CrcEngine()->UnextendByZeroes(&crc, length);
  return static_cast<crc32c_t>(crc ^ kCRC32Xor);
}

// Called by `absl::ExtendCrc32c()` on strings with size > 64 or when hardware
// CRC32C support is missing.
crc32c_t ExtendCrc32cInternal(crc32c_t initial_crc,
                              absl::string_view buf_to_add) {
  uint32_t crc = static_cast<uint32_t>(initial_crc) ^ kCRC32Xor;
  CrcEngine()->Extend(&crc, buf_to_add.data(), buf_to_add.size());
  return static_cast<crc32c_t>(crc ^ kCRC32Xor);
}

}  // namespace crc_internal

crc32c_t ComputeCrc32c(absl::string_view buf) {
  return ExtendCrc32c(crc32c_t{0}, buf);
}

crc32c_t ExtendCrc32cByZeroes(crc32c_t initial_crc, size_t length) {
  uint32_t crc = static_cast<uint32_t>(initial_crc) ^ kCRC32Xor;
  CrcEngine()->ExtendByZeroes(&crc, length);
  return static_cast<crc32c_t>(crc ^ kCRC32Xor);
}

crc32c_t ConcatCrc32c(crc32c_t lhs_crc, crc32c_t rhs_crc, size_t rhs_len) {
  uint32_t result = static_cast<uint32_t>(lhs_crc);
  CrcEngine()->ExtendByZeroes(&result, rhs_len);
  return crc32c_t{result ^ static_cast<uint32_t>(rhs_crc)};
}

crc32c_t RemoveCrc32cPrefix(crc32c_t crc_a, crc32c_t crc_ab, size_t length_b) {
  return ConcatCrc32c(crc_a, crc_ab, length_b);
}

crc32c_t MemcpyCrc32c(void* dest, const void* src, size_t count,
                      crc32c_t initial_crc) {
  return static_cast<crc32c_t>(
      crc_internal::Crc32CAndCopy(dest, src, count, initial_crc, false));
}

// Remove a Suffix of given size from a buffer
//
// Given a CRC32C of an existing buffer, `full_string_crc`; the CRC32C of a
// suffix of that buffer to remove, `suffix_crc`; and suffix buffer's length,
// `suffix_len` return the CRC32C of the buffer with suffix removed
//
// This operation has a runtime cost of O(log(`suffix_len`))
crc32c_t RemoveCrc32cSuffix(crc32c_t full_string_crc, crc32c_t suffix_crc,
                            size_t suffix_len) {
  uint32_t result = static_cast<uint32_t>(full_string_crc) ^
                    static_cast<uint32_t>(suffix_crc);
  CrcEngine()->UnextendByZeroes(&result, suffix_len);
  return crc32c_t{result};
}

ABSL_NAMESPACE_END
}  // namespace absl
