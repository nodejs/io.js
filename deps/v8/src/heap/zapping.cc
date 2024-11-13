// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/zapping.h"

#include "src/base/memory.h"
#include "src/heap/heap.h"
#include "src/heap/memory-chunk-metadata-inl.h"
#include "src/objects/slots-inl.h"

namespace v8::internal::heap {

void ZapCodeBlock(Address start, int size_in_bytes) {
#ifdef DEBUG
  DCHECK(ShouldZapGarbage());
  CodePageMemoryModificationScopeForDebugging code_modification_scope(
      MemoryChunkMetadata::FromAddress(start));
  DCHECK(IsAligned(start, kIntSize));
  for (int i = 0; i < size_in_bytes / kIntSize; i++) {
    base::Memory<int>(start + i * kIntSize) = kCodeZapValue;
  }
#endif
}

void ZapBlock(Address start, size_t size, uintptr_t zap_value) {
  DCHECK(ShouldZapGarbage());
  DCHECK(IsAligned(start, kTaggedSize));
  DCHECK(IsAligned(size, kTaggedSize));
  MemsetTagged(ObjectSlot(start),
               Tagged<Object>(static_cast<Address>(zap_value)),
               size >> kTaggedSizeLog2);
}

}  // namespace v8::internal::heap
