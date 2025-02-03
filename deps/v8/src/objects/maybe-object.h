// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MAYBE_OBJECT_H_
#define V8_OBJECTS_MAYBE_OBJECT_H_

#include <type_traits>

#include "src/objects/tagged-impl.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

inline Tagged<ClearedWeakValue> ClearedValue(PtrComprCageBase cage_base);

template <typename THeapObjectSlot>
inline void UpdateHeapObjectReferenceSlot(THeapObjectSlot slot,
                                          Tagged<HeapObject> value);

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_MAYBE_OBJECT_H_
