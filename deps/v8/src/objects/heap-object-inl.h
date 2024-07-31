// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HEAP_OBJECT_INL_H_
#define V8_OBJECTS_HEAP_OBJECT_INL_H_

#include "src/common/ptr-compr-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#define TYPE_CHECKER(type, ...)                                               \
  bool Is##type(Tagged<HeapObject> obj) {                                     \
    /* IsBlah() predicates needs to load the map and thus they require the */ \
    /* main cage base. */                                                     \
    PtrComprCageBase cage_base = GetPtrComprCageBase();                       \
    return Is##type(obj, cage_base);                                          \
  }                                                                           \
  /* The cage_base passed here must be the base of the main pointer */        \
  /* compression cage, i.e. the one where the Map space is allocated. */      \
  bool Is##type(Tagged<HeapObject> obj, PtrComprCageBase cage_base) {         \
    Tagged<Map> map_object = obj->map(cage_base);                             \
    return InstanceTypeChecker::Is##type(map_object);                         \
  }

INSTANCE_TYPE_CHECKERS(TYPE_CHECKER)
#undef TYPE_CHECKER

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HEAP_OBJECT_INL_H_
