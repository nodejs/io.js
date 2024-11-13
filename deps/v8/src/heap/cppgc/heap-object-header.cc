// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-object-header.h"

#include "include/cppgc/internal/api-constants.h"
#include "src/base/macros.h"
#include "src/base/sanitizer/asan.h"
#include "src/heap/cppgc/gc-info-table.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-page.h"

namespace cppgc {
namespace internal {

static_assert((kAllocationGranularity % sizeof(HeapObjectHeader)) == 0);

void HeapObjectHeader::CheckApiConstants() {
  static_assert(api_constants::kFullyConstructedBitMask ==
                FullyConstructedField::kMask);
  static_assert(api_constants::kFullyConstructedBitFieldOffsetFromPayload ==
                (sizeof(encoded_high_) + sizeof(encoded_low_)));
}

void HeapObjectHeader::Finalize() {
#ifdef V8_USE_ADDRESS_SANITIZER
  const size_t size =
      IsLargeObject()
          ? LargePage::From(BasePage::FromPayload(this))->ObjectSize()
          : ObjectSize();
  ASAN_UNPOISON_MEMORY_REGION(ObjectStart(), size);
#endif  // V8_USE_ADDRESS_SANITIZER
  const GCInfo& gc_info = GlobalGCInfoTable::GCInfoFromIndex(GetGCInfoIndex());
  if (gc_info.finalize) {
    gc_info.finalize(ObjectStart());
  }
}

HeapObjectName HeapObjectHeader::GetName() const {
  return GetName(BasePage::FromPayload(this)->heap().name_of_unnamed_object());
}

HeapObjectName HeapObjectHeader::GetName(
    HeapObjectNameForUnnamedObject heap_object_name) const {
  const GCInfo& gc_info = GlobalGCInfoTable::GCInfoFromIndex(GetGCInfoIndex());
  return gc_info.name(ObjectStart(), heap_object_name);
}

}  // namespace internal
}  // namespace cppgc
