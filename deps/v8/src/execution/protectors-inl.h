// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_PROTECTORS_INL_H_
#define V8_EXECUTION_PROTECTORS_INL_H_

#include "src/execution/protectors.h"
#include "src/objects/property-cell-inl.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

#define DEFINE_PROTECTOR_ON_ISOLATE_CHECK(name, root_index, unused_cell) \
  bool Protectors::Is##name##Intact(Isolate* isolate) {                  \
    Tagged<PropertyCell> cell =                                          \
        PropertyCell::cast(isolate->root(RootIndex::k##root_index));     \
    return IsSmi(cell->value()) &&                                       \
           Smi::ToInt(cell->value()) == kProtectorValid;                 \
  }
DECLARED_PROTECTORS_ON_ISOLATE(DEFINE_PROTECTOR_ON_ISOLATE_CHECK)
#undef DEFINE_PROTECTORS_ON_ISOLATE_CHECK

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_PROTECTORS_INL_H_
