// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_MEASUREMENT_INL_H_
#define V8_HEAP_MEMORY_MEASUREMENT_INL_H_

#include "src/heap/memory-measurement.h"
#include "src/objects/contexts-inl.h"
#include "src/objects/contexts.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/map-inl.h"
#include "src/objects/map.h"

namespace v8 {
namespace internal {

bool NativeContextInferrer::Infer(PtrComprCageBase cage_base, Tagged<Map> map,
                                  Tagged<HeapObject> object,
                                  Address* native_context) {
  Tagged<Object> maybe_native_context =
      map->map()->raw_native_context_or_null();
  *native_context = maybe_native_context.ptr();
  // The value might be equal to Smi::uninitialized_deserialization_value()
  // during NativeContext deserialization.
  return !IsSmi(maybe_native_context) && !IsNull(maybe_native_context);
}

V8_INLINE bool NativeContextStats::HasExternalBytes(Tagged<Map> map) {
  InstanceType instance_type = map->instance_type();
  return (instance_type == JS_ARRAY_BUFFER_TYPE ||
          InstanceTypeChecker::IsExternalString(instance_type));
}

V8_INLINE void NativeContextStats::IncrementSize(Address context,
                                                 Tagged<Map> map,
                                                 Tagged<HeapObject> object,
                                                 size_t size) {
  size_by_context_[context] += size;
  if (HasExternalBytes(map)) {
    IncrementExternalSize(context, map, object);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_MEASUREMENT_INL_H_
