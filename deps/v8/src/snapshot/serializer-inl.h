// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_SERIALIZER_INL_H_
#define V8_SNAPSHOT_SERIALIZER_INL_H_

#include "src/roots/roots-inl.h"
#include "src/snapshot/serializer.h"

namespace v8 {
namespace internal {

bool Serializer::IsNotMappedSymbol(Tagged<HeapObject> obj) const {
  Tagged<Object> not_mapped_symbol =
      ReadOnlyRoots(isolate()).not_mapped_symbol();
  if (V8_EXTERNAL_CODE_SPACE_BOOL) {
    // It's possible that a InstructionStream object might have the same
    // compressed value as the not_mapped_symbol, so we must compare full
    // pointers.
    // TODO(v8:11880): Avoid the need for this special case by never putting
    // InstructionStream references anywhere except the CodeDadaContainer
    // objects. In particular, the InstructionStream objects should not appear
    // in serializer's identity map. This should be possible once the
    // IsolateData::builtins table is migrated to contain Code
    // references.
    return obj.ptr() == not_mapped_symbol.ptr();
  }
  return obj == not_mapped_symbol;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_SERIALIZER_INL_H_
