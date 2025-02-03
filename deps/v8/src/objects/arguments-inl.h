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

OBJECT_CONSTRUCTORS_IMPL(SloppyArgumentsElements, FixedArrayBase)

ACCESSORS_NOCAGE(SloppyArgumentsElements, context, Tagged<Context>,
                 kContextOffset)
ACCESSORS_NOCAGE(SloppyArgumentsElements, arguments, Tagged<FixedArray>,
                 kArgumentsOffset)

Tagged<Object> SloppyArgumentsElements::mapped_entries(
    int index, RelaxedLoadTag tag) const {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  return RELAXED_READ_FIELD(*this, OffsetOfElementAt(index));
}

void SloppyArgumentsElements::set_mapped_entries(int index,
                                                 Tagged<Object> value) {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  WRITE_FIELD(*this, OffsetOfElementAt(index), value);
}

void SloppyArgumentsElements::set_mapped_entries(int index,
                                                 Tagged<Object> value,
                                                 RelaxedStoreTag tag) {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  RELAXED_WRITE_FIELD(*this, OffsetOfElementAt(index), value);
}

int SloppyArgumentsElements::AllocatedSize() const { return SizeFor(length()); }

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ARGUMENTS_INL_H_
