// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/utils/hex-format.h"

#include <stddef.h>
#include <stdint.h>

#include "src/base/logging.h"

namespace v8 {
namespace internal {

void FormatBytesToHex(char* formatted, size_t size_of_formatted,
                      const uint8_t* val, size_t size_of_val) {
  // Prevent overflow by ensuring that the value can't exceed
  // 0x20000000 in length, which would be 0x40000000 when formatted
  CHECK_LT(size_of_val, 0x20000000);
  CHECK(size_of_formatted >= (size_of_val * 2));

  for (size_t index = 0; index < size_of_val; index++) {
    size_t dest_index = index << 1;
    snprintf(&formatted[dest_index], size_of_formatted - dest_index, "%02x",
             val[index]);
  }
}

}  // namespace internal
}  // namespace v8
