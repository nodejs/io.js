// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DEPENDENT_CODE_INL_H_
#define V8_OBJECTS_DEPENDENT_CODE_INL_H_

#include "src/objects/dependent-code.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/tagged.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(DependentCode, WeakArrayList)
CAST_ACCESSOR(DependentCode)

// static
template <typename ObjectT>
void DependentCode::DeoptimizeDependencyGroups(Isolate* isolate, ObjectT object,
                                               DependencyGroups groups) {
  static_assert(kTaggedCanConvertToRawObjects);
  DeoptimizeDependencyGroups(isolate, Tagged<ObjectT>(object), groups);
}

// static
template <typename ObjectT>
void DependentCode::DeoptimizeDependencyGroups(Isolate* isolate,
                                               Tagged<ObjectT> object,
                                               DependencyGroups groups) {
  // Shared objects are designed to never invalidate code.
  DCHECK(!InAnySharedSpace(object) && !InReadOnlySpace(object));
  object->dependent_code()->DeoptimizeDependencyGroups(isolate, groups);
}

// static
template <typename ObjectT>
bool DependentCode::MarkCodeForDeoptimization(Isolate* isolate,
                                              Tagged<ObjectT> object,
                                              DependencyGroups groups) {
  // Shared objects are designed to never invalidate code.
  DCHECK(!InAnySharedSpace(object) && !InReadOnlySpace(object));
  return object->dependent_code()->MarkCodeForDeoptimization(isolate, groups);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DEPENDENT_CODE_INL_H_
