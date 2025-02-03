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

#include <cstddef>

#include "absl/base/config.h"
#include "absl/crc/crc32c.h"
#include "absl/crc/internal/crc_memcpy.h"
#include "absl/crc/internal/non_temporal_memcpy.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

crc32c_t CrcNonTemporalMemcpyEngine::Compute(void* __restrict dst,
                                             const void* __restrict src,
                                             std::size_t length,
                                             crc32c_t initial_crc) const {
  constexpr size_t kBlockSize = 8192;
  crc32c_t crc = initial_crc;

  const char* src_bytes = reinterpret_cast<const char*>(src);
  char* dst_bytes = reinterpret_cast<char*>(dst);

  // Copy + CRC loop - run 8k chunks until we are out of full chunks.
  std::size_t offset = 0;
  for (; offset + kBlockSize < length; offset += kBlockSize) {
    crc = absl::ExtendCrc32c(crc,
                             absl::string_view(src_bytes + offset, kBlockSize));
    non_temporal_store_memcpy(dst_bytes + offset, src_bytes + offset,
                              kBlockSize);
  }

  // Save some work if length is 0.
  if (offset < length) {
    std::size_t final_copy_size = length - offset;
    crc = ExtendCrc32c(crc,
                       absl::string_view(src_bytes + offset, final_copy_size));

    non_temporal_store_memcpy(dst_bytes + offset, src_bytes + offset,
                              final_copy_size);
  }

  return crc;
}

crc32c_t CrcNonTemporalMemcpyAVXEngine::Compute(void* __restrict dst,
                                                const void* __restrict src,
                                                std::size_t length,
                                                crc32c_t initial_crc) const {
  constexpr size_t kBlockSize = 8192;
  crc32c_t crc = initial_crc;

  const char* src_bytes = reinterpret_cast<const char*>(src);
  char* dst_bytes = reinterpret_cast<char*>(dst);

  // Copy + CRC loop - run 8k chunks until we are out of full chunks.
  std::size_t offset = 0;
  for (; offset + kBlockSize < length; offset += kBlockSize) {
    crc = ExtendCrc32c(crc, absl::string_view(src_bytes + offset, kBlockSize));

    non_temporal_store_memcpy_avx(dst_bytes + offset, src_bytes + offset,
                                  kBlockSize);
  }

  // Save some work if length is 0.
  if (offset < length) {
    std::size_t final_copy_size = length - offset;
    crc = ExtendCrc32c(crc,
                       absl::string_view(src_bytes + offset, final_copy_size));

    non_temporal_store_memcpy_avx(dst_bytes + offset, src_bytes + offset,
                                  final_copy_size);
  }

  return crc;
}

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl
