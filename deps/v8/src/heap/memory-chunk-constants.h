// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_CHUNK_CONSTANTS_H_
#define V8_HEAP_MEMORY_CHUNK_CONSTANTS_H_

#include "include/v8-internal.h"
#include "src/base/bits.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE MemoryChunkConstants final : public AllStatic {
 public:
#ifdef V8_ENABLE_SANDBOX
  static constexpr size_t kPagesInMainCage =
      kPtrComprCageReservationSize / kRegularPageSize;
  static constexpr size_t kPagesInCodeCage =
      kMaximalCodeRangeSize / kRegularPageSize;
  static constexpr size_t kPagesInTrustedCage =
      kMaximalTrustedRangeSize / kRegularPageSize;

  static constexpr size_t kMainCageMetadataOffset = 0;
  static constexpr size_t kTrustedSpaceMetadataOffset =
      kMainCageMetadataOffset + kPagesInMainCage;
  static constexpr size_t kCodeRangeMetadataOffset =
      kTrustedSpaceMetadataOffset + kPagesInTrustedCage;

  static constexpr size_t kMetadataPointerTableSizeLog2 = base::bits::BitWidth(
      kPagesInMainCage + kPagesInCodeCage + kPagesInTrustedCage);
  static constexpr size_t kMetadataPointerTableSize =
      1 << kMetadataPointerTableSizeLog2;
  static constexpr size_t kMetadataPointerTableSizeMask =
      kMetadataPointerTableSize - 1;
#endif  // V8_ENABLE_SANDBOX
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_CHUNK_CONSTANTS_H_
