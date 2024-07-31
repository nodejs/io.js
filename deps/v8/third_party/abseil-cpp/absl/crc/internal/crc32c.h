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

#ifndef ABSL_CRC_INTERNAL_CRC32C_H_
#define ABSL_CRC_INTERNAL_CRC32C_H_

#include "absl/base/config.h"
#include "absl/crc/crc32c.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

// Modifies a CRC32 value by removing `length` bytes with a value of 0 from
// the end of the string.
//
// This is the inverse operation of ExtendCrc32cByZeroes().
//
// This operation has a runtime cost of O(log(`length`))
//
// Internal implementation detail, exposed for testing only.
crc32c_t UnextendCrc32cByZeroes(crc32c_t initial_crc, size_t length);

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CRC_INTERNAL_CRC32C_H_
