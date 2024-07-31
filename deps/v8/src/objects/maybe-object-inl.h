// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MAYBE_OBJECT_INL_H_
#define V8_OBJECTS_MAYBE_OBJECT_INL_H_

#include "src/common/ptr-compr-inl.h"
#include "src/objects/maybe-object.h"
#include "src/objects/smi-inl.h"
#include "src/objects/tagged-impl-inl.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

inline Tagged<ClearedWeakValue> ClearedValue(PtrComprCageBase cage_base) {
  // Construct cleared weak ref value.
  Address value;
#ifdef V8_COMPRESS_POINTERS
  // This is necessary to make pointer decompression computation also
  // suitable for cleared weak references.
  value = V8HeapCompressionScheme::DecompressTagged(
      cage_base, kClearedWeakHeapObjectLower32);
#else
  value = kClearedWeakHeapObjectLower32;
#endif
  // The rest of the code will check only the lower 32-bits.
  DCHECK_EQ(kClearedWeakHeapObjectLower32, static_cast<uint32_t>(value));
  return Tagged<ClearedWeakValue>(value);
}

template <typename THeapObjectSlot>
void UpdateHeapObjectReferenceSlot(THeapObjectSlot slot,
                                   Tagged<HeapObject> value) {
  static_assert(std::is_same<THeapObjectSlot, FullHeapObjectSlot>::value ||
                    std::is_same<THeapObjectSlot, HeapObjectSlot>::value,
                "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
  Address old_value = (*slot).ptr();
  DCHECK(!HAS_SMI_TAG(old_value));
  Address new_value = value.ptr();
  DCHECK(Internals::HasHeapObjectTag(new_value));

#ifdef DEBUG
  bool weak_before = HAS_WEAK_HEAP_OBJECT_TAG(old_value);
#endif

  slot.store(Tagged<HeapObjectReference>::cast(
      Tagged<MaybeObject>(new_value | (old_value & kWeakHeapObjectMask))));

#ifdef DEBUG
  bool weak_after = HAS_WEAK_HEAP_OBJECT_TAG((*slot).ptr());
  DCHECK_EQ(weak_before, weak_after);
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_MAYBE_OBJECT_INL_H_
