// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ARGUMENTS_INL_H_
#define V8_OBJECTS_ARGUMENTS_INL_H_

#include "src/execution/isolate-inl.h"
#include "src/objects/arguments.h"
#include "src/objects/contexts-inl.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/arguments-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSArgumentsObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(AliasedArgumentsEntry)

Tagged<Context> SloppyArgumentsElements::context() const {
  return context_.load();
}
void SloppyArgumentsElements::set_context(Tagged<Context> value,
                                          WriteBarrierMode mode) {
  context_.store(this, value, mode);
}
Tagged<UnionOf<FixedArray, NumberDictionary>>
SloppyArgumentsElements::arguments() const {
  return arguments_.load();
}
void SloppyArgumentsElements::set_arguments(
    Tagged<UnionOf<FixedArray, NumberDictionary>> value,
    WriteBarrierMode mode) {
  arguments_.store(this, value, mode);
}

Tagged<UnionOf<Smi, Hole>> SloppyArgumentsElements::mapped_entries(
    int index, RelaxedLoadTag tag) const {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  return objects()[index].Relaxed_Load();
}

void SloppyArgumentsElements::set_mapped_entries(
    int index, Tagged<UnionOf<Smi, Hole>> value) {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  objects()[index].store(this, value);
}

void SloppyArgumentsElements::set_mapped_entries(
    int index, Tagged<UnionOf<Smi, Hole>> value, RelaxedStoreTag tag) {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  objects()[index].Relaxed_Store(this, value);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ARGUMENTS_INL_H_
